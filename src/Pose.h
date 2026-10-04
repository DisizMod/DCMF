#pragma once

#include "ActorState.h"
#include "apply/Data.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

// A segment's pose, spine, head or gaze: an offset on the animation, a fixed pose, or a target tracked, with
// angles on top; expanded each frame into the bone and mfg numbers the appliers already take
namespace Pose
{
	constexpr auto kSpine = "spine"sv;
	constexpr auto kHead = "head"sv;
	constexpr auto kGaze = "gaze"sv;

	// How a turn is shared down a chain and how far each bone may go, degrees, from the settings
	struct ChainBone
	{
		const char* name;
		float share;
		float yawReach;
		float pitchReach;
	};
	[[nodiscard]] std::array<ChainBone, 3> SpineChain();
	[[nodiscard]] std::array<ChainBone, 1> HeadChain();

	// The modes a segment offers, by the names the channel and the file use
	[[nodiscard]] std::vector<std::string> ModesFor(std::string_view a_segment);
	[[nodiscard]] std::string_view ModeName(Apply::PoseSample::Mode a_mode);
	[[nodiscard]] Apply::PoseSample::Mode ModeOf(std::string_view a_name);

	[[nodiscard]] std::vector<std::string> Targets();
	[[nodiscard]] std::string_view TargetName(Apply::PoseSample::Target a_target);
	[[nodiscard]] Apply::PoseSample::Target TargetOf(std::string_view a_name);

	// Main thread, each frame after the fold: the pose samples turned into what they drive, smoothed per actor and
	// added onto what the sample already holds for those bones and modifiers
	void Expand(ActorState::State& a_state, RE::Actor* a_actor, float a_now, ActorState::Sampled& a_out);
}
