#pragma once

#include <string>
#include <vector>

namespace Components
{
	class AnimationValue;
	class TextValue;
}

// The animations an actor's behavior graph plays: each clip seen as it activates and deactivates, by its file and the
// OAR replacer it is played as, for IsAnimationPlaying and the OnAnimationStart and OnAnimationEnd triggers
namespace AnimationGraph
{
	// Main thread, once the game's data is loaded: the clip generator hooked, OAR's animations interface asked for
	void Install();

	// Main thread, every frame: the clips deactivated a couple of frames ago and not activated again sent as ended
	void Flush();

	// Main thread: whether the ref plays a clip the value names
	[[nodiscard]] bool IsPlaying(RE::TESObjectREFR* a_refr, const Components::AnimationValue& a_value);

	// Main thread: the files the ref plays now, with their replacers
	[[nodiscard]] std::vector<std::string> PlayingOn(RE::TESObjectREFR* a_refr);

	// The file and replacer pickers, over the reference actor's graph and its project's OAR folders
	void SetPickers(Components::AnimationValue& a_value);

	// The event name picker, over the reference actor's graph
	void SetEventPicker(Components::TextValue& a_value);
}
