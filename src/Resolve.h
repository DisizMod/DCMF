#pragma once

#include "ActorState.h"
#include "apply/Entries.h"

#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

class SubMod;

namespace Channels
{
	class Channel;
	class IChannelComponent;
	class NumberChannel;
}

// From the rules that hold to the values the appliers read: which modifiers are active on an actor, kept while
// their end transition runs, and each frame their channels folded by priority, blend mode and transition weight
namespace Resolve
{
	// The clock the transitions run on: real seconds, advanced by the scheduler only while the game runs
	void AdvanceClock(float a_deltaSeconds);
	[[nodiscard]] float Now();

	// Main thread, on a tick: the modifiers held for the actor become its active set. One newly held enters now,
	// one no longer held leaves now, or at once when it has no end transition, and one still held stays as it was
	void Tick(ActorState::State& a_state, const std::vector<const SubMod*>& a_held, float a_now);

	// Main thread, each frame: the active set's channels into the sample. Highest priority first decides what is
	// held, a group dropped whole when any of its channels already is; then lowest first folds the values, a
	// claim replacing what is below, the other modes blending with it, each by the modifier's transition weight
	void Fold(ActorState::State& a_state, RE::TESObjectREFR* a_refr, float a_now, ActorState::Sampled& a_out);

	// No active modifier easing in or out
	[[nodiscard]] bool Settled(ActorState::State& a_state, float a_now);

	// One modifier's channels alone, at full weight, as the fold would write them
	void FoldModifier(const SubMod* a_modifier, RE::TESObjectREFR* a_refr, ActorState::Sampled& a_out);

	// A key's curve at a fraction of the way, by the transition's name: legacy's shapes, a bounce or a spring by its strength
	[[nodiscard]] float CurveAt(std::string_view a_name, float a_u, float a_strength = 0.3f);

	// A number channel's value for the actor: static, or read from a global or an actor value and mapped
	[[nodiscard]] float NumberValue(Channels::NumberChannel* a_channel, RE::TESObjectREFR* a_refr);

	// Where a random pick is drawn: the actor, the clip or modifier the channel is in, and since when it runs
	struct PickContext
	{
		RE::TESObjectREFR* refr = nullptr;
		const SubMod* owner = nullptr;
		float activatedAt = 0.f;
	};

	// A reference row's random pick: the entry's value component drawn, null when nothing can be; nothing for a static row
	[[nodiscard]] std::optional<const Channels::IChannelComponent*> RandomPick(Channels::Channel* a_channel, const PickContext& a_context);

	// The name a reference row stands for: its random pick, or its own value; empty for none. A draw with nothing the game
	// has says so: the channel is set to empty, not left to the rows below
	[[nodiscard]] std::string NameOf(Channels::Channel* a_channel, const PickContext& a_context, bool* a_outDrewNothing = nullptr);

	// What a reference channel names, as its applier counts it; a head part's random pick by its editor ID
	[[nodiscard]] std::optional<std::uint64_t> ReferenceValue(Channels::Channel* a_channel, const Apply::Entry& a_entry, const PickContext& a_context = {}, bool* a_outDrewNothing = nullptr);

	// A preset's sliders at both weights, by body morph index; nothing for a preset not found
	[[nodiscard]] ActorState::PresetMix PresetSliders(std::string_view a_name);

	// Partway from one set of sliders to another, a slider missing from either at 0
	[[nodiscard]] ActorState::PresetMix MixSliders(const ActorState::PresetMix& a_from, const ActorState::PresetMix& a_to, float a_u);

	// The weight a preset is read at, 0 to 1: body:weight when something drives it, else the actor's own
	[[nodiscard]] float BodyWeight(const ActorState::Sampled& a_sampled, RE::TESObjectREFR* a_refr);

	// Why each channel of the active modifiers came out as it did: its value applied, mixed under a higher one's blend,
	// shut out by a higher claim, or left out with its group; for the log, which alone asks
	struct ChannelOutcome
	{
		enum class Kind : std::uint8_t
		{
			kWins,
			kBlended,
			kOverridden,
			kHeldBack
		};

		const SubMod* owner = nullptr;
		std::uint32_t channel = 0;  // its ChannelId, raw
		Kind kind = Kind::kWins;
		const SubMod* by = nullptr;  // the one above it, when blended or overridden
		std::string mode;            // that one's blend mode
	};
	[[nodiscard]] std::vector<ChannelOutcome> Explain(const std::vector<ActorState::Active>& a_active);

	// Per channel, the highest priority modifier on the actor that drives it and is not leaving
	[[nodiscard]] std::unordered_map<std::uint32_t, const SubMod*> ModifierHolders(ActorState::State& a_state);

	// Per channel, the highest priority of a modifier on the actor that drives it and is not leaving
	[[nodiscard]] std::unordered_map<std::uint32_t, std::int32_t> ModifierPriorities(ActorState::State& a_state);
}
