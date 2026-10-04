#pragma once

#include "StateData.h"

namespace Conditions
{
	class ICondition;
	class ConditionSet;
}

namespace Functions
{
	class IFunction;
	class IFunctionComponent;

	struct Trigger;

	class FunctionSet;

	using FunctionFactory = IFunction* (*)();
	using FunctionComponentFactory = IFunctionComponent* (*)(const IFunction* a_parentFunction, const char* a_name, const char* a_description);

	enum class FunctionComponentType : uint8_t
	{
		kMulti,
		kForm,
		kNumeric,
		kNiPoint3,
		kKeyword,
		kText,
		kBool,
		kCondition,

		kCustom
	};

	enum class EssentialState : uint8_t
	{
		kEssential,
		kNonEssential_True,
		kNonEssential_False
	};

	struct Trigger
	{
		RE::BSString event{};
		RE::BSString payload{};

		Trigger() = default;

		Trigger(const RE::BSString& a_event, const RE::BSString& a_payload) :
			event(a_event), payload(a_payload)
		{}

		bool operator<(const Trigger& a_other) const
		{
			if (std::string_view(event.data()) == std::string_view(a_other.event.data())) {
				return std::string_view(payload.data()) < std::string_view(a_other.payload.data());
			}
			return std::string_view(event.data()) < std::string_view(a_other.event.data());
		}
	};

	// The parent class of all functions
	class IFunction
	{
	public:
		virtual ~IFunction() = default;

		virtual bool Run(RE::TESObjectREFR* a_refr, void* a_parentSubMod, Trigger* a_trigger = nullptr) const = 0;

		virtual void Initialize(void* a_value) = 0;                                                                // rapidjson::Value* a_value
		virtual void Serialize(void* a_value, void* a_allocator) = 0;  // rapidjson::Value* a_value, rapidjson::Document::AllocatorType* a_allocator

		virtual void PreInitialize() {}
		virtual void PostInitialize() {}

		[[nodiscard]] virtual RE::BSString GetArgument() const = 0;
		[[nodiscard]] virtual RE::BSString GetName() const = 0;
		[[nodiscard]] virtual RE::BSString GetDescription() const = 0;
		[[nodiscard]] virtual REL::Version GetRequiredVersion() const = 0;
		[[nodiscard]] virtual RE::BSString GetRequiredPluginName() const = 0;
		[[nodiscard]] virtual RE::BSString GetRequiredPluginAuthor() const = 0;

		virtual bool DisplayInUI([[maybe_unused]] bool a_bEditable, [[maybe_unused]] float a_firstColumnWidthPercent) { return false; }

		[[nodiscard]] virtual bool IsDisabled() const = 0;
		virtual void SetDisabled(bool a_bDisabled) = 0;
		[[nodiscard]] virtual EssentialState GetEssential() const = 0;
		virtual void SetEssential(EssentialState a_essentialState) = 0;
		[[nodiscard]] virtual bool IsValid() const { return true; }

		[[nodiscard]] virtual uint32_t GetNumComponents() const = 0;
		[[nodiscard]] virtual IFunctionComponent* GetComponent(uint32_t a_index) const = 0;
		virtual IFunctionComponent* AddComponent(FunctionComponentFactory a_factory, const char* a_name, const char* a_description = "") = 0;

		[[nodiscard]] virtual bool IsDeprecated() const { return false; }
		[[nodiscard]] virtual RE::TESObjectREFR* GetRefrToEvaluate(RE::TESObjectREFR* a_refr) const { return a_refr; }

		[[nodiscard]] virtual bool HasTrigger(const RE::BSString& a_trigger, const RE::BSString& a_payload = nullptr) const = 0;
		virtual void AddTrigger(const RE::BSString& a_trigger, const RE::BSString& a_payload = nullptr) = 0;
		virtual void RemoveTrigger(const RE::BSString& a_trigger, const RE::BSString& a_payload = nullptr) = 0;
		virtual RE::BSVisit::BSVisitControl ForEachTrigger(const std::function<RE::BSVisit::BSVisitControl(const Trigger&)>& a_func) const = 0;

		[[nodiscard]] FunctionSet* GetParentSet() const { return _parentSet; }
		void SetParentSet(FunctionSet* a_set) { _parentSet = a_set; }

	protected:
		virtual bool RunImpl(RE::TESObjectREFR* a_refr, void* a_subMod, Trigger* a_trigger = nullptr) const = 0;

		FunctionSet* _parentSet = nullptr;

	public:
		[[nodiscard]] virtual RE::BSString GetComment() const = 0;
		virtual void SetComment(const char* a_comment) = 0;
	};

	// A function's configurable values: a form, a number, a keyword and the like
	class IFunctionComponent
	{
	public:
		IFunctionComponent(const IFunction* a_parentFunction, const char* a_name, const char* a_description = "") :
			_parentFunction(a_parentFunction),
			_name(a_name),
			_description(a_description) {}

		virtual ~IFunctionComponent() = default;

		virtual void InitializeComponent(void* a_value) = 0;                    // rapidjson::Value* a_value
		virtual void SerializeComponent(void* a_value, void* a_allocator) = 0;  // rapidjson::Value* a_value, rapidjson::Document::AllocatorType* a_allocator

		virtual void PostInitialize() {}

		virtual bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) = 0;

		[[nodiscard]] virtual FunctionComponentType GetType() const = 0;
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

		[[nodiscard]] const IFunction* GetParentFunction() const { return _parentFunction; }

	protected:
		const IFunction* _parentFunction = nullptr;

		const std::string _name;
		const std::string _description;
	};

	class IMultiFunctionComponent : public IFunctionComponent
	{
	public:
		inline static constexpr auto FUNCTION_COMPONENT_TYPE = FunctionComponentType::kMulti;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A list of child functions."sv;

		IMultiFunctionComponent(const IFunction* a_parentFunction, const char* a_name, const char* a_description = "") :
			IFunctionComponent(a_parentFunction, a_name, a_description) {}

		[[nodiscard]] FunctionComponentType GetType() const override { return FUNCTION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual FunctionSet* GetFunctions() const = 0;
		[[nodiscard]] virtual bool Run(RE::TESObjectREFR* a_refr, void* a_parentSubMod, void* a_trigger) const = 0;
		virtual RE::BSVisit::BSVisitControl ForEachFunction(const std::function<RE::BSVisit::BSVisitControl(std::unique_ptr<IFunction>&)>& a_func) const = 0;
	};

	class IFormFunctionComponent : public IFunctionComponent
	{
	public:
		inline static constexpr auto FUNCTION_COMPONENT_TYPE = FunctionComponentType::kForm;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A reference to a form."sv;

		IFormFunctionComponent(const IFunction* a_parentFunction, const char* a_name, const char* a_description = "") :
			IFunctionComponent(a_parentFunction, a_name, a_description) {}

		[[nodiscard]] FunctionComponentType GetType() const override { return FUNCTION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual RE::TESForm* GetTESFormValue() const = 0;
		virtual void SetTESFormValue(RE::TESForm* a_form) = 0;
	};

	class INumericFunctionComponent : public IFunctionComponent
	{
	public:
		inline static constexpr auto FUNCTION_COMPONENT_TYPE = FunctionComponentType::kNumeric;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A numeric value - a static value, a reference to a global variable, an Actor Value, or a behavior graph variable."sv;

		INumericFunctionComponent(const IFunction* a_parentFunction, const char* a_name, const char* a_description = "") :
			IFunctionComponent(a_parentFunction, a_name, a_description) {}

		[[nodiscard]] FunctionComponentType GetType() const override { return FUNCTION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual float GetNumericValue(RE::TESObjectREFR* a_refr) const = 0;
		virtual void SetStaticValue(float a_value) = 0;
		virtual void SetGlobalVariable(RE::TESGlobal* a_global) = 0;
		virtual void SetActorValue(RE::ActorValue a_actorValue, Components::ActorValueType a_valueType) = 0;
		[[nodiscard]] virtual Components::ActorValueType GetActorValueType() const = 0;
		[[nodiscard]] virtual RE::ActorValue GetActorValue() const = 0;
	};

	class INiPoint3FunctionComponent : public IFunctionComponent
	{
	public:
		inline static constexpr auto FUNCTION_COMPONENT_TYPE = FunctionComponentType::kNiPoint3;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A user defined static NiPoint3 value (x, y, z vector)."sv;

		INiPoint3FunctionComponent(const IFunction* a_parentFunction, const char* a_name, const char* a_description = "") :
			IFunctionComponent(a_parentFunction, a_name, a_description) {}

		[[nodiscard]] FunctionComponentType GetType() const override { return FUNCTION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual const RE::NiPoint3& GetNiPoint3Value() const = 0;
		virtual void SetNiPoint3Value(const RE::NiPoint3& a_point) = 0;
	};

	class IKeywordFunctionComponent : public IFunctionComponent
	{
	public:
		inline static constexpr auto FUNCTION_COMPONENT_TYPE = FunctionComponentType::kKeyword;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A keyword value - a reference to a BGSKeyword form, or a literal EditorID of the keyword."sv;

		IKeywordFunctionComponent(const IFunction* a_parentFunction, const char* a_name, const char* a_description = "") :
			IFunctionComponent(a_parentFunction, a_name, a_description) {}

		[[nodiscard]] FunctionComponentType GetType() const override { return FUNCTION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual bool HasKeyword(const RE::BGSKeywordForm* a_form) const = 0;
		virtual void SetKeyword(RE::BGSKeyword* a_keyword) = 0;
		virtual void SetLiteral(const char* a_literal) = 0;
	};

	class ITextFunctionComponent : public IFunctionComponent
	{
	public:
		inline static constexpr auto FUNCTION_COMPONENT_TYPE = FunctionComponentType::kText;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A text value."sv;

		ITextFunctionComponent(const IFunction* a_parentFunction, const char* a_name, const char* a_description = "") :
			IFunctionComponent(a_parentFunction, a_name, a_description) {}

		[[nodiscard]] FunctionComponentType GetType() const override { return FUNCTION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual RE::BSString GetTextValue() const = 0;
		virtual void SetTextValue(const char* a_text) = 0;
		virtual void SetAllowSpaces(bool a_bAllowSpaces) = 0;
	};

	class IBoolFunctionComponent : public IFunctionComponent
	{
	public:
		inline static constexpr auto FUNCTION_COMPONENT_TYPE = FunctionComponentType::kBool;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A boolean value."sv;

		IBoolFunctionComponent(const IFunction* a_parentFunction, const char* a_name, const char* a_description = "") :
			IFunctionComponent(a_parentFunction, a_name, a_description) {}

		[[nodiscard]] FunctionComponentType GetType() const override { return FUNCTION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual bool GetBoolValue() const = 0;
		virtual void SetBoolValue(bool a_value) = 0;
	};

	class IConditionFunctionComponent : public IFunctionComponent
	{
	public:
		inline static constexpr auto FUNCTION_COMPONENT_TYPE = FunctionComponentType::kCondition;
		inline static constexpr auto DEFAULT_DESCRIPTION = "A set of functions that will only run if the given condition evaluates to true."sv;

		IConditionFunctionComponent(const IFunction* a_parentFunction, const char* a_name, const char* a_description = "") :
			IFunctionComponent(a_parentFunction, a_name, a_description) {}

		[[nodiscard]] FunctionComponentType GetType() const override { return FUNCTION_COMPONENT_TYPE; }
		[[nodiscard]] RE::BSString GetDefaultDescription() const override { return DEFAULT_DESCRIPTION; }

		[[nodiscard]] virtual Conditions::ConditionSet* GetConditions() const = 0;
		[[nodiscard]] virtual FunctionSet* GetFunctions() const = 0;
		virtual bool TryRun(RE::TESObjectREFR* a_refr, void* a_parentSubMod, Trigger* a_trigger = nullptr) const = 0;
		virtual RE::BSVisit::BSVisitControl ForEachCondition(const std::function<RE::BSVisit::BSVisitControl(std::unique_ptr<Conditions::ICondition>&)>& a_func) const = 0;
		virtual RE::BSVisit::BSVisitControl ForEachFunction(const std::function<RE::BSVisit::BSVisitControl(std::unique_ptr<IFunction>&)>& a_func) const = 0;
	};

	class ICustomFunctionComponent : public IFunctionComponent
	{
	public:
		inline static constexpr auto FUNCTION_COMPONENT_TYPE = FunctionComponentType::kCustom;

		ICustomFunctionComponent(const IFunction* a_parentFunction, const char* a_name, const char* a_description = "") :
			IFunctionComponent(a_parentFunction, a_name, a_description) {}

		[[nodiscard]] FunctionComponentType GetType() const override { return FUNCTION_COMPONENT_TYPE; }
	};
}
