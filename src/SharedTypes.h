#pragma once

#include "UI/UICommon.h"
#include "Utils.h"

#include "StateData.h"

#include <imgui_stdlib.h>
#include <rapidjson/document.h>

template <class T, class U>
concept Derived = std::is_base_of_v<U, T>;

template <typename T>
concept NonAbstract = !std::is_abstract_v<T>;

namespace Resources
{
	// What a form resource holds for a reference; Resources.h has the rest
	[[nodiscard]] RE::TESForm* GetForm(std::string_view a_key, RE::TESObjectREFR* a_refr);
}

namespace Components
{
	// Every loaded form of a type, labelled and grouped by the plugin it came
	// from, in load order. None is every form there is.
	using FormPredicate = bool (*)(const RE::TESForm*);

	[[nodiscard]] std::vector<UI::UICommon::PickerItem> FormPickerItems(const std::vector<RE::FormType>& a_formTypes,
		FormPredicate a_predicate);

	// Actors and references, too many to name all at once: the plugins that hold any, closed, each one's forms named
	// the first time it is opened, as the dialogue line picker reads its lines
	[[nodiscard]] bool IsPickedByPlugin(const std::vector<RE::FormType>& a_formTypes);
	[[nodiscard]] std::vector<UI::UICommon::AsyncPickerGroup> FormPickerGroups(const std::vector<RE::FormType>& a_formTypes, FormPredicate a_predicate);
	[[nodiscard]] std::vector<UI::UICommon::PickerItem> FormPickerGroupItems(const std::vector<RE::FormType>& a_formTypes, FormPredicate a_predicate, std::uint64_t a_group);

	// An NPC whose race is not a person's: a horse, a dragon, a modded mount.
	[[nodiscard]] bool IsCreature(const RE::TESForm* a_form);

	template <Derived<RE::TESForm> T>
	class TESFormValue
	{
	public:
		bool IsValid() const { return _bFeed ? !_feed.empty() : _form != nullptr; }

		// What the picker offers. None is anything, which is what a condition
		// that takes any form leaves it as.
		void SetFormTypeFilter(RE::FormType a_formType)
		{
			_formTypeFilter.clear();
			if (a_formType != RE::FormType::None) {
				_formTypeFilter.push_back(a_formType);
			}
		}
		void SetFormTypeFilter(std::initializer_list<RE::FormType> a_formTypes) { _formTypeFilter.assign(a_formTypes); }
		[[nodiscard]] const std::vector<RE::FormType>& GetFormTypeFilter() const { return _formTypeFilter; }

		// A narrower test than the type, where the type alone lets in too much.
		void SetFormPredicate(FormPredicate a_predicate) { _formPredicate = a_predicate; }
		T* GetValue() const { return _form; }
		[[nodiscard]] bool IsFeed() const { return _bFeed; }
		void SetIsFeed(bool a_bFeed) { _bFeed = a_bFeed; }

		// The feed's id with Pick, for a host whose own slider chooses between a form and a feed; true when edited
		bool DisplayFeedRow(bool a_bEditable, float a_firstColumnWidthPercent)
		{
			bool bEdited = false;
			DisplayFeed(a_bEditable, a_firstColumnWidthPercent, bEdited, false);
			return bEdited;
		}
		[[nodiscard]] const std::string& GetFeed() const { return _feed; }

		// The form, or what its feed holds for the actor
		T* GetValue(RE::TESObjectREFR* a_refr) const
		{
			if (!_bFeed) {
				return _form;
			}
			auto* form = Resources::GetForm(_feed, a_refr);
			if constexpr (std::is_same_v<T, RE::TESForm>) {
				return form;
			} else {
				return form ? form->As<T>() : nullptr;
			}
		}

		void SetValue(T* a_form)
		{
			_form = a_form;

			if (_form) {
				RE::FormID fileIndex = 0;

				if ((_form->formID & 0xFF000000) == 0) {
					_pluginNameString = "Skyrim.esm"sv;
				} else if (auto file = _form->GetFile(0)) {
					fileIndex = file->compileIndex << (3 * 8);
					fileIndex += file->smallFileCompileIndex << ((1 * 8) + 4);
					_pluginNameString = file->GetFilename();
				} else {
					_pluginNameString = "Skyrim.esm"sv;
				}

				RE::FormID localFormID = _form->formID & ~fileIndex;
				_formIdString = std::format("{:X}", localFormID);
			}
		}

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
		{
			bool bEdited = false;

			// A form, or a feed holding one, where the host offers feeds
			if (getFeeds && DisplayFeed(a_bEditable, a_firstColumnWidthPercent, bEdited)) {
				return bEdited;
			}

			if (a_bEditable) {
				ImGui::PushID(this);
				const auto& style = ImGui::GetStyle();
				const float formIDWidth = ImGui::GetFontSize() * 4;
				const float pickWidth = UI::UICommon::PickerButtonWidth();
				const float pluginNameWidth = UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent) - formIDWidth - style.ItemSpacing.x - pickWidth - style.ItemInnerSpacing.x;
				ImGui::SetNextItemWidth(pluginNameWidth);
				if (ImGui::InputTextWithHint("##PluginName", "Plugin name...", &_pluginNameString, ImGuiInputTextFlags_CallbackAlways, &TESFormValue::FormInputTextCallback, this)) {
					bEdited = true;
				}
				ImGui::SameLine();
				ImGui::SetNextItemWidth(formIDWidth);
				if (UI::UICommon::InputTextWithHint("##FormID", "FormID", &_formIdString, 6, ImGuiInputTextFlags_CallbackAlways | ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase, &TESFormValue::FormInputTextCallback, this)) {
					bEdited = true;
				}
				ImGui::SameLine(0.f, style.ItemInnerSpacing.x);

				// Only what the list offers. Typing a plugin and a form id above is
				// never narrowed by it. A picker of the caller's own, when set, stands
				// in the same place.
				auto formTypes = _formTypeFilter;
				if (formTypes.empty() && T::FORMTYPE != RE::FormType::None) {
					formTypes.push_back(T::FORMTYPE);
				}
				const auto predicate = _formPredicate;
				std::uint64_t picked = 0;
				bool bPicked = false;
				if (pickOverride) {
					bPicked = pickOverride(_form ? _form->GetFormID() : 0, picked);
				} else if (IsPickedByPlugin(formTypes)) {
					// A picker per set of types, so one list's plugins are not read back for another's
					std::string pickerId = "FormByPlugin";
					for (const auto type : formTypes) {
						pickerId += std::format("|{}", static_cast<int>(type));
					}
					bPicked = UI::UICommon::AsyncPicker(pickerId.data(), [formTypes, predicate] { return FormPickerGroups(formTypes, predicate); },
						[formTypes, predicate](std::uint64_t a_group) { return FormPickerGroupItems(formTypes, predicate, a_group); }, _form ? _form->GetFormID() : 0, picked);
				} else {
					bPicked = UI::UICommon::Picker("Form", [formTypes, predicate] { return FormPickerItems(formTypes, predicate); }, _form ? _form->GetFormID() : 0, picked);
				}
				if (bPicked) {
					auto* form = RE::TESForm::LookupByID(static_cast<RE::FormID>(picked));
					if constexpr (std::is_same_v<T, RE::TESForm>) {
						SetValue(form);
					} else {
						SetValue(form ? form->As<T>() : nullptr);
					}
					bEdited = true;
				}
				//ImGui::TableSetColumnIndex(1);
				UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
				ImGui::TextUnformatted(GetArgument().data());
				ImGui::PopID();
			} else {
				if (_form) {
					ImGui::TextUnformatted(std::format("[{:08X}]", _form->GetFormID()).data());
					//ImGui::TableSetColumnIndex(1);
					UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
					ImGui::TextUnformatted(GetArgument().data());
				} else {
					ImGui::TextUnformatted(std::format("{}, {}", _pluginNameString, _formIdString).data());
					//ImGui::TableSetColumnIndex(1);
					UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
					ImGui::TextUnformatted("(Not found)"sv.data());
				}
			}

			return bEdited;
		}

		std::string GetArgument() const
		{
			if (_bFeed) {
				return _feed.empty() ? "(no feed)" : _feed;
			}
			if (_form) {
				switch (T::FORMTYPE) {
				case RE::FormType::Global:
					{
						const auto global = reinterpret_cast<RE::TESGlobal*>(_form);
						return std::format("{} ({})", global->value, global->GetFormEditorID());
					}
				case RE::FormType::Keyword:
				case RE::FormType::LocationRefType:
					return _form->GetFormEditorID();
				default:
					return Utils::GetFormNameString(_form);
				}
			}
			return "(Not found)";
		}

		void Parse(rapidjson::Value& a_value)
		{
			const auto formObject = a_value.GetObj();
			if (const auto feedIt = formObject.FindMember("feed"); feedIt != formObject.MemberEnd() && feedIt->value.IsString()) {
				_bFeed = true;
				_feed = feedIt->value.GetString();
				return;
			}
			if (const auto pluginNameIt = formObject.FindMember("pluginName"); pluginNameIt != formObject.MemberEnd() && pluginNameIt->value.IsString()) {
				if (const auto formIDIt = formObject.FindMember("formID"); formIDIt != formObject.MemberEnd() && formIDIt->value.IsString()) {
					_pluginNameString = Utils::TrimWhitespace(pluginNameIt->value.GetString());
					_formIdString = Utils::TrimHexPrefix(Utils::TrimWhitespace(formIDIt->value.GetString()));
					RE::FormID formID;
					auto [ptr, ec] = std::from_chars(_formIdString.data(), _formIdString.data() + _formIdString.size(), formID, 16);
					if (ec == std::errc()) {
						_formIdString = std::format("{:X}", formID);

						auto form = Utils::LookupForm<T>(formID, _pluginNameString);
						SetValue(form);
					}
				}
			}
		}

		void ParseLegacy(std::string_view a_argument)
		{
			if (const size_t splitPos = a_argument.find('|'); splitPos != std::string_view::npos) {
				_pluginNameString = Utils::TrimQuotes(Utils::TrimWhitespace(a_argument.substr(0, splitPos)));
				_formIdString = std::string(Utils::TrimHexPrefix(Utils::TrimWhitespace(a_argument.substr(splitPos + 1))));
				RE::FormID formID;
				auto [ptr, ec] = std::from_chars(_formIdString.data(), _formIdString.data() + _formIdString.size(), formID, 16);
				if (ec == std::errc()) {
					_formIdString = std::format("{:X}", formID);

					auto form = Utils::LookupForm<T>(formID, _pluginNameString);
					SetValue(form);
				}
			}
		}

		rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const
		{
			rapidjson::Value result(rapidjson::kObjectType);
			if (_bFeed) {
				result.AddMember("feed", rapidjson::StringRef(_feed.data(), _feed.length()), a_allocator);
				return result;
			}
			result.AddMember("pluginName", rapidjson::StringRef(_pluginNameString.data(), _pluginNameString.length()), a_allocator);
			result.AddMember("formID", rapidjson::StringRef(_formIdString.data(), _formIdString.length()), a_allocator);
			return result;
		}

		// Draws the Pick button and whatever it opens, in the place of the form list; true with the picked form id
		std::function<bool(RE::FormID a_current, std::uint64_t& a_outPicked)> pickOverride;

		// The form feeds the host may read, by id; unset, the value is a form alone
		std::function<std::vector<std::string>()> getFeeds = nullptr;

		void LookupForm()
		{
			RE::FormID formID;
			auto [ptr, ec]{ std::from_chars(_formIdString.data(), _formIdString.data() + _formIdString.size(), formID, 16) };
			if (ec == std::errc()) {
				_form = Utils::LookupForm<T>(formID, _pluginNameString);
			} else {
				_form = nullptr;
			}
		}

	protected:
		static int FormInputTextCallback(struct ImGuiInputTextCallbackData* a_data)
		{
			auto* value = static_cast<TESFormValue*>(a_data->UserData);
			value->LookupForm();

			return 0;
		}

		// The Static Value / Feed Value slider, and as a feed its id with Pick; true when the value is a feed, drawn whole
		bool DisplayFeed(bool a_bEditable, float a_firstColumnWidthPercent, bool& a_bOutEdited, bool a_bSlider = true)
		{
			constexpr std::array names{ "Static Value", "Feed Value" };
			if (a_bEditable && a_bSlider) {
				int index = _bFeed ? 1 : 0;
				ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
				ImGui::PushID(&_bFeed);
				if (ImGui::SliderInt("", &index, 0, 1, names[index])) {
					_bFeed = index == 1;
					a_bOutEdited = true;
				}
				ImGui::PopID();
			}
			if (!_bFeed) {
				return false;
			}
			const auto ids = getFeeds();
			if (a_bEditable) {
				ImGui::PushID(&_feed);
				const auto& style = ImGui::GetStyle();
				ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent) - UI::UICommon::PickerButtonWidth() - style.ItemInnerSpacing.x);
				if (ImGui::InputTextWithHint("##Feed", "feed key", &_feed)) {
					a_bOutEdited = true;
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
					a_bOutEdited = true;
				}
				ImGui::PopID();
			} else {
				ImGui::TextUnformatted(GetArgument().data());
			}
			if (std::ranges::find(ids, _feed) == ids.end()) {
				UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
				ImGui::TextUnformatted("(Not found)");
			}
			return true;
		}

		bool _bFeed = false;
		std::string _feed;

		T* _form = nullptr;
		std::vector<RE::FormType> _formTypeFilter;
		FormPredicate _formPredicate = nullptr;
		std::string _pluginNameString{};
		std::string _formIdString{};
	};

	class TextValue
	{
	public:
		[[nodiscard]] std::string_view GetValue() const { return _text; }
		void SetValue(std::string_view a_text) { _text = a_text; }
		void SetTextHint(std::string_view a_text) { _uiTextHint = a_text; }

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent);

		void Parse(const rapidjson::Value& a_value);

		rapidjson::Value Serialize([[maybe_unused]] rapidjson::Document::AllocatorType& a_allocator) const;

		void SetAllowSpaces(bool a_bAllowSpaces) { _bAllowSpaces = a_bAllowSpaces; }

		// A [Pick] button beside the box, over these items; picking one writes its label. Typing is never narrowed.
		// The resolver says what the text names, shown in the second column, or nothing for "(Not found)".
		void SetPickerItems(std::function<std::vector<UI::UICommon::PickerItem>()> a_items, std::function<std::string(std::string_view)> a_resolve)
		{
			_pickerItems = std::move(a_items);
			_pickerResolve = std::move(a_resolve);
		}

		// What the text names, or "(Not found)"; the text itself when there is no resolver
		[[nodiscard]] std::string GetArgument() const
		{
			if (!_pickerResolve) {
				return _text;
			}
			const auto resolved = _pickerResolve(_text);
			return resolved.empty() ? std::string("(Not found)") : resolved;
		}

	protected:
		std::string _text{};
		std::string _uiTextHint = "Text...";
		bool _bAllowSpaces = true;
		std::function<std::vector<UI::UICommon::PickerItem>()> _pickerItems;
		std::function<std::string(std::string_view)> _pickerResolve;
	};

	// An animation file, and when the OAR replacer box is ticked, the one OAR replacer it must be played as
	class AnimationValue
	{
	public:
		AnimationValue();

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent);

		void Parse(const rapidjson::Value& a_value);
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const;

		[[nodiscard]] std::string GetArgument() const;
		[[nodiscard]] bool IsValid() const { return !file.GetValue().empty() && (!bReplacer || !replacer.GetValue().empty()); }

		// Whether a clip playing this file, as this replacer or none, is the one named
		[[nodiscard]] bool Matches(std::string_view a_file, std::string_view a_replacer) const;

		// A file as clips and pickers name it: lowercase, backslashes, relative to the animations folder
		[[nodiscard]] static std::string Normalize(std::string_view a_path);

		TextValue file;
		bool bReplacer = false;
		TextValue replacer;
	};

	class NumericValue
	{
	public:
		enum class Type : int
		{
			kStaticValue,
			kGlobalVariable,
			kActorValue,
			kFeed,  // offered only where getFeeds is set

			kTotal,
			kNone = kTotal
		};

		[[nodiscard]] bool IsValid() const { return _type == Type::kGlobalVariable ? _globalVariable.IsValid() : true; }

		[[nodiscard]] float GetValue(RE::TESObjectREFR* a_refr) const;

		ActorValueType GetActorValueType() const { return _actorValueType; }
		RE::ActorValue GetActorValue() const { return _actorValue; }
		[[nodiscard]] RE::TESGlobal* GetGlobalVariable() const { return _globalVariable.GetValue(); }

		void SetType(Type a_type) { _type = a_type; }

		void SetStaticValue(float a_value)
		{
			_floatValue = a_value;
			_type = Type::kStaticValue;
		}

		void SetGlobalVariable(RE::TESGlobal* a_global)
		{
			_globalVariable.SetValue(a_global);
			_type = Type::kGlobalVariable;
		}

		void SetActorValue(RE::ActorValue a_actorValue)
		{
			_actorValue = a_actorValue;
			_type = Type::kActorValue;
		}

		void SetActorValue(RE::ActorValue a_actorValue, ActorValueType a_valueType)
		{
			_actorValue = a_actorValue;
			_actorValueType = a_valueType;
			_type = Type::kActorValue;
		}

		void SetActorValueType(ActorValueType a_valueType)
		{
			_actorValueType = a_valueType;
			_type = Type::kActorValue;
		}

		void SetStaticRange(float a_min, float a_max)
		{
			_staticRange = { a_min, a_max };
			_type = Type::kStaticValue;
			_forcedType = Type::kStaticValue;
		}

		void SetStaticInteger(bool a_bIsInteger)
		{
			_bStaticInteger = a_bIsInteger;
		}

		void SetForcedType(Type a_forcedType)
		{
			_type = a_forcedType;
			_forcedType = a_forcedType;
		}

		[[nodiscard]] Type GetType() const { return _type; }

		[[nodiscard]] std::string_view GetTypeName() const
		{
			switch (_type) {
			case Type::kStaticValue:
				if (getEnumMap) {
					return "Enum Value"sv;
				}
				return "Static Value"sv;
			case Type::kGlobalVariable:
				return "Global Variable"sv;
			case Type::kActorValue:
				return "Actor Value"sv;
			case Type::kFeed:
				return "Feed Value"sv;
			}

			return ""sv;
		}

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent);

		[[nodiscard]] std::string GetArgument() const;

		void Parse(rapidjson::Value& a_value);

		void ParseLegacy(std::string_view a_argument, bool a_bIsActorValue = false);

		rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator);

		std::function<const std::map<int32_t, std::string_view>&()> getEnumMap = nullptr;
		std::function<std::map<uint32_t, std::string_view>()> getUnsignedEnumMap = nullptr;

		bool HasEnumMap() const { return getEnumMap || getUnsignedEnumMap; }

		// The number feeds the host may read, by id; unset, Feed stays out of the type slider
		std::function<std::vector<std::string>()> getFeeds = nullptr;

	protected:
		Type _type = Type::kStaticValue;
		Type _forcedType = Type::kNone;
		bool _bStaticInteger = false;

		// static value
		float _floatValue = 0.f;
		std::optional<std::pair<float, float>> _staticRange = std::nullopt;

		// global variable
		TESFormValue<RE::TESGlobal> _globalVariable;

		// actor value
		ActorValueType _actorValueType = ActorValueType::kActorValue;
		RE::ActorValue _actorValue = RE::ActorValue::kNone;

		// feed
		std::string _feed;

		bool GetActorValue(RE::TESObjectREFR* a_refr, float& a_outValue) const;
		bool GetActorValueBase(RE::TESObjectREFR* a_refr, float& a_outValue) const;
		bool GetActorValueMax(RE::TESObjectREFR* a_refr, float& a_outValue) const;
		bool GetActorValuePercentage(RE::TESObjectREFR* a_refr, float& a_outValue) const;

		static constexpr std::string_view GetActorValueTypeString(ActorValueType a_type)
		{
			switch (a_type) {
			case ActorValueType::kActorValue:
				return "Value"sv;
			case ActorValueType::kBase:
				return "Base"sv;
			case ActorValueType::kMax:
				return "Max"sv;
			case ActorValueType::kPercentage:
				return "Percentage"sv;
			default:
				return ""sv;
			}
		}

	private:
		template <typename Key, typename T>
		bool DisplayComboBox(const std::map<Key, std::string_view>& a_enumMap, T& a_value, float a_firstColumnWidthPercent);
	};

	class NiPoint3Value
	{
	public:
		[[nodiscard]] const RE::NiPoint3& GetValue() const { return _value; }

		void SetValue(const RE::NiPoint3& a_value)
		{
			_value = a_value;
			_bIsValid = true;
		}

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent);

		[[nodiscard]] std::string GetArgument() const;

		void Parse(rapidjson::Value& a_value);

		rapidjson::Value Serialize([[maybe_unused]] rapidjson::Document::AllocatorType& a_allocator) const;

		[[nodiscard]] bool IsValid() const { return _bIsValid; }

	protected:
		RE::NiPoint3 _value;
		bool _bIsValid = false;
	};

	class ColourValue
	{
	public:
		[[nodiscard]] const RE::NiColor& GetValue() const { return _value; }

		void SetValue(const RE::NiColor& a_value)
		{
			_value = a_value;
			_bIsValid = true;
		}

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent);

		[[nodiscard]] std::string GetArgument() const;

		void Parse(rapidjson::Value& a_value);

		rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const;

		[[nodiscard]] bool IsValid() const { return _bIsValid; }

	protected:
		RE::NiColor _value{ 1.f, 1.f, 1.f };
		bool _bIsValid = false;
	};

	template <Derived<RE::BGSKeyword> T>
	class KeywordValue
	{
	public:
		KeywordValue() = default;

		KeywordValue(const KeywordValue& a_rhs) :
			_type(a_rhs._type),
			_keywordForm(a_rhs._keywordForm),
			_keywordLiteral(a_rhs._keywordLiteral),
			_keywordFormsMatchingLiteral(a_rhs._keywordFormsMatchingLiteral) {}

		KeywordValue& operator=(KeywordValue&& a_rhs) noexcept
		{
			_type = a_rhs._type;
			_keywordLiteral = a_rhs._keywordLiteral;
			_keywordForm = a_rhs._keywordForm;
			_keywordFormsMatchingLiteral = a_rhs._keywordFormsMatchingLiteral;

			return *this;
		}

		enum class Type : uint8_t
		{
			kLiteral,
			kForm
		};

		bool IsValid() const
		{
			if (_keywordForm.IsValid()) {
				return true;
			}
			ReadLocker locker(_dataLock);
			return !_keywordFormsMatchingLiteral.empty();
		}

		T* GetFormValue() const { return _keywordForm.GetValue(); }

		bool HasKeyword(const RE::BGSKeywordForm* a_keywordForm) const
		{
			bool bFound = false;
			ForEachKeyword([&](T* a_keyword) {
				if (a_keywordForm->HasKeyword(a_keyword)) {
					bFound = true;
					return RE::BSContainer::ForEachResult::kStop;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});

			return bFound;
		}

		void LookupFromLiteral() const
		{
			WriteLocker locker(_dataLock);
			_keywordFormsMatchingLiteral.clear();

			auto& keywords = RE::TESDataHandler::GetSingleton()->GetFormArray<T>();
			for (auto& kywd : keywords) {
				if (kywd && kywd->formEditorID == std::string_view(_keywordLiteral)) {
					_keywordFormsMatchingLiteral.emplace_back(kywd);
				}
			}
		}

		void SetType(Type a_type)
		{
			WriteLocker locker(_dataLock);

			_type = a_type;
			if (a_type == Type::kForm) {
				_keywordFormsMatchingLiteral.clear();
			} else {
				_keywordForm.SetValue(nullptr);
			}
		}

		void SetKeyword(T* a_keyword)
		{
			SetType(Type::kForm);

			WriteLocker locker(_dataLock);
			_keywordForm.SetValue(a_keyword);
		}

		void ClearKeyword()
		{
			WriteLocker locker(_dataLock);
			_keywordForm.SetValue(nullptr);
		}

		void SetLiteral(std::string_view a_literal)
		{
			SetType(Type::kLiteral);

			{
				WriteLocker locker(_dataLock);
				_keywordLiteral = a_literal;
			}

			LookupFromLiteral();
		}

		void LateMatchLiteral() const
		{
			if (!_bLateMatchingRan) {  // re-run the lookup once, in case the keyword was created after this object was created (e.g. dynamically by KID)
				LookupFromLiteral();
				_bLateMatchingRan = true;
			}
		}

		Type GetType() const { return _type; }
		std::string_view GetTypeName() const { return _type == Type::kLiteral ? "Literal"sv : "Form"sv; }

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent)
		{
			bool bEdited = false;

			if (a_bEditable) {
				ImGui::PushID(&_type);
				ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
				if (ImGui::SliderInt("", reinterpret_cast<int*>(&_type), 0, 1, GetTypeName().data())) {
					// Lookup cached form again if type changed
					SetType(_type);
					switch (_type) {
					case Type::kLiteral:
						LookupFromLiteral();
						break;
					case Type::kForm:
						_keywordForm.LookupForm();
						break;
					}
					bEdited = true;
				}
				ImGui::PopID();

				switch (_type) {
				case Type::kLiteral:
					ImGui::SetNextItemWidth(UI::UICommon::FirstColumnWidth(a_firstColumnWidthPercent));
					if (ImGui::InputTextWithHint("##Keyword", "Keyword...", &_keywordLiteral, ImGuiInputTextFlags_CallbackAlways, &KeywordValue::KeywordInputTextCallback, this)) {
						bEdited = true;
					}
					UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
					LateMatchLiteral();
					if (_keywordFormsMatchingLiteral.size() > 0) {
						std::string formIDs{};
						bool bIsFirst = true;
						ForEachKeyword([&](auto a_kywd) {
							if (!bIsFirst) {
								formIDs.append(", "sv);
							}
							formIDs.append(std::format("[{:08X}]", a_kywd->GetFormID()));
							bIsFirst = false;
							return RE::BSContainer::ForEachResult::kContinue;
						});
						ImGui::TextUnformatted(formIDs.data());
					} else {
						ImGui::TextUnformatted("(Not found)");
					}
					break;
				case Type::kForm:
					if (_keywordForm.DisplayInUI(a_bEditable, a_firstColumnWidthPercent)) {
						bEdited = true;
					}
					break;
				}
			} else {
				switch (_type) {
				case Type::kLiteral:
					ImGui::TextUnformatted(_keywordLiteral.data());
					UI::UICommon::SecondColumn(a_firstColumnWidthPercent);
					LateMatchLiteral();
					if (_keywordFormsMatchingLiteral.size() > 0) {
						std::string formIDs{};
						bool bIsFirst = true;
						ForEachKeyword([&](auto a_kywd) {
							if (!bIsFirst) {
								formIDs.append(", "sv);
							}
							formIDs.append(std::format("[{:08X}]", a_kywd->GetFormID()));
							bIsFirst = false;
							return RE::BSContainer::ForEachResult::kContinue;
						});
						ImGui::TextUnformatted(formIDs.data());
					} else {
						ImGui::TextUnformatted("(Not found)");
					}
					break;
				case Type::kForm:
					_keywordForm.DisplayInUI(a_bEditable, a_firstColumnWidthPercent);
					break;
				}
			}

			return bEdited;
		}

		std::string GetArgument() const
		{
			switch (_type) {
			case Type::kLiteral:
				return _keywordLiteral;

			case Type::kForm:
				if (_keywordForm.IsValid()) {
					return _keywordForm.GetValue()->GetFormEditorID();
				}

			default:
				return "(Not found)";
			}
		}

		void Parse(rapidjson::Value& a_value)
		{
			const auto object = a_value.GetObj();

			if (const auto keywordIt = object.FindMember("editorID"); keywordIt != object.MemberEnd() && keywordIt->value.IsString()) {
				const std::string_view editorID = keywordIt->value.GetString();

				SetLiteral(editorID);

				return;
			}

			if (const auto formIt = object.FindMember("form"); formIt != object.MemberEnd() && formIt->value.IsObject()) {
				SetType(Type::kForm);
				_keywordForm.Parse(formIt->value);
			}
		}

		void ParseLegacy(std::string_view a_argument)
		{
			_type = Type::kForm;
			_keywordForm.ParseLegacy(a_argument);
		}

		rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator)
		{
			rapidjson::Value object(rapidjson::kObjectType);

			switch (_type) {
			case Type::kLiteral:
				object.AddMember("editorID", rapidjson::StringRef(_keywordLiteral.data(), _keywordLiteral.length()), a_allocator);
				break;
			case Type::kForm:
				object.AddMember("form", _keywordForm.Serialize(a_allocator), a_allocator);
				break;
			}

			return object;
		}

		void ForEachKeyword(std::function<RE::BSContainer::ForEachResult(T*)> a_callback) const
		{
			if (_type == Type::kForm) {
				if (_keywordForm.IsValid()) {
					a_callback(_keywordForm.GetValue());
				}
			} else {
				LateMatchLiteral();

				ReadLocker locker(_dataLock);
				for (auto& kywd : _keywordFormsMatchingLiteral) {
					if (a_callback(kywd) == RE::BSContainer::ForEachResult::kStop) {
						return;
					}
				}
			}
		}

	protected:
		static int KeywordInputTextCallback(struct ImGuiInputTextCallbackData* a_data)
		{
			auto* value = static_cast<KeywordValue*>(a_data->UserData);
			value->ClearKeyword();
			value->LookupFromLiteral();

			return 0;
		}

		Type _type = Type::kLiteral;

		TESFormValue<T> _keywordForm;

		std::string _keywordLiteral{};
		mutable SharedLock _dataLock{};
		mutable std::vector<T*> _keywordFormsMatchingLiteral{};

		mutable bool _bLateMatchingRan = false;
	};

}
