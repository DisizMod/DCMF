#pragma once

#include "BaseFunctions.h"

namespace Functions
{
	void RegisterFunctions();

	[[nodiscard]] std::unique_ptr<IFunction> CreateFunction(std::string_view a_functionName);

	// Used by the copied BaseFunctions.cpp when it deserializes a nested set.
	[[nodiscard]] std::unique_ptr<IFunction> CreateFunctionFromJson(rapidjson::Value& a_value, FunctionSet* a_parentFunctionSet = nullptr);
	[[nodiscard]] std::unique_ptr<IFunction> DuplicateFunction(const std::unique_ptr<IFunction>& a_function);
	[[nodiscard]] std::unique_ptr<FunctionSet> DuplicateFunctionSet(FunctionSet* a_functionSetToDuplicate);

	// ---- structure -------------------------------------------------------

	class CONDITIONFunction : public FunctionBase
	{
	public:
		CONDITIONFunction()
		{
			conditionComponent = AddComponent<ConditionFunctionComponent>("Conditions");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return conditionComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "CONDITION"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Run a set of functions only if the specified conditions evaluate to true."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

		ConditionFunctionComponent* conditionComponent;

	protected:
		bool RunImpl(RE::TESObjectREFR* a_refr, void* a_parentSubMod, Trigger* a_trigger) const override;
	};

	class RANDOMFunctionComponent : public MultiFunctionComponent
	{
	public:
		RANDOMFunctionComponent(const IFunction* a_parentFunction, const char* a_name, const char* a_description = "") :
			MultiFunctionComponent(a_parentFunction, a_name, a_description)
		{
			std::function<void()> fun = std::bind(&RANDOMFunctionComponent::UpdateWeightCache, this);
			functionSet->AddOnDirtyCallback(fun);
		}

		void InitializeComponent(void* a_value) override;
		void SerializeComponent(void* a_value, void* a_allocator) override;

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override;

		bool Run(RE::TESObjectREFR* a_refr, void* a_parentSubMod, void* a_trigger) const override;

		void UpdateWeightCache();

		std::vector<float> weights;

	protected:
		void UpdateWeightCacheImpl();

		mutable SharedLock _lock;
		std::vector<IFunction*> _functions;
		std::vector<float> _cumulativeWeights;
	};

	class RANDOMFunction : public FunctionBase
	{
	public:
		RANDOMFunction()
		{
			functionsComponent = AddComponent<RANDOMFunctionComponent>("Functions");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return functionsComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "RANDOM"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Runs one of the child functions, picked at random by weight."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

		RANDOMFunctionComponent* functionsComponent;

	protected:
		bool RunImpl(RE::TESObjectREFR* a_refr, void* a_parentSubMod, Trigger* a_trigger) const override;
	};

	class ONEFunction : public FunctionBase
	{
	public:
		ONEFunction()
		{
			functionsComponent = AddComponent<MultiFunctionComponent>("Functions");
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return functionsComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "ONE"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Runs the first child function that succeeds."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

		MultiFunctionComponent* functionsComponent;

	protected:
		bool RunImpl(RE::TESObjectREFR* a_refr, void* a_parentSubMod, Trigger* a_trigger) const override;
	};

	// ---- leaves ----------------------------------------------------------

	class PlaySoundFunction : public FunctionBase
	{
	public:
		PlaySoundFunction()
		{
			formComponent = AddComponent<FormFunctionComponent>("Sound FormID", ""sv, RE::FormType::SoundRecord);
		}

		[[nodiscard]] RE::BSString GetArgument() const override { return formComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "PlaySound"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Plays a sound at the ref's location."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

		FormFunctionComponent* formComponent;

	protected:
		bool RunImpl(RE::TESObjectREFR* a_refr, void* a_parentSubMod, Trigger* a_trigger) const override;
	};

	class PlayDialogueFunction : public FunctionBase
	{
	public:
		PlayDialogueFunction();

		[[nodiscard]] RE::BSString GetArgument() const override { return topicComponent->GetArgument(); }
		[[nodiscard]] RE::BSString GetName() const override { return "PlayDialogue"sv.data(); }
		[[nodiscard]] RE::BSString GetDescription() const override { return "Has the ref say a dialogue topic, as Papyrus's Say does: the first of its lines whose conditions hold for the ref, voiced, lip synced and subtitled. Nothing while the ref is already talking."sv.data(); }
		[[nodiscard]] constexpr REL::Version GetRequiredVersion() const override { return { 3, 0, 0 }; }

		FormFunctionComponent* topicComponent;
		BoolFunctionComponent* inHeadComponent;

	protected:
		bool RunImpl(RE::TESObjectREFR* a_refr, void* a_parentSubMod, Trigger* a_trigger) const override;
	};
}
