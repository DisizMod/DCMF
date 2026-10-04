#pragma once
#include <rapidjson/document.h>

#include "API/DCMF-ResourcesAPI.h"
#include "BaseConditions.h"
#include "scan/Scan.h"

namespace Conditions
{
	[[nodiscard]] std::string_view CorrectLegacyConditionName(std::string_view a_conditionName);
	[[nodiscard]] std::string_view CorrectConditionName(std::string_view a_conditionName);
	[[nodiscard]] std::unique_ptr<ICondition> CreateConditionFromString(std::string_view a_line);
	[[nodiscard]] std::unique_ptr<ICondition> CreateConditionFromJson(rapidjson::Value& a_value, ConditionSet* a_parentConditionSet = nullptr);
	[[nodiscard]] std::unique_ptr<ICondition> CreateCondition(std::string_view a_conditionName);
	// What a condition asks about, legacy's groups; a plugin's under the plugin's name; and its rank in the picker
	[[nodiscard]] std::string_view CategoryOf(std::string_view a_conditionName);
	[[nodiscard]] int CategoryRank(std::string_view a_category);
	[[nodiscard]] std::unique_ptr<ICondition> DuplicateCondition(const std::unique_ptr<ICondition>& a_conditionToDuplicate);
	[[nodiscard]] std::unique_ptr<ConditionSet> DuplicateConditionSet(ConditionSet* a_conditionSetToDuplicate);

	[[nodiscard]] std::unique_ptr<ICondition> ConvertDeprecatedCondition(std::unique_ptr<Conditions::ICondition>& a_deprecatedCondition, std::string_view a_conditionName, rapidjson::Value& a_value);

	class InvalidCondition : public ConditionBase
	{
	public:
		InvalidCondition() = default;

		InvalidCondition(std::string_view a_argument)
		{
			_argument = a_argument;
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return _argument.data(); }
		[[nodiscard]] RE::BSString GetName() const override { return "! INVALID !"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "The condition was not found!"sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 0, 0, 0 }; }

		[[nodiscard]] bool IsValid() const override { return false; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		std::string _argument;
	};

	class InvalidNonEssentialCondition : public ConditionBase
	{
	public:
		InvalidNonEssentialCondition() = default;

		InvalidNonEssentialCondition(std::string_view a_argument, std::string_view a_name, const rapidjson::Value& a_json)
		{
			_argument = a_argument;
			_name = a_name;
			_json.CopyFrom(a_json, _json.GetAllocator());
		}

		void Serialize(void* a_value, void* a_allocator) override;

		[[nodiscard]] RE::BSString GetArgument() const override { return _argument.data(); }
		[[nodiscard]] RE::BSString GetName() const override;
		[[nodiscard]] RE::BSString GetDescription() const override;
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 0, 0, 0 }; }

		[[nodiscard]] bool IsValid() const override { return true; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		std::string _argument;
		std::string _name;
		rapidjson::Document _json;
	};

	class DeprecatedCondition : public ConditionBase
	{
	public:
		DeprecatedCondition() = default;

		DeprecatedCondition(std::string_view a_argument)
		{
			_argument = a_argument;
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return _argument.data(); }
		[[nodiscard]] RE::BSString GetName() const override { return "! DEPRECATED !"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "The condition has been deprecated in this version of DCMF. This clip or modifier needs to be updated."; }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 0, 0, 0 }; }

		[[nodiscard]] bool IsValid() const override { return false; }

		[[nodiscard]] bool IsDeprecated() const override { return true; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		std::string _argument;
	};

	class ORCondition : public ConditionBase
	{
	public:
		ORCondition()
		{
			conditionsComponent = AddComponent<MultiConditionComponent>("Conditions");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return conditionsComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "OR"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if any of the child conditions are true."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		[[nodiscard]] bool IsValid() const override { return conditionsComponent->IsValid(); }

		MultiConditionComponent* conditionsComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class ANDCondition : public ConditionBase
	{
	public:
		ANDCondition()
		{
			conditionsComponent = AddComponent<MultiConditionComponent>("Conditions");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return conditionsComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "AND"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if all of the child conditions are true."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		[[nodiscard]] bool IsValid() const override { return conditionsComponent->IsValid(); }

		MultiConditionComponent* conditionsComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsFormCondition : public ConditionBase
	{
	public:
		IsFormCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Form");
			formComponent->form.SetFormTypeFilter(RE::FormType::ActorCharacter);  // only actors are evaluated
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsForm"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref matches the specified form."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsEquippedCondition : public ConditionBase
	{
	public:
		IsEquippedCondition(bool a_bIsLeft = false)
		{
			formComponent = AddComponent<FormConditionComponent>("Form", "The form that should be equipped.");
			formComponent->form.SetFormTypeFilter({ RE::FormType::Weapon, RE::FormType::Armor, RE::FormType::Spell, RE::FormType::Scroll, RE::FormType::Light });
			boolComponent = AddComponent<BoolConditionComponent>("Left hand", "Enable to check left hand. Disable to check right hand.");
			boolComponent->bValue = a_bIsLeft;
		}

		void InitializeLegacy(const char* a_argument) override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "IsEquipped"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has the specified form equipped in the right or left hand."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;
		BoolConditionComponent* boolComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		RE::TESForm* GetEquippedForm(RE::TESObjectREFR* a_refr) const;
	};

	class IsEquippedTypeCondition : public ConditionBase
	{
	public:
		IsEquippedTypeCondition(bool a_bIsLeft = false)
		{
			numericComponent = AddComponent<NumericConditionComponent>("Type", "The type that should be equipped in the right or left hand.");
			boolComponent = AddComponent<BoolConditionComponent>("Left hand", "Enable to check left hand. Disable to check right hand.");
			boolComponent->bValue = a_bIsLeft;
		}

		void InitializeLegacy(const char* a_argument) override;

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "IsEquippedType"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has an item of the specified type equipped in the right or left hand."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		NumericConditionComponent* numericComponent;
		BoolConditionComponent* boolComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		RE::TESForm* GetEquippedForm(RE::TESObjectREFR* a_refr) const;
		int8_t GetEquippedType(RE::TESObjectREFR* a_refr) const;

		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class IsEquippedHasKeywordCondition : public ConditionBase
	{
	public:
		IsEquippedHasKeywordCondition(bool a_bIsLeft = false)
		{
			keywordComponent = AddComponent<KeywordConditionComponent>("Keyword");
			boolComponent = AddComponent<BoolConditionComponent>("Left hand", "Enable to check left hand. Disable to check right hand.");
			boolComponent->bValue = a_bIsLeft;
		}

		void InitializeLegacy(const char* a_argument) override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "IsEquippedHasKeyword"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has an item equipped in the right or left hand that has the specified keyword."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		KeywordConditionComponent* keywordComponent;
		BoolConditionComponent* boolComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		RE::BGSKeywordForm* GetEquippedKeywordForm(RE::TESObjectREFR* a_refr) const;
	};

	class IsEquippedPowerCondition : public ConditionBase
	{
	public:
		IsEquippedPowerCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Power", "The power that should be matched by the equipped one.", RE::FormType::Spell);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsEquippedPower"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has the specified spell equipped in the power slot."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static RE::TESForm* GetEquippedPower(RE::TESObjectREFR* a_refr);
	};

	class IsWornCondition : public ConditionBase
	{
	public:
		IsWornCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Form", "The form that should be worn.");
			formComponent->form.SetFormTypeFilter({ RE::FormType::Armor, RE::FormType::Weapon, RE::FormType::Ammo, RE::FormType::Light });
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsWorn"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has the specified form equipped in any slot."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsWornHasKeywordCondition : public ConditionBase
	{
	public:
		IsWornHasKeywordCondition()
		{
			keywordComponent = AddComponent<KeywordConditionComponent>("Keyword");
		}

		void InitializeLegacy(const char* a_argument) override { keywordComponent->keyword.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return keywordComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsWornHasKeyword"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has an item equipped in any slot that has the specified keyword."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		KeywordConditionComponent* keywordComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	// A condition another plugin declared as a test of one of its resources
	class ResourceCondition : public ConditionBase
	{
	public:
		struct Definition
		{
			std::string name;
			std::string description;
			std::string resource;
			std::string operandName;
			std::string plugin;
			DCMF_API::Resources::ConditionTest test = DCMF_API::Resources::ConditionTest::Compare;
			RE::FormType formType = RE::FormType::None;
		};

		explicit ResourceCondition(std::shared_ptr<const Definition> a_definition);

		[[nodiscard]] RE::BSString GetArgument() const override;
		[[nodiscard]] RE::BSString GetName() const override { return _definition->name.c_str(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return _definition->description.c_str(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		ComparisonConditionComponent* comparisonComponent = nullptr;
		NumericConditionComponent* numericComponent = nullptr;
		FormConditionComponent* formComponent = nullptr;
		TextConditionComponent* textComponent = nullptr;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		std::shared_ptr<const Definition> _definition;
	};

	// Any thread: a plugin's condition added to the picker and to what files may name; false when the name is taken
	bool AddResourceCondition(ResourceCondition::Definition a_definition);

	class IsWornNamedCondition : public ConditionBase
	{
	public:
		IsWornNamedCondition()
		{
			textComponent = AddComponent<TextConditionComponent>("Name", "The item's name, as the inventory shows it or as its form has it; whatever the case.");
			textComponent->SetAllowSpaces(true);
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return textComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsWornNamed"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref wears an item of this name."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		TextConditionComponent* textComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsWornFromPluginCondition : public ConditionBase
	{
	public:
		IsWornFromPluginCondition()
		{
			textComponent = AddComponent<TextConditionComponent>("Plugin", "The plugin's file name, with its extension; whatever the case.");
			textComponent->SetAllowSpaces(true);
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return textComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsWornFromPlugin"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref wears an item a plugin adds."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		TextConditionComponent* textComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsFemaleCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsFemale"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is female."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsDeadCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsDead"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is dead."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsChildCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsChild"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is a child."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsPlayerTeammateCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsPlayerTeammate"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is a teammate of the player."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsInInteriorCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsInInterior"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is in an interior cell."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsInFactionCondition : public ConditionBase
	{
	public:
		IsInFactionCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Faction", "The faction that the evaluated actor should be a member of.", RE::FormType::Faction);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsInFaction"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is in the specified faction."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class HasKeywordCondition : public ConditionBase
	{
	public:
		HasKeywordCondition()
		{
			keywordComponent = AddComponent<KeywordConditionComponent>("Keyword");
		}

		void InitializeLegacy(const char* a_argument) override { keywordComponent->keyword.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return keywordComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "HasKeyword"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has the specified keyword."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		KeywordConditionComponent* keywordComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class HasMagicEffectCondition : public ConditionBase
	{
	public:
		HasMagicEffectCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Magic effect", "The magic effect to check.", RE::FormType::MagicEffect);
			boolComponent = AddComponent<BoolConditionComponent>("Active effects only", "Enable to check active magic effects only.");
		}

		void InitializeLegacy(const char* a_argument) override;

		[[nodiscard]] RE::BSString GetArgument() const override;
		[[nodiscard]] RE::BSString GetName() const override { return "HasMagicEffect"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is currently affected by the specified magic effect."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;
		BoolConditionComponent* boolComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class HasMagicEffectWithKeywordCondition : public ConditionBase
	{
	public:
		HasMagicEffectWithKeywordCondition()
		{
			keywordComponent = AddComponent<KeywordConditionComponent>("Keyword");
			boolComponent = AddComponent<BoolConditionComponent>("Active effects only", "Enable to check active magic effects only.");
		}

		void InitializeLegacy(const char* a_argument) override;

		[[nodiscard]] RE::BSString GetArgument() const override;
		[[nodiscard]] RE::BSString GetName() const override { return "HasMagicEffectWithKeyword"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is currently affected by a magic effect that has the specified keyword."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		KeywordConditionComponent* keywordComponent;
		BoolConditionComponent* boolComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class HasPerkCondition : public ConditionBase
	{
	public:
		HasPerkCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Perk", "The perk that the evaluated ref should have.", RE::FormType::Perk);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "HasPerk"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has the specified perk."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class HasSpellCondition : public ConditionBase
	{
	public:
		HasSpellCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Spell", "The spell or shout that the evaluated ref should have.");
			formComponent->form.SetFormTypeFilter({ RE::FormType::Spell });
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "HasSpell"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has the specified spell or shout."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class CompareValues : public ConditionBase
	{
	public:
		CompareValues(ComparisonOperator a_comparisonOperator = ComparisonOperator::kEqual)
		{
			numericComponentA = AddComponent<NumericConditionComponent>("Value A");
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponentB = AddComponent<NumericConditionComponent>("Value B");

			comparisonComponent->comparisonOperator = a_comparisonOperator;
		}

		CompareValues(Components::ActorValueType a_actorValueType, ComparisonOperator a_comparisonOperator)
		{
			numericComponentA = AddComponent<NumericConditionComponent>("Value A");
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponentB = AddComponent<NumericConditionComponent>("Value B");

			numericComponentA->value.SetActorValueType(a_actorValueType);
			comparisonComponent->comparisonOperator = a_comparisonOperator;
		}

		void InitializeLegacy(const char* a_argument) override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CompareValues"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Compares two values."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		NumericConditionComponent* numericComponentA;
		NumericConditionComponent* numericComponentB;
		ComparisonConditionComponent* comparisonComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class LevelCondition : public ConditionBase
	{
	public:
		LevelCondition(ComparisonOperator a_comparisonOperator = ComparisonOperator::kEqual)
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");

			comparisonComponent->comparisonOperator = a_comparisonOperator;
		}

		void InitializeLegacy(const char* a_argument) override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "Level"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the actor's level against the specified value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsActorBaseCondition : public ConditionBase
	{
	public:
		IsActorBaseCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Actor base", "The actor base form that should be matched by the evaluated actor.", RE::FormType::NPC);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsActorBase"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref's actor base form is the specified form."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static RE::TESNPC* GetActorBase(RE::TESObjectREFR* a_refr);
	};

	class IsRaceCondition : public ConditionBase
	{
	public:
		IsRaceCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Race", "The race that should be matched by the evaluated actor.", RE::FormType::Race);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsRace"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref's race is the specified race."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static RE::TESRace* GetRace(RE::TESObjectREFR* a_refr);
	};

	class CurrentWeatherCondition : public ConditionBase
	{
	public:
		CurrentWeatherCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Weather", "The weather (TESWeather) or a form list of weathers (BGSListForm) that should be matched by the current weather.");
			formComponent->form.SetFormTypeFilter({ RE::FormType::Weather, RE::FormType::FormList });
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "CurrentWeather"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the current weather is the specified weather."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class CurrentGameTimeCondition : public ConditionBase
	{
	public:
		CurrentGameTimeCondition(ComparisonOperator a_comparisonOperator = ComparisonOperator::kEqual)
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");

			comparisonComponent->comparisonOperator = a_comparisonOperator;
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentGameTime"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the current game time against the specified time."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		[[nodiscard]] static float GetHours();
	};

	class RandomCondition : public ConditionBase
	{
	public:
		RandomCondition()
		{
			stateComponent = AddComponent<ConditionStateComponent>("State");
			stateComponent->SetShouldResetOnLoopOrEcho(true);
			stateComponent->SetAllowedDataScopes(StateDataScope::kActor | StateDataScope::kSubMod | StateDataScope::kRegisteredMod);
			minRandomComponent = AddComponent<NumericConditionComponent>("Minimum random value");
			minRandomComponent->SetForcedType(Components::NumericValue::Type::kStaticValue);
			maxRandomComponent = AddComponent<NumericConditionComponent>("Maximum random value");
			maxRandomComponent->SetForcedType(Components::NumericValue::Type::kStaticValue);
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
			comparisonComponent->SetComparisonOperator(ComparisonOperator::kLess);
		}

		void Initialize(void* a_value) override;
		void InitializeLegacy(const char* a_argument) override;

		[[nodiscard]] RE::BSString GetArgument() const override;
		[[nodiscard]] RE::BSString GetName() const override { return "Random"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Compares a random value with a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 3, 0 }; }

		ConditionStateComponent* stateComponent;
		NumericConditionComponent* minRandomComponent;
		NumericConditionComponent* maxRandomComponent;
		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsUniqueCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsUnique"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is flagged as unique."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsClassCondition : public ConditionBase
	{
	public:
		IsClassCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Class", "The class that should be matched by the evaluated actor.", RE::FormType::Class);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsClass"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref's class is the specified class."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static RE::TESClass* GetTESClass(RE::TESObjectREFR* a_refr);
	};

	class IsCombatStyleCondition : public ConditionBase
	{
	public:
		IsCombatStyleCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Combat style", "The combat style that should be matched by the evaluated actor.", RE::FormType::CombatStyle);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsCombatStyle"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref's combat style is the specified combat style."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static RE::TESCombatStyle* GetCombatStyle(RE::TESObjectREFR* a_refr);
	};

	class IsVoiceTypeCondition : public ConditionBase
	{
	public:
		IsVoiceTypeCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Voice type", "The voice type that should be matched by the evaluated actor.", RE::FormType::VoiceType);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsVoiceType"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref's voice type is the specified voice type."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static RE::BGSVoiceType* GetVoiceType(RE::TESObjectREFR* a_refr);
	};

	class IsAttackingCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsAttacking"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is attacking."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsRunningCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsRunning"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is running."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsSneakingCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsSneaking"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is sneaking."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsSprintingCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsSprinting"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is sprinting."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsInAirCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsInAir"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is in the air."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsInCombatCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsInCombat"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is in combat."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsWeaponDrawnCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsWeaponDrawn"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has a weapon drawn."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsInLocationCondition : public ConditionBase
	{
	public:
		IsInLocationCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Location", "The location that the evaluated ref should be in.", RE::FormType::Location);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsInLocation"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is in the specified location."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static bool IsLocation(RE::BGSLocation* a_location, RE::BGSLocation* a_locationToCompare);
	};

	class HasRefTypeCondition : public ConditionBase
	{
	public:
		HasRefTypeCondition()
		{
			locRefTypeComponent = AddComponent<LocRefTypeConditionComponent>("Location ref type", "The BGSLocationRefType form that should be checked.");
		}

		void InitializeLegacy(const char* a_argument) override;

		[[nodiscard]] RE::BSString GetArgument() const override;
		[[nodiscard]] RE::BSString GetName() const override { return "HasRefType"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has the specified LocRefType attached."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

	protected:
		LocRefTypeConditionComponent* locRefTypeComponent;

		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsParentCellCondition : public ConditionBase
	{
	public:
		IsParentCellCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Cell", "The cell that the evaluated ref should be in.", RE::FormType::Cell);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsParentCell"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is currently in the specified cell."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsWorldSpaceCondition : public ConditionBase
	{
	public:
		IsWorldSpaceCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Worldspace", "The worldspace that the evaluated ref should be in.", RE::FormType::WorldSpace);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsWorldSpace"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is currently in the specified worldspace."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class FactionRankCondition : public ConditionBase
	{
	public:
		FactionRankCondition(ComparisonOperator a_comparisonOperator = ComparisonOperator::kEqual)
		{
			factionComponent = AddComponent<FormConditionComponent>("Faction", "The faction that should be checked.", RE::FormType::Faction);
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");

			comparisonComponent->comparisonOperator = a_comparisonOperator;
		}

		void InitializeLegacy(const char* a_argument) override;

		[[nodiscard]] RE::BSString GetArgument() const override;
		[[nodiscard]] RE::BSString GetName() const override { return "FactionRank"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the ref's faction rank against the specified rank."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* factionComponent;
		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		int32_t GetFactionRank(RE::TESObjectREFR* a_refr) const;
	};

	class IsMovementDirectionCondition : public ConditionBase
	{
	public:
		IsMovementDirectionCondition()
		{
			numericComponent = AddComponent<NumericConditionComponent>("Direction", "The direction that the ref should be moving in.");
		}

		void InitializeLegacy(const char* a_argument) override;
		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;
		[[nodiscard]] RE::BSString GetName() const override { return "IsMovementDirection"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is moving in the specified direction."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	// ========== END OF LEGACY CONDITIONS ==========

	class IsEquippedShoutCondition : public ConditionBase
	{
	public:
		IsEquippedShoutCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Shout", "The shout that should be matched by the equipped one.", RE::FormType::Shout);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsEquippedShout"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has the specified shout equipped."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static RE::TESShout* GetEquippedShout(RE::TESObjectREFR* a_refr);
	};

	class SubmergeLevelCondition : public ConditionBase
	{
	public:
		SubmergeLevelCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "SubmergeLevel"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the ref's water submerge level (0-1) against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsCurrentPackageCondition : public ConditionBase
	{
	public:
		IsCurrentPackageCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Package", "The package that should be matched by the evaluated actor's current package.", RE::FormType::Package);
		}

		void InitializeLegacy(const char* a_argument) override { formComponent->form.ParseLegacy(a_argument); }

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "IsCurrentPackage"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref's currently running the specified package."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static RE::TESPackage* GetCurrentPackage(RE::TESObjectREFR* a_refr);
	};

	class IsWornInSlotHasKeywordCondition : public ConditionBase
	{
	public:
		IsWornInSlotHasKeywordCondition()
		{
			slotComponent = AddComponent<NumericConditionComponent>("Slot", "The slot that should be checked.");
			keywordComponent = AddComponent<KeywordConditionComponent>("Keyword");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "IsWornInSlotHasKeyword"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has an item worn in the specified slot that has the specified keyword."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		NumericConditionComponent* slotComponent;
		KeywordConditionComponent* keywordComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class ScaleCondition : public ConditionBase
	{
	public:
		ScaleCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "Scale"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the ref's scale against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class HeightCondition : public ConditionBase
	{
	public:
		HeightCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "Height"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the actor's height against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class WeightCondition : public ConditionBase
	{
	public:
		WeightCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "Weight"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the actor's weight against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class MovementSpeedCondition : public ConditionBase
	{
	public:
		MovementSpeedCondition()
		{
			movementTypeComponent = AddComponent<NumericConditionComponent>("Movement type", "The movement type whose speed should be read.");
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "MovementSpeed"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the ref's movement speed of a given type against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		NumericConditionComponent* movementTypeComponent;
		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		float GetMovementSpeed(RE::Actor* a_actor) const;

		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class CurrentMovementSpeedCondition : public ConditionBase
	{
	public:
		CurrentMovementSpeedCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentMovementSpeed"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the ref's current movement speed against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class WindSpeedCondition : public ConditionBase
	{
	public:
		WindSpeedCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "WindSpeed"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the current weather's wind speed against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static float GetWindSpeed();
	};

	class WindAngleDifferenceCondition : public ConditionBase
	{
	public:
		WindAngleDifferenceCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
			degreesComponent = AddComponent<BoolConditionComponent>("Degrees", "Whether the angle values are in degrees or radians.");
			absoluteComponent = AddComponent<BoolConditionComponent>("Absolute", "Whether the angle values are absolute. Disable to be able to differentiate between clockwise and counterclockwise. Enable if you don't care about that, it should make the comparison easier to do in one condition.");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "WindAngleDifference"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the difference between current weather's wind angle and the ref's angle against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;
		BoolConditionComponent* degreesComponent;
		BoolConditionComponent* absoluteComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		float GetWindAngleDifference(RE::TESObjectREFR* a_refr) const;
	};

	class CrimeGoldCondition : public ConditionBase
	{
	public:
		CrimeGoldCondition()
		{
			factionComponent = AddComponent<FormConditionComponent>("Faction", "The crime faction to check.", RE::FormType::Faction);
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CrimeGold"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the actor's current crime gold against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		FormConditionComponent* factionComponent;
		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsBlockingCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsBlocking"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is blocking."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsCombatStateCondition : public ConditionBase
	{
	public:
		IsCombatStateCondition()
		{
			combatStateComponent = AddComponent<NumericConditionComponent>("Combat state", "The required combat state.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "IsCombatState"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref's current combat state matches the given state."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

		NumericConditionComponent* combatStateComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		RE::ACTOR_COMBAT_STATE GetCombatState(RE::Actor* a_actor) const;
		std::string_view GetCombatStateName(RE::ACTOR_COMBAT_STATE a_state) const;

		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class InventoryCountCondition : public ConditionBase
	{
	public:
		InventoryCountCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Form", "The form that should be searched for in the inventory.");
			formComponent->form.SetFormTypeFilter({ RE::FormType::Armor, RE::FormType::Weapon, RE::FormType::Ammo, RE::FormType::Book, RE::FormType::Misc, RE::FormType::KeyMaster, RE::FormType::SoulGem, RE::FormType::AlchemyItem, RE::FormType::Ingredient, RE::FormType::Scroll, RE::FormType::Light });
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value", "The value to compare the inventory count with.");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "InventoryCount"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the actor's current inventory count of a specified form against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

		FormConditionComponent* formComponent;
		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		int GetItemCount(RE::TESObjectREFR* a_refr) const;
	};

	class FallDistanceCondition : public ConditionBase
	{
	public:
		FallDistanceCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "FallDistance"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Gets the actor's current fall distance and tests it against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static float GetFallDistance(RE::TESObjectREFR* a_refr);
	};

	class FallDamageCondition : public ConditionBase
	{
	public:
		FallDamageCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "FallDamage"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Calculates the actor's fall damage if they landed at this moment and tests it against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		static float GetFallDamage(RE::TESObjectREFR* a_refr);
	};

	class CurrentPackageTypeCondition : public ConditionBase
	{
	public:
		CurrentPackageTypeCondition()
		{
			packageTypeComponent = AddComponent<NumericConditionComponent>("Package type", "The required type of the current package.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentPackageType"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the actor's current package is of a given type."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

		NumericConditionComponent* packageTypeComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		RE::PACKAGE_TYPE GetPackageType(RE::Actor* a_actor) const;
		std::string_view GetPackageTypeName(RE::PACKAGE_TYPE a_type) const;

		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class IsOnMountCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsOnMount"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is riding a mount."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsRidingCondition : public ConditionBase
	{
	public:
		IsRidingCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Mount", "The form that the actor should be riding.");
			formComponent->form.SetFormTypeFilter({ RE::FormType::NPC });
			formComponent->form.SetFormPredicate(&Components::IsCreature);
			boolComponent = AddComponent<BoolConditionComponent>("Base", "Enable to check the mount's base actor.");
			boolComponent->SetBoolValue(true);
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "IsRiding"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is riding the specified form."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

		FormConditionComponent* formComponent;
		BoolConditionComponent* boolComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsRidingHasKeywordCondition : public ConditionBase
	{
	public:
		IsRidingHasKeywordCondition()
		{
			keywordComponent = AddComponent<KeywordConditionComponent>("Keyword");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return keywordComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "IsRidingHasKeyword"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is riding a form with the specified keyword."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

		KeywordConditionComponent* keywordComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class CurrentFurnitureCondition : public ConditionBase
	{
	public:
		CurrentFurnitureCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Furniture", "The furniture form that the actor should be occupying.", RE::FormType::Furniture);
			boolComponent = AddComponent<BoolConditionComponent>("Base", "Enable to check the furniture base object Disable to check the current furniture ref.");
			boolComponent->SetBoolValue(true);
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentFurniture"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is currently occupying the specified furniture."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

		FormConditionComponent* formComponent;
		BoolConditionComponent* boolComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class CurrentFurnitureHasKeywordCondition : public ConditionBase
	{
	public:
		CurrentFurnitureHasKeywordCondition()
		{
			keywordComponent = AddComponent<KeywordConditionComponent>("Keyword");
			boolComponent = AddComponent<BoolConditionComponent>("Base", "Enable to check the furniture base object. Disable to check the current furniture ref.");
			boolComponent->SetBoolValue(true);
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return keywordComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentFurnitureHasKeyword"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is currently occupying furniture with the specified keyword."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

		KeywordConditionComponent* keywordComponent;
		BoolConditionComponent* boolComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class TargetConditionBase : public ConditionBase
	{
	public:
		TargetConditionBase()
		{
			targetTypeComponent = AddComponent<NumericConditionComponent>("Target type", "The required target type.");
		}

		void PostInitialize() override;

		NumericConditionComponent* targetTypeComponent;

	protected:
		std::string_view GetTargetTypeName(Utils::TargetType a_targetType) const;
		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class HasTargetCondition : public TargetConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "HasTarget"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the actor has a target of a given type."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class CurrentTargetDistanceCondition : public TargetConditionBase
	{
	public:
		CurrentTargetDistanceCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentTargetDistance"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the distance to an actor's current target against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		RE::TESObjectREFRPtr GetTarget(RE::TESObjectREFR* a_refr) const;
	};

	class CurrentTargetRelationshipCondition : public TargetConditionBase
	{
	public:
		CurrentTargetRelationshipCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentTargetRelationship"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the relationship between the actor and their current target against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 1, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		RE::TESObjectREFRPtr GetTarget(RE::TESObjectREFR* a_refr) const;
		RE::TESNPC* GetActorBase(RE::TESObjectREFR* a_refr) const;

		std::string_view GetRelationshipRankName(int32_t a_relationshipRank) const;
		static const std::map<int32_t, std::string_view>& GetRelationshipRankEnumMap();
	};

	class EquippedObjectWeightCondition : public ConditionBase
	{
	public:
		EquippedObjectWeightCondition()
		{
			boolComponent = AddComponent<BoolConditionComponent>("Left hand", "Enable to check left hand. Disable to check right hand.");
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "EquippedObjectWeight"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the weight of the object currently equipped in the right or left hand against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		BoolConditionComponent* boolComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		RE::TESForm* GetEquippedForm(RE::TESObjectREFR* a_refr) const;
	};

	class CastingSourceConditionBase : public ConditionBase
	{
	public:
		CastingSourceConditionBase()
		{
			castingSourceComponent = AddComponent<NumericConditionComponent>("Casting source", "The casting source to check.");
		}

		void PostInitialize() override;

		NumericConditionComponent* castingSourceComponent;

	protected:
		std::string_view GetCastingSourceName(RE::MagicSystem::CastingSource a_source) const;
		static const std::map<int32_t, std::string_view>& GetCastingSourceEnumMap();
	};

	class CurrentCastingTypeCondition : public CastingSourceConditionBase
	{
	public:
		CurrentCastingTypeCondition()
		{
			castingTypeComponent = AddComponent<NumericConditionComponent>("Casting type", "The required casting type.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentCastingType"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the actor's current casting type of the given casting source is the required type."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

		NumericConditionComponent* castingTypeComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		bool GetCastingType(RE::Actor* a_actor, RE::MagicSystem::CastingSource a_source, RE::MagicSystem::CastingType& a_outType) const;
		std::string_view GetCastingTypeName(RE::MagicSystem::CastingType a_type) const;

		static const std::map<int32_t, std::string_view>& GetCastingTypeEnumMap();
	};

	class CurrentDeliveryTypeCondition : public CastingSourceConditionBase
	{
	public:
		CurrentDeliveryTypeCondition()
		{
			deliveryTypeComponent = AddComponent<NumericConditionComponent>("Delivery type", "The required delivery type.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentDeliveryType"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the actor's current delivery type of the given casting source is the required type."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

		NumericConditionComponent* deliveryTypeComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		bool GetDeliveryType(RE::Actor* a_actor, RE::MagicSystem::CastingSource a_source, RE::MagicSystem::Delivery& a_outDeliveryType) const;
		std::string_view GetDeliveryTypeName(RE::MagicSystem::Delivery a_deliveryType) const;

		static const std::map<int32_t, std::string_view>& GetDeliveryTypeEnumMap();
	};

	class IsQuestStageDoneCondition : public ConditionBase
	{
	public:
		IsQuestStageDoneCondition()
		{
			questComponent = AddComponent<FormConditionComponent>("Quest form", "The quest to check.", RE::FormType::Quest);
			stageIndexComponent = AddComponent<NumericConditionComponent>("Stage index", "The quest stage index that should be completed for the given quest.");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "IsQuestStageDone"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the specified stage in the given quest has been completed."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

		FormConditionComponent* questComponent;
		NumericConditionComponent* stageIndexComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class CurrentWeatherHasFlagCondition : public ConditionBase
	{
	public:
		CurrentWeatherHasFlagCondition()
		{
			weatherFlagComponent = AddComponent<NumericConditionComponent>("Flag", "The current weather must have this flag enabled.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentWeatherHasFlag"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the current weather has the specified flag enabled."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

		NumericConditionComponent* weatherFlagComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		std::string_view GetFlagName(int32_t a_index) const;

		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class InventoryCountHasKeywordCondition : public ConditionBase
	{
	public:
		InventoryCountHasKeywordCondition()
		{
			keywordComponent = AddComponent<KeywordConditionComponent>("Keyword");
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value", "The value to compare the inventory count with.");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "InventoryCountHasKeyword"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the actor's current inventory count of all items with a specified keyword against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

		KeywordConditionComponent* keywordComponent;
		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		int GetItemCount(RE::TESObjectREFR* a_refr) const;
	};

	class CurrentTargetRelativeAngleCondition : public TargetConditionBase
	{
	public:
		CurrentTargetRelativeAngleCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
			degreesComponent = AddComponent<BoolConditionComponent>("Degrees", "Whether the angle values are in degrees or radians.");
			absoluteComponent = AddComponent<BoolConditionComponent>("Absolute", "Whether the angle values are absolute. Disable to be able to differentiate between clockwise and counterclockwise. Enable if you don't care about that, it should make the comparison easier to do in one condition.");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentTargetRelativeAngle"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the relative angle between an actor and their current target."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;
		BoolConditionComponent* degreesComponent;
		BoolConditionComponent* absoluteComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		RE::TESObjectREFRPtr GetTarget(RE::TESObjectREFR* a_refr) const;
		float GetRelativeAngle(RE::TESObjectREFR* a_refr) const;
	};

	class CurrentTargetLineOfSightCondition : public TargetConditionBase
	{
	public:
		CurrentTargetLineOfSightCondition()
		{
			boolComponent = AddComponent<BoolConditionComponent>("Evaluate target", "Enable to check the target's line of sight to the actor. Disable to check the actor's line of sight to the target.");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentTargetLineOfSight"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the actor's current target is in their line of sight, or if the actor is in their current target's line of sight."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

		BoolConditionComponent* boolComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		RE::TESObjectREFRPtr GetTarget(RE::TESObjectREFR* a_refr) const;
	};

	class CurrentRotationSpeedCondition : public ConditionBase
	{
	public:
		CurrentRotationSpeedCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CurrentRotationSpeed"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the ref's current rotation speed against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsTalkingCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsTalking"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is talking either in monologue or in dialogue."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsGreetingPlayerCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsGreetingPlayer"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is greeting the player."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsInSceneCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsInScene"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is currently in a scene."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsInSpecifiedSceneCondition : public ConditionBase
	{
	public:
		IsInSpecifiedSceneCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Scene", "The scene to check.", RE::FormType::Scene);
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "IsInSpecifiedScene"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is currently in the specified scene."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsScenePlayingCondition : public ConditionBase
	{
	public:
		IsScenePlayingCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Scene", "The scene to check.", RE::FormType::Scene);
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "IsScenePlaying"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if a specific scene is currently playing."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

		FormConditionComponent* formComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsDoingFavorCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsDoingFavor"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has been asked to do something by the player."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 2, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class AttackStateCondition : public ConditionBase
	{
	public:
		AttackStateCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			attackStateComponent = AddComponent<NumericConditionComponent>("Attack state", "The attack state value used in the comparison.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "AttackState"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks the actor's attack state."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 3, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* attackStateComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		RE::ATTACK_STATE_ENUM GetAttackState(RE::TESObjectREFR* a_refr) const;

		std::string_view GetAttackStateName(RE::ATTACK_STATE_ENUM a_attackState) const;
		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class IsMenuOpenCondition : public ConditionBase
	{
	public:
		IsMenuOpenCondition()
		{
			textComponent = AddComponent<TextConditionComponent>("Menu name", "The menu name that should be open.\n\nKnown names:\nConsole\nConsole Native UI Menu\nContainerMenu\nCrafting Menu\nCreation Club Menu\nCredits Menu\nCursor Menu\nDialogue Menu\nFader Menu\nFavoritesMenu\nGiftMenu\nHUD Menu\nInventoryMenu\nJournal Menu\nKinect Menu\nLevelUp Menu\nLoading Menu\nLoadWaitSpinner\nLockpicking Menu\nMagicMenu\nMain Menu\nMapMenu\nMessageBoxMenu\nMist Menu\nMod Manager Menu\nRaceSex Menu\nSafeZoneMenu\nSleep/Wait Menu\nStatsMenu\nTitleSequence Menu\nTraining Menu\nTutorial Menu\nTweenMenu");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return textComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "IsMenuOpen"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if a specific menu is currently open."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 3, 0 }; }

		TextConditionComponent* textComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsAnimationPlayingCondition : public ConditionBase
	{
	public:
		IsAnimationPlayingCondition();

		[[nodiscard]] RE::BSString GetArgument() const override { return animationComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "IsAnimationPlaying"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref's behavior graph is playing the animation file, blending in or out included, and with the box ticked, played as that OAR replacer."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		AnimationConditionComponent* animationComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class HasMorphCondition : public ConditionBase
	{
	public:
		HasMorphCondition();

		[[nodiscard]] RE::BSString GetArgument() const override { return textComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "HasMorph"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if a morph of this name is on the actor's loaded 3D: a BodySlide slider on its body or an expression or chargen morph on its head, whichever body or head it wears. False while the 3D is not loaded."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		TextConditionComponent* textComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	// IsBodyType and IsHeadType: one class, the part says which
	class SkinTypeCondition : public ConditionBase
	{
	public:
		explicit SkinTypeCondition(Scan::SkinPart a_part);

		[[nodiscard]] RE::BSString GetArgument() const override { return textComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return _part == Scan::SkinPart::kHead ? "IsHeadType"sv.data() : "IsBodyType"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override;
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		TextConditionComponent* textComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		Scan::SkinPart _part;
	};

	// IsBodyUVLayoutType and IsHeadUVLayoutType: the layout the actor's textures are painted for, as Scanned Data's Body types resolves it
	class UVLayoutCondition : public ConditionBase
	{
	public:
		explicit UVLayoutCondition(Scan::SkinPart a_part);

		[[nodiscard]] RE::BSString GetArgument() const override { return textComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return _part == Scan::SkinPart::kHead ? "IsHeadUVLayoutType"sv.data() : "IsBodyUVLayoutType"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override;
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 0, 0 }; }

		TextConditionComponent* textComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		Scan::SkinPart _part;
	};

	class TARGETCondition : public TargetConditionBase
	{
	public:
		TARGETCondition()
		{
			conditionsComponent = AddComponent<MultiConditionComponent>("Conditions");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return conditionsComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "TARGET"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if all of the child conditions are true, but evaluates them for the current target instead."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 3, 0 }; }

		[[nodiscard]] bool IsValid() const override { return conditionsComponent->IsValid(); }

		[[nodiscard]] RE::TESObjectREFR* GetRefrToEvaluate(RE::TESObjectREFR* a_refr) const override;

		MultiConditionComponent* conditionsComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		RE::TESObjectREFRPtr GetTarget(RE::TESObjectREFR* a_refr) const;
	};

	class PLAYERCondition : public ConditionBase
	{
	public:
		PLAYERCondition()
		{
			conditionsComponent = AddComponent<MultiConditionComponent>("Conditions");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return conditionsComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "PLAYER"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if all of the child conditions are true, but evaluates them for the player instead."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 1, 3, 0 }; }

		[[nodiscard]] bool IsValid() const override { return conditionsComponent->IsValid(); }

		[[nodiscard]] RE::TESObjectREFR* GetRefrToEvaluate(RE::TESObjectREFR* a_refr) const override;

		MultiConditionComponent* conditionsComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class LightLevelCondition : public ConditionBase
	{
	public:
		LightLevelCondition(ComparisonOperator a_comparisonOperator = ComparisonOperator::kEqual)
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");

			comparisonComponent->comparisonOperator = a_comparisonOperator;
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "LightLevel"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the current strength of lighting on this ref against the specified value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 0, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class LocationHasKeywordCondition : public ConditionBase
	{
	public:
		LocationHasKeywordCondition()
		{
			keywordComponent = AddComponent<KeywordConditionComponent>("Keyword");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return keywordComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "LocationHasKeyword"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the current location has the specified keyword."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 0, 0 }; }

		KeywordConditionComponent* keywordComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class LifeStateCondition : public ConditionBase
	{
	public:
		LifeStateCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			lifeStateComponent = AddComponent<NumericConditionComponent>("Life state", "The actor life state value used in the comparison.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "LifeState"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks the actor's life state."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 1, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* lifeStateComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		RE::ACTOR_LIFE_STATE GetLifeState(RE::TESObjectREFR* a_refr) const;

		std::string_view GetLifeStateName(RE::ACTOR_LIFE_STATE a_lifeState) const;
		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class SitSleepStateCondition : public ConditionBase
	{
	public:
		SitSleepStateCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			sitSleepStateComponent = AddComponent<NumericConditionComponent>("Sit/Sleep state", "The actor sit/sleep state value used in the comparison.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "SitSleepState"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks the actor's sit/sleep state."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 1, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* sitSleepStateComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		RE::SIT_SLEEP_STATE GetSitSleepState(RE::TESObjectREFR* a_refr) const;

		std::string_view GetSitSleepStateName(RE::SIT_SLEEP_STATE a_sitSleepState) const;
		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class XORCondition : public ConditionBase
	{
	public:
		XORCondition()
		{
			conditionsComponent = AddComponent<MultiConditionComponent>("Conditions");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return conditionsComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "XOR"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if only one of the child conditions is true (Exclusive OR)."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 1, 0 }; }

		[[nodiscard]] bool IsValid() const override { return conditionsComponent->IsValid(); }

		MultiConditionComponent* conditionsComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class PRESETCondition : public ConditionBase
	{
	public:
		PRESETCondition()
		{
			conditionsComponent = AddComponent<ConditionPresetComponent>("Preset");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return conditionsComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "PRESET"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Evaluate a condition preset defined in the replacer mod in place of this condition. Useful if you want to reuse the same set of conditions in multiple submods.\n\nManage condition presets in the replacer mod and don't forget to save the config!"sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 2, 0 }; }

		[[nodiscard]] bool IsValid() const override { return conditionsComponent->IsValid(); }

		[[nodiscard]] ConditionType GetConditionType() const override { return ConditionType::kPreset; }

		ConditionPresetComponent* conditionsComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsAttackTypeKeywordCondition : public ConditionBase
	{
	public:
		IsAttackTypeKeywordCondition()
		{
			keywordComponent = AddComponent<KeywordConditionComponent>("Keyword");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return keywordComponent->GetArgument(); }

		[[nodiscard]] RE::BSString GetName() const override { return "IsAttackTypeKeyword"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the performed attack type is equal to the specified keyword."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 2, 0 }; }

		KeywordConditionComponent* keywordComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		RE::BGSKeyword* GetAttackType(RE::TESObjectREFR* a_refr) const;
	};

	class IsAttackTypeFlagCondition : public ConditionBase
	{
	public:
		IsAttackTypeFlagCondition()
		{
			attackFlagComponent = AddComponent<NumericConditionComponent>("Flag", "The performed attack must have this flag enabled.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "IsAttackTypeFlag"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the performed attack has the specified flag enabled."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 2, 0 }; }

		NumericConditionComponent* attackFlagComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		std::string_view GetFlagName(int32_t a_index) const;

		static const std::map<int32_t, std::string_view>& GetEnumMap();

		RE::NiPointer<RE::BGSAttackData> GetAttackData(RE::TESObjectREFR* a_refr) const;
	};

	class LocationClearedCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "LocationCleared"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the current location is cleared."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 2, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsSummonedCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsSummoned"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is a summoned creature."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 2, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsEquippedHasEnchantmentCondition : public ConditionBase
	{
	public:
		IsEquippedHasEnchantmentCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Enchantment", "The enchantment to check.", RE::FormType::Enchantment);
			leftHandComponent = AddComponent<BoolConditionComponent>("Left hand", "Enable to check left hand. Disable to check right hand.");
			chargedComponent = AddComponent<BoolConditionComponent>("Charged", "Enable to check if the enchantment has enough charge.");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "IsEquippedHasEnchantment"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has an item equipped in the right or left hand that has the specified enchantment."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 2, 0 }; }

		FormConditionComponent* formComponent;
		BoolConditionComponent* leftHandComponent;
		BoolConditionComponent* chargedComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsEquippedHasEnchantmentWithKeywordCondition : public ConditionBase
	{
	public:
		IsEquippedHasEnchantmentWithKeywordCondition()
		{
			keywordComponent = AddComponent<KeywordConditionComponent>("Keyword");
			leftHandComponent = AddComponent<BoolConditionComponent>("Left hand", "Enable to check left hand. Disable to check right hand.");
			chargedComponent = AddComponent<BoolConditionComponent>("Charged", "Enable to check if the enchantment has enough charge.");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "IsEquippedHasEnchantmentWithKeyword"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has an item equipped in the right or left hand that has an enchantment with the specified keyword."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 2, 0 }; }

		KeywordConditionComponent* keywordComponent;
		BoolConditionComponent* leftHandComponent;
		BoolConditionComponent* chargedComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsOverEncumberedCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsOverEncumbered"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is over-encumbered."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 3, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsTrespassingCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsTrespassing"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is trespassing."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 3, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsGuardCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsGuard"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is a guard."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 3, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsCrimeSearchingCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsCrimeSearching"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is searching for a criminal."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 3, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsCombatSearchingCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsCombatSearching"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is searching for a target in combat."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 3, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IdleTimeCondition : public ConditionBase
	{
	public:
		class IdleTimeConditionStateData : public IStateData
		{
		public:
			void Initialize(RE::TESObjectREFR* a_refr)
			{
				_refHandle = a_refr;
			}

			bool Update(float a_deltaTime) override;

			float GetIdleTime() const { return _idleTime; }

		protected:
			RE::ObjectRefHandle _refHandle;
			float _idleTime = 0.f;
		};

		IdleTimeCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
			stateComponent = AddComponent<ConditionStateComponent>("State");
			stateComponent->SetStateDataScope(StateDataScope::kReference);
			stateComponent->SetAllowedDataScopes(StateDataScope::kReference);
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "IdleTime"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Compares the time the actor has spent idling with a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 3, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;
		ConditionStateComponent* stateComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsAboveWaterCondition : public ConditionBase
	{
	public:
		IsAboveWaterCondition()
		{
			distanceNumericComponent = AddComponent<NumericConditionComponent>("Distance", "Minimum distance from water surface.");
			depthNumericComponent = AddComponent<NumericConditionComponent>("Depth", "Minimum depth of the water.");
		}

		[[nodiscard]] RE::BSString GetName() const override { return "IsAboveWater"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the actor is above water - as in, if it fell straight down, it'd hit water."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

		NumericConditionComponent* distanceNumericComponent;
		NumericConditionComponent* depthNumericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class MagicEffectElapsedTimeCondition : public ConditionBase
	{
	public:
		MagicEffectElapsedTimeCondition()
		{
			formComponent = AddComponent<FormConditionComponent>("Magic effect", "The magic effect to check.", RE::FormType::MagicEffect);
			boolComponent = AddComponent<BoolConditionComponent>("Active effects only", "Enable to check active magic effects only.");
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "MagicEffectElapsedTime"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Compares the time the ref has been affected by the specified magic effect with a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

		FormConditionComponent* formComponent;
		BoolConditionComponent* boolComponent;
		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsWornInSlotCondition : public ConditionBase
	{
	public:
		IsWornInSlotCondition()
		{
			slotComponent = AddComponent<NumericConditionComponent>("Slot", "The slot that should be checked.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "IsWornInSlot"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has an item worn in the specified slot."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

		NumericConditionComponent* slotComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class InventoryWeightCondition : public ConditionBase
	{
	public:
		InventoryWeightCondition()
		{
			boolComponent = AddComponent<BoolConditionComponent>("Percentage", "Enable to check the inventory weight as a percentage of the actor's carry capacity instead of the absolute weight.");
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "InventoryWeight"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the actor's total inventory weight, or the current encumbrance percentage, against a numeric value."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

		BoolConditionComponent* boolComponent;
		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
		float GetInventoryWeight(RE::TESObjectREFR* a_refr) const;
	};

	class IsGhostCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsGhost"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is in the ghost (invulnerable) state. Not related to in-game ghosts."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsSwimmingCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsSwimming"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the actor is swimming."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class IsStaggeredCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsStaggered"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the actor is staggered."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 1, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class CastingSpellCondition : public ConditionBase
	{
	public:
		CastingSpellCondition()
		{
			castingSourceComponent = AddComponent<NumericConditionComponent>("Casting source", "The source of the spell being cast.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "CastingSpell"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the actor is currently casting a spell with the specified casting source."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 1, 0 }; }

		NumericConditionComponent* castingSourceComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class HasBoundWeaponEquippedCondition : public ConditionBase
	{
	public:
		HasBoundWeaponEquippedCondition()
		{
			boolComponent = AddComponent<BoolConditionComponent>("Left hand", "Enable to check left hand. Disable to check right hand.");
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "HasBoundWeaponEquipped"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref has a bound weapon equipped in the right or left hand."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 2, 0 }; }

		BoolConditionComponent* boolComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	void RegisterConditions();
	class IsOnStairsCondition : public ConditionBase
	{
	public:
		[[nodiscard]] RE::BSString GetName() const override { return "IsOnStairs"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the ref is on stairs. Keep in mind that not all stairs in the game are marked as such."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 3, 0 }; }

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

	class SurfaceMaterialCondition : public ConditionBase
	{
	public:
		SurfaceMaterialCondition()
		{
			numericComponent = AddComponent<NumericConditionComponent>("Material ID", "The surface has to have this material ID.");
		}

		void PostInitialize() override;

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "SurfaceMaterial"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Checks if the surface the ref is standing on has a specified material ID."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 3, 0 }; }

		NumericConditionComponent* numericComponent;

	protected:
		bool GetSurfaceMaterialID(RE::TESObjectREFR* a_refr, RE::MATERIAL_ID& a_outMaterialID) const;
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;

		RE::MATERIAL_ID GetRequiredMaterialID() const;

		static const std::vector<RE::MATERIAL_ID>& GetMaterialIDs();
		static const std::map<int32_t, std::string_view>& GetEnumMap();
	};

	class MovementSurfaceAngleCondition : public ConditionBase
	{
	public:
		class MovementSurfaceAngleConditionStateData : public IStateData
		{
		public:
			void Initialize(RE::TESObjectREFR* a_refr, float a_smoothingFactor, bool a_bUseNavmesh)
			{
				_refHandle = a_refr;
				_smoothingFactor = pow(a_smoothingFactor, 0.1f);
				_bUseNavmesh = a_bUseNavmesh;
				_bHasValue = Utils::GetSurfaceNormal(a_refr, _smoothedNormal, a_bUseNavmesh);
			}

			bool Update(float a_deltaTime) override;

			[[nodiscard]] bool GetSmoothedSurfaceNormal(RE::hkVector4& a_outVector) const;

		protected:
			RE::ObjectRefHandle _refHandle;

			RE::hkVector4 _smoothedNormal;
			float _smoothingFactor = 1.f;
			bool _bUseNavmesh = true;

			bool _bHasValue = false;
		};

		MovementSurfaceAngleCondition()
		{
			comparisonComponent = AddComponent<ComparisonConditionComponent>("Comparison");
			numericComponent = AddComponent<NumericConditionComponent>("Numeric value");
			degreesComponent = AddComponent<BoolConditionComponent>("Degrees", "Whether the angle values are in degrees or radians.");
			useNavmeshComponent = AddComponent<BoolConditionComponent>("Use Navmesh", "Enable to try to access the navmesh first to calculate the surface's normal vector, if possible. Will be more inaccurate, but less volatile.");
			useNavmeshComponent->SetBoolValue(true);
			stateComponent = AddComponent<ConditionStateComponent>("State");
			stateComponent->SetStateDataScope(StateDataScope::kSubMod);
			stateComponent->SetAllowedDataScopes(StateDataScope::kSubMod);
			smoothingFactorComponent = AddComponent<NumericConditionComponent>("Smoothing factor", "The smoothing factor controls how much the previous value influences the new value.\n\nA factor closer to 0 means the smoothing will respond more slowly to changes, while a factor closer to 1 means it will respond more quickly. 1 is effectively instant.");
			smoothingFactorComponent->SetStaticRange(0.f, 1.f);
			smoothingFactorComponent->SetStaticValue(1.f);
		}

		[[nodiscard]] RE::BSString GetArgument() const override;

		[[nodiscard]] RE::BSString GetName() const override { return "MovementSurfaceAngle"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Tests the angle of the surface that the ref is walking on against a numeric value.\nThe angle is calculated by comparing the surface's normal vector and the ref's forward vector."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 2, 3, 0 }; }

		ComparisonConditionComponent* comparisonComponent;
		NumericConditionComponent* numericComponent;
		BoolConditionComponent* degreesComponent;
		BoolConditionComponent* useNavmeshComponent;
		ConditionStateComponent* stateComponent;
		NumericConditionComponent* smoothingFactorComponent;

	protected:
		[[nodiscard]] Program::Expr Compile(Program::Builder& a_builder) const override;
	};

}
