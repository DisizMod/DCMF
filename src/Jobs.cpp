#include "Jobs.h"

#include "Channels.h"

#include "Conditions.h"
#include "DetectedProblems.h"
#include "Functions.h"
#include "ModRegistry.h"
#include "RegisteredMods.h"


namespace Jobs
{
	void InsertConditionJob::Run()
	{
		conditionSet->Insert(conditionToInsert, insertAfterThisCondition, true);
	}

	void InsertFunctionJob::Run()
	{
		functionSet->Insert(functionToInsert, insertAfterThisFunction, true);
	}

	void RemoveChannelJob::Run()
	{
		channelSet->Remove(channelToRemove);
	}

	void InsertChannelJob::Run()
	{
		channelSet->Insert(channelToInsert, insertAfterThisChannel, true);
	}

	void ReplaceChannelJob::Run()
	{
		if (auto newChannel = Channels::CreateChannel(std::string_view(newChannelName))) {
			channelSet->Replace(channelToReplace, newChannel);
		}
	}

	void MoveChannelJob::Run()
	{
		targetSet->Move(sourceChannel, sourceSet, targetChannel, bInsertAfter);
	}

	void ClearChannelSetJob::Run()
	{
		channelSet->Clear();
	}

	void RemoveConditionJob::Run()
	{
		conditionSet->Remove(conditionToRemove);
	}

	void RemoveFunctionJob::Run()
	{
		functionSet->Remove(functionToRemove);
	}

	void ReplaceConditionJob::Run()
	{
		if (auto newCondition = Conditions::CreateCondition(newConditionName)) {
			conditionSet->Replace(conditionToReplace, newCondition);
		}
	}

	void ReplaceFunctionJob::Run()
	{
		if (auto newFunction = Functions::CreateFunction(newFunctionName)) {
			functionSet->Replace(functionToReplace, newFunction);
		}
	}

	void MoveConditionJob::Run()
	{
		targetSet->Move(sourceCondition, sourceSet, targetCondition, bInsertAfter);
	}

	void MoveFunctionJob::Run()
	{
		targetSet->Move(sourceFunction, sourceSet, targetFunction, bInsertAfter);
	}

	void AddTriggerJob::Run()
	{
		if (func) {
			func->AddTrigger(trigger.event, trigger.payload);
			if (functionSet) {
				functionSet->SetDirty(true);
			}
		}
	}

	void RemoveTriggerJob::Run()
	{
		if (func) {
			func->RemoveTrigger(trigger.event, trigger.payload);
			if (functionSet) {
				functionSet->SetDirty(true);
			}
		}
	}

	void ClearConditionSetJob::Run()
	{
		conditionSet->Clear();
	}

	void ClearFunctionSetJob::Run()
	{
		functionSet->Clear();
	}

	void UpdateSubModJob::Run()
	{
		if (subMod) {
			subMod->SetDirty(true);

			if (bCheckProblems) {
				auto& detectedProblems = DetectedProblems::GetSingleton();
				detectedProblems.CheckForSubModsSharingPriority();
				detectedProblems.CheckForSubModsWithInvalidEntries();
			}

			if (const auto parentMod = subMod->GetParentMod()) {
				parentMod->SortSubMods();
			}
		}
	}

	void ReloadSubModConfigJob::Run()
	{
		if (subMod) {
			subMod->ReloadConfig();
		}
	}

	void ReloadRegisteredModConfigJob::Run()
	{
		if (registeredMod) {
			registeredMod->ReloadConfig();
		}
	}

	void RemoveOverlaySlotJob::Run()
	{
		if (registeredMod) {
			registeredMod->RemoveOverlaySlot(slotName);
			registeredMod->SetDirty(true);
		}
	}

	void RemoveTriggerRowJob::Run()
	{
		triggerSet->Remove(trigger);
	}

	void ClearTriggerSetJob::Run()
	{
		triggerSet->Clear();
	}

	void RemoveTriggerPresetJob::Run()
	{
		if (registeredMod) {
			registeredMod->RemoveTriggerPreset(presetName);
			registeredMod->SetDirty(true);
		}
	}

	void RemoveSubModJob::Run()
	{
		if (registeredMod && subMod) {
			registeredMod->RemoveSubMod(subMod);
			registeredMod->SetDirty(true);
		}
	}

	void RemoveKeyframeJob::Run()
	{
		if (clip && variant && keyframe) {
			clip->RemoveKeyframe(*variant, keyframe);
			clip->SetDirty(true);
		}
	}

	void RemoveClipVariantJob::Run()
	{
		if (clip) {
			clip->RemoveVariant(index);
			clip->SetDirty(true);
		}
	}

	void RemoveSkinSetJob::Run()
	{
		if (registeredMod) {
			registeredMod->RemoveSkinSet(setName);
			registeredMod->SetDirty(true);
		}
	}

	void RemoveConditionPresetJob::Run()
	{
		if (registeredMod) {
			registeredMod->RemoveConditionPreset(conditionPresetName);
			registeredMod->SetDirty(true);
		}
	}

	void NotifyAnimationGraphJob::Run()
	{
		if (refr) {
			refr->NotifyAnimationGraph(eventName.c_str());
		}
	}
}
