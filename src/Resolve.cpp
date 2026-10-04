#include "Resolve.h"

#include "Channels.h"
#include "ClipPlayer.h"
#include "ModRegistry.h"
#include "RegisteredMods.h"
#include "SkinSets.h"
#include "apply/Entries.h"
#include "scan/Scan.h"

#include <mutex>
#include <numbers>
#include <unordered_map>
#include <unordered_set>

namespace Resolve
{
	namespace
	{
		float g_now = 0.f;

		// A row whose channel is not the type its entry says, which is how it was made: a bug, said once per row
		void WrongType(const Channels::Channel* a_channel, std::string_view a_expected)
		{
			static std::mutex lock;
			static std::unordered_set<const void*> said;
			std::scoped_lock guard(lock);
			if (said.insert(a_channel).second) {
				logger::error("resolve: '{}' is not a {} row though its entry is; it is skipped", a_channel->GetName(), a_expected);
			}
		}

		// A transition's seconds, or none
		float StartSeconds(const SubMod* a_subMod)
		{
			const auto* modifier = static_cast<const AppearanceModifier*>(a_subMod);
			return modifier->HasStartTransition() ? modifier->GetStartTransitionTime() : 0.f;
		}

		float EndSeconds(const SubMod* a_subMod)
		{
			const auto* modifier = static_cast<const AppearanceModifier*>(a_subMod);
			return modifier->HasEndTransition() ? modifier->GetEndTransitionTime() : 0.f;
		}

		float SmoothStep(float a_t)
		{
			a_t = std::clamp(a_t, 0.f, 1.f);
			return a_t * a_t * (3.f - 2.f * a_t);
		}

		// How much of the modifier is on: rising over its start transition, falling over its end one
		float Weight(const ActorState::Active& a_active, float a_now)
		{
			float weight = 1.f;
			if (const auto start = StartSeconds(a_active.subMod); start > 0.f) {
				weight = SmoothStep((a_now - a_active.enteredAt) / start);
			}
			if (a_active.leftAt >= 0.f) {
				const auto end = EndSeconds(a_active.subMod);
				weight *= end > 0.f ? 1.f - SmoothStep((a_now - a_active.leftAt) / end) : 0.f;
			}
			return weight;
		}

		// One channel one modifier drives, with how much of it is on
		struct Contribution
		{
			Channels::Channel* channel;
			float weight;
			std::int32_t priority;
			const SubMod* subMod;  // whose, and since when it is active, for a random pick
			float enteredAt;
		};

		// The ids a group holds, down through its groups
		void CollectIds(Channels::ChannelSet* a_set, std::vector<Apply::ChannelId>& a_out)
		{
			a_set->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
				if (a_entry->IsDisabled()) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				if (a_entry->IsGroup()) {
					CollectIds(static_cast<Channels::GroupChannel*>(a_entry.get())->channels.get(), a_out);
				} else if (const auto id = static_cast<Channels::Channel*>(a_entry.get())->GetId(); id.IsValid()) {
					a_out.push_back(id);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		// A set's channels into the contributions, a group whole or not at all
		void Gather(Channels::ChannelSet* a_set, const ActorState::Active& a_active, float a_weight, std::unordered_set<std::uint32_t>& a_held, std::unordered_map<std::uint32_t, std::vector<Contribution>>& a_out)
		{
			a_set->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
				if (a_entry->IsDisabled()) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				if (a_entry->IsGroup()) {
					auto* group = static_cast<Channels::GroupChannel*>(a_entry.get());
					std::vector<Apply::ChannelId> ids;
					CollectIds(group->channels.get(), ids);
					const bool bTaken = std::ranges::any_of(ids, [&](Apply::ChannelId a_id) { return a_held.contains(a_id.raw); });
					if (!bTaken) {
						Gather(group->channels.get(), a_active, a_weight, a_held, a_out);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto* channel = static_cast<Channels::Channel*>(a_entry.get());
				const auto id = channel->GetId();
				if (id.IsValid()) {
					a_held.insert(id.raw);
					a_out[id.raw].push_back({ channel, a_weight, a_active.priority, a_active.subMod, a_active.enteredAt });
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		// A number channel's value for the actor: static, or read from a global or an actor value and mapped
		float NumberOf(Channels::NumberChannel* a_channel, RE::TESObjectREFR* a_refr)
		{
			float value = a_channel->value->value.GetValue(a_refr);
			if (!a_channel->value->IsStatic()) {
				const float readLow = a_channel->readLow->value.GetValue(nullptr);
				const float readHigh = a_channel->readHigh->value.GetValue(nullptr);
				const float channelLow = a_channel->channelLow->value.GetValue(nullptr);
				const float channelHigh = a_channel->channelHigh->value.GetValue(nullptr);
				const float t = readHigh != readLow ? std::clamp((value - readLow) / (readHigh - readLow), 0.f, 1.f) : 0.f;
				value = channelLow + t * (channelHigh - channelLow);
			}
			return value;
		}

		// An actor by what stays when the load order changes: its plugin and its id within it; one made in game by its id
		std::string ActorKey(RE::TESObjectREFR* a_refr)
		{
			if (!a_refr) {
				return {};
			}
			const auto id = a_refr->GetFormID();
			const auto* file = (id >> 24) == 0xFF ? nullptr : a_refr->GetFile(0);
			if (!file) {
				return std::format("{:08X}", id);
			}
			return std::format("{}:{:X}", file->GetFilename(), file->IsLight() ? (id & 0xFFF) : (id & 0xFFFFFF));
		}

		std::uint64_t Fnv1a(std::string_view a_text)
		{
			std::uint64_t hash = 0xcbf29ce484222325ull;
			for (const char c : a_text) {
				hash ^= static_cast<std::uint8_t>(c);
				hash *= 0x100000001b3ull;
			}
			return hash;
		}

		// Whether a BodySlide preset of this name was scanned: a body preset draw skips one that is not installed
		bool PresetInstalled(std::string_view a_name)
		{
			static std::mutex lock;
			static const Scan::Catalogue* from = nullptr;
			static std::unordered_set<std::string> names;
			const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue();
			std::scoped_lock guard(lock);
			if (catalogue.get() != from) {
				from = catalogue.get();
				names.clear();
				for (const auto& preset : catalogue ? catalogue->bodyPresets : std::vector<Scan::BodyPreset>{}) {
					names.insert(preset.name);
				}
			}
			return names.contains(std::string(a_name));
		}

		// A reference row's draw, when its value is a Random Value
		Channels::RandomValue* PickOf(Channels::Channel* a_channel)
		{
			if (auto* reference = dynamic_cast<Channels::ReferenceChannel*>(a_channel)) {
				return reference->value->bRandom ? reference->value->random.get() : nullptr;
			}
			if (auto* headPart = dynamic_cast<Channels::HeadPartChannel*>(a_channel)) {
				return headPart->value->bRandom ? headPart->value->random.get() : nullptr;
			}
			return nullptr;
		}

		// An overlay's place among its slot's enabled items, by name; none for one the slot does not have
		std::optional<std::uint64_t> OverlayIndex(std::string_view a_slotName, std::string_view a_name)
		{
			std::optional<std::uint64_t> found;
			std::uint64_t at = 0;
			Overlays::ForEachItem(a_slotName, [&](Overlays::Slot&, std::string_view, Overlays::Item& a_item) {
				if (a_item.bDisabled) {
					return true;
				}
				if (a_item.name == a_name) {
					found = at;
					return false;
				}
				++at;
				return true;
			});
			return found;
		}

		// What a reference names, as the applier counts it: an overlay by its place in the slot's pool, a skin set
		// by its place in the list, a head part by its form
		std::optional<std::uint64_t> ReferenceOf(Channels::Channel* a_channel, const Apply::Entry& a_entry, RE::TESObjectREFR* a_refr)
		{
			if (auto* headPart = dynamic_cast<Channels::HeadPartChannel*>(a_channel)) {
				auto* part = headPart->value->form.GetValue(a_refr);
				return part ? std::optional<std::uint64_t>(part->GetFormID()) : std::nullopt;
			}
			auto* reference = dynamic_cast<Channels::ReferenceChannel*>(a_channel);
			if (!reference || reference->value->value.empty()) {
				return std::nullopt;
			}
			const auto& name = reference->value->value;
			switch (a_channel->kind) {
			case Apply::Kind::kOverlay:
				return OverlayIndex(a_entry.name, name);
			case Apply::Kind::kSkin: {
				const auto sets = Skins::ListSets();
				for (std::size_t i = 0; i < sets.size(); ++i) {
					if (sets[i].set->GetName() == name) {
						return i;
					}
				}
				return std::nullopt;
			}
			default:
				return std::nullopt;
			}
		}

		template <class T>
		T* FindSample(std::vector<T>& a_samples, std::uint32_t a_index)
		{
			const auto it = std::ranges::find(a_samples, a_index, &T::index);
			return it != a_samples.end() ? &*it : nullptr;
		}
	}

	namespace
	{
		// Legacy's curves, as its Transitions.cpp has them: a ball landing's constants for the bounce
		constexpr float kBounceSteepness = 7.5625f;
		constexpr float kBounceSpan = 2.75f;
		constexpr float kMaxStrength = 0.5f;

		// The overshoot a back curve gives for a tension, and the tension for an overshoot, found by halving
		float OvershootOf(float a_tension)
		{
			const float settle = a_tension + 1.f;
			return (4.f / 27.f) * a_tension * a_tension * a_tension / (settle * settle);
		}

		float TensionFor(float a_overshoot)
		{
			if (a_overshoot <= 0.f) {
				return 0.f;
			}
			float low = 0.f;
			float high = 20.f;
			for (int step = 0; step < 24; ++step) {
				const float middle = (low + high) * 0.5f;
				if (OvershootOf(middle) < a_overshoot) {
					low = middle;
				} else {
					high = middle;
				}
			}
			return (low + high) * 0.5f;
		}
	}

	float CurveAt(std::string_view a_name, float a_u, float a_strength)
	{
		const float u = std::clamp(a_u, 0.f, 1.f);
		const float strength = std::clamp(a_strength, 0.f, kMaxStrength);
		if (a_name == "Linear"sv) {
			return u;
		}
		if (a_name == "Instant"sv) {
			return u >= 1.f ? 1.f : 0.f;
		}
		if (a_name == "Ease in"sv) {
			return u * u * u;
		}
		if (a_name == "Ease out"sv) {
			const float inverse = 1.f - u;
			return 1.f - inverse * inverse * inverse;
		}
		if (a_name == "Bounce"sv) {
			// Four landings, each shorter than the last; the dips after the first scaled so the first is the strength
			const float scale = strength / 0.25f;
			float shifted = u;
			if (shifted < 1.f / kBounceSpan) {
				return kBounceSteepness * shifted * shifted;
			}
			float classic = 0.f;
			if (shifted < 2.f / kBounceSpan) {
				shifted -= 1.5f / kBounceSpan;
				classic = kBounceSteepness * shifted * shifted + 0.75f;
			} else if (shifted < 2.5f / kBounceSpan) {
				shifted -= 2.25f / kBounceSpan;
				classic = kBounceSteepness * shifted * shifted + 0.9375f;
			} else {
				shifted -= 2.625f / kBounceSpan;
				classic = kBounceSteepness * shifted * shifted + 0.984375f;
			}
			return 1.f - (1.f - classic) * scale;
		}
		if (a_name == "Spring"sv) {
			// A back curve whose tension is solved for the overshoot asked
			const float tension = TensionFor(strength);
			const float settle = tension + 1.f;
			const float shifted = u - 1.f;
			return 1.f + settle * shifted * shifted * shifted + tension * shifted * shifted;
		}
		if (a_name != "Ease in out"sv) {
			static std::mutex lock;
			static std::unordered_set<std::string> said;
			std::scoped_lock guard(lock);
			if (said.emplace(a_name).second) {
				logger::error("resolve: transition '{}' is not one of the curves, though the loader checks them; it eases in and out", a_name);
			}
		}
		// Ease in out
		if (u < 0.5f) {
			return 4.f * u * u * u;
		}
		const float inverse = -2.f * u + 2.f;
		return 1.f - inverse * inverse * inverse / 2.f;
	}

	float NumberValue(Channels::NumberChannel* a_channel, RE::TESObjectREFR* a_refr)
	{
		return NumberOf(a_channel, a_refr);
	}

	std::optional<std::uint64_t> ReferenceValue(Channels::Channel* a_channel, const Apply::Entry& a_entry, const PickContext& a_context, bool* a_outDrewNothing)
	{
		// A head part drawn at random: the entry's own part
		if (dynamic_cast<Channels::HeadPartChannel*>(a_channel)) {
			if (const auto picked = RandomPick(a_channel, a_context)) {
				const auto* part = dynamic_cast<const Channels::HeadPartChannelComponent*>(*picked);
				auto* form = part ? part->form.GetValue() : nullptr;
				if (!form && a_outDrewNothing) {
					*a_outDrewNothing = true;
				}
				return form ? std::optional<std::uint64_t>(form->GetFormID()) : std::nullopt;
			}
		}
		return ReferenceOf(a_channel, a_entry, a_context.refr);
	}

	std::vector<ChannelOutcome> Explain(const std::vector<ActorState::Active>& a_active)
	{
		std::vector<ChannelOutcome> out;
		std::unordered_set<std::uint32_t> held;
		std::unordered_map<std::uint32_t, std::vector<Contribution>> contributions;

		// As the fold gathers, highest first; a group some higher modifier already holds a channel of is left out whole
		const std::function<void(Channels::ChannelSet*, const ActorState::Active&)> gather = [&](Channels::ChannelSet* a_set, const ActorState::Active& a_active) {
			a_set->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
				if (a_entry->IsDisabled()) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				if (a_entry->IsGroup()) {
					auto* group = static_cast<Channels::GroupChannel*>(a_entry.get());
					std::vector<Apply::ChannelId> ids;
					CollectIds(group->channels.get(), ids);
					if (std::ranges::any_of(ids, [&](Apply::ChannelId a_id) { return held.contains(a_id.raw); })) {
						for (const auto id : ids) {
							out.push_back({ a_active.subMod, id.raw, ChannelOutcome::Kind::kHeldBack });
						}
					} else {
						gather(group->channels.get(), a_active);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto* channel = static_cast<Channels::Channel*>(a_entry.get());
				if (const auto id = channel->GetId(); id.IsValid()) {
					held.insert(id.raw);
					contributions[id.raw].push_back({ channel, 1.f, a_active.priority, a_active.subMod, a_active.enteredAt });
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		};
		for (const auto& active : a_active) {
			gather(active.subMod->GetChannelSet(), active);
		}

		// From the top down: the top wins; under a blend a lower one mixes in, under a claim it no longer counts. A
		// number blends by its mode; anything else is set whole by the highest
		for (auto& [raw, list] : contributions) {
			std::ranges::stable_sort(list, [](const Contribution& a, const Contribution& b) { return a.priority < b.priority; });
			const Contribution* above = nullptr;
			const Contribution* claimer = nullptr;
			for (auto it = list.rbegin(); it != list.rend(); ++it) {
				ChannelOutcome outcome{ it->subMod, raw };
				if (claimer) {
					outcome.kind = ChannelOutcome::Kind::kOverridden;
					outcome.by = claimer->subMod;
				} else if (above) {
					outcome.kind = ChannelOutcome::Kind::kBlended;
					outcome.by = above->subMod;
					auto* number = dynamic_cast<Channels::NumberChannel*>(above->channel);
					outcome.mode = number ? number->blend->GetArgument() : std::string{};
				}
				out.push_back(std::move(outcome));
				auto* number = dynamic_cast<Channels::NumberChannel*>(it->channel);
				if (!claimer && (!number || number->blend->mode == Channels::BlendChannelComponent::Mode::kClaim)) {
					claimer = &*it;
				}
				above = &*it;
			}
		}
		return out;
	}

	std::unordered_map<std::uint32_t, const SubMod*> ModifierHolders(ActorState::State& a_state)
	{
		std::unordered_map<std::uint32_t, const SubMod*> out;
		const auto resolved = a_state.resolved.With([](const auto& a_resolved) { return a_resolved; });
		if (!resolved) {
			return out;
		}
		for (const auto& active : resolved->active) {
			if (active.leftAt >= 0.f) {
				continue;
			}
			std::vector<Apply::ChannelId> ids;
			CollectIds(active.subMod->GetChannelSet(), ids);
			for (const auto id : ids) {
				auto [it, bNew] = out.emplace(id.raw, active.subMod);
				if (!bNew && active.priority > it->second->GetPriority()) {
					it->second = active.subMod;
				}
			}
		}
		return out;
	}

	std::unordered_map<std::uint32_t, std::int32_t> ModifierPriorities(ActorState::State& a_state)
	{
		std::unordered_map<std::uint32_t, std::int32_t> out;
		const auto resolved = a_state.resolved.With([](const auto& a_resolved) { return a_resolved; });
		if (!resolved) {
			return out;
		}
		for (const auto& active : resolved->active) {
			if (active.leftAt >= 0.f) {
				continue;
			}
			std::vector<Apply::ChannelId> ids;
			CollectIds(active.subMod->GetChannelSet(), ids);
			for (const auto id : ids) {
				auto [it, bNew] = out.emplace(id.raw, active.priority);
				if (!bNew) {
					it->second = (std::max)(it->second, active.priority);
				}
			}
		}
		return out;
	}

	void AdvanceClock(float a_deltaSeconds)
	{
		g_now += a_deltaSeconds;
	}

	float Now()
	{
		return g_now;
	}

	ActorState::PresetMix PresetSliders(std::string_view a_name)
	{
		ActorState::PresetMix out;
		const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue();
		if (!catalogue || a_name.empty()) {
			return out;
		}
		const auto it = std::ranges::find(catalogue->bodyPresets, a_name, &Scan::BodyPreset::name);
		if (it == catalogue->bodyPresets.end()) {
			return out;
		}
		auto& entries = Apply::Entries::GetSingleton();
		for (const auto& slider : it->sliders) {
			if (const auto id = entries.Find(Apply::Kind::kBodyMorph, slider.name); id.IsValid() && (slider.atZero != 0.f || slider.atHundred != 0.f)) {
				out[id.GetIndex()] = { slider.atZero / 100.f, slider.atHundred / 100.f };
			}
		}
		return out;
	}

	ActorState::PresetMix MixSliders(const ActorState::PresetMix& a_from, const ActorState::PresetMix& a_to, float a_u)
	{
		ActorState::PresetMix out;
		for (const auto& [index, value] : a_to) {
			const auto was = a_from.find(index);
			const auto from = was != a_from.end() ? was->second : std::array{ 0.f, 0.f };
			out[index] = { std::lerp(from[0], value[0], a_u), std::lerp(from[1], value[1], a_u) };
		}
		for (const auto& [index, value] : a_from) {
			if (!a_to.contains(index)) {
				out[index] = { std::lerp(value[0], 0.f, a_u), std::lerp(value[1], 0.f, a_u) };
			}
		}
		return out;
	}

	float BodyWeight(const ActorState::Sampled& a_sampled, RE::TESObjectREFR* a_refr)
	{
		static const auto id = Apply::Entries::GetSingleton().Require(Apply::Kind::kBodyMorph, "weight"sv);
		const auto& body = a_sampled.numbers[static_cast<std::size_t>(Apply::Kind::kBodyMorph)];
		if (const auto it = id.IsValid() ? std::ranges::find(body, id.GetIndex(), &Apply::NumberSample::index) : body.end(); it != body.end()) {
			return std::clamp(it->value, 0.f, 100.f) / 100.f;
		}
		auto* actor = a_refr ? a_refr->As<RE::Actor>() : nullptr;
		auto* base = actor ? actor->GetActorBase() : nullptr;
		return base ? std::clamp(base->weight, 0.f, 100.f) / 100.f : 0.5f;
	}

	std::optional<const Channels::IChannelComponent*> RandomPick(Channels::Channel* a_channel, const PickContext& a_context)
	{
		auto* pick = PickOf(a_channel);
		if (!pick) {
			return std::nullopt;
		}
		// Every entry in, a pool opened into its own, each by its weight times the pool's; one the game does not have left out
		const auto* mod = a_context.owner ? a_context.owner->GetParentMod() : nullptr;
		std::vector<std::pair<const Channels::IChannelComponent*, float>> entries;
		float total = 0.f;
		std::optional<std::vector<Skins::Listed>> skins;
		const auto drawable = [&](const Channels::IChannelComponent* a_value) {
			if (const auto* part = dynamic_cast<const Channels::HeadPartChannelComponent*>(a_value)) {
				return part->form.GetValue() != nullptr;
			}
			const auto* reference = dynamic_cast<const Channels::ReferenceChannelComponent*>(a_value);
			if (!reference) {
				return true;
			}
			const auto& name = reference->value;
			switch (a_channel->kind) {
			case Apply::Kind::kBodyMorph:
				return PresetInstalled(name);
			case Apply::Kind::kSkin:
				if (!skins) {
					skins = Skins::ListSets();
				}
				return std::ranges::any_of(*skins, [&](const Skins::Listed& a_listed) { return a_listed.set->GetName() == name; });
			case Apply::Kind::kOverlay:
				return OverlayIndex(a_channel->identifier, name).has_value();
			default:
				return !name.empty();
			}
		};
		for (const auto& entry : pick->list.entries) {
			if (entry->bDisabled || (entry->pool.empty() && entry->weight <= 0.f)) {
				continue;
			}
			if (!entry->pool.empty()) {
				if (const auto* pool = mod ? mod->FindReferencePool(entry->pool) : nullptr; pool && pool->GetChannel() == a_channel->GetName()) {
					for (const auto& item : pool->entries.entries) {
						if (const float weight = item->weight; !item->bDisabled && item->value && weight > 0.f && drawable(item->value.get())) {
							entries.emplace_back(item->value.get(), weight);
							total += weight;
						}
					}
				}
			} else if (entry->value && drawable(entry->value.get())) {
				entries.emplace_back(entry->value.get(), entry->weight);
				total += entry->weight;
			}
		}
		if (entries.empty() || total <= 0.f) {
			return nullptr;
		}
		// Drawn from a hash of who and what shares it, so a draw holds across sessions: the scope decides the sharing,
		// the activation's time in it for one held a single activation
		using Scope = Channels::RandomValue::Scope;
		const std::string_view modName = mod ? mod->GetName() : ""sv;
		const std::string_view ownerName = a_context.owner ? a_context.owner->GetName() : ""sv;
		std::string key = ActorKey(a_context.refr);
		switch (pick->scope) {
		case Scope::kActivation:
			key += std::format("|{}|{}|{}|{}", modName, ownerName, a_channel->GetName(), std::bit_cast<std::uint32_t>(a_context.activatedAt));
			break;
		case Scope::kChannel:
			key += std::format("|{}|{}|{}", modName, ownerName, a_channel->GetName());
			break;
		case Scope::kSubMod:
			key += std::format("|{}|{}", modName, ownerName);
			break;
		case Scope::kRegisteredMod:
			key += std::format("|{}", modName);
			break;
		case Scope::kActor:
		default:
			break;
		}
		const double unit = static_cast<double>(Fnv1a(key) >> 11) * (1.0 / 9007199254740992.0);
		double at = unit * total;
		for (const auto& [value, weight] : entries) {
			if (at < weight) {
				return value;
			}
			at -= weight;
		}
		return entries.back().first;
	}

	std::string NameOf(Channels::Channel* a_channel, const PickContext& a_context, bool* a_outDrewNothing)
	{
		if (const auto picked = RandomPick(a_channel, a_context)) {
			const auto* reference = dynamic_cast<const Channels::ReferenceChannelComponent*>(*picked);
			if (!reference && a_outDrewNothing) {
				*a_outDrewNothing = true;
			}
			return reference ? reference->value : std::string{};
		}
		auto* reference = dynamic_cast<Channels::ReferenceChannel*>(a_channel);
		return reference ? reference->value->value : std::string{};
	}

	void Tick(ActorState::State& a_state, const std::vector<const SubMod*>& a_held, float a_now)
	{
		const auto previous = a_state.resolved.With([](const auto& a_resolved) { return a_resolved; });
		auto next = std::make_shared<ActorState::Resolved>();
		next->generation = previous ? previous->generation + 1 : 1;

		// What activates and deactivates this tick, for the clips' triggers, which the clip player reads right after
		std::vector<const SubMod*> activated;
		std::vector<const SubMod*> deactivated;
		if (previous) {
			for (const auto& was : previous->active) {
				auto active = was;
				const bool bHeld = std::ranges::find(a_held, was.subMod) != a_held.end();
				if (bHeld) {
					if (was.leftAt >= 0.f) {
						activated.push_back(was.subMod);  // held again on its way out
					}
					active.leftAt = -1.f;  // held again: back as it was, or still there
				} else if (was.leftAt < 0.f) {
					deactivated.push_back(was.subMod);
					if (EndSeconds(was.subMod) <= 0.f) {
						continue;
					}
					active.leftAt = a_now;
				} else if (a_now - was.leftAt >= EndSeconds(was.subMod)) {
					continue;
				}
				next->active.push_back(active);
			}
		}
		for (const auto* subMod : a_held) {
			if (std::ranges::find(next->active, subMod, &ActorState::Active::subMod) == next->active.end()) {
				next->active.push_back({ subMod, subMod->GetPriority(), a_now, -1.f });
				activated.push_back(subMod);
			}
		}
		for (const auto* subMod : activated) {
			ClipPlayer::QueueModifier(a_state, ClipPlayer::Event::kModifierActivated, subMod);
		}
		for (const auto* subMod : deactivated) {
			ClipPlayer::QueueModifier(a_state, ClipPlayer::Event::kModifierDeactivated, subMod);
		}
		std::ranges::stable_sort(next->active, [](const ActorState::Active& a, const ActorState::Active& b) { return a.priority > b.priority; });

		a_state.resolved.With([&](auto& a_resolved) { a_resolved = std::move(next); });
	}

	namespace
	{
		void FoldActive(const std::vector<ActorState::Active>& a_active, RE::TESObjectREFR* a_refr, float a_now, ActorState::Sampled& a_out);
	}

	void Fold(ActorState::State& a_state, RE::TESObjectREFR* a_refr, float a_now, ActorState::Sampled& a_out)
	{
		const auto resolved = a_state.resolved.With([](const auto& a_resolved) { return a_resolved; });
		if (!resolved || resolved->active.empty()) {
			return;
		}
		FoldActive(resolved->active, a_refr, a_now, a_out);
	}

	bool Settled(ActorState::State& a_state, float a_now)
	{
		const auto resolved = a_state.resolved.With([](const auto& a_resolved) { return a_resolved; });
		if (!resolved) {
			return true;
		}
		return std::ranges::none_of(resolved->active, [&](const ActorState::Active& a_active) {
			return a_active.leftAt >= 0.f || a_now - a_active.enteredAt < StartSeconds(a_active.subMod);
		});
	}

	void FoldModifier(const SubMod* a_modifier, RE::TESObjectREFR* a_refr, ActorState::Sampled& a_out)
	{
		// Entered long ago and not leaving: the whole of it
		FoldActive({ ActorState::Active{ a_modifier, 0, -1e9f, -1.f } }, a_refr, 0.f, a_out);
	}

	namespace
	{
	void FoldActive(const std::vector<ActorState::Active>& a_active, RE::TESObjectREFR* a_refr, float a_now, ActorState::Sampled& a_out)
	{
		auto& entries = Apply::Entries::GetSingleton();

		// Highest first: what each modifier gets to drive
		std::unordered_set<std::uint32_t> held;
		std::unordered_map<std::uint32_t, std::vector<Contribution>> contributions;
		for (const auto& active : a_active) {
			const float weight = Weight(active, a_now);
			if (weight <= 0.f) {
				continue;
			}
			Gather(active.subMod->GetChannelSet(), active, weight, held, contributions);
		}

		// Lowest first: the values folded
		using Mode = Channels::BlendChannelComponent::Mode;
		for (auto& [raw, list] : contributions) {
			std::ranges::sort(list, [](const Contribution& a, const Contribution& b) { return a.priority < b.priority; });
			Apply::ChannelId id;
			id.raw = raw;
			const auto entry = entries.Get(id);
			const auto kind = static_cast<std::size_t>(id.GetKind());

			// A blended pick, the body preset: its sliders, each modifier's over those below by its transition weight
			if (entry.type == Apply::ValueType::kBlendedPick) {
				ActorState::PresetMix running;
				for (const auto& contribution : list) {
					bool bNothing = false;
					if (auto name = NameOf(contribution.channel, { a_refr, contribution.subMod, contribution.enteredAt }, &bNothing); !name.empty() || bNothing) {
						running = MixSliders(running, PresetSliders(name), contribution.weight);
						a_out.presetName = std::move(name);
					}
				}
				a_out.presetSliders = std::move(running);
				continue;
			}

			switch (entry.type) {
			case Apply::ValueType::kNumber:
			case Apply::ValueType::kSteppedNumber: {
				// From the entry's rest: its neutral, or where its slider starts when no value means not driven
				const float base = entry.neutral < entry.low ? entry.start : entry.neutral;
				float running = base;
				bool bDriven = false;
				float averaged = 0.f;
				float averagedWeight = 0.f;
				for (const auto& contribution : list) {
					auto* number = dynamic_cast<Channels::NumberChannel*>(contribution.channel);
					if (!number) {
						WrongType(contribution.channel, "number");
						continue;
					}
					const float value = NumberOf(number, a_refr);
					// A stepped number is set at once, not blended by the transition
					const float w = entry.type == Apply::ValueType::kSteppedNumber ? (contribution.weight > 0.f ? 1.f : 0.f) : contribution.weight;
					switch (number->blend->mode) {
					case Mode::kAdd:
						running += value * w;
						break;
					case Mode::kSaturate:
						running = 1.f - (1.f - running) * (1.f - value * w);
						break;
					case Mode::kAverage:
						averaged += value * w;
						averagedWeight += w;
						break;
					case Mode::kMax:
						running = std::lerp(running, (std::max)(running, value), w);
						break;
					case Mode::kMin:
						running = std::lerp(running, (std::min)(running, value), w);
						break;
					case Mode::kClaim:
					default:
						running = std::lerp(running, value, w);
						break;
					}
					bDriven = true;
				}
				if (averagedWeight > 0.f) {
					running = std::lerp(running, averaged / averagedWeight, (std::min)(averagedWeight, 1.f));
				}
				if (bDriven) {
					a_out.numbers[kind].push_back({ id.GetIndex(), running });
				}
				break;
			}
			case Apply::ValueType::kColour: {
				// Each over the last by its weight, the highest priority ending on top
				std::optional<RE::NiColor> running;
				for (const auto& contribution : list) {
					auto* colour = dynamic_cast<Channels::ColourChannel*>(contribution.channel);
					if (!colour) {
						WrongType(contribution.channel, "colour");
						continue;
					}
					const auto& value = colour->value->value.GetValue();
					if (!running) {
						running = value;
					} else {
						running = RE::NiColor{ std::lerp(running->red, value.red, contribution.weight), std::lerp(running->green, value.green, contribution.weight), std::lerp(running->blue, value.blue, contribution.weight) };
					}
				}
				if (running) {
					a_out.colours[kind].push_back({ id.GetIndex(), running->red, running->green, running->blue });
				}
				break;
			}
			case Apply::ValueType::kReference: {
				// Set at once: the highest priority that names something. A skin set or an overlay goes by its name,
				// looked up when it is applied; anything else by what its applier counts
				const bool bByName = id.GetKind() == Apply::Kind::kSkin || id.GetKind() == Apply::Kind::kOverlay;
				for (auto it = list.rbegin(); it != list.rend(); ++it) {
					const PickContext context{ a_refr, it->subMod, it->enteredAt };
					bool bNothing = false;
					if (bByName) {
						if (const auto name = NameOf(it->channel, context, &bNothing); !name.empty()) {
							a_out.texts[kind].push_back({ id.GetIndex(), name });
							break;
						}
					} else if (const auto value = ReferenceValue(it->channel, entry, context, &bNothing)) {
						a_out.references[kind].push_back({ id.GetIndex(), *value });
						break;
					}
					if (bNothing) {
						break;
					}
				}
				break;
			}
			case Apply::ValueType::kMode: {
				// Set at once: the highest priority's name, carried as its place in the entry's list
				for (auto it = list.rbegin(); it != list.rend(); ++it) {
					auto* mode = dynamic_cast<Channels::ModeChannel*>(it->channel);
					if (!mode) {
						WrongType(it->channel, "mode");
						continue;
					}
					// The loader only takes a mode the entry lists
					const auto at = std::ranges::find(entry.modes, mode->value->value);
					if (at == entry.modes.end()) {
						WrongType(it->channel, std::format("mode of {}", entry.name));
						continue;
					}
					a_out.numbers[kind].push_back({ id.GetIndex(), static_cast<float>(at - entry.modes.begin()) });
					break;
				}
				break;
			}
			case Apply::ValueType::kText: {
				for (auto it = list.rbegin(); it != list.rend(); ++it) {
					bool bNothing = false;
					if (const auto name = NameOf(it->channel, { a_refr, it->subMod, it->enteredAt }, &bNothing); !name.empty()) {
						a_out.texts[kind].push_back({ id.GetIndex(), name });
						break;
					}
					if (bNothing) {
						break;
					}
				}
				break;
			}
			case Apply::ValueType::kForm: {
				for (auto it = list.rbegin(); it != list.rend(); ++it) {
					auto* form = dynamic_cast<Channels::FormChannel*>(it->channel);
					if (!form) {
						WrongType(it->channel, "form");
						continue;
					}
					// A form from a plugin not loaded names nothing, and the rows below are asked
					if (auto* value = form->value->form.GetValue()) {
						a_out.references[kind].push_back({ id.GetIndex(), value->GetFormID() });
						break;
					}
				}
				break;
			}
			case Apply::ValueType::kSwitch: {
				// Set at once: the highest priority, on or off
				for (auto it = list.rbegin(); it != list.rend(); ++it) {
					auto* channel = dynamic_cast<Channels::SwitchChannel*>(it->channel);
					if (!channel) {
						WrongType(it->channel, "switch");
						continue;
					}
					a_out.numbers[kind].push_back({ id.GetIndex(), channel->value->bValue ? 1.f : 0.f });
					break;
				}
				break;
			}
			default:
				// TODO: modes, once an entry of the type exists
				break;
			}
		}
	}
}
}
