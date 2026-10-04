#pragma once

#include "Timing.h"

#include <cstdint>
#include <string_view>

#include <optional>
#include <string>

namespace Scheduler
{
	enum class TargetMode : std::uint8_t
	{
		kDisabled,
		kPlayerOnly,
		kAll,

		kTotal
	};

	[[nodiscard]] constexpr std::string_view TargetModeName(TargetMode a_mode)
	{
		switch (a_mode) {
		case TargetMode::kDisabled:
			return "Disabled"sv;
		case TargetMode::kPlayerOnly:
			return "Player"sv;
		case TargetMode::kAll:
			return "All"sv;
		default:
			return ""sv;
		}
	}

	// Main thread, once a frame, given the real seconds since the last call.
	// Fires a tick when one is due and drains part of the one already in
	// flight.
	void Advance(float a_realDeltaSeconds);

	// A row's compiled answer for the reference the editor inspects, and the number it compared, as its latest tick had them
	struct RowAnswer
	{
		bool bHolds = false;
		std::string current;
	};

	// Any thread: the inspected reference's answer to a row of a clip or modifier; none when the row was not compiled for
	// it or the reference not answered
	[[nodiscard]] std::optional<RowAnswer> AnswerFor(const void* a_owner, const void* a_condition);

	// Main thread: whether a CONDITION function's list held for the actor on its latest tick; false for an actor not
	// answered, or a list not compiled
	[[nodiscard]] bool FunctionConditionsHold(const void* a_conditions, RE::TESObjectREFR* a_refr);

	// Rebuilds the compiled program on the next tick. The mods are walked
	// once, not every tick, so an edit has to say so.
	void Recompile();

	// Drops the frame in flight and forgets the accumulator. For a load, or
	// for the mod being switched off.
	void Reset();

	[[nodiscard]] Timing::Phases GetTimings();
	void ResetTimings();
}
