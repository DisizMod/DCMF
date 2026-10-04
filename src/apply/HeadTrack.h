#pragma once

#include "Applier.h"

namespace Apply
{
	// The engine's own look-at held off while a pose channel drives the actor: the behaviour graph's gate for the
	// look-at modifier closed each frame, vanilla's or TDM's, and opened again as it was when the pose goes
	class HeadTrack : public IApplier
	{
	public:
		[[nodiscard]] Kind GetKind() const override { return Kind::kHeadTrack; }
		void Apply(Pass a_pass, ActorState::State& a_state) override;
	};
}
