#pragma once

#include <cstdint>
#include <vector>

namespace Facts
{
	// What Gather read off one actor: one value per probe of the compiled program, the world's left unread. Plain
	// numbers on purpose, so the phase that answers from them needs nothing of the game
	struct ActorFacts
	{
		std::uint32_t formID = 0;
		std::vector<float> values;
		std::vector<std::vector<std::uint64_t>> sets;  // a set probe's members, sorted; empty for every other probe

		// Back to nothing with the room kept, for the next tick
		void Reset()
		{
			formID = 0;
			values.clear();
			sets.clear();
		}
	};

	// Read once when the tick fires and by every actor drained over the frames that follow, so an actor reached on
	// the last frame of a drain sees the same world as one reached on the first
	struct WorldFacts
	{
		std::vector<float> values;
		std::vector<std::vector<std::uint64_t>> sets;
	};

	struct Frame
	{
		std::uint64_t tick = 0;
		WorldFacts world;
		std::vector<ActorFacts> actors;

		void Begin(std::uint64_t a_tick)
		{
			tick = a_tick;
			world = {};
			actors.clear();
		}
	};
}
