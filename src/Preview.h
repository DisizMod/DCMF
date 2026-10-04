#pragma once

#include "ActorState.h"

#include <optional>

class SubMod;
class Clip;
struct Keyframe;

// One item previewed on the reference actor at a time: a modifier held, a clip's variant played on a loop, or a
// frame held at its time. Above the modifiers and the clips; with Clear channels it is all the actor has, without it
// only its own channels are taken and the rest resolve as usual. The Actor window's holds and a preview do not mix:
// starting one lets the holds go, and a hold made while previewing ends the preview
namespace Preview
{
	void StartModifier(RE::TESObjectREFR* a_refr, SubMod* a_modifier);
	void StartClip(RE::TESObjectREFR* a_refr, Clip* a_clip, std::uint16_t a_variant);
	void StartFrame(RE::TESObjectREFR* a_refr, Clip* a_clip, std::uint16_t a_variant, const Keyframe* a_frame);
	// The variant held at a time, as the timeline's ruler scrubs it; moving it keeps the run
	void HoldAt(RE::TESObjectREFR* a_refr, Clip* a_clip, std::uint16_t a_variant, float a_time);
	void Stop();

	// The time a variant is held at, when it is
	[[nodiscard]] std::optional<float> HeldAt(const Clip* a_clip, std::uint16_t a_variant);
	// How far into a previewed variant's play the preview is, when it plays
	[[nodiscard]] std::optional<float> Playhead(const Clip* a_clip, std::uint16_t a_variant);

	// Whether this item is the one previewed: the modifier, the variant's track, or the frame
	[[nodiscard]] bool IsPreviewing(const void* a_item);

	// Main thread, each frame, after the modifiers, the clips and the Actor window's holds
	void Apply(ActorState::State& a_state, RE::TESObjectREFR* a_refr, float a_now, ActorState::Sampled& a_out);
}
