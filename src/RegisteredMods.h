#pragma once

#include "BaseConditions.h"
#include "BaseFunctions.h"
#include "Channels.h"
#include "OverlaySlots.h"
#include "SkinSets.h"
#include "Triggers.h"
#include "Variants.h"
#include "Parsing.h"
#include "Settings.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class RegisteredMod;

enum class EditMode : int
{
	kNone = 0,
	kUser = 1,
	kAuthor = 2
};

enum class SubModKind : uint8_t
{
	kModifier,
	kClip
};

// A list of items for one reference channel, each with its weight: a random pick naming it draws from its items
class ReferencePool
{
public:
	explicit ReferencePool(std::string_view a_name) :
		_name(a_name) {}

	[[nodiscard]] std::string_view GetName() const { return _name; }
	void SetName(std::string_view a_name) { _name = a_name; }

	[[nodiscard]] std::string_view GetDescription() const { return _description; }
	void SetDescription(std::string_view a_description) { _description = a_description; }

	// The channel it is for, as a file names it: "headpart:hair", "body:preset"
	[[nodiscard]] std::string_view GetChannel() const { return _channel; }
	void SetChannel(std::string_view a_channel) { _channel = a_channel; }

	// Its entries, each the channel's own value; no pool within a pool
	Channels::PickList entries;

private:
	std::string _name;
	std::string _description;
	std::string _channel;
};

class SubMod
{
public:
	SubMod(RegisteredMod* a_parentMod) :
		_parentMod(a_parentMod)
	{
		_conditionSet = std::make_unique<Conditions::ConditionSet>(this);
		_channelSet = std::make_unique<Channels::ChannelSet>(this);
	}

	virtual ~SubMod() = default;

	[[nodiscard]] virtual SubModKind GetKind() const = 0;

	Conditions::ConditionSet* GetConditionSet() const { return _conditionSet.get(); }

	// What a modifier drives; a clip has one too, empty and never shown
	Channels::ChannelSet* GetChannelSet() const { return _channelSet.get(); }

	Functions::FunctionSet* GetFunctionSet(Functions::FunctionSetType a_setType) const;
	bool HasFunctionSet(Functions::FunctionSetType a_setType) const;
	bool HasValidFunctionSet(Functions::FunctionSetType a_setType) const;
	bool HasAnyFunctionSet() const;
	Functions::FunctionSet* CreateOrGetFunctionSet(Functions::FunctionSetType a_setType);

	std::string_view GetName() const { return _name; }
	void SetName(std::string_view a_name) { _name = a_name; }

	std::string_view GetDescription() const { return _description; }
	void SetDescription(std::string_view a_description) { _description = a_description; }

	int32_t GetPriority() const { return _priority; }
	void SetPriority(int32_t a_priority) { _priority = a_priority; }

	std::string_view GetPath() const { return _path; }
	Parsing::ConfigSource GetConfigSource() const { return _configSource; }

	bool IsDisabled() const { return _bDisabled; }
	void SetDisabled(bool a_bDisabled) { _bDisabled = a_bDisabled; }

	bool IsInterruptible() const { return _bInterruptible; }
	void SetInterruptible(bool a_bInterruptible) { _bInterruptible = a_bInterruptible; }

	bool IsReevaluatingOnEcho() const { return _bReplaceOnEcho; }
	void SetReevaluatingOnEcho(bool a_bReplaceOnEcho) { _bReplaceOnEcho = a_bReplaceOnEcho; }

	bool IsDirty() const;
	bool IsFromUserConfig() const { return _configSource == Parsing::ConfigSource::kUser; }
	// An edit marks the owner to save, and has the program compiled again on the next tick
	void SetDirty(bool a_bDirty);

	void SetDirtyRecursive(bool a_bDirty);

	bool HasInvalidConditions() const;
	bool HasInvalidFunctions() const;

	bool ReloadConfig();
	void SaveConfig(EditMode a_editMode, bool a_bResetDirty = true);

	// Its part of the mod's file: what every clip and modifier has, then its kind's own. Parsing replaces what it has
	[[nodiscard]] virtual rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const;
	virtual void Parse(rapidjson::Value& a_value);

	RegisteredMod* GetParentMod() const;

	void SetPath(std::string_view a_path) { _path = a_path; }

private:
	friend class RegisteredMod;
	RegisteredMod* _parentMod = nullptr;

	std::string _name;
	std::string _description;
	int32_t _priority = 0;
	std::string _path;
	Parsing::ConfigSource _configSource = Parsing::ConfigSource::kAuthor;
	bool _bDisabled = false;
	bool _bInterruptible = false;
	bool _bReplaceOnEcho = false;

	std::unique_ptr<Conditions::ConditionSet> _conditionSet;
	std::unique_ptr<Channels::ChannelSet> _channelSet;
	std::unique_ptr<Functions::FunctionSet> _functionSetOnActivate = nullptr;
	std::unique_ptr<Functions::FunctionSet> _functionSetOnDeactivate = nullptr;
	std::unique_ptr<Functions::FunctionSet> _functionSetOnTrigger = nullptr;
	bool _bDirty = false;
};


class AppearanceModifier : public SubMod
{
public:
	using SubMod::SubMod;

	[[nodiscard]] SubModKind GetKind() const override { return SubModKind::kModifier; }

	[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override;
	void Parse(rapidjson::Value& a_value) override;

	[[nodiscard]] bool HasStartTransition() const { return _bStartTransition; }
	void SetStartTransition(bool a_bEnable) { _bStartTransition = a_bEnable; }

	[[nodiscard]] float GetStartTransitionTime() const { return _startTransitionTime; }
	void SetStartTransitionTime(float a_time) { _startTransitionTime = a_time; }
	[[nodiscard]] bool HasCustomStartTransitionTime() const { return _bCustomStartTransitionTime; }
	void SetCustomStartTransitionTime(bool a_bCustom) { _bCustomStartTransitionTime = a_bCustom; }

	// The way out, the same shape as the way in.
	[[nodiscard]] bool HasEndTransition() const { return _bEndTransition; }
	void SetEndTransition(bool a_bEnable) { _bEndTransition = a_bEnable; }

	[[nodiscard]] float GetEndTransitionTime() const { return _endTransitionTime; }
	void SetEndTransitionTime(float a_time) { _endTransitionTime = a_time; }
	[[nodiscard]] bool HasCustomEndTransitionTime() const { return _bCustomEndTransitionTime; }
	void SetCustomEndTransitionTime(bool a_bCustom) { _bCustomEndTransitionTime = a_bCustom; }

private:
	bool _bStartTransition = false;
	float _startTransitionTime = 0.3f;
	bool _bCustomStartTransitionTime = false;

	bool _bEndTransition = false;
	float _endTransitionTime = 0.3f;
	bool _bCustomEndTransitionTime = false;
};

// One moment of a clip: when, whether it holds there and for how long, one row per claimed channel with the value
// and the way into it, and what runs when the clock passes it
struct Keyframe
{
	float time = 0.f;
	bool bHold = false;
	float holdTime = 0.5f;
	std::unique_ptr<Channels::ChannelSet> channels;
	std::unique_ptr<Functions::FunctionSet> functions;

	[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const;
};

// One variant's keyframes; the variant's weight, order and play-once live in the clip's Variants by the same index
struct ClipVariant
{
	std::string name;
	std::vector<std::unique_ptr<Keyframe>> keyframes;
};

class Clip : public SubMod
{
public:
	Clip(RegisteredMod* a_parentMod);

	[[nodiscard]] SubModKind GetKind() const override { return SubModKind::kClip; }

	[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const override;
	void Parse(rapidjson::Value& a_value) override;

	Triggers::TriggerSet* GetTriggerSet() const { return _triggerSet.get(); }

	// Let go the moment its conditions stop holding, mid-run
	[[nodiscard]] bool StopsWhenConditionsFail() const { return _bStopWhenConditionsFail; }
	void SetStopWhenConditionsFail(bool a_bStop) { _bStopWhenConditionsFail = a_bStop; }

	// Interrupted, the channels leave through their exit transitions rather than handing over at once, over their own
	// times or over one the clip sets
	[[nodiscard]] bool AppliesExitOnInterrupt() const { return _bApplyExitOnInterrupt; }
	void SetApplyExitOnInterrupt(bool a_bApply) { _bApplyExitOnInterrupt = a_bApply; }
	[[nodiscard]] bool HasCustomExitTime() const { return _bCustomExitTime; }
	void SetCustomExitTime(bool a_bCustom) { _bCustomExitTime = a_bCustom; }
	[[nodiscard]] float GetCustomExitTime() const { return _customExitTime; }
	void SetCustomExitTime(float a_time) { _customExitTime = a_time; }

	[[nodiscard]] Variants& GetVariants() { return _variants; }
	[[nodiscard]] std::vector<ClipVariant>& GetTracks() { return _tracks; }
	ClipVariant& AddVariant(std::string_view a_name);
	void RemoveVariant(uint16_t a_index);

	// A frame at a time, its rows mirrored from the claimed channels, kept in time order
	Keyframe* AddKeyframe(ClipVariant& a_variant, float a_time);
	void RemoveKeyframe(ClipVariant& a_variant, Keyframe* a_keyframe);
	void SortKeyframes(ClipVariant& a_variant);
	Keyframe* AddKeyframeFromJson(ClipVariant& a_variant, const rapidjson::Value& a_value);

	// Every frame's rows made to match the claimed channels: one per claim in the claimed order, a new claim added
	// off its rest, a dropped claim removed, a row that stays keeping its value
	void RefitKeyframes();
	// One frame's rows put back to their rest
	void ResetKeyframe(Keyframe& a_keyframe);

	// A frame's row is set or not: off is not set, and only a set row is the frame's business
	// Whether the frame is the last of its variant to set the channel: the one whose exit transition shows
	[[nodiscard]] bool IsLastKeyframeFor(const ClipVariant& a_variant, const Keyframe& a_keyframe, Apply::ChannelId a_id) const;
	// The row that last set the channel before this frame, and the first to set it after, with their frames; what
	// the run has at this frame lies between them
	struct SetRow
	{
		const Keyframe* keyframe = nullptr;
		const Channels::Channel* row = nullptr;
	};
	[[nodiscard]] SetRow LastSetBefore(const ClipVariant& a_variant, const Keyframe& a_keyframe, Apply::ChannelId a_id) const;
	[[nodiscard]] SetRow FirstSetAfter(const ClipVariant& a_variant, const Keyframe& a_keyframe, Apply::ChannelId a_id) const;
	// The earlier frame whose hold covers this frame's time for the channel, or none: a held channel cannot be set here
	[[nodiscard]] const Keyframe* HeldBy(const ClipVariant& a_variant, const Keyframe& a_keyframe, Apply::ChannelId a_id) const;

	// The variant a frame belongs to; asked at call time, since the variants move when one is added or reordered
	[[nodiscard]] const ClipVariant* VariantOf(const Keyframe* a_keyframe) const;

private:
	void FitKeyframe(ClipVariant& a_variant, Keyframe& a_keyframe, const std::vector<Apply::ChannelId>& a_claimed);

	std::unique_ptr<Triggers::TriggerSet> _triggerSet;
	bool _bStopWhenConditionsFail = false;
	bool _bApplyExitOnInterrupt = false;
	bool _bCustomExitTime = false;
	float _customExitTime = 0.3f;
	Variants _variants;
	std::vector<ClipVariant> _tracks;
};

class RegisteredMod
{
public:
	RegisteredMod(std::string_view a_path, std::string_view a_name, std::string_view a_author,
		std::string_view a_description) :
		_name(a_name),
		_author(a_author),
		_description(a_description),
		_path(a_path)
	{}

	std::string_view GetName() const { return _name; }
	void SetName(std::string_view a_name);
	std::string_view GetAuthor() const { return _author; }
	void SetAuthor(std::string_view a_author) { _author = a_author; }
	std::string_view GetDescription() const { return _description; }
	void SetDescription(std::string_view a_description) { _description = a_description; }

	std::string_view GetPath() const { return _path; }

	Parsing::ConfigSource GetConfigSource() const { return _configSource; }

	// The mods this one builds on, by name: their presets and reference pools offered to its own, their overlay slots
	// listed in its definitions to add overlays to
	[[nodiscard]] const std::vector<std::string>& GetRequiredMods() const { return _requiredMods; }
	[[nodiscard]] std::vector<std::string>& GetRequiredMods() { return _requiredMods; }
	// The required mods that are registered, in the list's order
	void ForEachRequiredMod(const std::function<void(RegisteredMod*)>& a_func) const;
	// This mod's own of the name, else the first required mod's that has it
	[[nodiscard]] Conditions::ConditionPreset* FindConditionPreset(std::string_view a_name) const;
	[[nodiscard]] Triggers::TriggerPreset* FindTriggerPreset(std::string_view a_name) const;
	[[nodiscard]] ReferencePool* FindReferencePool(std::string_view a_name) const;
	// Every PRESET row of its clips and modifiers pointed at its preset again: after every mod is read, or after a
	// mod it requires was read again
	void ResolveConditionPresets();

	// Anything of it edited since it was read or saved: itself, a clip or modifier, a preset
	bool IsDirty() const;
	// An edit marks the owner to save, and has the program compiled again on the next tick
	void SetDirty(bool a_bDirty);

	// Its folder's file, read again: user.json when there is one, config.json otherwise. What it had is replaced in place,
	// so a clip or modifier still running is the same object; one the file no longer has is retired, not freed
	bool ReloadConfig();
	// One clip or modifier of it, read again from the file
	bool ReloadSubMod(SubMod* a_subMod);
	// The whole mod to config.json as its author, or to user.json beside it
	void SaveConfig(EditMode a_editMode);

	// Every mod folder under Data/SKSE/Plugins/DCMF, once the scan has named what the channels can drive; any thread,
	// the first call does it
	static void LoadAll();
	[[nodiscard]] static bool IsLoaded();

	void AddSubMod(std::unique_ptr<SubMod>& a_subMod);
	// Taken out of the mod, kept alive for whatever still holds it: a running clip, an active modifier, a preview
	void RemoveSubMod(SubMod* a_subMod);
	bool HasSubMod(std::string_view a_path) const;
	SubMod* GetSubMod(std::string_view a_path) const;
	RE::BSVisit::BSVisitControl ForEachSubMod(const std::function<RE::BSVisit::BSVisitControl(SubMod*)>& a_func) const;
	void SortSubMods();

	void AddConditionPreset(std::unique_ptr<Conditions::ConditionPreset>& a_conditionPreset);
	void RemoveConditionPreset(std::string_view a_name);
	bool HasConditionPresets() const;
	bool HasConditionPreset(std::string_view a_name) const;
	Conditions::ConditionPreset* GetConditionPreset(std::string_view a_name) const;
	RE::BSVisit::BSVisitControl ForEachConditionPreset(const std::function<RE::BSVisit::BSVisitControl(Conditions::ConditionPreset*)>& a_func) const;
	void SortConditionPresets();

	// The trigger presets this mod defines, by name
	void AddReferencePool(std::unique_ptr<ReferencePool>& a_pool);
	void RemoveReferencePool(std::string_view a_name);
	bool HasReferencePools() const;
	ReferencePool* GetReferencePool(std::string_view a_name) const;
	RE::BSVisit::BSVisitControl ForEachReferencePool(const std::function<RE::BSVisit::BSVisitControl(ReferencePool*)>& a_func) const;

	void AddTriggerPreset(std::unique_ptr<Triggers::TriggerPreset>& a_preset);
	void RemoveTriggerPreset(std::string_view a_name);
	bool HasTriggerPresets() const;
	bool HasTriggerPreset(std::string_view a_name) const;
	Triggers::TriggerPreset* GetTriggerPreset(std::string_view a_name) const;
	RE::BSVisit::BSVisitControl ForEachTriggerPreset(const std::function<RE::BSVisit::BSVisitControl(Triggers::TriggerPreset*)>& a_func) const;
	void SortTriggerPresets();

	// The overlay slots this mod defines, by name
	void AddOverlaySlot(std::unique_ptr<Overlays::Slot>& a_slot);
	void RemoveOverlaySlot(std::string_view a_name);
	void RenameOverlaySlot(Overlays::Slot* a_slot, std::string_view a_name);
	bool HasOverlaySlots() const;
	bool HasOverlaySlot(std::string_view a_name) const;
	RE::BSVisit::BSVisitControl ForEachOverlaySlot(const std::function<RE::BSVisit::BSVisitControl(Overlays::Slot*)>& a_func) const;
	void SortOverlaySlots();
	[[nodiscard]] Overlays::Slot* GetOverlaySlot(std::string_view a_name) const;

	// The skin sets this mod defines, by name
	void AddSkinSet(std::unique_ptr<Skins::Set>& a_set);
	void RemoveSkinSet(std::string_view a_name);
	bool HasSkinSets() const;
	bool HasSkinSet(std::string_view a_name) const;
	RE::BSVisit::BSVisitControl ForEachSkinSet(const std::function<RE::BSVisit::BSVisitControl(Skins::Set*)>& a_func) const;
	void SortSkinSets();

	bool HasInvalidConditions(bool a_bCheckPresetsOnly) const;
	bool HasInvalidFunctions() const;

private:
	[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const;
	void Parse(rapidjson::Value& a_value);
	// The file it reads: user.json when there is one, else config.json; none when neither is there
	[[nodiscard]] std::optional<std::filesystem::path> ConfigFile(Parsing::ConfigSource& a_outSource) const;

	std::string _name;
	std::string _author;
	std::string _description;
	std::string _path;
	std::vector<std::string> _requiredMods;
	Parsing::ConfigSource _configSource = Parsing::ConfigSource::kAuthor;

	mutable SharedLock _dataLock;
	std::vector<std::unique_ptr<SubMod>> _subMods;

	mutable SharedLock _presetsLock;
	std::vector<std::unique_ptr<Conditions::ConditionPreset>> _conditionPresets;
	std::vector<std::unique_ptr<Triggers::TriggerPreset>> _triggerPresets;
	std::vector<std::unique_ptr<ReferencePool>> _referencePools;
	std::vector<std::unique_ptr<Overlays::Slot>> _overlaySlots;
	std::vector<std::unique_ptr<Skins::Set>> _skinSets;

	bool _bDirty = false;
};
