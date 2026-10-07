#pragma once

#include <string>

namespace Components
{
	class AnimationValue;
}

// An animation file played on an actor on demand, over whatever its behavior graph does, the way Open Animation Replacer
// previews one: a clip of our own bound to the file, stepped and blended from hooks on the graph's Update and Generate
namespace AnimationPlay
{
	// Main thread, once the game's data is loaded: hkbBehaviorGraph::Update and ::Generate hooked, after OAR's own
	void Install();

	// Main thread: the value's file, or its OAR replacer's own, played once from its first frame; its start and end are
	// the tracker's like a graph's clip. False, with why, when the actor's graph does not list it
	bool Play(RE::Actor* a_actor, const Components::AnimationValue& a_value, std::string* a_outWhy = nullptr);

	// Main thread: what Play started stopped, the graph's own pose back
	void Stop(RE::Actor* a_actor);

	[[nodiscard]] bool IsPlaying(RE::Actor* a_actor);
}
