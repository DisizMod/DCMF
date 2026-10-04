#pragma once

#include "FunctionTypes.h"

namespace Conditions
{
	class ICondition;
	class ConditionSet;
}

namespace Functions
{
	class IFunction;
	class FunctionSet;
}

namespace Channels
{
	class ChannelBase;
	class ChannelSet;
}

namespace Triggers
{
	class TriggerBase;
	class TriggerSet;
}

class SubMod;
class RegisteredMod;
class Clip;
struct ClipVariant;
struct Keyframe;

namespace Jobs
{
	struct GenericJob
	{
		GenericJob() = default;
		virtual ~GenericJob() = default;

		virtual void Run() = 0;
	};

	struct LatentJob
	{
		LatentJob() = delete;
		LatentJob(float a_delay) :
			_timeRemaining(a_delay)
		{}

		virtual ~LatentJob() = default;

		virtual bool Run(float a_deltaTime) = 0;

		float _timeRemaining;
	};

	struct InsertConditionJob : GenericJob
	{
		InsertConditionJob(std::unique_ptr<Conditions::ICondition>& a_conditionToInsert, Conditions::ConditionSet* a_conditionSet, const std::unique_ptr<Conditions::ICondition>& a_insertAfterThisCondition) :
			conditionToInsert(std::move(a_conditionToInsert)),
			conditionSet(a_conditionSet),
			insertAfterThisCondition(a_insertAfterThisCondition)
		{}

		std::unique_ptr<Conditions::ICondition> conditionToInsert;
		Conditions::ConditionSet* conditionSet;
		const std::unique_ptr<Conditions::ICondition>& insertAfterThisCondition;

		void Run() override;
	};

	struct InsertFunctionJob : GenericJob
	{
		InsertFunctionJob(std::unique_ptr<Functions::IFunction>& a_functionToInsert, Functions::FunctionSet* a_functionSet, const std::unique_ptr<Functions::IFunction>& a_insertAfterThisFunction) :
			functionToInsert(std::move(a_functionToInsert)),
			functionSet(a_functionSet),
			insertAfterThisFunction(a_insertAfterThisFunction)
		{}

		std::unique_ptr<Functions::IFunction> functionToInsert;
		Functions::FunctionSet* functionSet;
		const std::unique_ptr<Functions::IFunction>& insertAfterThisFunction;

		void Run() override;
	};

	struct InsertChannelJob : GenericJob
	{
		InsertChannelJob(std::unique_ptr<Channels::ChannelBase>& a_channelToInsert, Channels::ChannelSet* a_channelSet, const std::unique_ptr<Channels::ChannelBase>& a_insertAfterThisChannel) :
			channelToInsert(std::move(a_channelToInsert)),
			channelSet(a_channelSet),
			insertAfterThisChannel(a_insertAfterThisChannel)
		{}

		std::unique_ptr<Channels::ChannelBase> channelToInsert;
		Channels::ChannelSet* channelSet;
		const std::unique_ptr<Channels::ChannelBase>& insertAfterThisChannel;

		void Run() override;
	};

	struct RemoveChannelJob : GenericJob
	{
		RemoveChannelJob(std::unique_ptr<Channels::ChannelBase>& a_channelToRemove, Channels::ChannelSet* a_channelSet) :
			channelToRemove(a_channelToRemove),
			channelSet(a_channelSet)
		{}

		std::unique_ptr<Channels::ChannelBase>& channelToRemove;
		Channels::ChannelSet* channelSet;

		void Run() override;
	};

	struct ReplaceChannelJob : GenericJob
	{
		ReplaceChannelJob(std::unique_ptr<Channels::ChannelBase>& a_channelToReplace, std::string_view a_newChannelName, Channels::ChannelSet* a_channelSet) :
			channelToReplace(a_channelToReplace),
			newChannelName(a_newChannelName),
			channelSet(a_channelSet)
		{}

		std::unique_ptr<Channels::ChannelBase>& channelToReplace;
		std::string newChannelName;
		Channels::ChannelSet* channelSet;

		void Run() override;
	};

	struct MoveChannelJob : GenericJob
	{
		MoveChannelJob(std::unique_ptr<Channels::ChannelBase>& a_sourceChannel, Channels::ChannelSet* a_sourceSet, const std::unique_ptr<Channels::ChannelBase>& a_targetChannel, Channels::ChannelSet* a_targetSet, bool a_bInsertAfter) :
			sourceChannel(a_sourceChannel),
			sourceSet(a_sourceSet),
			targetChannel(a_targetChannel),
			targetSet(a_targetSet),
			bInsertAfter(a_bInsertAfter)
		{}

		std::unique_ptr<Channels::ChannelBase>& sourceChannel;
		Channels::ChannelSet* sourceSet;
		const std::unique_ptr<Channels::ChannelBase>& targetChannel;
		Channels::ChannelSet* targetSet;
		bool bInsertAfter;

		void Run() override;
	};

	struct ClearChannelSetJob : GenericJob
	{
		ClearChannelSetJob(Channels::ChannelSet* a_channelSet) :
			channelSet(a_channelSet)
		{}

		Channels::ChannelSet* channelSet;

		void Run() override;
	};

	struct RemoveConditionJob : GenericJob
	{
		RemoveConditionJob(std::unique_ptr<Conditions::ICondition>& a_conditionToRemove, Conditions::ConditionSet* a_conditionSet) :
			conditionToRemove(a_conditionToRemove),
			conditionSet(a_conditionSet)
		{}

		std::unique_ptr<Conditions::ICondition>& conditionToRemove;
		Conditions::ConditionSet* conditionSet;

		void Run() override;
	};

	struct RemoveFunctionJob : GenericJob
	{
		RemoveFunctionJob(std::unique_ptr<Functions::IFunction>& a_functionToRemove, Functions::FunctionSet* a_functionSet) :
			functionToRemove(a_functionToRemove),
			functionSet(a_functionSet)
		{}

		std::unique_ptr<Functions::IFunction>& functionToRemove;
		Functions::FunctionSet* functionSet;

		void Run() override;
	};

	struct ReplaceConditionJob : GenericJob
	{
		ReplaceConditionJob(std::unique_ptr<Conditions::ICondition>& a_conditionToReplace, std::string_view a_newConditionName, Conditions::ConditionSet* a_conditionSet) :
			conditionToReplace(a_conditionToReplace),
			newConditionName(a_newConditionName),
			conditionSet(a_conditionSet)
		{}

		std::unique_ptr<Conditions::ICondition>& conditionToReplace;
		std::string newConditionName;
		Conditions::ConditionSet* conditionSet;

		void Run() override;
	};

	struct ReplaceFunctionJob : GenericJob
	{
		ReplaceFunctionJob(std::unique_ptr<Functions::IFunction>& a_functionToReplace, std::string_view a_newFunctionName, Functions::FunctionSet* a_functionSet) :
			functionToReplace(a_functionToReplace),
			newFunctionName(a_newFunctionName),
			functionSet(a_functionSet)
		{}

		std::unique_ptr<Functions::IFunction>& functionToReplace;
		std::string newFunctionName;
		Functions::FunctionSet* functionSet;

		void Run() override;
	};

	struct MoveConditionJob : GenericJob
	{
		MoveConditionJob(std::unique_ptr<Conditions::ICondition>& a_sourceCondition, Conditions::ConditionSet* a_sourceSet, const std::unique_ptr<Conditions::ICondition>& a_targetCondition, Conditions::ConditionSet* a_targetSet, bool a_bInsertAfter) :
			sourceCondition(a_sourceCondition),
			sourceSet(a_sourceSet),
			targetCondition(a_targetCondition),
			targetSet(a_targetSet),
			bInsertAfter(a_bInsertAfter)
		{}

		std::unique_ptr<Conditions::ICondition>& sourceCondition;
		Conditions::ConditionSet* sourceSet;
		const std::unique_ptr<Conditions::ICondition>& targetCondition;
		Conditions::ConditionSet* targetSet;
		bool bInsertAfter;

		void Run() override;
	};

	struct MoveFunctionJob : GenericJob
	{
		MoveFunctionJob(std::unique_ptr<Functions::IFunction>& a_sourceFunction, Functions::FunctionSet* a_sourceSet, const std::unique_ptr<Functions::IFunction>& a_targetFunction, Functions::FunctionSet* a_targetSet, bool a_bInsertAfter) :
			sourceFunction(a_sourceFunction),
			sourceSet(a_sourceSet),
			targetFunction(a_targetFunction),
			targetSet(a_targetSet),
			bInsertAfter(a_bInsertAfter)
		{}

		std::unique_ptr<Functions::IFunction>& sourceFunction;
		Functions::FunctionSet* sourceSet;
		const std::unique_ptr<Functions::IFunction>& targetFunction;
		Functions::FunctionSet* targetSet;
		bool bInsertAfter;

		void Run() override;
	};

	struct AddTriggerJob : GenericJob
	{
		AddTriggerJob(std::unique_ptr<Functions::IFunction>& a_function, Functions::FunctionSet* a_functionSet, const Functions::Trigger& a_trigger) :
			func(a_function),
			functionSet(a_functionSet),
			trigger(a_trigger)
		{}

		std::unique_ptr<Functions::IFunction>& func;
		Functions::FunctionSet* functionSet;
		Functions::Trigger trigger;

		void Run() override;
	};

	struct RemoveTriggerJob : GenericJob
	{
		RemoveTriggerJob(std::unique_ptr<Functions::IFunction>& a_function, Functions::FunctionSet* a_functionSet, const Functions::Trigger& a_trigger) :
			func(a_function),
			functionSet(a_functionSet),
			trigger(a_trigger)
		{}

		std::unique_ptr<Functions::IFunction>& func;
		Functions::FunctionSet* functionSet;
		Functions::Trigger trigger;

		void Run() override;
	};

	struct ClearConditionSetJob : GenericJob
	{
		ClearConditionSetJob(Conditions::ConditionSet* a_conditionSet) :
			conditionSet(a_conditionSet)
		{}

		Conditions::ConditionSet* conditionSet;

		void Run() override;
	};

	struct ClearFunctionSetJob : GenericJob
	{
		ClearFunctionSetJob(Functions::FunctionSet* a_functionSet) :
			functionSet(a_functionSet)
		{}

		Functions::FunctionSet* functionSet;

		void Run() override;
	};

	struct UpdateSubModJob : GenericJob
	{
		UpdateSubModJob(SubMod* a_subMod, bool a_bCheckProblems) :
			subMod(a_subMod),
			bCheckProblems(a_bCheckProblems)
		{}

		SubMod* subMod;
		bool bCheckProblems;

		void Run() override;
	};

	struct ReloadSubModConfigJob : GenericJob
	{
		ReloadSubModConfigJob(SubMod* a_subMod) :
			subMod(a_subMod)
		{}

		SubMod* subMod;

		void Run() override;
	};

	struct ReloadRegisteredModConfigJob : GenericJob
	{
		ReloadRegisteredModConfigJob(RegisteredMod* a_registeredMod) :
			registeredMod(a_registeredMod)
		{}

		RegisteredMod* registeredMod;

		void Run() override;
	};

	struct RemoveOverlaySlotJob : GenericJob
	{
		RemoveOverlaySlotJob(RegisteredMod* a_registeredMod, std::string_view a_slotName) :
			registeredMod(a_registeredMod),
			slotName(a_slotName)
		{}

		RegisteredMod* registeredMod;
		std::string slotName;

		void Run() override;
	};

	struct RemoveTriggerRowJob : GenericJob
	{
		RemoveTriggerRowJob(std::unique_ptr<Triggers::TriggerBase>& a_trigger, Triggers::TriggerSet* a_triggerSet) :
			trigger(a_trigger),
			triggerSet(a_triggerSet)
		{}

		std::unique_ptr<Triggers::TriggerBase>& trigger;
		Triggers::TriggerSet* triggerSet;

		void Run() override;
	};

	struct ClearTriggerSetJob : GenericJob
	{
		ClearTriggerSetJob(Triggers::TriggerSet* a_triggerSet) :
			triggerSet(a_triggerSet)
		{}

		Triggers::TriggerSet* triggerSet;

		void Run() override;
	};

	struct RemoveTriggerPresetJob : GenericJob
	{
		RemoveTriggerPresetJob(RegisteredMod* a_registeredMod, std::string_view a_presetName) :
			registeredMod(a_registeredMod),
			presetName(a_presetName)
		{}

		RegisteredMod* registeredMod;
		std::string presetName;

		void Run() override;
	};

	struct RemoveSubModJob : GenericJob
	{
		RemoveSubModJob(RegisteredMod* a_registeredMod, SubMod* a_subMod) :
			registeredMod(a_registeredMod),
			subMod(a_subMod)
		{}

		RegisteredMod* registeredMod;
		SubMod* subMod;

		void Run() override;
	};

	struct RemoveKeyframeJob : GenericJob
	{
		RemoveKeyframeJob(Clip* a_clip, ClipVariant* a_variant, Keyframe* a_keyframe) :
			clip(a_clip),
			variant(a_variant),
			keyframe(a_keyframe)
		{}

		Clip* clip;
		ClipVariant* variant;
		Keyframe* keyframe;

		void Run() override;
	};

	struct RemoveClipVariantJob : GenericJob
	{
		RemoveClipVariantJob(Clip* a_clip, uint16_t a_index) :
			clip(a_clip),
			index(a_index)
		{}

		Clip* clip;
		uint16_t index;

		void Run() override;
	};

	struct RemoveSkinSetJob : GenericJob
	{
		RemoveSkinSetJob(RegisteredMod* a_registeredMod, std::string_view a_setName) :
			registeredMod(a_registeredMod),
			setName(a_setName)
		{}

		RegisteredMod* registeredMod;
		std::string setName;

		void Run() override;
	};

	struct RemoveConditionPresetJob : GenericJob
	{
		RemoveConditionPresetJob(RegisteredMod* a_registeredMod, std::string_view a_conditionPresetName) :
			registeredMod(a_registeredMod),
			conditionPresetName(a_conditionPresetName)
		{}

		RegisteredMod* registeredMod;
		std::string conditionPresetName;

		void Run() override;
	};

	struct NotifyAnimationGraphJob : GenericJob
	{
		NotifyAnimationGraphJob(RE::TESObjectREFR* a_refr, const RE::BSString& a_eventName) :
			refr(a_refr),
			eventName(a_eventName)
		{}

		RE::TESObjectREFR* refr;
		RE::BSString eventName;

		void Run() override;
	};
}
