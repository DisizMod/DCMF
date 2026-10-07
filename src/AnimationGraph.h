#pragma once

#include <optional>
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

	// Any thread: the last files the ref started, newest first, five at most
	[[nodiscard]] std::vector<std::string> RecentOn(RE::TESObjectREFR* a_refr);

	// The file and replacer pickers, over the reference actor's graph and its project's OAR folders
	void SetPickers(Components::AnimationValue& a_value);

	// The event name picker, over the reference actor's graph
	void SetEventPicker(Components::TextValue& a_value);

	// Main thread: the graph the ref animates with now
	[[nodiscard]] RE::BShkbAnimationGraph* GraphOn(RE::TESObjectREFR* a_refr);

	// A file's place in a graph's animation list
	struct Binding
	{
		RE::BShkbAnimationGraph* graph = nullptr;
		std::uint16_t index = 0;
	};

	// Main thread: where the ref's graph lists the value's file, or its OAR replacer's own file; why not, when nowhere
	[[nodiscard]] std::optional<Binding> BindingOf(RE::TESObjectREFR* a_refr, const Components::AnimationValue& a_value, std::string* a_outWhy = nullptr);

	// Behavior threads: a clip played outside the graph, started and ended as a graph's own clip is
	void Started(const RE::hkbContext& a_context, const void* a_clip, std::string a_file, std::string a_replacer);
	void Ended(const RE::hkbContext& a_context, const void* a_clip);
}
