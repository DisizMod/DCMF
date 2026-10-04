#include "SharedTypes.h"
#include "Resources.h"

#include "RegisteredMods.h"

namespace Components
{
	std::vector<UI::UICommon::PickerItem> FormPickerItems(const std::vector<RE::FormType>& a_formTypes, FormPredicate a_predicate)
	{
		struct Entry
		{
			std::uint32_t order;
			UI::UICommon::PickerItem item;
		};
		std::vector<Entry> entries;

		const auto add = [&](RE::TESForm* a_form) {
			if (!a_form || (a_predicate && !a_predicate(a_form))) {
				return;
			}

			const auto formID = a_form->GetFormID();
			const auto* file = a_form->GetFile(0);

			// Regular plugins first in load order, then light ones, then what
			// the game made at runtime and no plugin owns.
			const std::uint32_t order = file ? file->GetPartialIndex() : 0xFFFFFFFF;
			const auto localID = !file ? formID : file->IsLight() ? (formID & 0xFFF) : (formID & 0xFFFFFF);

			std::string_view name = a_form->GetFormEditorID();
			if (name.empty()) {
				name = a_form->GetName();
			}
			UI::UICommon::PickerItem item;
			item.label = name.empty() ? std::format("{:X}", localID) : std::format("{} ({:X})", name, localID);
			item.group = file ? std::string(file->GetFilename()) : "Created in game";
			item.value = formID;
			entries.push_back({ order, std::move(item) });
		};

		// References live in their cells, not in the data handler's arrays, so
		// asking for one means walking every form; so does asking for none.
		const bool bScanAll = a_formTypes.empty() || std::ranges::any_of(a_formTypes, [](RE::FormType a_type) {
			return a_type == RE::FormType::Reference || a_type == RE::FormType::ActorCharacter;
		});

		if (bScanAll) {
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			const RE::BSReadLockGuard guard{ lock };
			if (map) {
				for (const auto& [formID, form] : *map) {
					if (a_formTypes.empty() || std::ranges::find(a_formTypes, form->GetFormType()) != a_formTypes.end()) {
						add(form);
					}
				}
			}
		} else if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
			for (const auto formType : a_formTypes) {
				for (auto* form : dataHandler->GetFormArray(formType)) {
					add(form);
				}
			}
		}

		std::ranges::sort(entries, [](const Entry& a_lhs, const Entry& a_rhs) {
			if (a_lhs.order != a_rhs.order) {
				return a_lhs.order < a_rhs.order;
			}
			// The picker draws a heading each time the group changes, so a group's items sit together
			if (a_lhs.item.group != a_rhs.item.group) {
				return a_lhs.item.group < a_rhs.item.group;
			}
			return a_lhs.item.label < a_rhs.item.label;
		});

		std::vector<UI::UICommon::PickerItem> items;
		items.reserve(entries.size());
		for (auto& entry : entries) {
			items.push_back(std::move(entry.item));
		}
		return items;
	}

	bool IsPickedByPlugin(const std::vector<RE::FormType>& a_formTypes)
	{
		return std::ranges::any_of(a_formTypes, [](RE::FormType a_type) {
			return a_type == RE::FormType::NPC || a_type == RE::FormType::ActorCharacter || a_type == RE::FormType::Reference;
		});
	}

	namespace
	{
		// Every form of the types, each with the plugin it came from: by its place in the load order, the game's own
		// last; references live in their cells, so their walk is over every form
		void ForEachPickable(const std::vector<RE::FormType>& a_formTypes, FormPredicate a_predicate, const std::function<void(RE::TESForm*, std::uint64_t)>& a_func)
		{
			const auto visit = [&](RE::TESForm* a_form) {
				if (!a_form || (a_predicate && !a_predicate(a_form))) {
					return;
				}
				const auto* file = a_form->GetFile(0);
				a_func(a_form, file ? file->GetPartialIndex() : 0xFFFFFFFF);
			};
			const bool bScanAll = std::ranges::any_of(a_formTypes, [](RE::FormType a_type) {
				return a_type == RE::FormType::Reference || a_type == RE::FormType::ActorCharacter;
			});
			if (bScanAll) {
				const auto& [map, lock] = RE::TESForm::GetAllForms();
				const RE::BSReadLockGuard guard{ lock };
				if (map) {
					for (const auto& [formID, form] : *map) {
						if (std::ranges::find(a_formTypes, form->GetFormType()) != a_formTypes.end()) {
							visit(form);
						}
					}
				}
			} else if (auto* dataHandler = RE::TESDataHandler::GetSingleton()) {
				for (const auto formType : a_formTypes) {
					for (auto* form : dataHandler->GetFormArray(formType)) {
						visit(form);
					}
				}
			}
		}

		// What a form is called: a reference by its own name or its base's, an NPC by its name, else its editor id
		std::string PickableName(RE::TESForm* a_form)
		{
			if (auto* refr = a_form->As<RE::TESObjectREFR>()) {
				if (const char* name = refr->GetDisplayFullName(); name && *name) {
					return name;
				}
				if (auto* base = refr->GetBaseObject()) {
					if (const auto name = base->GetName(); name && *name) {
						return name;
					}
					if (const auto editorID = base->GetFormEditorID(); editorID && *editorID) {
						return editorID;
					}
				}
				return {};
			}
			if (const auto name = a_form->GetName(); name && *name) {
				return name;
			}
			const auto editorID = a_form->GetFormEditorID();
			return editorID ? editorID : "";
		}
	}

	std::vector<UI::UICommon::AsyncPickerGroup> FormPickerGroups(const std::vector<RE::FormType>& a_formTypes, FormPredicate a_predicate)
	{
		// The plugins that hold any, in load order, the game's own last
		std::map<std::uint64_t, std::string> plugins;
		ForEachPickable(a_formTypes, a_predicate, [&](RE::TESForm* a_form, std::uint64_t a_group) {
			if (!plugins.contains(a_group)) {
				const auto* file = a_form->GetFile(0);
				plugins.emplace(a_group, file ? std::string(file->GetFilename()) : std::string("Created in game"));
			}
		});
		std::vector<UI::UICommon::AsyncPickerGroup> groups;
		for (auto& [key, name] : plugins) {
			groups.push_back({ std::move(name), key, {} });
		}
		return groups;
	}

	std::vector<UI::UICommon::PickerItem> FormPickerGroupItems(const std::vector<RE::FormType>& a_formTypes, FormPredicate a_predicate, std::uint64_t a_group)
	{
		// One plugin's, named, by name
		std::vector<UI::UICommon::PickerItem> items;
		ForEachPickable(a_formTypes, a_predicate, [&](RE::TESForm* a_form, std::uint64_t a_formGroup) {
			if (a_formGroup != a_group) {
				return;
			}
			const auto formID = a_form->GetFormID();
			const auto* file = a_form->GetFile(0);
			const auto localID = !file ? formID : file->IsLight() ? (formID & 0xFFF) : (formID & 0xFFFFFF);
			const auto name = PickableName(a_form);
			items.push_back({ name.empty() ? std::format("{:X}", localID) : std::format("{} ({:X})", name, localID), {}, formID });
		});
		std::ranges::sort(items, {}, &UI::UICommon::PickerItem::label);
		return items;
	}

	bool IsCreature(const RE::TESForm* a_form)
	{
		// ActorTypeNPC, Skyrim.esm: on the race of everything that is a person.
		static const auto* actorTypeNPC = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
		auto* npc = a_form ? const_cast<RE::TESForm*>(a_form)->As<RE::TESNPC>() : nullptr;
		const auto* race = npc ? npc->GetRace() : nullptr;
		return race && !(actorTypeNPC && race->HasKeyword(actorTypeNPC));
	}

	namespace
	{
		// Every actor value the engine knows, in index order, for the same
		// combo box the reading above it uses. Fixed at 168 entries: a mod
		// cannot add one, since a custom perk is a form and not an actor value.
		//
		// The views point at the engine's own enumName strings, which outlive
		// anything here.
		const std::map<int32_t, std::string_view>& ActorValueEnumMap()
		{
			static const std::map<int32_t, std::string_view> map = [] {
				std::map<int32_t, std::string_view> out;
				const auto total = static_cast<int32_t>(RE::ActorValue::kTotal);
				for (int32_t i = 0; i < total; ++i) {
					out.emplace(i, Utils::GetActorValueName(static_cast<RE::ActorValue>(i)));
				}
				return out;
			}();

			return map;
		}	}

	float NumericValue::GetValue(RE::TESObjectREFR* a_refr) const
	{
		switch (_type) {
		case Type::kStaticValue:
			return _floatValue;
		case Type::kGlobalVariable:
			return _globalVariable.IsValid() ? _globalVariable.GetValue()->value : 0.f;
		case Type::kActorValue:
			{
				float outValue = 0.f;
				if (a_refr) {
					switch (_actorValueType) {
					case ActorValueType::kActorValue:
						GetActorValue(a_refr, outValue);
						break;
					case ActorValueType::kBase:
						GetActorValueBase(a_refr, outValue);
						break;
					case ActorValueType::kMax:
						GetActorValueMax(a_refr, outValue);
						break;
					case ActorValueType::kPercentage:
						GetActorValuePercentage(a_refr, outValue);
						break;
					}
				}
				return outValue;
			}
		case Type::kFeed:
			return Resources::GetNumber(_feed, a_refr).value_or(0.f);
		}

		return 0.f;
	}

	bool NumericValue::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;

		if (a_bEditable) {
			if (_forcedType == Type::kNone && !(HasEnumMap() && _type == Type::kStaticValue)) {
				ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
				ImGui::PushID(&_type);
				if (ImGui::SliderInt("", reinterpret_cast<int*>(&_type), 0, static_cast<int>(getFeeds ? Type::kTotal : Type::kFeed) - 1, GetTypeName().data())) {
					bEdited = true;
				}
				ImGui::PopID();
			}

			switch (_type) {
			case Type::kStaticValue:
				{
					if (HasEnumMap()) {
						if (getEnumMap) {
							if (DisplayComboBox<int32_t, float>(getEnumMap(), _floatValue, a_firstColumnWidthPercent)) {
								bEdited = true;
							}
						} else {
							if (DisplayComboBox<uint32_t, float>(getUnsignedEnumMap(), _floatValue, a_firstColumnWidthPercent)) {
								bEdited = true;
							}
						}

						UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
						ImGui::TextUnformatted(GetArgument().data());
					} else if (_staticRange) {
						ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
						ImGui::PushID(&_floatValue);
						if (_bStaticInteger) {
							int tempInt = static_cast<int>(_floatValue);
							if (ImGui::SliderInt("", &tempInt, static_cast<int>(_staticRange->first), static_cast<int>(_staticRange->second), "%d", ImGuiSliderFlags_AlwaysClamp)) {
								_floatValue = static_cast<float>(tempInt);
								bEdited = true;
							}
						} else {
							if (ImGui::SliderFloat("", &_floatValue, _staticRange->first, _staticRange->second, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
								bEdited = true;
							}
						}

						// Right click: an exact value typed, outside the slider's range if need be
						if (ImGui::BeginPopupContextItem("##setValue")) {
							static float typed = 0.f;
							if (ImGui::IsWindowAppearing()) {
								typed = _floatValue;
								ImGui::SetKeyboardFocusHere();
							}
							ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.f);
							if (ImGui::InputFloat("Set value", &typed, 0.f, 0.f, _bStaticInteger ? "%.0f" : "%.3f", ImGuiInputTextFlags_EnterReturnsTrue)) {
								_floatValue = _bStaticInteger ? std::round(typed) : typed;
								bEdited = true;
								ImGui::CloseCurrentPopup();
							}
							ImGui::EndPopup();
						}

						ImGui::PopID();

						UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
						ImGui::TextUnformatted(GetArgument().data());
					} else {
						ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
						ImGui::PushID(&_floatValue);
						if (_bStaticInteger) {
							int tempInt = static_cast<int>(_floatValue);
							if (ImGui::InputInt("", &tempInt, 1, 10, ImGuiSliderFlags_AlwaysClamp)) {
								_floatValue = static_cast<float>(tempInt);
								bEdited = true;
							}
						} else {
							if (ImGui::InputFloat("", &_floatValue, 0.01f, 1.0f, "%.3f")) {
								bEdited = true;
							}
						}

						ImGui::PopID();

						UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
						ImGui::TextUnformatted(GetArgument().data());
					}
					break;
				}

			case Type::kGlobalVariable:
				{
					if (_globalVariable.DisplayInUI(a_bEditable, a_firstColumnWidthPercent)) {
						bEdited = true;
					}
					break;
				}
			case Type::kActorValue:
				{
					std::map<int32_t, std::string_view> actorValueTypeEnumMap;
					actorValueTypeEnumMap[0] = "Actor Value"sv;
					actorValueTypeEnumMap[1] = "Base Actor Value"sv;
					actorValueTypeEnumMap[2] = "Max Actor Value"sv;
					actorValueTypeEnumMap[3] = "Actor Value Percentage (0-1)"sv;

					if (DisplayComboBox<int32_t>(actorValueTypeEnumMap, *reinterpret_cast<int32_t*>(&_actorValueType), a_firstColumnWidthPercent)) {
						bEdited = true;
					}

					// The number as before, with its -/+ replaced by one [Pick] button in the
					// same space.
					const auto& style = ImGui::GetStyle();
					const float totalWidth = UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent);

					ImGui::PushID(&_actorValue);
					ImGui::SetNextItemWidth(totalWidth - UI::UICommon::PickerButtonWidth() - style.ItemInnerSpacing.x);
					if (ImGui::InputInt("##ActorValue", reinterpret_cast<int*>(&_actorValue), 0, 0)) {
						bEdited = true;
					}
					ImGui::SameLine(0.f, style.ItemInnerSpacing.x);

					std::uint64_t picked = 0;
					const auto items = [] {
						std::vector<UI::UICommon::PickerItem> out;
						for (const auto& [index, name] : ActorValueEnumMap()) {
							out.push_back({ std::format("{} ({})", name, index), {}, static_cast<std::uint64_t>(index) });
						}
						return out;
					};
					if (UI::UICommon::Picker("ActorValue", items, static_cast<std::uint64_t>(_actorValue), picked)) {
						_actorValue = static_cast<RE::ActorValue>(picked);
						bEdited = true;
					}
					ImGui::PopID();

					UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
					ImGui::TextUnformatted(GetArgument().data());
					break;
				}
			case Type::kFeed:
				{
					// The feed's id, typed or picked from those the host offers; said when it names none
					const auto ids = getFeeds ? getFeeds() : std::vector<std::string>{};
					ImGui::PushID(&_feed);
					const auto& style = ImGui::GetStyle();
					ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent) - UI::UICommon::PickerButtonWidth() - style.ItemInnerSpacing.x);
					if (ImGui::InputTextWithHint("##Feed", "feed key", &_feed)) {
						bEdited = true;
					}
					ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
					const auto at = std::ranges::find(ids, _feed);
					std::uint64_t picked = 0;
					const auto items = [&ids] {
						std::vector<UI::UICommon::PickerItem> out;
						for (std::size_t i = 0; i < ids.size(); ++i) {
							out.push_back({ ids[i], ids[i].substr(0, ids[i].find('.')), i });
						}
						return out;
					};
					if (UI::UICommon::Picker("Feed", items, at != ids.end() ? static_cast<std::uint64_t>(at - ids.begin()) : UINT64_MAX, picked) && picked < ids.size()) {
						_feed = ids[picked];
						bEdited = true;
					}
					ImGui::PopID();
					if (std::ranges::find(ids, _feed) == ids.end()) {
						UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
						ImGui::TextUnformatted("(Not found)");
					}
					break;
				}
			}
		} else {
			switch (_type) {
			case Type::kStaticValue:
				if (!HasEnumMap()) {
					ImGui::TextUnformatted("Static value");

					UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
					ImGui::TextUnformatted(GetArgument().data());
				} else {
					std::string currentEnumName = std::format("Unknown ({})", _floatValue);
					if (getEnumMap) {
						const auto& enumMap = getEnumMap();
						const auto it = enumMap.find(static_cast<int32_t>(_floatValue));
						if (it != enumMap.end()) {
							currentEnumName = it->second;
						}
					} else {
						const auto& enumMap = getUnsignedEnumMap();
						const auto it = enumMap.find(static_cast<uint32_t>(_floatValue));
						if (it != enumMap.end()) {
							currentEnumName = it->second;
						}
					}

					ImGui::TextUnformatted(currentEnumName.data());

					UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
					ImGui::TextUnformatted(GetArgument().data());
				}
				break;
			case Type::kGlobalVariable:
				_globalVariable.DisplayInUI(a_bEditable, a_firstColumnWidthPercent);
				break;
			case Type::kActorValue:
				ImGui::TextUnformatted(GetArgument().data());
				break;
			case Type::kFeed:
				ImGui::TextUnformatted(GetArgument().data());
				if (const auto ids = getFeeds ? getFeeds() : std::vector<std::string>{}; std::ranges::find(ids, _feed) == ids.end()) {
					UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
					ImGui::TextUnformatted("(Not found)");
				}
				break;
			}
		}

		return bEdited;
	}

	std::string NumericValue::GetArgument() const
	{
		switch (_type) {
		case Type::kStaticValue:
			{
				if (HasEnumMap()) {
					if (getEnumMap) {
						const auto& enumMap = getEnumMap();
						if (const auto nameIt = enumMap.find(static_cast<int32_t>(_floatValue)); nameIt != enumMap.end()) {
							return nameIt->second.data();
						}
					} else {
						const auto& enumMap = getUnsignedEnumMap();
						if (const auto nameIt = enumMap.find(static_cast<uint32_t>(_floatValue)); nameIt != enumMap.end()) {
							return nameIt->second.data();
						}
					}
				}
				return std::format("{:.3}", _floatValue);
			}
		case Type::kGlobalVariable:
			{
				if (_globalVariable.IsValid()) {
					return _globalVariable.GetValue()->GetFormEditorID();
				}
				return "(Not found)";
			}
		case Type::kActorValue:
			{
				std::string actorValueArgument = Utils::GetActorValueName(_actorValue).data();
				switch (_actorValueType) {
				case ActorValueType::kActorValue:
					break;
				case ActorValueType::kBase:
					actorValueArgument.insert(0, "Base "sv);
					break;
				case ActorValueType::kMax:
					actorValueArgument.insert(0, "Max "sv);
					break;
				case ActorValueType::kPercentage:
					actorValueArgument.append("%"sv);
					break;
				}
				return actorValueArgument;
			}
		case Type::kFeed:
			return _feed.empty() ? "(no feed)" : _feed;
		}

		return "(Invalid)";
	}

	void NumericValue::Parse(rapidjson::Value& a_value)
	{
		const auto valueObject = a_value.GetObj();

		if (const auto valueIt = valueObject.FindMember("value"); valueIt != valueObject.MemberEnd() && valueIt->value.IsNumber()) {
			SetStaticValue(valueIt->value.GetFloat());
			return;
		}

		if (const auto feedIt = valueObject.FindMember("feed"); feedIt != valueObject.MemberEnd() && feedIt->value.IsString()) {
			_feed = feedIt->value.GetString();
			SetType(Type::kFeed);
			return;
		}

		if (const auto formIt = valueObject.FindMember("form"); formIt != valueObject.MemberEnd() && formIt->value.IsObject()) {
			SetType(Type::kGlobalVariable);
			_globalVariable.Parse(formIt->value);
			return;
		}

		if (const auto actorValueIt = valueObject.FindMember("actorValue"); actorValueIt != valueObject.MemberEnd() && actorValueIt->value.IsNumber()) {
			const auto actorValueTypeIt = valueObject.FindMember("actorValueType");
			if (actorValueTypeIt != valueObject.MemberEnd() && actorValueTypeIt->value.IsString()) {
				const std::string actorValueTypeString = actorValueTypeIt->value.GetString();

				auto valueType = ActorValueType::kActorValue;
				if (actorValueTypeString == GetActorValueTypeString(ActorValueType::kActorValue)) {
					valueType = ActorValueType::kActorValue;
				} else if (actorValueTypeString == GetActorValueTypeString(ActorValueType::kBase)) {
					valueType = ActorValueType::kBase;
				} else if (actorValueTypeString == GetActorValueTypeString(ActorValueType::kMax)) {
					valueType = ActorValueType::kMax;
				} else if (actorValueTypeString == GetActorValueTypeString(ActorValueType::kPercentage)) {
					valueType = ActorValueType::kPercentage;
				}

				SetActorValue(static_cast<RE::ActorValue>(actorValueIt->value.GetInt()), valueType);
				return;
			}
		}
	}

	void NumericValue::ParseLegacy(std::string_view a_argument, bool a_bIsActorValue /*= false*/)
	{
		if (a_bIsActorValue) {
			int32_t valueInt = -1;
			std::from_chars(a_argument.data(), a_argument.data() + a_argument.size(), valueInt);
			SetActorValue(static_cast<RE::ActorValue>(valueInt));
		} else {
			float floatValue = 0.f;
			auto [ptr, ec]{ std::from_chars(a_argument.data(), a_argument.data() + a_argument.size(), floatValue) };

			if (ec == std::errc()) {
				SetStaticValue(floatValue);
				return;
			}

			if (ec == std::errc::invalid_argument) {
				SetType(Type::kGlobalVariable);
				_globalVariable.ParseLegacy(a_argument);
			}
		}
	}

	rapidjson::Value NumericValue::Serialize(rapidjson::Document::AllocatorType& a_allocator)
	{
		rapidjson::Value object(rapidjson::kObjectType);

		switch (_type) {
		case Type::kStaticValue:
			object.AddMember("value", _floatValue, a_allocator);
			break;
		case Type::kGlobalVariable:
			object.AddMember("form", _globalVariable.Serialize(a_allocator), a_allocator);
			break;
		case Type::kActorValue:
			object.AddMember("actorValue", static_cast<int32_t>(_actorValue), a_allocator);
			object.AddMember("actorValueType", rapidjson::StringRef(GetActorValueTypeString(_actorValueType).data(), GetActorValueTypeString(_actorValueType).length()), a_allocator);
			break;
		case Type::kFeed:
			object.AddMember("feed", rapidjson::Value(_feed.data(), static_cast<rapidjson::SizeType>(_feed.size()), a_allocator), a_allocator);
			break;
		}

		return object;
	}

	bool NumericValue::GetActorValue(RE::TESObjectREFR* a_refr, float& a_outValue) const
	{
		if (_actorValue > RE::ActorValue::kNone && _actorValue < RE::ActorValue::kTotal && a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				const auto actorValueOwner = actor->AsActorValueOwner();
				a_outValue = actorValueOwner->GetActorValue(_actorValue);
				return true;
			}
		}

		return false;
	}

	bool NumericValue::GetActorValueBase(RE::TESObjectREFR* a_refr, float& a_outValue) const
	{
		if (_actorValue > RE::ActorValue::kNone && _actorValue < RE::ActorValue::kTotal && a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				const auto actorValueOwner = actor->AsActorValueOwner();
				a_outValue = actorValueOwner->GetBaseActorValue(_actorValue);
				return true;
			}
		}

		return false;
	}

	bool NumericValue::GetActorValueMax(RE::TESObjectREFR* a_refr, float& a_outValue) const
	{
		if (_actorValue > RE::ActorValue::kNone && _actorValue < RE::ActorValue::kTotal && a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				const auto actorValueOwner = actor->AsActorValueOwner();
				const float permanentValue = actorValueOwner->GetPermanentActorValue(_actorValue);
				const float temporaryValue = actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, _actorValue);
				a_outValue = permanentValue + temporaryValue;
				return true;
			}
		}

		return false;
	}

	bool NumericValue::GetActorValuePercentage(RE::TESObjectREFR* a_refr, float& a_outValue) const
	{
		if (_actorValue > RE::ActorValue::kNone && _actorValue < RE::ActorValue::kTotal && a_refr) {
			if (const auto actor = a_refr->As<RE::Actor>()) {
				const auto actorValueOwner = actor->AsActorValueOwner();
				const float currentValue = actorValueOwner->GetActorValue(_actorValue);
				const float permanentValue = actorValueOwner->GetPermanentActorValue(_actorValue);
				float temporaryValue = temporaryValue = actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, _actorValue);
				const float maxValue = permanentValue + temporaryValue;

				if (maxValue <= 0.f) {
					a_outValue = 0.f;
					return true;
				}

				if (maxValue < std::numeric_limits<float>::epsilon() && currentValue < std::numeric_limits<float>::epsilon()) {
					a_outValue = 0.f;
					return true;
				}

				const float percent = currentValue / maxValue;
				a_outValue = fmin(fmax(percent, 0.f), 1.f);
				return true;
			}
		}

		return false;
	}

	template <typename Key, typename T>
	bool NumericValue::DisplayComboBox(const std::map<Key, std::string_view>& a_enumMap, T& a_value, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;

		ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
		ImGui::PushID(&a_value);
		std::string currentEnumName;

		if (const auto it = a_enumMap.find(static_cast<Key>(a_value)); it != a_enumMap.end()) {
			currentEnumName = it->second;
		} else {
			currentEnumName = std::format("Unknown ({})", static_cast<Key>(a_value));
		}

		if (ImGui::BeginCombo("##Enum value", currentEnumName.data())) {
			for (auto& [enumValue, enumName] : a_enumMap) {
				const bool bIsCurrent = enumValue == static_cast<Key>(a_value);
				if (ImGui::Selectable(enumName.data(), bIsCurrent)) {
					if (!bIsCurrent) {
						a_value = static_cast<T>(enumValue);
						bEdited = true;
					}
				}
				if (bIsCurrent) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopID();

		return bEdited;
	}

	bool TextValue::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;

		if (a_bEditable) {
			ImGui::PushID(&_text);
			const auto& style = ImGui::GetStyle();
			const float totalWidth = UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent);
			ImGui::SetNextItemWidth(_pickerItems ? totalWidth - UI::UICommon::PickerButtonWidth() - style.ItemInnerSpacing.x : totalWidth);
			ImGuiInputTextFlags flags = ImGuiInputTextFlags_None;
			if (!_bAllowSpaces) {
				flags |= ImGuiInputTextFlags_CharsNoBlank;
			}
			if (ImGui::InputTextWithHint("##Text", _uiTextHint.data(), &_text, flags)) {
				bEdited = true;
			}
			if (_pickerItems) {
				ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
				std::uint64_t picked = 0;
				if (UI::UICommon::Picker("Text", _pickerItems, UINT64_MAX, picked)) {
					const auto items = _pickerItems();
					if (picked < items.size()) {
						_text = items[picked].label;
						bEdited = true;
					}
				}
				UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
				ImGui::TextUnformatted(GetArgument().data());
			}
			ImGui::PopID();
		} else {
			ImGui::TextUnformatted(_text.data());
			if (_pickerResolve) {
				UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
				ImGui::TextUnformatted(GetArgument().data());
			}
		}

		return bEdited;
	}

	void TextValue::Parse(const rapidjson::Value& a_value)
	{
		_text = (a_value.GetString());
	}

	rapidjson::Value TextValue::Serialize([[maybe_unused]] rapidjson::Document::AllocatorType& a_allocator) const
	{
		rapidjson::Value object(rapidjson::StringRef(_text.data(), _text.length()));

		return object;
	}

	AnimationValue::AnimationValue()
	{
		file.SetTextHint("Animation file...");
		replacer.SetTextHint("Mod / Submod...");
	}

	bool AnimationValue::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = file.DisplayInUI(a_bEditable, a_firstColumnWidthPercent);

		// The replacer box, and the replacer under it once ticked
		ImGui::PushID(this);
		ImGui::BeginDisabled(!a_bEditable);
		ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
		if (ImGui::Checkbox("##Replacer", &bReplacer)) {
			bEdited = true;
		}
		ImGui::EndDisabled();
		UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
		ImGui::TextUnformatted("OAR replacer");
		ImGui::SameLine();
		UI::UICommon::HelpMarker("Only while the file is played as this Open Animation Replacer submod. Unticked, the file matches however it is played.");
		ImGui::PopID();
		if (bReplacer && replacer.DisplayInUI(a_bEditable, a_firstColumnWidthPercent)) {
			bEdited = true;
		}
		return bEdited;
	}

	void AnimationValue::Parse(const rapidjson::Value& a_value)
	{
		if (!a_value.IsObject()) {
			return;
		}
		if (const auto it = a_value.FindMember("file"); it != a_value.MemberEnd() && it->value.IsString()) {
			file.SetValue(it->value.GetString());
		}
		if (const auto it = a_value.FindMember("replacer"); it != a_value.MemberEnd() && it->value.IsString()) {
			bReplacer = true;
			replacer.SetValue(it->value.GetString());
		}
	}

	rapidjson::Value AnimationValue::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
	{
		rapidjson::Value object(rapidjson::kObjectType);
		object.AddMember("file", rapidjson::Value(file.GetValue().data(), static_cast<rapidjson::SizeType>(file.GetValue().size()), a_allocator), a_allocator);
		if (bReplacer) {
			object.AddMember("replacer", rapidjson::Value(replacer.GetValue().data(), static_cast<rapidjson::SizeType>(replacer.GetValue().size()), a_allocator), a_allocator);
		}
		return object;
	}

	std::string AnimationValue::GetArgument() const
	{
		return bReplacer ? std::format("{} as {}", file.GetValue(), replacer.GetValue()) : std::string(file.GetValue());
	}

	bool AnimationValue::Matches(std::string_view a_file, std::string_view a_replacer) const
	{
		if (Normalize(file.GetValue()) != a_file) {
			return false;
		}
		// A submod picked matches any of its variants; a variant only itself
		const auto wanted = replacer.GetValue();
		if (!bReplacer || Utils::CompareStringsIgnoreCase(wanted, a_replacer)) {
			return true;
		}
		return a_replacer.size() > wanted.size() + 3 && Utils::CompareStringsIgnoreCase(a_replacer.substr(0, wanted.size()), wanted) && a_replacer.substr(wanted.size(), 3) == " / "sv;
	}

	std::string AnimationValue::Normalize(std::string_view a_path)
	{
		std::string out(a_path);
		std::ranges::transform(out, out.begin(), [](char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
		if (out.starts_with("animations\\")) {
			out.erase(0, 11);
		}
		return out;
	}

	bool NiPoint3Value::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;

		if (a_bEditable) {
			ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
			ImGui::PushID(&_value);
			if (ImGui::InputFloat3("", reinterpret_cast<float*>(&_value))) {
				bEdited = true;
			}
			ImGui::PopID();

			UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
			ImGui::TextUnformatted(GetArgument().data());
		} else {
			ImGui::TextUnformatted(GetArgument().data());
		}

		return bEdited;
	}

	std::string NiPoint3Value::GetArgument() const
	{
		return std::format("({}, {}, {})", GetValue().x, GetValue().y, GetValue().z);
	}

	void NiPoint3Value::Parse(rapidjson::Value& a_value)
	{
		if (const auto valueArray = a_value.GetArray(); valueArray.Size() == 3) {
			_value.x = valueArray[0].GetFloat();
			_value.y = valueArray[1].GetFloat();
			_value.z = valueArray[2].GetFloat();
			_bIsValid = true;
		}
	}

	rapidjson::Value NiPoint3Value::Serialize([[maybe_unused]] rapidjson::Document::AllocatorType& a_allocator) const
	{
		rapidjson::Value arrayValue(rapidjson::kArrayType);

		arrayValue.PushBack(_value.x, a_allocator);
		arrayValue.PushBack(_value.y, a_allocator);
		arrayValue.PushBack(_value.z, a_allocator);

		return arrayValue;
	}

	bool ColourValue::DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
	{
		bool bEdited = false;

		if (a_bEditable) {
			// The swatch fills the field's width and height, like a slider or a
			// combo beside it; clicking it opens the picker ColorEdit3 would.
			ImGui::PushID(&_value);
			auto* colour = reinterpret_cast<float*>(&_value);
			if (ImGui::ColorButton("##swatch", ImVec4(colour[0], colour[1], colour[2], 1.f), ImGuiColorEditFlags_NoTooltip,
					ImVec2(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent), ImGui::GetFrameHeight()))) {
				ImGui::OpenPopup("##picker");
			}
			if (ImGui::BeginPopup("##picker")) {
				if (ImGui::ColorPicker3("##picker", colour, ImGuiColorEditFlags_PickerHueWheel)) {
					_bIsValid = true;
					bEdited = true;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();

			UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
			ImGui::TextUnformatted(GetArgument().data());
		} else {
			ImGui::TextUnformatted(GetArgument().data());
		}

		return bEdited;
	}

	std::string ColourValue::GetArgument() const
	{
		const auto channel = [](float a_value) {
			return static_cast<int>(std::lround(std::clamp(a_value, 0.f, 1.f) * 255.f));
		};

		return std::format("#{:02X}{:02X}{:02X}", channel(_value.red), channel(_value.green), channel(_value.blue));
	}

	void ColourValue::Parse(rapidjson::Value& a_value)
	{
		if (const auto valueArray = a_value.GetArray(); valueArray.Size() == 3) {
			_value.red = valueArray[0].GetFloat();
			_value.green = valueArray[1].GetFloat();
			_value.blue = valueArray[2].GetFloat();
			_bIsValid = true;
		}
	}

	rapidjson::Value ColourValue::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
	{
		rapidjson::Value arrayValue(rapidjson::kArrayType);

		arrayValue.PushBack(_value.red, a_allocator);
		arrayValue.PushBack(_value.green, a_allocator);
		arrayValue.PushBack(_value.blue, a_allocator);

		return arrayValue;
	}

}
