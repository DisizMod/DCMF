#include "Channels.h"

#include "ModRegistry.h"
#include "RegisteredMods.h"
#include "Resources.h"
#include "SkinSets.h"
#include "apply/Entries.h"
#include "OverlaySlots.h"
#include "UI/UIManager.h"
#include "apply/HeadPart.h"
#include "scan/Nif.h"
#include "scan/Scan.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <format>

namespace Channels
{
	Apply::ChannelId Channel::GetId() const
	{
		return Apply::Entries::GetSingleton().Find(kind, identifier);
	}

	std::string Channel::GetName() const
	{
		return std::format("{}:{}", Apply::KindName(kind), identifier);
	}

	std::string Channel::GetArgument() const
	{
		return ChannelBase::GetArgument();
	}

	NumberChannel::NumberChannel(float a_low, float a_high, float a_start)
	{
		value = AddComponent<NumberChannelComponent>("Value"sv, "What the channel is set to."sv, a_low, a_high, a_start);
		value->value.getFeeds = [] { return Resources::FeedKeys(Resources::ValueType::Number); };
		readLow = AddComponent<BoundChannelComponent>("Read low"sv, "The lowest the read value goes."sv, 0.f, value);
		readHigh = AddComponent<BoundChannelComponent>("Read high"sv, "The highest the read value goes."sv, 1.f, value);
		channelLow = AddComponent<BoundChannelComponent>("Channel low"sv, "What the read low becomes on the channel."sv, a_low, value);
		channelHigh = AddComponent<BoundChannelComponent>("Channel high"sv, "What the read high becomes on the channel."sv, a_high, value);
		blend = AddComponent<BlendChannelComponent>("Blend"sv, "Claim the channel, or blend this value with what lower priority modifiers set."sv);
	}

	ColourChannel::ColourChannel()
	{
		value = AddComponent<ColourChannelComponent>("Value"sv, "What the channel is set to."sv);
	}

	ModeChannel::ModeChannel(std::vector<std::string> a_modes)
	{
		value = AddComponent<ModeChannelComponent>("Value"sv, "The mode, by name."sv, std::move(a_modes));
	}

	namespace
	{
		// The mod a row belongs to, through its set's clip or modifier
		const RegisteredMod* ModOf(const Channel* a_channel)
		{
			auto* set = a_channel->GetParentSet();
			auto* subMod = set ? set->GetParentSubMod() : nullptr;
			return subMod ? subMod->GetParentMod() : nullptr;
		}

		// One of the mod's pools of a channel, its entries
		PickList* PoolEntries(const Channel* a_channel, std::string_view a_name)
		{
			const auto* mod = ModOf(a_channel);
			auto* pool = mod ? mod->FindReferencePool(a_name) : nullptr;
			return pool && pool->GetChannel() == a_channel->GetName() ? &pool->entries : nullptr;
		}

		// The mod's pools of a channel, by name
		std::vector<std::string> PoolsOf(const Channel* a_channel)
		{
			std::vector<std::string> out;
			const auto* mod = ModOf(a_channel);
			if (!mod) {
				return out;
			}
			const auto identifier = a_channel->GetName();
			// The mod's own, then each required mod's; a name already listed is the one the mod's own lookup finds
			const auto add = [&](const RegisteredMod* a_mod) {
				a_mod->ForEachReferencePool([&](ReferencePool* a_pool) {
					if (a_pool->GetChannel() == identifier && std::ranges::find(out, a_pool->GetName()) == out.end()) {
						out.emplace_back(a_pool->GetName());
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			};
			add(mod);
			mod->ForEachRequiredMod(add);
			return out;
		}
	}

	ReferenceChannel::ReferenceChannel(ReferenceChannelComponent::ItemsFn a_items)
	{
		// Set, or drawn among entries made as the value is, once the channel knows which it is
		value = AddComponent<ReferenceChannelComponent>("Value"sv, "What the channel is set to, typed or picked; or a draw among entries and the mod's pools, each by its weight, held for the state data scope."sv, std::move(a_items));
		value->random = std::make_unique<RandomValue>([this] { return ValueFactoryFor(GetId())(); }, PoolSource{ [this] { return PoolsOf(this); }, [this](std::string_view a_name) { return PoolEntries(this, a_name); } });
		auto* state = AddComponent<RandomStateChannelComponent>("State"sv, "What a Random Value's draw is held for, and what shares it."sv, value->random.get());
		state->shownWhen = [value = value] { return value->bRandom; };
	}

	HeadPartChannel::HeadPartChannel(RE::BGSHeadPart::HeadPartType a_type)
	{
		value = AddComponent<HeadPartChannelComponent>("Value"sv, "The part, picked from every scanned one of the slot's type; or a draw among parts and the mod's pools, each by its weight, held for the state data scope."sv, a_type);
		value->form.getFeeds = [] { return Resources::FeedKeys(Resources::ValueType::Form); };
		value->random = std::make_unique<RandomValue>([this] { return ValueFactoryFor(GetId())(); }, PoolSource{ [this] { return PoolsOf(this); }, [this](std::string_view a_name) { return PoolEntries(this, a_name); } });
		auto* state = AddComponent<RandomStateChannelComponent>("State"sv, "What a Random Value's draw is held for, and what shares it."sv, value->random.get());
		state->shownWhen = [value = value] { return value->bRandom; };
	}

	namespace
	{
		// The human skeleton's nodes, read once from the installed one, for the point pickers; a creature's are typed
		std::vector<UI::UICommon::PickerItem> SkeletonBones()
		{
			static std::vector<UI::UICommon::PickerItem> items;
			static bool bRead = false;
			if (!bRead) {
				bRead = true;
				const auto bytes = Scan::Files::Read("meshes\\actors\\character\\character assets\\skeleton.nif");
				std::vector<std::string> names;
				std::string error;
				if (!bytes.empty() && Scan::Nif::ReadNodeNames(bytes, names, error)) {
					for (const auto& name : names) {
						items.push_back({ name, {}, items.size() });
					}
				} else {
					logger::warn("pose: the human skeleton could not be read for the bone picker: {}", bytes.empty() ? "no skeleton.nif" : error);
				}
			}
			return items;
		}

		// Every BodySlide preset the scan found, by its group
		std::vector<UI::UICommon::PickerItem> BodyPresets()
		{
			std::vector<UI::UICommon::PickerItem> items;
			if (const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue()) {
				for (const auto& preset : catalogue->bodyPresets) {
					items.push_back({ preset.name, preset.groups.empty() ? std::string{} : preset.groups.front() });
				}
			}
			// The picker draws a group once: each group's presets together
			std::ranges::sort(items, [](const auto& a, const auto& b) { return a.group != b.group ? a.group < b.group : a.label < b.label; });
			return items;
		}
	}

	FormChannel::FormChannel()
	{
		value = AddComponent<FormChannelComponent>("Value"sv, "The reference, by plugin and form id."sv);
		value->form.SetFormTypeFilter(RE::FormType::ActorCharacter);  // the picker's actors; an object is typed by plugin and form id
	}

	SwitchChannel::SwitchChannel()
	{
		value = AddComponent<SwitchChannelComponent>("Value"sv, "Whether the channel is on."sv);
	}

	// ---- the set ----

	void ChannelSet::SetAsParentImpl(std::unique_ptr<ChannelBase>& a_channel)
	{
		a_channel->SetParentSet(this);
	}

	bool ChannelSet::IsChildOfImpl(ChannelBase* a_channel)
	{
		if (!a_channel || !a_channel->IsGroup()) {
			return false;
		}
		const auto* group = static_cast<GroupChannel*>(a_channel);
		if (group->channels.get() == this) {
			return true;
		}
		for (const auto& child : group->channels->_entries) {
			if (IsChildOf(child.get())) {
				return true;
			}
		}
		return false;
	}

	SubMod* ChannelSet::GetParentSubModImpl() const
	{
		if (_parentSubMod) {
			return _parentSubMod;
		}
		if (_parentGroup) {
			if (const auto* parentSet = _parentGroup->GetParentSet()) {
				return parentSet->GetParentSubMod();
			}
		}
		return nullptr;
	}

	std::string ChannelSet::NumTextImpl() const
	{
		return Num() == 1 ? "1 channel" : std::format("{} channels", Num());
	}

	bool ChannelSet::IsDirtyRecursiveImpl() const
	{
		ReadLocker locker(_lock);
		if (IsDirty()) {
			return true;
		}
		for (const auto& channel : _entries) {
			if (channel->IsGroup() && static_cast<GroupChannel*>(channel.get())->channels->IsDirtyRecursive()) {
				return true;
			}
		}
		return false;
	}

	void ChannelSet::SetDirtyRecursiveImpl(bool a_bDirty)
	{
		ReadLocker locker(_lock);
		for (const auto& channel : _entries) {
			if (channel->IsGroup()) {
				static_cast<GroupChannel*>(channel.get())->channels->SetDirtyRecursiveImpl(a_bDirty);
			}
		}
		SetDirty(a_bDirty);
	}

	bool ChannelSet::Contains(Apply::ChannelId a_id, const ChannelBase* a_except)
	{
		ReadLocker locker(_lock);
		for (const auto& channel : _entries) {
			if (channel.get() == a_except) {
				continue;
			}
			if (channel->IsGroup()) {
				if (static_cast<GroupChannel*>(channel.get())->channels->Contains(a_id, a_except)) {
					return true;
				}
			} else if (static_cast<Channel*>(channel.get())->GetId() == a_id) {
				return true;
			}
		}
		return false;
	}

	// ---- the group ----

	GroupChannel::GroupChannel()
	{
		channels = std::make_unique<ChannelSet>(this);
		// A change inside the group is a change of the set it sits in: a clip's frames follow its claims through it
		channels->AddOnDirtyCallback([this] {
			if (auto* set = GetParentSet(); set && channels->IsDirty()) {
				set->SetDirty(true);
			}
		});
	}

	void GroupChannel::Parse(const rapidjson::Value& a_value)
	{
		ChannelBase::Parse(a_value);
		if (!a_value.IsObject()) {
			return;
		}
		const auto object = a_value.GetObj();
		if (const auto it = object.FindMember("group"); it != object.MemberEnd() && it->value.IsArray()) {
			for (const auto& child : it->value.GetArray()) {
				auto channel = CreateChannelFromJson(child);
				if (!channel) {
					logger::error("channels: a group's row no table names is not read: {}", child.IsObject() && child.HasMember("channel") && child["channel"].IsString() ? child["channel"].GetString() : "(no channel)");
					continue;
				}
				if (const auto invalid = InvalidModeOf(*channel); !invalid.empty()) {
					logger::error("channels: a group's row {} is not read: {}", channel->GetName(), invalid);
					continue;
				}
				channels->Add(channel);
			}
		}
	}

	void GroupChannel::Serialize(void* a_value, void* a_allocator) const
	{
		ChannelBase::Serialize(a_value, a_allocator);
		auto& object = *static_cast<rapidjson::Value*>(a_value);
		auto& allocator = *static_cast<rapidjson::Document::AllocatorType*>(a_allocator);
		object.AddMember("group", channels->Serialize(allocator), allocator);
	}

	// ---- making them ----

	namespace
	{
		// The reference the editor inspects, as an actor; none when it is not one
		RE::Actor* ReferenceActor()
		{
			auto* reference = UI::UIManager::GetSingleton().GetRefrToEvaluate();
			return reference ? reference->As<RE::Actor>() : nullptr;
		}

		// Why an overlay slot's item would not be drawn on the reference; empty when it would, or when there is none
		std::function<std::string(std::string_view)> OverlayWarning(Apply::ChannelId a_id, const Apply::Entry& a_entry)
		{
			if (a_id.GetKind() != Apply::Kind::kOverlay) {
				return nullptr;
			}
			return [slot = a_entry.name](std::string_view a_item) {
				std::string why;
				auto* reference = ReferenceActor();
				Overlays::ForEachItem(slot, [&](Overlays::Slot& a_slot, std::string_view, Overlays::Item& a_overlay) {
					if (!a_overlay.bDisabled && a_overlay.name == a_item) {
						why = Overlays::WhySkipped(a_slot, a_overlay, reference);
						return false;
					}
					return true;
				});
				return why;
			};
		}

		// A reference channel's picker: a pose point's bones, the body presets, an overlay slot's pool, the skin sets;
		// anything else typed
		ReferenceChannelComponent::ItemsFn ItemsFor(Apply::ChannelId a_id, const Apply::Entry& a_entry)
		{
			if (a_entry.type == Apply::ValueType::kText) {
				return a_entry.name == "targetBone"sv || a_entry.name == "pairedBone"sv ? ReferenceChannelComponent::ItemsFn(SkeletonBones) : nullptr;
			}
			if (a_entry.type == Apply::ValueType::kBlendedPick) {
				return ReferenceChannelComponent::ItemsFn(BodyPresets);
			}
			if (a_id.GetKind() == Apply::Kind::kOverlay) {
				return [slot = a_entry.name] {
					std::vector<UI::UICommon::PickerItem> items;
					auto* reference = ReferenceActor();
					Overlays::ForEachItem(slot, [&](Overlays::Slot& a_slot, std::string_view a_mod, Overlays::Item& a_item) {
						if (!a_item.bDisabled) {
							items.push_back({ .label = a_item.name, .group = std::string(a_mod), .warning = Overlays::WhySkipped(a_slot, a_item, reference) });
						}
						return true;
					});
					return items;
				};
			}
			if (a_id.GetKind() == Apply::Kind::kSkin) {
				return [] {
					std::vector<UI::UICommon::PickerItem> items;
					for (const auto& listed : Skins::ListSets()) {
						items.push_back({ .label = std::string(listed.set->GetName()), .group = std::string(listed.mod) });
					}
					return items;
				};
			}
			return nullptr;
		}
	}

	ValueFactory ValueFactoryFor(Apply::ChannelId a_id)
	{
		const auto entry = Apply::Entries::GetSingleton().Get(a_id);
		return [a_id, entry]() -> std::unique_ptr<IChannelComponent> {
			if (a_id.GetKind() == Apply::Kind::kHeadPart && a_id.GetIndex() < Apply::HeadPart::Slots().size()) {
				return std::make_unique<HeadPartChannelComponent>("Value"sv, "The part, picked from every scanned one of the slot's type."sv, Apply::HeadPart::Slots()[a_id.GetIndex()].type);
			}
			auto value = std::make_unique<ReferenceChannelComponent>("Value"sv, "What the entry is, typed or picked."sv, ItemsFor(a_id, entry));
			value->warningOf = OverlayWarning(a_id, entry);
			return value;
		};
	}

	std::unique_ptr<ChannelBase> CreateChannel(Apply::ChannelId a_id)
	{
		if (!a_id.IsValid()) {
			return nullptr;
		}
		const auto& entries = Apply::Entries::GetSingleton();
		const auto entry = entries.Get(a_id);
		std::unique_ptr<Channel> channel;
		switch (entry.type) {
		case Apply::ValueType::kColour:
			channel = std::make_unique<ColourChannel>();
			break;
		case Apply::ValueType::kMode:
			channel = std::make_unique<ModeChannel>(entry.modes);
			break;
		case Apply::ValueType::kText:
		case Apply::ValueType::kBlendedPick:
			// A pose's point is a bone, picked from the human skeleton's or typed for a creature's; the body preset
			// from every preset
			channel = std::make_unique<ReferenceChannel>(ItemsFor(a_id, entry));
			break;
		case Apply::ValueType::kForm:
			channel = std::make_unique<FormChannel>();
			break;
		case Apply::ValueType::kReference:
			// A head part slot picks its part from pictures, an overlay slot from its pool, the skin from every set; anything else is typed
			if (a_id.GetKind() == Apply::Kind::kHeadPart && a_id.GetIndex() < Apply::HeadPart::Slots().size()) {
				channel = std::make_unique<HeadPartChannel>(Apply::HeadPart::Slots()[a_id.GetIndex()].type);
			} else {
				auto reference = std::make_unique<ReferenceChannel>(ItemsFor(a_id, entry));
				reference->value->warningOf = OverlayWarning(a_id, entry);
				channel = std::move(reference);
			}
			break;
		case Apply::ValueType::kSwitch:
			channel = std::make_unique<SwitchChannel>();
			break;
		default: {
			// Where the slider starts: the entry's start when it has one, else its neutral, on the range; a pose's neutral
			// only marks it undriven, so it starts at its start
			const auto kind = a_id.GetKind();
			const bool bPose = kind == Apply::Kind::kSpine || kind == Apply::Kind::kHead || kind == Apply::Kind::kGaze;
			const float start = std::clamp(bPose || entry.start != 0.f ? entry.start : entry.neutral, entry.low, entry.high);
			channel = std::make_unique<NumberChannel>(entry.low, entry.high, start);
			break;
		}
		}
		channel->kind = a_id.GetKind();
		channel->identifier = entry.name;
		return channel;
	}

	std::unique_ptr<ChannelBase> CreateChannel(std::string_view a_name)
	{
		if (a_name == kGroupName || a_name == "Group"sv) {
			return std::make_unique<GroupChannel>();
		}
		return CreateChannel(Apply::Entries::GetSingleton().Find(a_name));
	}

	std::string InvalidModeOf(ChannelBase& a_channel)
	{
		for (std::uint32_t i = 0; i < a_channel.GetNumComponents(); ++i) {
			const auto* mode = dynamic_cast<const ModeChannelComponent*>(a_channel.GetComponent(i));
			if (!mode || mode->modes.empty() || std::ranges::find(mode->modes, mode->value) != mode->modes.end()) {
				continue;
			}
			std::string listed;
			for (const auto& one : mode->modes) {
				listed += listed.empty() ? one : ", " + one;
			}
			return std::format("{} '{}' is not one of: {}", mode->GetName(), mode->value, listed);
		}
		return {};
	}

	std::unique_ptr<ChannelBase> CreateChannelFromJson(const rapidjson::Value& a_value)
	{
		if (!a_value.IsObject()) {
			return nullptr;
		}
		const auto object = a_value.GetObj();
		std::unique_ptr<ChannelBase> channel;
		if (object.HasMember("group")) {
			channel = std::make_unique<GroupChannel>();
		} else if (const auto it = object.FindMember("channel"); it != object.MemberEnd() && it->value.IsString()) {
			channel = CreateChannel(std::string_view(it->value.GetString()));
		}
		if (channel) {
			channel->Parse(a_value);
		}
		return channel;
	}

	std::unique_ptr<ChannelSet> DuplicateChannelSet(ChannelSet* a_channelSet)
	{
		rapidjson::Document doc(rapidjson::kObjectType);
		const rapidjson::Value serialized = a_channelSet->Serialize(doc.GetAllocator());

		auto out = std::make_unique<ChannelSet>();
		for (const auto& value : serialized.GetArray()) {
			if (auto channel = CreateChannelFromJson(value)) {
				out->Add(channel);
			} else {
				rapidjson::StringBuffer buffer;
				rapidjson::Writer writer(buffer);
				value.Accept(writer);
				logger::warn("channels: could not copy {}", buffer.GetString());
			}
		}
		logger::info("channels: copied {} of {}", out->Num(), a_channelSet->Num());
		return out;
	}
}
