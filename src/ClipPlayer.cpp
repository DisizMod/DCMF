#include "ClipPlayer.h"

#include "Channels.h"
#include "ModRegistry.h"
#include "RegisteredMods.h"
#include "Resolve.h"
#include "Triggers.h"
#include "apply/Entries.h"
#include "apply/Material.h"

#include <mutex>
#include <random>
#include <unordered_set>

namespace ClipPlayer
{
	namespace
	{
		// A key whose row is not the type its channel's keys were built for: a bug, said once per row
		void WrongType(const Channels::ChannelBase* a_row, std::string_view a_expected)
		{
			static std::mutex lock;
			static std::unordered_set<const void*> said;
			std::scoped_lock guard(lock);
			if (said.insert(a_row).second) {
				logger::error("clips: a key's row is not a {} row though its channel's keys are; the run holds what is under it", a_expected);
			}
		}

		// The claimed channels that are on, groups and all, in the claimed order
		void CollectClaimed(Channels::ChannelSet* a_set, std::vector<Apply::ChannelId>& a_out)
		{
			a_set->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
				if (a_entry->IsDisabled()) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				if (a_entry->IsGroup()) {
					CollectClaimed(static_cast<Channels::GroupChannel*>(a_entry.get())->channels.get(), a_out);
				} else if (const auto id = static_cast<Channels::Channel*>(a_entry.get())->GetId(); id.IsValid() && std::ranges::find(a_out, id) == a_out.end()) {
					a_out.push_back(id);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		std::vector<Apply::ChannelId> ClaimedOf(Clip* a_clip)
		{
			std::vector<Apply::ChannelId> out;
			CollectClaimed(a_clip->GetChannelSet(), out);
			return out;
		}

		Channels::Channel* FindRow(Channels::ChannelSet* a_set, Apply::ChannelId a_id)
		{
			Channels::Channel* found = nullptr;
			a_set->ForEach([&](std::unique_ptr<Channels::ChannelBase>& a_entry) {
				if (!a_entry->IsGroup() && static_cast<Channels::Channel*>(a_entry.get())->GetId() == a_id) {
					found = static_cast<Channels::Channel*>(a_entry.get());
					return RE::BSVisit::BSVisitControl::kStop;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return found;
		}

		Channels::IChannelComponent* ComponentNamed(Channels::ChannelBase* a_row, std::string_view a_name)
		{
			for (uint32_t i = 0; i < a_row->GetNumComponents(); ++i) {
				if (auto* component = a_row->GetComponent(i); component->GetName() == a_name) {
					return component;
				}
			}
			return nullptr;
		}

		std::string ModeNamed(Channels::ChannelBase* a_row, std::string_view a_name, std::string_view a_default)
		{
			auto* component = dynamic_cast<Channels::ModeChannelComponent*>(ComponentNamed(a_row, a_name));
			return component ? component->value : std::string(a_default);
		}

		float NumberNamed(Channels::ChannelBase* a_row, std::string_view a_name, float a_default)
		{
			auto* component = dynamic_cast<Channels::NumberChannelComponent*>(ComponentNamed(a_row, a_name));
			return component ? component->value.GetValue(nullptr) : a_default;
		}

		float RestOf(const Apply::Entry& a_entry)
		{
			return a_entry.neutral < a_entry.low ? a_entry.start : a_entry.neutral;
		}

		// What a run starts from and goes back to with no modifier's value: the entry's rest, the actor's own
		// weight for body:weight
		float RestFor(const Apply::Entry& a_entry, Apply::ChannelId a_id, RE::TESObjectREFR* a_refr)
		{
			static const auto bodyWeight = Apply::Entries::GetSingleton().Require(Apply::Kind::kBodyMorph, "weight"sv);
			if (a_id == bodyWeight) {
				return Resolve::BodyWeight(ActorState::Sampled{}, a_refr) * 100.f;
			}
			return RestOf(a_entry);
		}

		template <class T>
		T* SampleAt(std::vector<T>& a_samples, std::uint32_t a_index)
		{
			const auto it = std::ranges::find(a_samples, a_index, &T::index);
			return it != a_samples.end() ? &*it : nullptr;
		}

		template <class T>
		const T* SampleAt(const std::vector<T>& a_samples, std::uint32_t a_index)
		{
			const auto it = std::ranges::find(a_samples, a_index, &T::index);
			return it != a_samples.end() ? &*it : nullptr;
		}

		template <class T>
		void Put(std::vector<T>& a_samples, T a_sample)
		{
			if (auto* existing = SampleAt(a_samples, a_sample.index)) {
				*existing = std::move(a_sample);
			} else {
				a_samples.push_back(std::move(a_sample));
			}
		}

		// A row that is set at once, written as the modifiers' fold writes its type
		void WriteStep(Channels::Channel* a_row, const Apply::Entry& a_entry, Apply::ChannelId a_id, const Resolve::PickContext& a_context, ActorState::Sampled& a_out)
		{
			const auto kind = static_cast<std::size_t>(a_id.GetKind());
			if (auto* mode = dynamic_cast<Channels::ModeChannel*>(a_row)) {
				const auto at = std::ranges::find(a_entry.modes, mode->value->value);
				Put(a_out.numbers[kind], Apply::NumberSample{ a_id.GetIndex(), at != a_entry.modes.end() ? static_cast<float>(at - a_entry.modes.begin()) : 0.f });
			} else if (auto* toggle = dynamic_cast<Channels::SwitchChannel*>(a_row)) {
				Put(a_out.numbers[kind], Apply::NumberSample{ a_id.GetIndex(), toggle->value->bValue ? 1.f : 0.f });
			} else if (auto* form = dynamic_cast<Channels::FormChannel*>(a_row)) {
				if (auto* value = form->value->form.GetValue()) {
					Put(a_out.references[kind], Apply::ReferenceSample{ a_id.GetIndex(), value->GetFormID() });
				}
			} else if (a_entry.type == Apply::ValueType::kText || a_id.GetKind() == Apply::Kind::kSkin || a_id.GetKind() == Apply::Kind::kOverlay) {
				// A text, and a skin set or an overlay, which go by name, drawn when the row is random
				bool bNothing = false;
				if (const auto name = Resolve::NameOf(a_row, a_context, &bNothing); !name.empty()) {
					Put(a_out.texts[kind], Apply::TextSample{ a_id.GetIndex(), name });
				} else if (bNothing) {
					std::erase_if(a_out.texts[kind], [&](const Apply::TextSample& a_sample) { return a_sample.index == a_id.GetIndex(); });
				}
			} else {
				bool bNothing = false;
				if (const auto value = Resolve::ReferenceValue(a_row, a_entry, a_context, &bNothing)) {
					Put(a_out.references[kind], Apply::ReferenceSample{ a_id.GetIndex(), *value });
				} else if (bNothing) {
					std::erase_if(a_out.references[kind], [&](const Apply::ReferenceSample& a_sample) { return a_sample.index == a_id.GetIndex(); });
				}
			}
		}

		// Whether a trigger answers the event; a PRESET answers when any trigger of its preset does
		bool Answers(Triggers::TriggerBase* a_trigger, const ActorState::ClipFiring& a_firing, RegisteredMod* a_mod)
		{
			if (a_trigger->IsDisabled()) {
				return false;
			}
			const auto event = static_cast<Event>(a_firing.event);
			if (dynamic_cast<Triggers::OnHitTrigger*>(a_trigger)) {
				return event == Event::kHit;
			}
			if (dynamic_cast<Triggers::OnDeathTrigger*>(a_trigger)) {
				return event == Event::kDeath;
			}
			if (auto* line = dynamic_cast<Triggers::OnDialogueLineTrigger*>(a_trigger)) {
				auto* info = line->topicInfo->value.GetValue();
				return event == Event::kDialogueLine && info && info->GetFormID() == a_firing.topicInfo;
			}
			if (auto* start = dynamic_cast<Triggers::OnAnimationStartTrigger*>(a_trigger)) {
				return event == Event::kAnimationStart && start->animation->value.Matches(a_firing.animation, a_firing.replacer);
			}
			if (auto* end = dynamic_cast<Triggers::OnAnimationEndTrigger*>(a_trigger)) {
				return event == Event::kAnimationEnd && end->animation->value.Matches(a_firing.animation, a_firing.replacer);
			}
			if (auto* graphEvent = dynamic_cast<Triggers::OnAnimationEventTrigger*>(a_trigger)) {
				return event == Event::kAnimationEvent && Utils::CompareStringsIgnoreCase(graphEvent->event->value.GetValue(), a_firing.animation);
			}
			if (auto* activated = dynamic_cast<Triggers::OnModifierActivatedTrigger*>(a_trigger)) {
				return event == Event::kModifierActivated && activated->modifier->Names(a_firing.modifier, a_mod);
			}
			if (auto* deactivated = dynamic_cast<Triggers::OnModifierDeactivatedTrigger*>(a_trigger)) {
				return event == Event::kModifierDeactivated && deactivated->modifier->Names(a_firing.modifier, a_mod);
			}
			if (auto* preset = dynamic_cast<Triggers::PresetTrigger*>(a_trigger)) {
				preset->preset->mod = a_mod;
				bool bAnswered = false;
				if (auto* set = preset->preset->GetPreset()) {
					set->ForEach([&](std::unique_ptr<Triggers::TriggerBase>& a_inner) {
						if (Answers(a_inner.get(), a_firing, a_mod)) {
							bAnswered = true;
							return RE::BSVisit::BSVisitControl::kStop;
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				}
				return bAnswered;
			}
			return false;
		}

		const Triggers::TriggerBase* FiredBy(Clip* a_clip, const std::vector<ActorState::ClipFiring>& a_firings)
		{
			const Triggers::TriggerBase* fired = nullptr;
			a_clip->GetTriggerSet()->ForEach([&](std::unique_ptr<Triggers::TriggerBase>& a_trigger) {
				for (const auto& firing : a_firings) {
					if (firing.except != a_clip && Answers(a_trigger.get(), firing, a_clip->GetParentMod())) {
						fired = a_trigger.get();
						return RE::BSVisit::BSVisitControl::kStop;
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			return fired;
		}

		float RandomUnit()
		{
			thread_local std::mt19937 engine{ std::random_device{}() };
			thread_local std::uniform_real_distribution<float> distribution{ 0.f, 1.f };
			return distribution(engine);
		}

		// A clip's Interval triggers, those of its presets too, each with a name that stays across sessions
		void CollectIntervals(Triggers::TriggerSet* a_set, RegisteredMod* a_mod, const std::string& a_path, std::vector<std::pair<Triggers::IntervalTrigger*, std::string>>& a_out)
		{
			std::size_t index = 0;
			a_set->ForEach([&](std::unique_ptr<Triggers::TriggerBase>& a_trigger) {
				const auto path = std::format("{}/{}", a_path, index++);
				if (a_trigger->IsDisabled()) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				if (auto* interval = dynamic_cast<Triggers::IntervalTrigger*>(a_trigger.get())) {
					a_out.emplace_back(interval, path);
				} else if (auto* preset = dynamic_cast<Triggers::PresetTrigger*>(a_trigger.get())) {
					preset->preset->mod = a_mod;
					if (auto* set = preset->preset->GetPreset()) {
						CollectIntervals(set, a_mod, std::format("{}/{}", a_path, set->GetName()), a_out);
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		std::vector<std::pair<Triggers::IntervalTrigger*, std::string>> IntervalsOf(Clip* a_clip)
		{
			std::vector<std::pair<Triggers::IntervalTrigger*, std::string>> out;
			auto* mod = a_clip->GetParentMod();
			CollectIntervals(a_clip->GetTriggerSet(), mod, std::format("{}/{}", mod ? mod->GetName() : ""sv, a_clip->GetName()), out);
			return out;
		}

		float RandomUnit();

		// The same number in [0, 1) for the same trigger and actor, in every session
		float UnitFor(std::string_view a_identity, RE::FormID a_actor)
		{
			std::uint64_t x = std::hash<std::string_view>{}(a_identity) ^ (static_cast<std::uint64_t>(a_actor) * 0x9E3779B97F4A7C15ull);
			x ^= x >> 30;
			x *= 0xBF58476D1CE4E5B9ull;
			x ^= x >> 27;
			x *= 0x94D049BB133111EBull;
			x ^= x >> 31;
			return static_cast<float>(x >> 40) / static_cast<float>(1ull << 24);
		}

		// The seconds to wait: the value, or the roll, made the first time it is asked for
		float SecondsOf(const Triggers::IntervalTriggerComponent* a_interval, ActorState::IntervalTimer& a_timer, std::string_view a_identity, RE::TESObjectREFR* a_refr)
		{
			float seconds = 0.f;
			if (!a_interval->bRandom) {
				seconds = a_interval->GetSeconds(a_refr);
			} else {
				if (!a_timer.roll) {
					a_timer.roll = a_interval->reroll == Triggers::IntervalTriggerComponent::Reroll::kPerActor ? UnitFor(a_identity, a_refr ? a_refr->GetFormID() : 0) : RandomUnit();
				}
				seconds = a_interval->RollSeconds(*a_timer.roll);
			}
			// Never quicker than a tick
			return (std::max)(seconds, 0.25f);
		}

		// A play-once variant first, then by weight at random or the next in order; none when every one is off or played
		// once already
		std::optional<std::uint16_t> PickVariant(Clip* a_clip, ActorState::ClipHistory& a_history)
		{
			auto& variants = a_clip->GetVariants();
			const auto count = static_cast<std::uint16_t>(a_clip->GetTracks().size());
			std::vector<Variant*> candidates;
			for (std::uint16_t i = 0; i < count; ++i) {
				auto* variant = variants.GetVariant(i);
				if (!variant || variant->IsDisabled() || (variant->ShouldPlayOnce() && a_history.playedOnce.contains(i))) {
					continue;
				}
				candidates.push_back(variant);
			}
			if (candidates.empty()) {
				return std::nullopt;
			}
			Variant* picked = nullptr;
			if (variants.GetVariantMode() == VariantMode::kSequential) {
				std::ranges::sort(candidates, {}, &Variant::GetOrder);
				picked = candidates[a_history.next++ % candidates.size()];
			} else {
				const auto first = std::ranges::find_if(candidates, &Variant::ShouldPlayOnce);
				if (first != candidates.end()) {
					picked = *first;
				} else {
					float total = 0.f;
					for (auto* candidate : candidates) {
						total += (std::max)(candidate->GetWeight(), 0.f);
					}
					float roll = RandomUnit() * total;
					for (auto* candidate : candidates) {
						roll -= (std::max)(candidate->GetWeight(), 0.f);
						if (roll <= 0.f) {
							picked = candidate;
							break;
						}
					}
					if (!picked) {
						picked = candidates.back();
					}
				}
			}
			if (picked->ShouldPlayOnce()) {
				a_history.playedOnce.insert(picked->GetIndex());
			}
			return picked->GetIndex();
		}

		void RunSet(Functions::FunctionSet* a_set, RE::TESObjectREFR* a_refr, SubMod* a_subMod)
		{
			if (a_set && a_refr) {
				a_set->Run(a_refr, a_subMod);
			}
		}

		class Sink :
			public RE::BSTEventSink<RE::TESHitEvent>,
			public RE::BSTEventSink<RE::TESDeathEvent>,
			public RE::BSTEventSink<RE::TESTopicInfoEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				if (a_event && a_event->target) {
					Queue(a_event->target.get(), Event::kHit);
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
			{
				// Sent as dying begins and again once dead; the first is the one
				if (a_event && a_event->actorDying && !a_event->dead) {
					Queue(a_event->actorDying.get(), Event::kDeath);
				}
				return RE::BSEventNotifyControl::kContinue;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESTopicInfoEvent* a_event, RE::BSTEventSource<RE::TESTopicInfoEvent>*) override
			{
				if (a_event && a_event->speakerRef && a_event->type == RE::TESTopicInfoEvent::TopicInfoEventType::kTopicBegin) {
					Queue(a_event->speakerRef.get(), Event::kDialogueLine, a_event->topicInfoFormID);
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		static Sink sink;
		if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESHitEvent>(&sink);
			holder->AddEventSink<RE::TESDeathEvent>(&sink);
			holder->AddEventSink<RE::TESTopicInfoEvent>(&sink);
			logger::info("clips: listening for hits, deaths and dialogue lines");
		} else {
			logger::error("clips: no script event source; OnHit, OnDeath and dialogue triggers never fire");
		}
	}

	void Queue(RE::TESObjectREFR* a_refr, Event a_event, RE::FormID a_topicInfo)
	{
		if (!a_refr) {
			return;
		}
		// Only the actors the scheduler keeps: the rest have nothing to play
		if (const auto state = ActorState::Get(a_refr->GetHandle())) {
			// A line of a topic a clip said: not a firing for that clip
			auto* info = a_event == Event::kDialogueLine ? RE::TESForm::LookupByID<RE::TESTopicInfo>(a_topicInfo) : nullptr;
			const RE::FormID topic = info && info->parentTopic ? info->parentTopic->GetFormID() : 0;
			const float now = Resolve::Now();
			state->clips.With([&](ActorState::ClipState& a_clips) {
				std::erase_if(a_clips.said, [&](const ActorState::SaidTopic& a_said) { return a_said.until < now; });
				const Clip* except = nullptr;
				if (const auto it = std::ranges::find(a_clips.said, topic, &ActorState::SaidTopic::topic); topic && it != a_clips.said.end()) {
					except = it->clip;
					a_clips.said.erase(it);
				}
				a_clips.pending.push_back({ static_cast<std::uint8_t>(a_event), a_topicInfo, except });
			});
		}
	}

	void QueueAnimation(RE::TESObjectREFR* a_refr, Event a_event, std::string a_file, std::string a_replacer)
	{
		if (!a_refr) {
			return;
		}
		if (const auto state = ActorState::Get(a_refr->GetHandle())) {
			state->clips.With([&](ActorState::ClipState& a_clips) {
				ActorState::ClipFiring firing{ static_cast<std::uint8_t>(a_event) };
				firing.animation = std::move(a_file);
				firing.replacer = std::move(a_replacer);
				a_clips.pending.push_back(std::move(firing));
			});
		}
	}

	void QueueModifier(ActorState::State& a_state, Event a_event, const SubMod* a_modifier)
	{
		a_state.clips.With([&](ActorState::ClipState& a_clips) {
			ActorState::ClipFiring firing{ static_cast<std::uint8_t>(a_event) };
			firing.modifier = a_modifier;
			a_clips.pending.push_back(std::move(firing));
		});
	}

	void NoteSaid(RE::TESObjectREFR* a_refr, RE::FormID a_topic, const Clip* a_clip)
	{
		if (!a_refr) {
			return;
		}
		// Five seconds for the line to begin; a topic none of whose lines holds says nothing
		if (const auto state = ActorState::Get(a_refr->GetHandle())) {
			state->clips.With([&](ActorState::ClipState& a_clips) { a_clips.said.push_back({ a_topic, a_clip, Resolve::Now() + 5.f }); });
		}
	}

	void Tick(ActorState::State& a_state, RE::TESObjectREFR* a_refr, const std::vector<Clip*>& a_passing, float a_now)
	{
		const auto holders = Resolve::ModifierHolders(a_state);
		const auto previous = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		auto& entries = Apply::Entries::GetSingleton();

		// What a clip needs to start: every channel it claims free of a higher modifier; the first one held, said
		const auto modifierBlock = [&](Clip* a_clip, const std::vector<Apply::ChannelId>& a_claimed) -> std::optional<std::string> {
			for (const auto id : a_claimed) {
				if (const auto it = holders.find(id.raw); it != holders.end() && it->second->GetPriority() > a_clip->GetPriority()) {
					return std::format("{} held by {} (priority {})", entries.Identifier(id), it->second->GetName(), it->second->GetPriority());
				}
			}
			return std::nullopt;
		};

		std::vector<std::pair<Functions::FunctionSet*, Clip*>> deactivated;
		std::vector<std::pair<Functions::FunctionSet*, Clip*>> activated;

		a_state.clips.With([&](ActorState::ClipState& a_clips) {
			a_clips.report.clear();
			const auto report = [&](const Clip* a_clip, ActorState::ClipReport::Kind a_kind, std::string a_reason = {}, std::uint16_t a_variant = 0, const void* a_trigger = nullptr) {
				a_clips.report.push_back({ a_clip, a_kind, std::move(a_reason), a_variant, a_trigger });
			};

			// Let a running clip go: at once, or through its exits when it asks for them on an interrupt
			const auto interrupt = [&](ActorState::RunningClip& a_running) {
				if (a_running.bExiting) {
					return;
				}
				const float exit = a_running.clip->AppliesExitOnInterrupt() && a_running.clip->HasCustomExitTime() ? a_running.clip->GetCustomExitTime() : 0.f;
				a_running.bExiting = true;
				a_running.exitStart = a_now;
				a_running.exitLength = exit;
			};

			// Ended, done exiting, conditions failed, or a channel taken by a higher modifier
			for (auto& running : a_clips.running) {
				const bool bPassing = std::ranges::find(a_passing, running.clip) != a_passing.end();
				if (running.bExiting) {
					continue;
				}
				if (running.clip->StopsWhenConditionsFail() && !bPassing) {
					logger::info("clips: '{}' interrupted: its conditions failed", running.clip->GetName());
					report(running.clip, ActorState::ClipReport::Kind::kInterrupted, "its conditions failed", running.variant);
					interrupt(running);
				} else if (const auto block = modifierBlock(running.clip, ClaimedOf(running.clip))) {
					logger::info("clips: '{}' interrupted: {}", running.clip->GetName(), *block);
					report(running.clip, ActorState::ClipReport::Kind::kInterrupted, *block, running.variant);
					interrupt(running);
				}
			}
			std::erase_if(a_clips.running, [&](const ActorState::RunningClip& a_running) {
				const bool bDone = a_running.bExiting ? a_now >= a_running.exitStart + a_running.exitLength : a_now >= a_running.startedAt + a_running.length;
				if (bDone) {
					deactivated.emplace_back(a_running.clip->GetFunctionSet(Functions::FunctionSetType::kOnDeactivate), a_running.clip);
					if (!a_running.bExiting) {
						report(a_running.clip, ActorState::ClipReport::Kind::kEnd, {}, a_running.variant);
					}
				}
				return bDone;
			});

			// Interval triggers: counted from the clip's end while its conditions hold; the count is let go when they do not
			std::unordered_map<const Clip*, const void*> byInterval;
			std::unordered_set<const void*> counting;
			for (auto* clip : a_passing) {
				if (clip->IsDisabled()) {
					continue;
				}
				const bool bPlaying = std::ranges::any_of(a_clips.running, [&](const ActorState::RunningClip& a_running) { return a_running.clip == clip; });
				for (const auto& [trigger, identity] : IntervalsOf(clip)) {
					counting.insert(trigger);
					auto& timer = a_clips.intervals[trigger];
					if (bPlaying) {
						timer.nextFire.reset();
					} else if (!timer.nextFire) {
						timer.nextFire = a_now + SecondsOf(trigger->interval, timer, identity, a_refr);
					} else if (a_now >= *timer.nextFire) {
						byInterval.try_emplace(clip, trigger);
						timer.nextFire.reset();
					}
				}
			}
			std::erase_if(a_clips.intervals, [&](const auto& a_entry) { return !counting.contains(a_entry.first); });

			// Fired: a trigger answered an event or its interval came, or, for a clip with none, its conditions came to hold
			std::vector<Clip*> fired;
			std::unordered_map<const Clip*, const void*> firedBy;
			for (auto* clip : a_passing) {
				if (clip->IsDisabled()) {
					continue;
				}
				const auto* byEvent = !a_clips.pending.empty() ? FiredBy(clip, a_clips.pending) : nullptr;
				const bool bByConditions = clip->GetTriggerSet()->IsEmpty() && !a_clips.passingLast.contains(clip);
				const auto interval = byInterval.find(clip);
				if (byEvent || bByConditions || interval != byInterval.end()) {
					fired.push_back(clip);
					firedBy[clip] = byEvent ? static_cast<const void*>(byEvent) : interval != byInterval.end() ? interval->second : nullptr;
					if (firedBy[clip]) {
						a_clips.firedAt[firedBy[clip]] = a_now;
					}
				}
			}
			std::ranges::stable_sort(fired, [](Clip* a, Clip* b) { return a->GetPriority() > b->GetPriority(); });

			for (auto* clip : fired) {
				const auto claimed = ClaimedOf(clip);
				// The same clip again is an echo: it starts over when asked to, and is let be otherwise
				auto same = std::ranges::find_if(a_clips.running, [&](const ActorState::RunningClip& a_running) { return a_running.clip == clip && !a_running.bExiting; });
				if (same != a_clips.running.end() && !clip->IsReevaluatingOnEcho()) {
					report(clip, ActorState::ClipReport::Kind::kEchoIgnored, "playing, and not replaced on echo", same->variant, firedBy[clip]);
					continue;
				}
				if (const auto block = modifierBlock(clip, claimed)) {
					report(clip, ActorState::ClipReport::Kind::kBlocked, *block, 0, firedBy[clip]);
					continue;
				}
				// Another clip on a claimed channel: a higher one keeps it, as does one that cannot be interrupted
				bool bBlocked = false;
				std::vector<ActorState::RunningClip*> taken;
				for (auto& running : a_clips.running) {
					if (running.bExiting || running.clip == clip) {
						continue;
					}
					const auto theirs = ClaimedOf(running.clip);
					const bool bShared = std::ranges::any_of(claimed, [&](Apply::ChannelId a_id) { return std::ranges::find(theirs, a_id) != theirs.end(); });
					if (!bShared) {
						continue;
					}
					if (running.priority > clip->GetPriority() || !running.clip->IsInterruptible()) {
						report(clip, ActorState::ClipReport::Kind::kBlocked, std::format("clip {} playing on a channel it claims{}", running.clip->GetName(), running.priority > clip->GetPriority() ? "" : ", not interruptible"), 0, firedBy[clip]);
						bBlocked = true;
						break;
					}
					taken.push_back(&running);
				}
				if (bBlocked) {
					continue;
				}
				const auto variant = PickVariant(clip, a_clips.history[clip]);
				if (!variant) {
					report(clip, ActorState::ClipReport::Kind::kBlocked, "no variant can play", 0, firedBy[clip]);
					continue;
				}
				for (auto* running : taken) {
					report(running->clip, ActorState::ClipReport::Kind::kInterrupted, std::format("clip {} took a channel", clip->GetName()), running->variant);
				}
				for (auto* running : taken) {
					interrupt(*running);
				}
				const bool bEcho = same != a_clips.running.end();
				if (bEcho) {
					a_clips.running.erase(same);
				}
				report(clip, bEcho ? ActorState::ClipReport::Kind::kEcho : ActorState::ClipReport::Kind::kStart, {}, *variant, firedBy[clip]);
				ModRegistry::GetSingleton().OnLoopOrEcho(clip, a_refr);

				// Started from what the previous frame had on each channel: the modifiers' value, or the channel's rest
				ActorState::RunningClip running;
				running.clip = clip;
				running.priority = clip->GetPriority();
				running.variant = *variant;
				running.startedAt = a_now;
				running.length = LengthOf(clip->GetTracks()[*variant]);
				if (previous) {
					CaptureStart(running, *previous, a_refr);
				} else {
					CaptureStart(running, ActorState::Sampled{}, a_refr);
				}
				a_clips.running.push_back(std::move(running));
				// Activated: its intervals rolled on activation roll again
				for (const auto& [trigger, identity] : IntervalsOf(clip)) {
					if (trigger->interval->reroll == Triggers::IntervalTriggerComponent::Reroll::kOnActivation) {
						if (const auto it = a_clips.intervals.find(trigger); it != a_clips.intervals.end()) {
							it->second.roll.reset();
						}
					}
				}
				activated.emplace_back(clip->GetFunctionSet(Functions::FunctionSetType::kOnActivate), clip);
				logger::info("clips: '{}' plays variant {} for {:.2f} s", clip->GetName(), *variant, a_clips.running.back().length);
			}

			a_clips.pending.clear();
			a_clips.passingLast = { a_passing.begin(), a_passing.end() };
			std::ranges::stable_sort(a_clips.running, [](const auto& a, const auto& b) { return a.priority < b.priority; });
		});

		// Outside the lock: a function may reach back into the actor's state
		for (const auto& [set, clip] : deactivated) {
			RunSet(set, a_refr, clip);
		}
		for (const auto& [set, clip] : activated) {
			RunSet(set, a_refr, clip);
		}
	}

	namespace
	{
		ActorState::PresetMix PresetAt(const ChannelKeys& a_keys, float a_time, const ActorState::PresetMix& a_from, const ActorState::PresetMix& a_underlay, const std::function<Resolve::PickContext(const Keyframe*)>& a_contextOf);
	}

	void FoldRun(ActorState::RunningClip& a_running, RE::TESObjectREFR* a_refr, float a_now, ActorState::Sampled& a_out, std::vector<std::pair<Functions::FunctionSet*, Clip*>>* a_functions)
	{
		auto& entries = Apply::Entries::GetSingleton();
			auto* clip = a_running.clip;
			if (a_running.variant >= clip->GetTracks().size()) {
				return;
			}
			const auto& track = clip->GetTracks()[a_running.variant];
			const float t = a_now - a_running.startedAt;
			const float exitU = a_running.bExiting ? (a_running.exitLength > 0.f ? std::clamp((a_now - a_running.exitStart) / a_running.exitLength, 0.f, 1.f) : 1.f) : 0.f;

			// The frames passed since the last fold run their functions, once each
			while (!a_running.bExiting && a_running.nextFunctionFrame < track.keyframes.size() && track.keyframes[a_running.nextFunctionFrame]->time <= t) {
				const auto& keyframe = track.keyframes[a_running.nextFunctionFrame++];
				if (keyframe->functions && !keyframe->functions->IsEmpty()) {
					if (a_functions) {
						a_functions->emplace_back(keyframe->functions.get(), clip);
					}
				}
			}

			// A random row draws for the actor and this clip, the same on every frame; again on each run when it asks
			const auto contextOf = [&](const Keyframe*) {
				return Resolve::PickContext{ a_refr, clip, a_running.activatedAt };
			};

			for (const auto id : ClaimedOf(clip)) {
				const auto keys = KeysOf(track, id);
				if (keys.keys.empty()) {
					continue;
				}
				const auto entry = entries.Get(id);
				const auto kind = static_cast<std::size_t>(id.GetKind());

				// A blended pick, the body preset: its sliders along the keys, from and back to what the modifiers have
				if (entry.type == Apply::ValueType::kBlendedPick) {
					auto value = a_running.bExiting ? Resolve::MixSliders(a_running.lastPreset, a_out.presetSliders, Resolve::CurveAt("Ease in out", exitU)) :
					                                  PresetAt(keys, t, a_running.startPreset, a_out.presetSliders, contextOf);
					a_running.lastPreset = value;
					a_out.presetSliders = std::move(value);
					continue;
				}

				// A stepped number runs as a number whose every way in and out is instant
				if (entry.type == Apply::ValueType::kNumber || entry.type == Apply::ValueType::kSteppedNumber) {
					auto* sample = SampleAt(a_out.numbers[kind], id.GetIndex());
					const float underlay = sample ? sample->value : RestFor(entry, id, a_refr);
					float value = underlay;
					if (a_running.bExiting) {
						// Interrupted: from where it was toward what is underneath, at once for a stepped number
						const auto it = a_running.lastNumbers.find(id.raw);
						const float from = it != a_running.lastNumbers.end() ? it->second : underlay;
						value = std::lerp(from, underlay, Resolve::CurveAt(entry.type == Apply::ValueType::kSteppedNumber ? "Instant" : "Ease in out", exitU));
					} else {
						const auto start = a_running.startNumbers.find(id.raw);
						value = NumberAt(keys, t, start != a_running.startNumbers.end() ? start->second : underlay, underlay, a_refr).first;
					}
					a_running.lastNumbers[id.raw] = value;
					Put(a_out.numbers[kind], Apply::NumberSample{ id.GetIndex(), value });
				} else if (entry.type == Apply::ValueType::kColour) {
					auto* sample = SampleAt(a_out.colours[kind], id.GetIndex());
					std::optional<RE::NiColor> underlay = sample ? std::optional(RE::NiColor{ sample->red, sample->green, sample->blue }) : std::nullopt;
					if (const auto own = a_running.ownColours.find(id.raw); !underlay && own != a_running.ownColours.end()) {
						underlay = RE::NiColor{ own->second[0], own->second[1], own->second[2] };
					}
					std::optional<RE::NiColor> value;
					if (a_running.bExiting) {
						const auto it = a_running.lastColours.find(id.raw);
						if (it != a_running.lastColours.end() && underlay) {
							value = Channels::MixColour({ it->second[0], it->second[1], it->second[2] }, *underlay, exitU, "sRGB"sv);
						}
					} else {
						const auto start = a_running.startColours.find(id.raw);
						const std::optional<RE::NiColor> from = start != a_running.startColours.end() ? std::optional(RE::NiColor{ start->second[0], start->second[1], start->second[2] }) : std::nullopt;
						value = ColourAt(keys, t, from, underlay).first;
					}
					if (value) {
						a_running.lastColours[id.raw] = { value->red, value->green, value->blue };
						Put(a_out.colours[kind], Apply::ColourSample{ id.GetIndex(), value->red, value->green, value->blue });
					}
				} else if (!a_running.bExiting) {
					// Set at once: the key reached, set until the next key or the clip's end
					if (auto* row = StepAt(keys, t)) {
						const auto key = std::ranges::find(keys.keys, row, &ChannelKey::row);
						WriteStep(row, entry, id, contextOf(key != keys.keys.end() ? key->keyframe : nullptr), a_out);
					}
				}
			}
	}

	std::vector<Apply::ChannelId> Claims(Clip* a_clip)
	{
		return ClaimedOf(a_clip);
	}

	ChannelKeys KeysOf(const ClipVariant& a_track, Apply::ChannelId a_id)
	{
		ChannelKeys out;
		out.length = LengthOf(a_track);
		out.last = a_track.keyframes.empty() ? nullptr : a_track.keyframes.back().get();
		for (const auto& keyframe : a_track.keyframes) {
			auto* row = FindRow(keyframe->channels.get(), a_id);
			if (!row || row->IsDisabled()) {
				continue;
			}
			// Within an earlier key's hold: barred
			if (!out.keys.empty() && keyframe->time < out.keys.back().holdEnd) {
				continue;
			}
			// The ways in and out read once, a colour's defaults a gradient in sRGB
			// A stepped number steps
			const bool bColour = dynamic_cast<Channels::ColourChannel*>(row) != nullptr;
			const auto way = bColour ? "Gradient"sv : Apply::Entries::GetSingleton().Get(a_id).type == Apply::ValueType::kSteppedNumber ? "Instant"sv : "Ease in out"sv;
			out.keys.push_back({ keyframe->time, keyframe->time + (keyframe->bHold ? keyframe->holdTime : 0.f), row, keyframe.get(),
				ModeNamed(row, "Transition"sv, way), NumberNamed(row, "Strength"sv, 0.3f), ModeNamed(row, "Colour space"sv, "sRGB"sv),
				ModeNamed(row, "Exit transition"sv, way), NumberNamed(row, "Exit strength"sv, 0.3f), ModeNamed(row, "Exit colour space"sv, "sRGB"sv) });
		}
		return out;
	}

	namespace
	{
		// The body preset along its keys, as NumberAt runs a morph: each key's preset as its sliders
		ActorState::PresetMix PresetAt(const ChannelKeys& a_keys, float a_time, const ActorState::PresetMix& a_from, const ActorState::PresetMix& a_underlay, const std::function<Resolve::PickContext(const Keyframe*)>& a_contextOf)
		{
			const auto& keys = a_keys.keys;
			const float t = a_time;
			const auto valueOf = [&](const ChannelKey& a_key) {
				if (!dynamic_cast<Channels::ReferenceChannel*>(a_key.row)) {
					WrongType(a_key.row, "body preset");
					return a_underlay;
				}
				return Resolve::PresetSliders(Resolve::NameOf(a_key.row, a_contextOf(a_key.keyframe)));
			};
			std::optional<std::size_t> reached;
			for (std::size_t i = 0; i < keys.size() && keys[i].time <= t; ++i) {
				reached = i;
			}
			if (!reached) {
				const auto& first = keys.front();
				const float u = first.time > 0.f ? t / first.time : 1.f;
				return Resolve::MixSliders(a_from, valueOf(first), Resolve::CurveAt(first.transition, u, first.strength));
			}
			const auto& key = keys[*reached];
			const auto held = valueOf(key);
			if (t < key.holdEnd) {
				return held;
			}
			if (*reached + 1 < keys.size()) {
				const auto& next = keys[*reached + 1];
				const float span = next.time - key.holdEnd;
				return Resolve::MixSliders(held, valueOf(next), Resolve::CurveAt(next.transition, span > 0.f ? (t - key.holdEnd) / span : 1.f, next.strength));
			}
			if (key.keyframe == a_keys.last) {
				return held;
			}
			const float span = a_keys.length - key.holdEnd;
			return Resolve::MixSliders(held, a_underlay, Resolve::CurveAt(key.exitTransition, span > 0.f ? std::clamp((t - key.holdEnd) / span, 0.f, 1.f) : 1.f, key.exitStrength));
		}
	}

	std::pair<float, Phase> NumberAt(const ChannelKeys& a_keys, float a_time, float a_from, float a_underlay, RE::TESObjectREFR* a_refr)
	{
		const auto& keys = a_keys.keys;
		if (keys.empty()) {
			return { a_underlay, Phase::kNone };
		}
		const float t = a_time;
		const auto valueOf = [&](const ChannelKey& a_key) {
			auto* number = dynamic_cast<Channels::NumberChannel*>(a_key.row);
			if (!number) {
				WrongType(a_key.row, "number");
				return a_underlay;
			}
			return Resolve::NumberValue(number, a_refr);
		};

		// The key reached, if any
		std::optional<std::size_t> reached;
		for (std::size_t i = 0; i < keys.size() && keys[i].time <= t; ++i) {
			reached = i;
		}
		if (!reached) {
			// On the way to the first key, from what the run started from
			const auto& first = keys.front();
			const float u = first.time > 0.f ? t / first.time : 1.f;
			return { std::lerp(a_from, valueOf(first), Resolve::CurveAt(first.transition, u, first.strength)), Phase::kEntering };
		}
		const auto& key = keys[*reached];
		const float held = valueOf(key);
		if (t < key.holdEnd) {
			return { held, Phase::kHeld };
		}
		if (*reached + 1 < keys.size()) {
			// On to the next key, once this one's hold is over
			const auto& next = keys[*reached + 1];
			const float span = next.time - key.holdEnd;
			const float u = span > 0.f ? (t - key.holdEnd) / span : 1.f;
			return { std::lerp(held, valueOf(next), Resolve::CurveAt(next.transition, u, next.strength)), Phase::kMoving };
		}
		if (key.keyframe == a_keys.last) {
			// Still set on the last frame: held to the end, then handed back at once
			return { held, Phase::kSteady };
		}
		// Past its last key: out to what is underneath by the clip's end
		const float span = a_keys.length - key.holdEnd;
		const float u = span > 0.f ? std::clamp((t - key.holdEnd) / span, 0.f, 1.f) : 1.f;
		return { std::lerp(held, a_underlay, Resolve::CurveAt(key.exitTransition, u, key.exitStrength)), Phase::kExiting };
	}

	std::pair<std::optional<RE::NiColor>, Phase> ColourAt(const ChannelKeys& a_keys, float a_time, const std::optional<RE::NiColor>& a_from, const std::optional<RE::NiColor>& a_underlay)
	{
		const auto& keys = a_keys.keys;
		if (keys.empty()) {
			return { a_underlay, Phase::kNone };
		}
		const float t = a_time;
		const auto colourOf = [&](const ChannelKey& a_key) {
			auto* colour = dynamic_cast<Channels::ColourChannel*>(a_key.row);
			if (!colour) {
				WrongType(a_key.row, "colour");
				return a_underlay.value_or(RE::NiColor{ 1.f, 1.f, 1.f });
			}
			return colour->value->value.GetValue();
		};
		// Mixed along a gradient in its space, or set at the far end
		const auto mix = [](const RE::NiColor& a_a, const RE::NiColor& a_b, float a_u, const std::string& a_transition, const std::string& a_space) {
			if (a_transition != "Gradient"sv) {
				return a_u >= 1.f ? a_b : a_a;
			}
			return Channels::MixColour(a_a, a_b, a_u, a_space);
		};

		std::optional<std::size_t> reached;
		for (std::size_t i = 0; i < keys.size() && keys[i].time <= t; ++i) {
			reached = i;
		}
		if (!reached) {
			const auto& first = keys.front();
			if (!a_from) {
				return { std::nullopt, Phase::kEntering };
			}
			const float u = first.time > 0.f ? t / first.time : 1.f;
			return { mix(*a_from, colourOf(first), u, first.transition, first.space), Phase::kEntering };
		}
		const auto& key = keys[*reached];
		const auto held = colourOf(key);
		if (t < key.holdEnd) {
			return { held, Phase::kHeld };
		}
		if (*reached + 1 < keys.size()) {
			const auto& next = keys[*reached + 1];
			const float span = next.time - key.holdEnd;
			return { mix(held, colourOf(next), span > 0.f ? (t - key.holdEnd) / span : 1.f, next.transition, next.space), Phase::kMoving };
		}
		if (key.keyframe == a_keys.last) {
			return { held, Phase::kSteady };
		}
		if (!a_underlay) {
			return { std::nullopt, Phase::kExiting };
		}
		const float span = a_keys.length - key.holdEnd;
		return { mix(held, *a_underlay, span > 0.f ? std::clamp((t - key.holdEnd) / span, 0.f, 1.f) : 1.f, key.exitTransition, key.exitSpace), Phase::kExiting };
	}

	Channels::Channel* StepAt(const ChannelKeys& a_keys, float a_time)
	{
		const auto& keys = a_keys.keys;
		std::optional<std::size_t> reached;
		for (std::size_t i = 0; i < keys.size() && keys[i].time <= a_time; ++i) {
			reached = i;
		}
		if (!reached) {
			return nullptr;
		}
		// Set until the next key or the clip's end, as a mode or a switch is: it has no way out to ease along
		return a_time <= a_keys.length ? keys[*reached].row : nullptr;
	}

	std::pair<float, Phase> NumberAt(const ClipVariant& a_track, Apply::ChannelId a_id, float a_time, float a_from, float a_underlay, RE::TESObjectREFR* a_refr)
	{
		return NumberAt(KeysOf(a_track, a_id), a_time, a_from, a_underlay, a_refr);
	}

	std::pair<std::optional<RE::NiColor>, Phase> ColourAt(const ClipVariant& a_track, Apply::ChannelId a_id, float a_time, const std::optional<RE::NiColor>& a_from, const std::optional<RE::NiColor>& a_underlay)
	{
		return ColourAt(KeysOf(a_track, a_id), a_time, a_from, a_underlay);
	}

	Channels::Channel* StepAt(const ClipVariant& a_track, Apply::ChannelId a_id, float a_time)
	{
		return StepAt(KeysOf(a_track, a_id), a_time);
	}

	float LengthOf(const ClipVariant& a_track)
	{
		if (a_track.keyframes.empty()) {
			return 0.f;
		}
		const auto& last = a_track.keyframes.back();
		return last->time + (last->bHold ? last->holdTime : 0.f);
	}

	std::optional<RE::NiColor> OwnColour(RE::TESObjectREFR* a_refr, Apply::ChannelId a_id)
	{
		// An overlay nothing colours is drawn as its applier falls back: tinted white, glowing black
		if (a_id.GetKind() == Apply::Kind::kOverlay) {
			const auto name = Apply::Entries::GetSingleton().Get(a_id).name;
			if (name.ends_with(".tint")) {
				return RE::NiColor{ 1.f, 1.f, 1.f };
			}
			if (name.ends_with(".glow")) {
				return RE::NiColor{ 0.f, 0.f, 0.f };
			}
			return std::nullopt;
		}
		if (a_id.GetKind() != Apply::Kind::kMaterial || a_id.GetIndex() >= static_cast<std::uint32_t>(Apply::Material::Slot::kTotal)) {
			return std::nullopt;
		}
		return Apply::Material::OwnColour(a_refr ? a_refr->As<RE::Actor>() : nullptr, static_cast<Apply::Material::Slot>(a_id.GetIndex()));
	}

	void CaptureStart(ActorState::RunningClip& a_running, const ActorState::Sampled& a_under, RE::TESObjectREFR* a_refr)
	{
		a_running.activatedAt = a_running.startedAt;
		auto& entries = Apply::Entries::GetSingleton();
		for (const auto id : ClaimedOf(a_running.clip)) {
			const auto entry = entries.Get(id);
			const auto kind = static_cast<std::size_t>(id.GetKind());
			if (entry.type == Apply::ValueType::kBlendedPick) {
				a_running.startPreset = a_under.presetSliders;
			} else if (entry.type == Apply::ValueType::kNumber || entry.type == Apply::ValueType::kSteppedNumber) {
				auto* sample = SampleAt(a_under.numbers[kind], id.GetIndex());
				a_running.startNumbers[id.raw] = sample ? sample->value : RestFor(entry, id, a_refr);
			} else if (entry.type == Apply::ValueType::kColour) {
				// No modifier's colour: the actor's own, read once for the run, to come from and go back to
				if (const auto own = OwnColour(a_refr, id)) {
					a_running.ownColours[id.raw] = { own->red, own->green, own->blue };
				}
				if (auto* sample = SampleAt(a_under.colours[kind], id.GetIndex())) {
					a_running.startColours[id.raw] = { sample->red, sample->green, sample->blue };
				} else if (const auto own = a_running.ownColours.find(id.raw); own != a_running.ownColours.end()) {
					a_running.startColours[id.raw] = own->second;
				}
			}
		}
	}

	void Fold(ActorState::State& a_state, RE::TESObjectREFR* a_refr, float a_now, ActorState::Sampled& a_out)
	{
		auto& entries = Apply::Entries::GetSingleton();
		std::vector<std::pair<Functions::FunctionSet*, Clip*>> frameFunctions;

		a_state.clips.With([&](ActorState::ClipState& a_clips) {
			// Lowest first, so a higher clip's value is the one left
			for (auto& running : a_clips.running) {
				FoldRun(running, a_refr, a_now, a_out, &frameFunctions);
			}
		});

		for (const auto& [set, clip] : frameFunctions) {
			RunSet(set, a_refr, clip);
		}
	}
}
