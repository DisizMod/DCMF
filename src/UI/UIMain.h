#pragma once
#include "UIComboFilter.h"
#include "UIWindow.h"

#include "ModRegistry.h"
#include <imgui_internal.h>

namespace UI
{
	class UIMain : public UIWindow
	{
	public:
		// Drag & Drop payload structs
		struct DragConditionPayload
		{
			DragConditionPayload(std::unique_ptr<Conditions::ICondition>& a_condition, Conditions::ConditionSet* a_conditionSet) :
				condition(a_condition), conditionSet(a_conditionSet) {}

			std::unique_ptr<Conditions::ICondition>& condition;
			Conditions::ConditionSet* conditionSet;
		};

		struct DragFunctionPayload
		{
			DragFunctionPayload(std::unique_ptr<Functions::IFunction>& a_function, Functions::FunctionSet* a_functionSet) :
				function(a_function), functionSet(a_functionSet) {}

			std::unique_ptr<Functions::IFunction>& function;
			Functions::FunctionSet* functionSet;
		};

		struct SubModNameFilterResult
		{
			bool bDisplay = true;
			bool bDuplicateName = false;
		};

		// The channel set Copy channel set took, the Actor window's Copy held too: what Paste channel set and its Paste put down
		static std::unique_ptr<Channels::ChannelSet>& ChannelSetCopy() { return _channelSetCopy; }

	protected:
		bool ShouldDrawImpl() const override;
		void DrawImpl() override;
		void OnOpen() override;
		void OnClose() override;

	private:
		void DrawSettings(const ImVec2& a_pos);
		void DrawTimings();
		void DrawMissingPlugins();
		void DrawInvalidPlugins();
		void DrawConflictingSubMods() const;
		void DrawRegisteredModsWithInvalidConditions() const;
		void DrawSubModsWithInvalidConditions() const;
		void DrawRegisteredMods();
		void DrawRegisteredMod(RegisteredMod* a_registeredMod, std::unordered_map<std::string, SubModNameFilterResult>& a_filterResults);
		void DrawSubMod(RegisteredMod* a_registeredMod, SubMod* a_subMod, bool a_bAddPathToName = false);
		void DrawScan();
		void DrawBodyTypes();
		void DrawPatches();
		void DrawFeeds();
		bool DrawConditionSet(Conditions::ConditionSet* a_conditionSet, SubMod* a_parentSubMod, EditMode a_editMode, Conditions::ConditionType a_conditionType, RE::TESObjectREFR* a_refrToEvaluate, bool a_bDrawLines, const ImVec2& a_drawStartPos);
		// How a channel set is drawn: whole, as a modifier's; bare, a clip's claims, names and toggles that never open;
		// locked, a keyframe's rows, opened for their values but never added to, removed or toggled
		enum class ChannelDrawMode : uint8_t
		{
			kFull,
			kBare,
			kLocked
		};
		// What a keyframe's row is at its frame: its claim off, held by an earlier frame's hold, or not set, with what
		// the run has there then, from the row that last set it or its rest
		struct KeyRowState
		{
			bool bIgnored = false;
			const Keyframe* heldBy = nullptr;
			const Channels::ChannelBase* computedFrom = nullptr;
			std::string computed;
			std::optional<float> computedNumber;  // a number mid-way between two keys: what Set keys exactly
			bool bMoving = false;
		};
		bool DrawChannelSet(Channels::ChannelSet* a_channelSet, SubMod* a_parentSubMod, EditMode a_editMode, bool a_bDrawLines, const ImVec2& a_drawStartPos, ChannelDrawMode a_mode = ChannelDrawMode::kFull);
		ImRect DrawChannel(std::unique_ptr<Channels::ChannelBase>& a_channel, Channels::ChannelSet* a_channelSet, SubMod* a_parentSubMod, EditMode a_editMode, bool& a_bOutSetDirty, ChannelDrawMode a_mode = ChannelDrawMode::kFull, const KeyRowState* a_keyRow = nullptr);
		// A preset's own set passes its mod, which it has no clip to reach by, and offers no PRESET
		bool DrawTriggerSet(Triggers::TriggerSet* a_triggerSet, SubMod* a_parentSubMod, EditMode a_editMode, bool a_bDrawLines, const ImVec2& a_drawStartPos, RegisteredMod* a_presetMod = nullptr);
		ImRect DrawTrigger(std::unique_ptr<Triggers::TriggerBase>& a_trigger, Triggers::TriggerSet* a_triggerSet, EditMode a_editMode, bool& a_bOutSetDirty, RegisteredMod* a_mod, bool a_bInPreset);
		void DrawKeyframes(Clip* a_clip, ClipVariant& a_variant, const ImVec2& a_drawStartPos);
		// A frame's time, hold, rows and functions: the frame block's body, and the timeline's frame window
		void DrawKeyframeBody(Clip* a_clip, ClipVariant& a_variant, Keyframe& a_keyframe);
		// The timeline: one clip variant's frames on a clock in a window of its own, and the frame picked on it in another
		void DrawTimelineButton(Clip* a_clip, std::uint16_t a_variant, bool a_bInHeader);
		void DrawTimeline();
		void DrawTimelineSheet(Clip* a_clip, ClipVariant& a_track, bool a_bEditable);
		void DrawTimelineFrame(Clip* a_clip, ClipVariant& a_track);
		void DrawClipVariants(RegisteredMod* a_registeredMod, Clip* a_clip);
		// Preview or Stop at the end of the row, or as a button of its own, on the reference actor; nothing when none is selected
		void DrawPreviewButton(const void* a_item, const std::function<void(RE::TESObjectREFR*)>& a_start, bool a_bInHeader = true);
		ImRect DrawBlankChannel(Channels::ChannelSet* a_channelSet, EditMode a_editMode);
		bool DrawFunctionSet(Functions::FunctionSet* a_functionSet, SubMod* a_parentSubMod, EditMode a_editMode, Functions::FunctionSetType a_functionSetType, RE::TESObjectREFR* a_refrToEvaluate, bool a_bDrawLines, const ImVec2& a_drawStartPos);
		ImRect DrawCondition(std::unique_ptr<Conditions::ICondition>& a_condition, Conditions::ConditionSet* a_conditionSet, SubMod* a_parentSubMod, EditMode a_editMode, Conditions::ConditionType a_conditionType, RE::TESObjectREFR* a_refrToEvaluate, bool& a_bOutSetDirty);
		ImRect DrawFunction(std::unique_ptr<Functions::IFunction>& a_function, Functions::FunctionSet* a_functionSet, SubMod* a_parentSubMod, EditMode a_editMode, Functions::FunctionSetType a_functionSetType, RE::TESObjectREFR* a_refrToEvaluate, bool& a_bOutSetDirty);
		ImRect DrawBlankCondition(Conditions::ConditionSet* a_conditionSet, EditMode a_editMode, Conditions::ConditionType a_conditionType);
		ImRect DrawBlankFunction(Functions::FunctionSet* a_functionSet, SubMod* a_parentSubMod, EditMode a_editMode, Functions::FunctionSetType a_functionSetType);
		bool DrawConditionPreset(RegisteredMod* a_registeredMod, Conditions::ConditionPreset* a_conditionPreset, bool& a_bOutWasPresetRenamed);
		bool DrawTriggerPreset(RegisteredMod* a_registeredMod, Triggers::TriggerPreset* a_triggerPreset, bool& a_bOutWasPresetRenamed);
		bool DrawReferencePool(RegisteredMod* a_registeredMod, ReferencePool* a_pool, std::string& a_outRemoved);
		// a_from: the required mod a slot comes from, drawn locked, the mod's own overlays of its name added beside its own
		bool DrawOverlaySlot(RegisteredMod* a_registeredMod, Overlays::Slot* a_slot, bool& a_bOutWasRenamed, RegisteredMod* a_from = nullptr);
		bool DrawSkinSet(RegisteredMod* a_registeredMod, Skins::Set* a_set, bool& a_bOutWasRenamed);

		static void DrawInfoTooltip(const Info& a_info, ImGuiHoveredFlags a_flags = ImGuiHoveredFlags_DelayNormal);

		static int ReferenceInputTextCallback(struct ImGuiInputTextCallbackData* a_data);

		EditMode _editMode = EditMode::kNone;
		float _firstColumnWidthPercent = 0.55f;

		bool _bShowSettings = false;

		// modified from imgui so it allows setting tooltip size
		static bool BeginDragDropSourceEx(ImGuiDragDropFlags a_flags = 0, ImVec2 a_tooltipSize = ImVec2(0, 0));

		std::string _lastAddNewConditionName;
		std::string _lastAddNewTriggerName;
		std::string _lastAddNewChannelName;

		// A condition whose type was changed is a new object, so its tree node
		// would come back closed. Its slot is remembered and opened once drawn.
		const Conditions::ConditionSet* _reopenSet = nullptr;
		const Conditions::ICondition* _reopenOld = nullptr;
		int _reopenIndex = -1;
		const Channels::ChannelSet* _reopenChannelSet = nullptr;
		const Channels::ChannelBase* _reopenChannelOld = nullptr;
		int _reopenChannelIndex = -1;

		std::string _lastAddNewFunctionName;

		UI::UIConditionComboFilter _conditionComboFilter;
		UI::UIChannelComboFilter _channelComboFilter;
		UI::UIChannelComboFilter _poolChannelComboFilter;  // a pool's channel, reference channels only
		UI::UIFunctionComboFilter _functionComboFilter;

		std::unique_ptr<Conditions::ICondition> _conditionCopy = nullptr;
		std::unique_ptr<Functions::IFunction> _functionCopy = nullptr;
		std::unique_ptr<Conditions::ConditionSet> _conditionSetCopy = nullptr;

		// A mod, modifier or clip being named before it exists; the popup reads and clears it
		struct NewEntryState
		{
			enum class Kind : uint8_t
			{
				kNone,
				kMod,
				kModifier,
				kClip
			};

			Kind kind = Kind::kNone;
			RegisteredMod* parentMod = nullptr;
			std::string name;
			bool bShouldOpen = false;
		} _newEntryState;

		// Opened once when next drawn: what was just created
		const RegisteredMod* _openMod = nullptr;
		const SubMod* _openSubMod = nullptr;

		void DrawNewEntryPopup();
		inline static std::unique_ptr<Channels::ChannelSet> _channelSetCopy = nullptr;
		std::unique_ptr<Channels::ChannelBase> _channelCopy = nullptr;
		Channels::ChannelSet* _claimPickerSet = nullptr;  // the clip claims whose Add channels list is open
		std::unique_ptr<Triggers::TriggerSet> _triggerSetCopy = nullptr;
		std::string _keyframesCopy;  // a variant's keyframes as JSON
		std::string _frameCopy;      // one keyframe as JSON, from the timeline
		// What the timeline shows and how: the clip and variant, fitted to the window or scrolled at a zoom, the frame
		// open in its window, a handle or a hold being dragged, the headings folded, and where a right click was
		struct TimelineState
		{
			Clip* clip = nullptr;
			std::uint16_t variant = 0;
			bool bOpen = false;
			bool bScroll = false;
			float pixelsPerSecond = 160.f;
			const Keyframe* selected = nullptr;
			bool bFrameOpen = false;
			const Keyframe* dragging = nullptr;
			bool bDraggingHold = false;
			bool bDragged = false;
			float dragFrom = 0.f;
			std::vector<std::string> folded;
			const Keyframe* menuFrame = nullptr;
			float menuTime = 0.f;
			bool bDockBottom = false;  // asked this frame, placed on the next
			float dockHeight = 380.f;
		} _timeline;
		// A button in the current window's title bar, left of its close box, that asks for the window along the bottom of the screen
		void DrawDockBottomButton(bool& a_bDockBottom, float& a_dockHeight);

		std::unique_ptr<Functions::FunctionSet> _functionSetCopy = nullptr;
		[[nodiscard]] bool ConditionContainsPreset(Conditions::ICondition* a_condition, Conditions::ConditionPreset* a_conditionPreset = nullptr) const;
		[[nodiscard]] bool ConditionSetContainsPreset(Conditions::ConditionSet* a_conditionSet, Conditions::ConditionPreset* a_conditionPreset = nullptr) const;

		struct CommentState
		{
			enum class Type
			{
				kNone,
				kCondition,
				kFunction
			};

			bool bShouldOpen = false;
			Type type = Type::kNone;
			Conditions::ICondition* condition;
			Conditions::ConditionSet* parentConditionSet;
			Functions::IFunction* function;
			Functions::FunctionSet* parentFunctionSet;
			std::string buffer;

			void Set(Conditions::ICondition* a_condition, Conditions::ConditionSet* a_parentConditionSet)
			{
				Clear();
				bShouldOpen = true;
				type = Type::kCondition;
				condition = a_condition;
				parentConditionSet = a_parentConditionSet;
				buffer = a_condition->GetComment();
			}

			void Set(Functions::IFunction* a_function, Functions::FunctionSet* a_parentFunctionSet)
			{
				Clear();
				bShouldOpen = true;
				type = Type::kFunction;
				function = a_function;
				parentFunctionSet = a_parentFunctionSet;
				buffer = a_function->GetComment();
			}

			bool Valid() const
			{
				if (type == Type::kCondition) {
					return condition != nullptr && parentConditionSet != nullptr;
				} else if (type == Type::kFunction) {
					return function != nullptr && parentFunctionSet != nullptr;
				}
				return false;
			}

			void Save()
			{
				switch (type) {
				case Type::kCondition:
					if (condition && parentConditionSet) {
						condition->SetComment(buffer.data());
						parentConditionSet->SetDirty(true);
					}
					break;
				case Type::kFunction:
					if (function && parentFunctionSet) {
						function->SetComment(buffer.data());
						parentFunctionSet->SetDirty(true);
					}
					break;
				}
				Clear();
			}

			std::string GetCurrentComment() const
			{
				switch (type) {
				case Type::kCondition:
					if (condition) {
						return condition->GetComment().c_str();
					}
					break;
				case Type::kFunction:
					if (function) {
						return function->GetComment().c_str();
					}
				}
				return std::string();
			}

			bool HasUnsavedChanges() const
			{
				return buffer != GetCurrentComment();
			}

			void Clear()
			{
				bShouldOpen = false;
				type = Type::kNone;
				condition = nullptr;
				parentConditionSet = nullptr;
				function = nullptr;
				parentFunctionSet = nullptr;
				buffer.clear();
			}

		} _commentState;
	};
}
