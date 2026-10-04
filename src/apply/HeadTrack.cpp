#include "HeadTrack.h"

#include "ActorState.h"

namespace Apply
{
	namespace
	{
		// The first of these names the graph has, with its value; TDM registers its second with a trailing space
		const char* Resolve(RE::Actor* a_actor, std::initializer_list<const char*> a_names, bool& a_value)
		{
			for (const auto* name : a_names) {
				if (a_actor->GetGraphVariableBool(name, a_value)) {
					return name;
				}
			}
			return nullptr;
		}

		// What binds the look-at modifier's enable on this graph: TDM's pair when its patch is in, else vanilla's
		// IsNPC, and bHeadTracking either way; each remembered as it was, to be put back
		void FindGates(HeadTrackData& a_data, RE::Actor* a_actor)
		{
			a_data.count = 0;
			bool value = false;
			if (const auto* skse = Resolve(a_actor, { "tdmHeadtrackingSKSE" }, value)) {
				a_data.gates[a_data.count++] = { skse, value };
				bool behaviour = false;
				if (const auto* name = Resolve(a_actor, { "tdmHeadtrackingBehavior ", "tdmHeadtrackingBehavior" }, behaviour)) {
					a_data.gates[a_data.count++] = { name, behaviour };
				}
			} else if (const auto* isNPC = Resolve(a_actor, { "IsNPC" }, value)) {
				a_data.gates[a_data.count++] = { isNPC, value };
			}
			bool tracking = false;
			if (const auto* name = Resolve(a_actor, { "bHeadTracking" }, tracking)) {
				a_data.gates[a_data.count++] = { name, tracking };
			}
		}
	}

	void HeadTrack::Apply(Pass, ActorState::State& a_state)
	{
		const auto refr = a_state.handle.get();
		auto* actor = refr ? refr->As<RE::Actor>() : nullptr;
		const auto sampled = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		const bool bRetired = a_state.bRetired.load();

		a_state.headTrack.With([&](HeadTrackData& a_data) {
			// A posed spine or head holds the look-at off, or the two would stack; the gaze alone does not, the eyes
			// being written over after the engine's update anyway
			const auto posed = [&](Kind a_kind) {
				const auto k = static_cast<std::size_t>(a_kind);
				return !sampled->numbers[k].empty() || !sampled->texts[k].empty() || !sampled->references[k].empty();
			};
			const bool bWanted = actor && !bRetired && sampled && (posed(Kind::kSpine) || posed(Kind::kHead));

			if (!bWanted) {
				if (a_data.bHeld && actor) {
					for (std::size_t i = 0; i < a_data.count; ++i) {
						actor->SetGraphVariableBool(a_data.gates[i].name.c_str(), a_data.gates[i].prior);
					}
				}
				a_data = {};
				return;
			}

			if (!a_data.bHeld) {
				FindGates(a_data, actor);
				a_data.bHeld = true;
				logger::info("headtrack: {:08X} look-at held off through {} gate(s)", actor->GetFormID(), a_data.count);
			}
			// Closed again every frame: the AI opens it underneath us
			for (std::size_t i = 0; i < a_data.count; ++i) {
				actor->SetGraphVariableBool(a_data.gates[i].name.c_str(), false);
			}
		});
	}
}
