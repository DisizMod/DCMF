#include "Applier.h"

#include "ActorState.h"
#include "ActorProperty.h"
#include "BodyMorph.h"
#include "Bone.h"
#include "FaceMorph.h"
#include "HeadPart.h"
#include "HeadTrack.h"
#include "Material.h"
#include "Mfg.h"
#include "Overlay.h"
#include "Skin.h"

namespace Apply
{
	namespace
	{
		std::vector<std::unique_ptr<IApplier>> g_appliers;
	}

	void Install()
	{
		if (!g_appliers.empty()) {
			return;
		}
		g_appliers.push_back(std::make_unique<BodyMorph>());
		g_appliers.push_back(std::make_unique<FaceMorph>());
		g_appliers.push_back(std::make_unique<Bone>());
		g_appliers.push_back(std::make_unique<Mfg>());
		g_appliers.push_back(std::make_unique<ActorProperty>());
		g_appliers.push_back(std::make_unique<HeadPart>());
		g_appliers.push_back(std::make_unique<Material>());
		g_appliers.push_back(std::make_unique<Overlay>());
		g_appliers.push_back(std::make_unique<Skin>());
		g_appliers.push_back(std::make_unique<HeadTrack>());

		FaceMorph::Install();
		Bone::Install();
		Mfg::Install();
	}

	void Run(Pass a_pass, ActorState::State& a_state)
	{
		for (const auto& applier : g_appliers) {
			if (RunsIn(applier->GetKind(), a_pass)) {
				applier->Apply(a_pass, a_state);
			}
		}
	}

	void RunUpdate()
	{
		ActorState::ForEach([](ActorState::State& a_state) { Run(Pass::kUpdate, a_state); });
		for (const auto& retired : ActorState::TakeRetired()) {
			Run(Pass::kUpdate, *retired);
		}
	}

	void Upload()
	{
		BodyMorph::Upload();
	}
}
