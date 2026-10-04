#pragma once

#include "StateData.h"

namespace Conditions
{
	class ICondition;
	class IConditionComponent;

	using ConditionFactory = ICondition* (*)();
	using ConditionComponentFactory = IConditionComponent* (*)(const ICondition* a_parentCondition, const char* a_name, const char* a_description);

	enum class ConditionComponentType : uint8_t
	{
		kMulti,
		kForm,
		kNumeric,
		kNiPoint3,
		kColour,
		kKeyword,
		kText,
		kBool,
		kComparison,
		kState,

		kCustom,

		kPreset
	};

	enum class ConditionType : uint8_t
	{
		kNormal,
		kPreset
	};

	enum class ComparisonOperator : uint8_t
	{
		kEqual,
		kNotEqual,
		kGreater,
		kGreaterEqual,
		kLess,
		kLessEqual,

		kInvalid
	};

	enum class StateDataScope : int
	{
		kNone = 0,
		kActor = 1 << 0,
		kSubMod = 1 << 1,
		kRegisteredMod = 1 << 2,
		kReference = 1 << 3
	};
	inline StateDataScope operator|(StateDataScope a, StateDataScope b) { return static_cast<StateDataScope>(static_cast<int>(a) | static_cast<int>(b)); }
	inline StateDataScope operator&(StateDataScope a, StateDataScope b) { return static_cast<StateDataScope>(static_cast<int>(a) & static_cast<int>(b)); }
	inline StateDataScope operator^(StateDataScope a, StateDataScope b) { return static_cast<StateDataScope>(static_cast<int>(a) ^ static_cast<int>(b)); }
	inline StateDataScope operator~(StateDataScope a) { return static_cast<StateDataScope>(~static_cast<int>(a)); }
	inline StateDataScope& operator|=(StateDataScope& a, StateDataScope b) { return a = a | b; }
	inline StateDataScope& operator&=(StateDataScope& a, StateDataScope b) { return a = a & b; }
	inline StateDataScope& operator^=(StateDataScope& a, StateDataScope b) { return a = a ^ b; }

	enum class EssentialState : uint8_t
	{
		kEssential,
		kNonEssential_True,
		kNonEssential_False
	};

	// The parent class of all conditions
	class ICondition
	{
	public:
		virtual ~ICondition() = default;

		virtual void Initialize(void* a_value) = 0;  // rapidjson::Value* a_value
		virtual void InitializeLegacy([[maybe_unused]] const char* a_argument) {}
		virtual void Serialize(void* a_value, void* a_allocator) = 0;  // rapidjson::Value* a_value, rapidjson::Document::AllocatorType* a_allocator

		virtual void PreInitialize() {}
		virtual void PostInitialize() {}

		[[nodiscard]] virtual RE::BSString GetArgument() const = 0;
		[[nodiscard]] virtual RE::BSString GetCurrent(RE::TESObjectREFR* a_refr) const = 0;

		[[nodiscard]] virtual RE::BSString GetName() const = 0;
		[[nodiscard]] virtual RE::BSString GetDescription() const = 0;
		[[nodiscard]] virtual REL::Version GetRequiredVersion() const = 0;
		[[nodiscard]] virtual RE::BSString GetRequiredPluginName() const = 0;
		[[nodiscard]] virtual RE::BSString GetRequiredPluginAuthor() const = 0;

		[[nodiscard]] virtual bool IsDisabled() const = 0;
		virtual void SetDisabled(bool a_bDisabled) = 0;
		[[nodiscard]] virtual bool IsNegated() const = 0;
		virtual void SetNegated(bool a_bNegated) = 0;
		[[nodiscard]] virtual bool IsValid() const { return true; }

		[[nodiscard]] virtual uint32_t GetNumComponents() const = 0;
		[[nodiscard]] virtual IConditionComponent* GetComponent(uint32_t a_index) const = 0;
		virtual IConditionComponent* AddComponent(ConditionComponentFactory a_factory, const char* a_name, const char* a_description = "") = 0;

		[[nodiscard]] virtual bool IsDeprecated() const { return false; }
		[[nodiscard]] virtual RE::TESObjectREFR* GetRefrToEvaluate(RE::TESObjectREFR* a_refr) const { return a_refr; }

		[[nodiscard]] class ConditionSet* GetParentConditionSet() const { return _parentConditionSet; }
		void SetParentConditionSet(ConditionSet* a_conditionSet) { _parentConditionSet = a_conditionSet; }

		[[nodiscard]] virtual ConditionType GetConditionType() const = 0;
		[[nodiscard]] virtual EssentialState GetEssential() const = 0;
		virtual void SetEssential(EssentialState a_essentialState) = 0;

		[[nodiscard]] virtual RE::BSString GetComment() const = 0;
		virtual void SetComment(const char* a_comment) = 0;

	protected:
		ConditionSet* _parentConditionSet = nullptr;
	};

	// A condition's configurable values: a form, a number, a keyword and the like
	class IConditionComponent
	{
	public:
		IConditionComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			_parentCondition(a_parentCondition),
			_name(a_name),
			_description(a_description) {}

		virtual ~IConditionComponent() = default;

		virtual void InitializeComponent(void* a_value) = 0;                    // rapidjson::Value* a_value
		virtual void SerializeComponent(void* a_value, void* a_allocator) = 0;  // rapidjson::Value* a_value, rapidjson::Document::AllocatorType* a_allocator

		virtual void PostInitialize() {}

		virtual bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) = 0;

		[[nodiscard]] virtual ConditionComponentType GetType() const = 0;
		[[nodiscard]] virtual RE::BSString GetArgument() const = 0;
		[[nodiscard]] virtual RE::BSString GetName() const { return _name.data(); }

		[[nodiscard]] virtual RE::BSString GetDescription() const
		{
			if (_description.empty()) {
				return GetDefaultDescription();
			}
			return _description.data();
		}

		[[nodiscard]] virtual RE::BSString GetDefaultDescription() const = 0;
		[[nodiscard]] virtual bool IsValid() const = 0;

		[[nodiscard]] const ICondition* GetParentCondition() const { return _parentCondition; }

	protected:
		const ICondition* _parentCondition = nullptr;

		const std::string _name;
		const std::string _description;
	};

	class IMultiConditionComponent : public IConditionComponent
	{
	public:
		inline static constexpr auto CONDITION_COMPONENT_TYPE = ConditionComponentType::kMulti;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A list of child conditions."sv;

		IMultiConditionComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			IConditionComponent(a_parentCondition, a_name, a_description) {}

		[[nodiscard]] ConditionComponentType GetType() const override { return CONDITION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual ConditionSet* GetConditions() const = 0;
		virtual RE::BSVisit::BSVisitControl ForEachCondition(const std::function<RE::BSVisit::BSVisitControl(std::unique_ptr<ICondition>&)>& a_func) const = 0;

		[[nodiscard]] virtual bool GetShouldDrawEvaluateResultForChildConditions() const = 0;
		virtual void SetShouldDrawEvaluateResultForChildConditions(bool a_bShouldDraw) = 0;
	};

	class IFormConditionComponent : public IConditionComponent
	{
	public:
		inline static constexpr auto CONDITION_COMPONENT_TYPE = ConditionComponentType::kForm;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A reference to a form."sv;

		IFormConditionComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			IConditionComponent(a_parentCondition, a_name, a_description) {}

		[[nodiscard]] ConditionComponentType GetType() const override { return CONDITION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual RE::TESForm* GetTESFormValue() const = 0;
		virtual void SetTESFormValue(RE::TESForm* a_form) = 0;
	};

	class INumericConditionComponent : public IConditionComponent
	{
	public:
		inline static constexpr auto CONDITION_COMPONENT_TYPE = ConditionComponentType::kNumeric;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A numeric value - a static value, a reference to a global variable, an Actor Value, or a behavior graph variable."sv;

		INumericConditionComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			IConditionComponent(a_parentCondition, a_name, a_description) {}

		[[nodiscard]] ConditionComponentType GetType() const override { return CONDITION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual float GetNumericValue(RE::TESObjectREFR* a_refr) const = 0;
		virtual void SetStaticValue(float a_value) = 0;
		virtual void SetGlobalVariable(RE::TESGlobal* a_global) = 0;
		virtual void SetActorValue(RE::ActorValue a_actorValue, Components::ActorValueType a_valueType) = 0;
	};

	class INiPoint3ConditionComponent : public IConditionComponent
	{
	public:
		inline static constexpr auto CONDITION_COMPONENT_TYPE = ConditionComponentType::kNiPoint3;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A user defined static NiPoint3 value (x, y, z vector)."sv;

		INiPoint3ConditionComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			IConditionComponent(a_parentCondition, a_name, a_description) {}

		[[nodiscard]] ConditionComponentType GetType() const override { return CONDITION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual const RE::NiPoint3& GetNiPoint3Value() const = 0;
		virtual void SetNiPoint3Value(const RE::NiPoint3& a_point) = 0;
	};

	class IColourConditionComponent : public IConditionComponent
	{
	public:
		inline static constexpr auto CONDITION_COMPONENT_TYPE = ConditionComponentType::kColour;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A user defined colour (red, green, blue), each component 0-1."sv;

		IColourConditionComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			IConditionComponent(a_parentCondition, a_name, a_description) {}

		[[nodiscard]] ConditionComponentType GetType() const override { return CONDITION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual const RE::NiColor& GetColourValue() const = 0;
		virtual void SetColourValue(const RE::NiColor& a_colour) = 0;
	};

	class IKeywordConditionComponent : public IConditionComponent
	{
	public:
		inline static constexpr auto CONDITION_COMPONENT_TYPE = ConditionComponentType::kKeyword;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A keyword value - a reference to a BGSKeyword form, or a literal EditorID of the keyword."sv;

		IKeywordConditionComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			IConditionComponent(a_parentCondition, a_name, a_description) {}

		[[nodiscard]] ConditionComponentType GetType() const override { return CONDITION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual bool HasKeyword(const RE::BGSKeywordForm* a_form) const = 0;
		virtual void SetKeyword(RE::BGSKeyword* a_keyword) = 0;
		virtual void SetLiteral(const char* a_literal) = 0;
	};

	class ITextConditionComponent : public IConditionComponent
	{
	public:
		inline static constexpr auto CONDITION_COMPONENT_TYPE = ConditionComponentType::kText;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A text value."sv;

		ITextConditionComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			IConditionComponent(a_parentCondition, a_name, a_description) {}

		[[nodiscard]] ConditionComponentType GetType() const override { return CONDITION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual RE::BSString GetTextValue() const = 0;
		virtual void SetTextValue(const char* a_text) = 0;
		virtual void SetAllowSpaces(bool a_bAllowSpaces) = 0;
	};

	class IBoolConditionComponent : public IConditionComponent
	{
	public:
		inline static constexpr auto CONDITION_COMPONENT_TYPE = ConditionComponentType::kBool;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A boolean value."sv;

		IBoolConditionComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			IConditionComponent(a_parentCondition, a_name, a_description) {}

		[[nodiscard]] ConditionComponentType GetType() const override { return CONDITION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual bool GetBoolValue() const = 0;
		virtual void SetBoolValue(bool a_value) = 0;
	};

	class IComparisonConditionComponent : public IConditionComponent
	{
	public:
		inline static constexpr auto CONDITION_COMPONENT_TYPE = ConditionComponentType::kComparison;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A comparison operator."sv;

		IComparisonConditionComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			IConditionComponent(a_parentCondition, a_name, a_description) {}

		[[nodiscard]] ConditionComponentType GetType() const override { return CONDITION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual bool GetComparisonResult(float a_valueA, float a_valueB) const = 0;
		[[nodiscard]] virtual ComparisonOperator GetComparisonOperator() const = 0;
		virtual void SetComparisonOperator(ComparisonOperator a_operator) = 0;

		[[nodiscard]] virtual RE::BSString GetComparisonOperatorFullName() const = 0;
	};

	class IConditionStateComponent : public IConditionComponent
	{
	public:
		inline static constexpr auto CONDITION_COMPONENT_TYPE = ConditionComponentType::kState;
		inline static constexpr auto DEFAULT_DESCRIPTION = "This condition stores state data. In some cases you can select whether that data is unique to a single animation clip, shared between animation clips in the same clip or modifier, or shared with other instances of the same condition class across a specified scope."sv;

		IConditionStateComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			IConditionComponent(a_parentCondition, a_name, a_description) {}

		[[nodiscard]] ConditionComponentType GetType() const override { return CONDITION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual StateDataScope GetAllowedDataScopes() const = 0;
		virtual void SetAllowedDataScopes(StateDataScope a_scopes) = 0;
		[[nodiscard]] virtual StateDataScope GetStateDataScope() const = 0;
		virtual void SetStateDataScope(StateDataScope a_scope) = 0;

		// only applicable for local and clip or modifier scopes
		[[nodiscard]] virtual bool CanResetOnLoopOrEcho() const = 0;
		virtual void SetCanResetOnLoopOrEcho(bool a_bCanResetOnLoopOrEcho) = 0;
		[[nodiscard]] virtual bool ShouldResetOnLoopOrEcho() const = 0;
		virtual void SetShouldResetOnLoopOrEcho(bool a_bShouldReset) = 0;
	};

	class ICustomConditionComponent : public IConditionComponent
	{
	public:
		inline static constexpr auto CONDITION_COMPONENT_TYPE = ConditionComponentType::kCustom;

		ICustomConditionComponent(const ICondition* a_parentCondition, const char* a_name, const char* a_description = "") :
			IConditionComponent(a_parentCondition, a_name, a_description) {}

		[[nodiscard]] ConditionComponentType GetType() const override { return CONDITION_COMPONENT_TYPE; }
	};
}
