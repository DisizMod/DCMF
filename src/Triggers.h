#pragma once

#include "BaseTriggers.h"
#include "Containers.h"

#include <functional>

class RegisteredMod;
#include <memory>
#include <string_view>
#include <format>
#include <vector>

class SubMod;

namespace UI::UICommon
{
	struct AsyncPickerGroup;
}

namespace Triggers
{
	class Trigger : public TriggerBase
	{
	protected:
		Trigger() = default;
	};

	class OnHitTrigger : public Trigger
	{
	public:
		[[nodiscard]] std::string_view GetTypeName() const override { return "OnHit"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "Fires when this actor is struck."sv; }
	};

	class OnDeathTrigger : public Trigger
	{
	public:
		[[nodiscard]] std::string_view GetTypeName() const override { return "OnDeath"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "Fires when this actor begins dying."sv; }
	};

	class TriggerPreset;

	// A trigger preset of the mod, by name; the list it picks from is the mod's, set when the trigger is drawn
	class TriggerPresetComponent : public ITriggerComponent
	{
	public:
		using ITriggerComponent::ITriggerComponent;

		[[nodiscard]] TriggerComponentType GetType() const override { return TriggerComponentType::kText; }

		void Parse(const rapidjson::Value& a_value) override;
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) override;

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override;

		[[nodiscard]] std::string GetArgument() const override { return presetName; }
		[[nodiscard]] bool IsValid() const override;

		[[nodiscard]] TriggerPreset* GetPreset() const;

		std::string presetName;
		RegisteredMod* mod = nullptr;
	};

	// Any trigger of a preset the mod defines, in place of this one
	class PresetTrigger : public Trigger
	{
	public:
		PresetTrigger();

		[[nodiscard]] std::string_view GetTypeName() const override { return "PRESET"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "Fires when any trigger of a trigger preset defined in the mod fires. Useful to reuse the same triggers in several clips.\n\nManage trigger presets in the mod's Presets section."sv; }

		TriggerPresetComponent* preset;
	};

	class OnDialogueLineTrigger : public Trigger
	{
	public:
		OnDialogueLineTrigger();

		[[nodiscard]] std::string_view GetTypeName() const override { return "OnDialogueLine"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "Fires when this actor begins speaking the topic info."sv; }

		FormTriggerComponent* topicInfo;
	};

	class OnAnimationStartTrigger : public Trigger
	{
	public:
		OnAnimationStartTrigger();

		[[nodiscard]] std::string_view GetTypeName() const override { return "OnAnimationStart"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "Fires when this actor's behavior graph starts playing the animation file, and again each time it restarts."sv; }

		AnimationTriggerComponent* animation;
	};

	class OnAnimationEndTrigger : public Trigger
	{
	public:
		OnAnimationEndTrigger();

		[[nodiscard]] std::string_view GetTypeName() const override { return "OnAnimationEnd"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "Fires when this actor's behavior graph stops playing the animation file, once it has blended out. A looping animation ends when the graph leaves it."sv; }

		AnimationTriggerComponent* animation;
	};

	class OnAnimationEventTrigger : public Trigger
	{
	public:
		OnAnimationEventTrigger();

		[[nodiscard]] std::string_view GetTypeName() const override { return "OnAnimationEvent"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "Fires when this actor's behavior graph sends the event: a notify in a clip, such as FootLeft or HitFrame, or one a state raises on entering. Not the events sent into the graph."sv; }

		TextTriggerComponent* event;
	};

	// A modifier of the clip's own mod, or of one of the mods the clip requires
	class ModifierTriggerComponent : public ITriggerComponent
	{
	public:
		using ITriggerComponent::ITriggerComponent;

		[[nodiscard]] TriggerComponentType GetType() const override { return TriggerComponentType::kText; }

		void Parse(const rapidjson::Value& a_value) override;
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) override;

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override;

		[[nodiscard]] std::string GetArgument() const override { return modName.empty() ? modifierName : std::format("{} / {}", modName, modifierName); }
		[[nodiscard]] bool IsValid() const override { return !modifierName.empty(); }

		// Whether the modifier is the one named, the clip's own mod standing in for an empty mod name
		[[nodiscard]] bool Names(const SubMod* a_modifier, const RegisteredMod* a_ownMod) const;

		std::string modName;  // empty for the clip's own mod
		std::string modifierName;
		RegisteredMod* mod = nullptr;       // the clip's mod, and the clip, set by the UI to list the choices
		const SubMod* clip = nullptr;
	};

	class OnModifierActivatedTrigger : public Trigger
	{
	public:
		OnModifierActivatedTrigger();

		[[nodiscard]] std::string_view GetTypeName() const override { return "OnModifierActivated"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "Fires in the very tick the modifier activates on this actor, as its start transition begins; also when its conditions hold again while it is on its way out."sv; }

		ModifierTriggerComponent* modifier;
	};

	class OnModifierDeactivatedTrigger : public Trigger
	{
	public:
		OnModifierDeactivatedTrigger();

		[[nodiscard]] std::string_view GetTypeName() const override { return "OnModifierDeactivated"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "Fires in the very tick the modifier deactivates on this actor, as its end transition begins."sv; }

		ModifierTriggerComponent* modifier;
	};

	// A number of seconds as any numeric value is, or a roll between two of them: fixed per actor, or rolled again each
	// time the clip activates
	class IntervalTriggerComponent : public ITriggerComponent
	{
	public:
		enum class Reroll : std::uint8_t
		{
			kPerActor,
			kOnActivation
		};

		using ITriggerComponent::ITriggerComponent;

		[[nodiscard]] TriggerComponentType GetType() const override { return TriggerComponentType::kNumeric; }

		void Parse(const rapidjson::Value& a_value) override;
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) override;

		bool DisplayInUI(bool a_bEditable, float a_firstColumnWidthPercent) override;

		[[nodiscard]] std::string GetArgument() const override;
		[[nodiscard]] bool IsValid() const override;

		// The seconds when not random; a random one is rolled by whoever keeps the roll
		[[nodiscard]] float GetSeconds(RE::TESObjectREFR* a_refr) const { return value.GetValue(a_refr); }
		[[nodiscard]] float RollSeconds(float a_unit) const { return minSeconds + (maxSeconds - minSeconds) * a_unit; }

		Components::NumericValue value;
		bool bRandom = false;
		float minSeconds = 10.f;
		float maxSeconds = 30.f;
		Reroll reroll = Reroll::kOnActivation;
	};

	// Every so many seconds while the clip's conditions hold, counted from when it last ended
	class IntervalTrigger : public Trigger
	{
	public:
		IntervalTrigger();

		[[nodiscard]] std::string_view GetTypeName() const override { return "Interval"sv; }
		[[nodiscard]] std::string_view GetDescription() const override { return "Fires every so many seconds while the clip's conditions hold. The time counts from when the clip last ended, so runs never overlap."sv; }

		IntervalTriggerComponent* interval;
	};

	using TriggerFactory = std::function<std::unique_ptr<Trigger>()>;

	// Registered once at startup, as the condition factories are.
	void RegisterTriggers();

	[[nodiscard]] std::unique_ptr<Trigger> CreateTrigger(std::string_view a_typeName);

	[[nodiscard]] const std::vector<std::string>& GetTriggerTypeNames();

	// A trigger from its JSON, by its "type"; null for a type nothing registered
	[[nodiscard]] std::unique_ptr<TriggerBase> CreateTriggerFromJson(const rapidjson::Value& a_value);

	// What fires a clip: any of them. Empty, the clip fires from its conditions alone
	class TriggerSet : public Set<TriggerBase, TriggerSet>
	{
	public:
		using Set::Set;

		void SetAsParentImpl(std::unique_ptr<TriggerBase>&) {}
		[[nodiscard]] bool IsChildOfImpl(TriggerBase*) { return false; }
		[[nodiscard]] SubMod* GetParentSubModImpl() const { return _parentSubMod; }
		[[nodiscard]] std::string NumTextImpl() const;
		[[nodiscard]] bool IsDirtyRecursiveImpl() const { return IsDirty(); }
		void SetDirtyRecursiveImpl(bool a_bDirty) { SetDirty(a_bDirty); }
	};

	// A copy, by way of JSON, as a condition set is copied
	[[nodiscard]] std::unique_ptr<TriggerSet> DuplicateTriggerSet(TriggerSet* a_triggerSet);

	// A named trigger set the mod defines, for clips to reuse through a PRESET trigger
	class TriggerPreset : public TriggerSet
	{
	public:
		TriggerPreset(std::string_view a_name, std::string_view a_description) :
			_name(a_name), _description(a_description) {}

		[[nodiscard]] std::string_view GetName() const { return _name; }
		void SetName(std::string_view a_name) { _name = a_name; }

		[[nodiscard]] std::string_view GetDescription() const { return _description; }
		void SetDescription(std::string_view a_description) { _description = a_description; }

	private:
		std::string _name;
		std::string _description;
	};

	// Whether a set uses the preset through a PRESET trigger
	[[nodiscard]] bool TriggerSetContainsPreset(TriggerSet* a_triggerSet, const TriggerPreset* a_preset);

	// Every dialogue topic with a line, for a topic picker
	[[nodiscard]] std::vector<UI::UICommon::AsyncPickerGroup> TopicGroups();
}
