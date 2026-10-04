#pragma once

#include "ActorState.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Functions
{
	class FunctionSet;
}

class Clip;
struct ClipVariant;
struct Keyframe;

namespace Channels
{
	class Channel;
}

// Clips on an actor: fired by their triggers, or by their conditions coming to hold when they have none, started
// only when every channel they claim is theirs to drive, played frame by frame over what the modifiers resolve,
// and let go at their end, on an interrupt, or when their conditions fail
namespace ClipPlayer
{
	// What an actor did that a trigger may answer
	enum class Event : std::uint8_t
	{
		kHit,
		kDeath,
		kDialogueLine,
		kAnimationStart,
		kAnimationEnd,
		kAnimationEvent,  // the graph sent an event, its name lowercase in the firing's animation
		kModifierActivated,
		kModifierDeactivated
	};

	// Main thread, once the game's data is loaded: the hit, death and dialogue events listened to
	void Install();

	// Any thread: an event on an actor, queued for its next tick
	void Queue(RE::TESObjectREFR* a_refr, Event a_event, RE::FormID a_topicInfo = 0);

	// Any thread: a clip of the actor's graph activated or deactivated, by its file and the OAR replacer it played as
	void QueueAnimation(RE::TESObjectREFR* a_refr, Event a_event, std::string a_file, std::string a_replacer);

	// Main thread, on a tick, from the modifiers' tick: a modifier activated or deactivated on the actor, read by the
	// clips' tick that follows it
	void QueueModifier(ActorState::State& a_state, Event a_event, const SubMod* a_modifier);

	// Any thread: a clip had the actor say a topic; the line it begins does not fire that clip
	void NoteSaid(RE::TESObjectREFR* a_refr, RE::FormID a_topic, const Clip* a_clip);

	// Main thread, on a tick, after the modifiers' tick: the clips that end, stop or lose a channel let go, and the
	// ones fired start when they can. a_passing is every clip whose conditions hold for the actor
	void Tick(ActorState::State& a_state, RE::TESObjectREFR* a_refr, const std::vector<Clip*>& a_passing, float a_now);

	// Main thread, each frame, after the modifiers' fold: the running clips' channels over it, and the frame
	// functions whose time has come
	void Fold(ActorState::State& a_state, RE::TESObjectREFR* a_refr, float a_now, ActorState::Sampled& a_out);

	// One run's channels over the sample at a time; the frame functions whose time came are added to a_functions
	// when given, and run by the caller
	void FoldRun(ActorState::RunningClip& a_running, RE::TESObjectREFR* a_refr, float a_now, ActorState::Sampled& a_out, std::vector<std::pair<Functions::FunctionSet*, Clip*>>* a_functions);

	// What a run starts from on each claimed channel: the sample's value, or the channel's rest
	void CaptureStart(ActorState::RunningClip& a_running, const ActorState::Sampled& a_under, RE::TESObjectREFR* a_refr);

	// Main thread: the actor's own colour on a colour channel, what it has with nothing of ours on it; none where it
	// has none to read
	[[nodiscard]] std::optional<RE::NiColor> OwnColour(RE::TESObjectREFR* a_refr, Apply::ChannelId a_id);

	// Where a channel is in a run at a time: before any key, on its way to its first, held, between keys, set to the
	// end on the last frame, or on its way out
	enum class Phase : std::uint8_t
	{
		kNone,
		kEntering,
		kHeld,
		kMoving,
		kSteady,
		kExiting
	};

	// One key of a channel: when it is reached and its hold ends, the row that sets it, and the ways in and out, read once
	struct ChannelKey
	{
		float time = 0.f;
		float holdEnd = 0.f;
		Channels::Channel* row = nullptr;
		const Keyframe* keyframe = nullptr;
		std::string transition;
		float strength = 0.3f;
		std::string space;
		std::string exitTransition;
		float exitStrength = 0.3f;
		std::string exitSpace;
	};

	// A channel's keys in a variant, read once for every time asked of them; a key an earlier frame's hold bars is not a key
	struct ChannelKeys
	{
		std::vector<ChannelKey> keys;
		float length = 0.f;
		const Keyframe* last = nullptr;
	};

	[[nodiscard]] ChannelKeys KeysOf(const ClipVariant& a_track, Apply::ChannelId a_id);
	[[nodiscard]] std::pair<float, Phase> NumberAt(const ChannelKeys& a_keys, float a_time, float a_from, float a_underlay, RE::TESObjectREFR* a_refr);
	[[nodiscard]] std::pair<std::optional<RE::NiColor>, Phase> ColourAt(const ChannelKeys& a_keys, float a_time, const std::optional<RE::NiColor>& a_from, const std::optional<RE::NiColor>& a_underlay);
	[[nodiscard]] Channels::Channel* StepAt(const ChannelKeys& a_keys, float a_time);

	// One channel of a variant at a time from its start, as a run folds it; what it started from and what is underneath
	// are the caller's. The keys read for the one call
	[[nodiscard]] std::pair<float, Phase> NumberAt(const ClipVariant& a_track, Apply::ChannelId a_id, float a_time, float a_from, float a_underlay, RE::TESObjectREFR* a_refr);
	[[nodiscard]] std::pair<std::optional<RE::NiColor>, Phase> ColourAt(const ClipVariant& a_track, Apply::ChannelId a_id, float a_time, const std::optional<RE::NiColor>& a_from, const std::optional<RE::NiColor>& a_underlay);
	// A stepped channel's row in force, none before its first key or after the clip's end
	[[nodiscard]] Channels::Channel* StepAt(const ClipVariant& a_track, Apply::ChannelId a_id, float a_time);

	// Every channel a clip claims, down through its groups
	[[nodiscard]] std::vector<Apply::ChannelId> Claims(Clip* a_clip);

	// The variant's end: its last frame, the frame's hold counted
	[[nodiscard]] float LengthOf(const ClipVariant& a_track);
}
