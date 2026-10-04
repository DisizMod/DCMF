#include "Triggers.h"

#include "AnimationGraph.h"
#include "ModRegistry.h"
#include "RegisteredMods.h"

#include <format>
#include <map>

namespace Triggers
{
	void TriggerPresetComponent::Parse(const rapidjson::Value& a_value)
	{
		if (a_value.IsString()) {
			presetName = a_value.GetString();
		}
	}

	rapidjson::Value TriggerPresetComponent::Serialize(rapidjson::Document::AllocatorType& a_allocator)
	{
		return rapidjson::Value(presetName.data(), static_cast<rapidjson::SizeType>(presetName.size()), a_allocator);
	}

	TriggerPreset* TriggerPresetComponent::GetPreset() const
	{
		return mod && !presetName.empty() ? mod->FindTriggerPreset(presetName) : nullptr;
	}

	bool TriggerPresetComponent::IsValid() const
	{
		// Unknown until the mod is known; then it has to name a preset with a trigger in it
		if (!mod) {
			return !presetName.empty();
		}
		const auto* preset = GetPreset();
		return preset && !preset->IsEmpty();
	}

	bool TriggerPresetComponent::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;
		if (a_bEditable && mod) {
			ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
			ImGui::PushID(&presetName);
			if (ImGui::BeginCombo("##Preset", presetName.data())) {
				const auto list = [&](RegisteredMod* a_mod) {
					a_mod->ForEachTriggerPreset([&](TriggerPreset* a_preset) {
						const auto name = a_preset->GetName();
						const bool bSelected = GetPreset() == a_preset;
						if (ImGui::Selectable(std::format("{}##{}", name, reinterpret_cast<std::uintptr_t>(a_preset)).data(), bSelected) && !bSelected) {
							presetName = name;
							bEdited = true;
						}
						if (bSelected) {
							ImGui::SetItemDefaultFocus();
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				};
				// The mod's own, then each mod it requires under its name
				list(mod);
				mod->ForEachRequiredMod([&](RegisteredMod* a_required) {
					ImGui::SeparatorText(a_required->GetName().data());
					list(a_required);
				});
				ImGui::EndCombo();
			}
			ImGui::PopID();
			if (const auto* preset = GetPreset()) {
				UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
				UI::UICommon::TextUnformattedEllipsis(preset->GetDescription().data());
			}
		} else {
			ImGui::TextUnformatted(presetName.data());
		}
		return bEdited;
	}

	PresetTrigger::PresetTrigger()
	{
		preset = AddComponent<TriggerPresetComponent>("Preset"sv, "The trigger preset whose triggers fire this clip."sv);
	}

	bool TriggerSetContainsPreset(TriggerSet* a_triggerSet, const TriggerPreset* a_preset)
	{
		bool bFound = false;
		a_triggerSet->ForEach([&](std::unique_ptr<TriggerBase>& a_trigger) {
			if (auto* presetTrigger = dynamic_cast<PresetTrigger*>(a_trigger.get()); presetTrigger && presetTrigger->preset->GetPreset() == a_preset) {
				bFound = true;
				return RE::BSVisit::BSVisitControl::kStop;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return bFound;
	}

	// Every dialogue topic with a line, by its editor id or name and its plugin, in load order
	std::vector<UI::UICommon::AsyncPickerGroup> TopicGroups()
	{
		struct Entry
		{
			std::uint32_t order;
			bool bUnnamed;
			UI::UICommon::AsyncPickerGroup group;
		};
		std::vector<Entry> entries;
		if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
			for (auto* topic : dataHandler->GetFormArray<RE::TESTopic>()) {
				if (!topic || topic->numTopicInfos == 0) {
					continue;
				}
				std::string_view name = topic->GetFormEditorID();
				if (name.empty()) {
					name = topic->GetName();
				}
				const auto* file = topic->GetFile(0);
				const auto localID = !file ? topic->GetFormID() : file->IsLight() ? (topic->GetFormID() & 0xFFF) : (topic->GetFormID() & 0xFFFFFF);
				UI::UICommon::AsyncPickerGroup group;
				group.label = std::format("{} ({:X})", name.empty() ? "No name"sv : name, localID);
				group.key = topic->GetFormID();
				group.section = file ? std::string(file->GetFilename()) : std::string("Created in game");
				entries.push_back({ file ? file->GetPartialIndex() : 0xFFFFFFFF, name.empty(), std::move(group) });
			}
		}
		// By plugin in load order, each plugin's named topics before its unnamed ones
		std::ranges::sort(entries, [](const Entry& a, const Entry& b) {
			if (a.order != b.order) {
				return a.order < b.order;
			}
			if (a.bUnnamed != b.bUnnamed) {
				return !a.bUnnamed;
			}
			return a.group.label < b.group.label;
		});
		std::vector<UI::UICommon::AsyncPickerGroup> out;
		out.reserve(entries.size());
		for (auto& entry : entries) {
			out.push_back(std::move(entry.group));
		}
		return out;
	}

	namespace
	{
		// A topic's lines, each its first response's text read from its plugin, "<text> (ID)"; main thread only
		std::vector<UI::UICommon::PickerItem> TopicLines(std::uint64_t a_topic)
		{
			std::vector<UI::UICommon::PickerItem> out;
			auto* topic = RE::TESForm::LookupByID<RE::TESTopic>(static_cast<RE::FormID>(a_topic));
			if (!topic || !topic->topicInfos) {
				return out;
			}
			for (std::uint32_t i = 0; i < topic->numTopicInfos; ++i) {
				auto* info = topic->topicInfos[i];
				if (!info) {
					continue;
				}
				std::string text;
				RE::TESTopicInfo::TESResponseList list{ nullptr };
				info->GetResponseList(&list);
				if (list.head && list.head->responseText.c_str()) {
					text = list.head->responseText.c_str();
				}
				// TODO: the list's nodes are not freed: who owns them is not known, and freeing them corrupted the heap.
				// A few hundred bytes per line, read once per topic per session
				const auto* file = info->GetFile(0);
				const auto localID = !file ? info->GetFormID() : file->IsLight() ? (info->GetFormID() & 0xFFF) : (info->GetFormID() & 0xFFFFFF);
				UI::UICommon::PickerItem item;
				item.label = text.empty() ? std::format("(no text) ({:X})", localID) : std::format("{} ({:X})", text, localID);
				item.value = info->GetFormID();
				out.push_back(std::move(item));
			}
			return out;
		}
	}

	OnDialogueLineTrigger::OnDialogueLineTrigger()
	{
		topicInfo = AddComponent<FormTriggerComponent>("Topic info"sv, "The line to fire on, a topic info. Pick lists the dialogue topics; opening one reads its lines."sv);
		topicInfo->value.SetFormTypeFilter(RE::FormType::Info);
		topicInfo->value.pickOverride = [](std::uint64_t a_current, std::uint64_t& a_picked) {
			return UI::UICommon::AsyncPicker("TopicInfo", TopicGroups, TopicLines, a_current, a_picked);
		};
	}

	void ModifierTriggerComponent::Parse(const rapidjson::Value& a_value)
	{
		if (!a_value.IsObject()) {
			return;
		}
		if (const auto it = a_value.FindMember("mod"); it != a_value.MemberEnd() && it->value.IsString()) {
			modName = it->value.GetString();
		}
		if (const auto it = a_value.FindMember("modifier"); it != a_value.MemberEnd() && it->value.IsString()) {
			modifierName = it->value.GetString();
		}
	}

	rapidjson::Value ModifierTriggerComponent::Serialize(rapidjson::Document::AllocatorType& a_allocator)
	{
		rapidjson::Value object(rapidjson::kObjectType);
		if (!modName.empty()) {
			object.AddMember("mod", rapidjson::Value(modName.data(), static_cast<rapidjson::SizeType>(modName.size()), a_allocator), a_allocator);
		}
		object.AddMember("modifier", rapidjson::Value(modifierName.data(), static_cast<rapidjson::SizeType>(modifierName.size()), a_allocator), a_allocator);
		return object;
	}

	bool ModifierTriggerComponent::Names(const SubMod* a_modifier, const RegisteredMod* a_ownMod) const
	{
		if (!a_modifier || a_modifier->GetName() != modifierName) {
			return false;
		}
		const auto* parent = a_modifier->GetParentMod();
		return parent && (modName.empty() ? parent == a_ownMod : parent->GetName() == modName);
	}

	bool ModifierTriggerComponent::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;
		if (a_bEditable && mod) {
			// The clip's own mod first, then each mod it requires under its name
			ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
			ImGui::PushID(this);
			if (ImGui::BeginCombo("##Modifier", GetArgument().data())) {
				const auto list = [&](RegisteredMod* a_mod, bool a_bOwn) {
					a_mod->ForEachSubMod([&](SubMod* a_subMod) {
						if (a_subMod->GetKind() != SubModKind::kModifier) {
							return RE::BSVisit::BSVisitControl::kContinue;
						}
						const bool bSelected = modifierName == a_subMod->GetName() && (a_bOwn ? modName.empty() : modName == a_mod->GetName());
						if (ImGui::Selectable(std::format("{}##{}", a_subMod->GetName(), reinterpret_cast<std::uintptr_t>(a_subMod)).data(), bSelected) && !bSelected) {
							modName = a_bOwn ? std::string{} : std::string(a_mod->GetName());
							modifierName = a_subMod->GetName();
							bEdited = true;
						}
						if (bSelected) {
							ImGui::SetItemDefaultFocus();
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				};
				list(mod, true);
				mod->ForEachRequiredMod([&](RegisteredMod* a_mod) {
					ImGui::SeparatorText(a_mod->GetName().data());
					list(a_mod, false);
				});
				ImGui::EndCombo();
			}
			ImGui::PopID();
		} else {
			ImGui::TextUnformatted(GetArgument().data());
		}
		return bEdited;
	}

	OnModifierActivatedTrigger::OnModifierActivatedTrigger()
	{
		modifier = AddComponent<ModifierTriggerComponent>("Modifier"sv, "A modifier of this mod, or of a mod this clip requires."sv);
	}

	OnModifierDeactivatedTrigger::OnModifierDeactivatedTrigger()
	{
		modifier = AddComponent<ModifierTriggerComponent>("Modifier"sv, "A modifier of this mod, or of a mod this clip requires."sv);
	}

	OnAnimationStartTrigger::OnAnimationStartTrigger()
	{
		animation = AddComponent<AnimationTriggerComponent>("Animation"sv, "The animation file, relative to the animations folder. Pick lists the reference's."sv);
		AnimationGraph::SetPickers(animation->value);
	}

	OnAnimationEndTrigger::OnAnimationEndTrigger()
	{
		animation = AddComponent<AnimationTriggerComponent>("Animation"sv, "The animation file, relative to the animations folder. Pick lists the reference's."sv);
		AnimationGraph::SetPickers(animation->value);
	}

	OnAnimationEventTrigger::OnAnimationEventTrigger()
	{
		event = AddComponent<TextTriggerComponent>("Event"sv, "The event's name, as the graph sends it; any case. Pick lists the reference's."sv);
		AnimationGraph::SetEventPicker(event->value);
	}

	namespace
	{
		constexpr std::array kIntervalTypeNames{ "Static Value", "Global Variable", "Actor Value", "Random" };
		constexpr std::array kRerollNames{ "Per actor", "On activation" };
	}

	void IntervalTriggerComponent::Parse(const rapidjson::Value& a_value)
	{
		if (!a_value.IsObject()) {
			return;
		}
		if (const auto random = a_value.FindMember("random"); random != a_value.MemberEnd() && random->value.IsObject()) {
			bRandom = true;
			const auto& object = random->value;
			if (const auto it = object.FindMember("min"); it != object.MemberEnd() && it->value.IsNumber()) {
				minSeconds = it->value.GetFloat();
			}
			if (const auto it = object.FindMember("max"); it != object.MemberEnd() && it->value.IsNumber()) {
				maxSeconds = it->value.GetFloat();
			}
			if (const auto it = object.FindMember("reroll"); it != object.MemberEnd() && it->value.IsString()) {
				reroll = std::string_view(it->value.GetString()) == "actor"sv ? Reroll::kPerActor : Reroll::kOnActivation;
			}
			return;
		}
		bRandom = false;
		value.Parse(const_cast<rapidjson::Value&>(a_value));
	}

	rapidjson::Value IntervalTriggerComponent::Serialize(rapidjson::Document::AllocatorType& a_allocator)
	{
		if (!bRandom) {
			return value.Serialize(a_allocator);
		}
		rapidjson::Value random(rapidjson::kObjectType);
		random.AddMember("min", minSeconds, a_allocator);
		random.AddMember("max", maxSeconds, a_allocator);
		random.AddMember("reroll", rapidjson::StringRef(reroll == Reroll::kPerActor ? "actor" : "activation"), a_allocator);
		rapidjson::Value object(rapidjson::kObjectType);
		object.AddMember("random", random, a_allocator);
		return object;
	}

	bool IntervalTriggerComponent::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;
		// The value's own type slider, with Random after its three
		value.SetForcedType(value.GetType());
		if (a_bEditable) {
			int type = bRandom ? static_cast<int>(kIntervalTypeNames.size()) - 1 : static_cast<int>(value.GetType());
			ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
			ImGui::PushID(this);
			if (ImGui::SliderInt("##IntervalType", &type, 0, static_cast<int>(kIntervalTypeNames.size()) - 1, kIntervalTypeNames[type])) {
				bRandom = type == static_cast<int>(kIntervalTypeNames.size()) - 1;
				if (!bRandom) {
					value.SetForcedType(static_cast<Components::NumericValue::Type>(type));
				}
				bEdited = true;
			}
			ImGui::PopID();
		}
		if (!bRandom) {
			return value.DisplayInUI(a_bEditable, a_firstColumnWidthPercent) || bEdited;
		}

		// The range rolled in, and when a roll is made again
		ImGui::PushID(this);
		ImGui::BeginDisabled(!a_bEditable);
		ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
		if (ImGui::InputFloat("##Minimum", &minSeconds, 0.5f, 5.f, "%.2f s")) {
			minSeconds = (std::max)(minSeconds, 0.f);
			bEdited = true;
		}
		UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
		ImGui::TextUnformatted("Minimum");
		ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
		if (ImGui::InputFloat("##Maximum", &maxSeconds, 0.5f, 5.f, "%.2f s")) {
			maxSeconds = (std::max)(maxSeconds, 0.f);
			bEdited = true;
		}
		UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
		ImGui::TextUnformatted("Maximum");
		int rerollIndex = static_cast<int>(reroll);
		ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
		if (ImGui::SliderInt("##Reroll", &rerollIndex, 0, static_cast<int>(kRerollNames.size()) - 1, kRerollNames[rerollIndex])) {
			reroll = static_cast<Reroll>(rerollIndex);
			bEdited = true;
		}
		UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
		ImGui::TextUnformatted("Reroll");
		ImGui::SameLine();
		UI::UICommon::HelpMarker("Per actor: each actor has its own interval, kept for good. On activation: a new interval is rolled each time the clip activates.");
		ImGui::EndDisabled();
		ImGui::PopID();
		return bEdited;
	}

	std::string IntervalTriggerComponent::GetArgument() const
	{
		if (bRandom) {
			return std::format("{:.2f}-{:.2f} s, {}", minSeconds, maxSeconds, reroll == Reroll::kPerActor ? "per actor" : "on activation");
		}
		return value.GetType() == Components::NumericValue::Type::kStaticValue ? value.GetArgument() + " s" : value.GetArgument();
	}

	bool IntervalTriggerComponent::IsValid() const
	{
		// An interval of nothing would fire every tick
		if (bRandom) {
			return minSeconds > 0.f && maxSeconds >= minSeconds;
		}
		return value.IsValid() && (value.GetType() != Components::NumericValue::Type::kStaticValue || value.GetValue(nullptr) > 0.f);
	}

	IntervalTrigger::IntervalTrigger()
	{
		interval = AddComponent<IntervalTriggerComponent>("Interval"sv, "The seconds between the clip's end and its next fire."sv);
		interval->value.SetStaticValue(30.f);
	}

	namespace
	{
		std::map<std::string, TriggerFactory, std::less<>>& Factories()
		{
			static std::map<std::string, TriggerFactory, std::less<>> factories;
			return factories;
		}

		std::vector<std::string>& TypeNames()
		{
			static std::vector<std::string> typeNames;
			return typeNames;
		}

		void Add(std::string_view a_typeName, TriggerFactory a_factory)
		{
			Factories().emplace(std::string(a_typeName), std::move(a_factory));
			TypeNames().emplace_back(a_typeName);
		}
	}

	void RegisterTriggers()
	{
		if (!TypeNames().empty()) {
			return;
		}

		Add("Interval"sv, []() { return std::make_unique<IntervalTrigger>(); });
		Add("OnHit"sv, []() { return std::make_unique<OnHitTrigger>(); });
		Add("OnDeath"sv, []() { return std::make_unique<OnDeathTrigger>(); });
		Add("OnDialogueLine"sv, []() { return std::make_unique<OnDialogueLineTrigger>(); });
		Add("OnAnimationStart"sv, []() { return std::make_unique<OnAnimationStartTrigger>(); });
		Add("OnAnimationEnd"sv, []() { return std::make_unique<OnAnimationEndTrigger>(); });
		Add("OnAnimationEvent"sv, []() { return std::make_unique<OnAnimationEventTrigger>(); });
		Add("OnModifierActivated"sv, []() { return std::make_unique<OnModifierActivatedTrigger>(); });
		Add("OnModifierDeactivated"sv, []() { return std::make_unique<OnModifierDeactivatedTrigger>(); });
		Add("PRESET"sv, []() { return std::make_unique<PresetTrigger>(); });
	}

	std::unique_ptr<Trigger> CreateTrigger(std::string_view a_typeName)
	{
		const auto& factories = Factories();
		if (const auto it = factories.find(a_typeName); it != factories.end()) {
			return it->second();
		}

		return nullptr;
	}

	const std::vector<std::string>& GetTriggerTypeNames()
	{
		return TypeNames();
	}

	std::unique_ptr<TriggerBase> CreateTriggerFromJson(const rapidjson::Value& a_value)
	{
		if (!a_value.IsObject()) {
			return nullptr;
		}
		const auto type = a_value.FindMember("type");
		if (type == a_value.MemberEnd() || !type->value.IsString()) {
			return nullptr;
		}
		std::unique_ptr<TriggerBase> trigger = CreateTrigger(type->value.GetString());
		if (trigger) {
			trigger->Parse(a_value);
		}
		return trigger;
	}

	std::string TriggerSet::NumTextImpl() const
	{
		ReadLocker locker(_lock);
		return _entries.size() == 1 ? "1 trigger" : std::format("{} triggers", _entries.size());
	}

	std::unique_ptr<TriggerSet> DuplicateTriggerSet(TriggerSet* a_triggerSet)
	{
		rapidjson::Document doc(rapidjson::kObjectType);
		const rapidjson::Value serialized = a_triggerSet->Serialize(doc.GetAllocator());
		auto out = std::make_unique<TriggerSet>();
		for (const auto& value : serialized.GetArray()) {
			if (auto trigger = CreateTriggerFromJson(value)) {
				out->Add(trigger);
			}
		}
		return out;
	}
}
