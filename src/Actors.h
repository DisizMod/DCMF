#pragma once

#include "Facts.h"

#include <vector>

namespace Program
{
	struct Compiled;
}

namespace Actors
{
	struct Filter
	{
		// Everyone loaded, or the player alone.
		bool bAllActors = false;

		// From the player, in units. Zero is no limit.
		float maxDistance = 0.f;
	};

	// Main thread. Everyone the filter admits, as handles rather than pointers:
	// the drain reaches them over the frames that follow and has to survive one
	// of them being unloaded in between.
	void Capture(const Filter& a_filter, std::vector<RE::ActorHandle>& a_outHandles);

	// Main thread. The program's world readings, as the world is at the moment the tick fires.
	void CaptureWorld(const Program::Compiled& a_program, Facts::WorldFacts& a_out);

	// Main thread. The program's readings off one actor and the subjects its
	// TARGET and PLAYER rows name. False when the handle no longer resolves or
	// the actor has gone, which the drain reads as "skip".
	bool Gather(const RE::ActorHandle& a_handle, const Program::Compiled& a_program, Facts::ActorFacts& a_out);
}
