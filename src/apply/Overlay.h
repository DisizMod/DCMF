#pragma once

#include "Applier.h"

namespace Apply
{
	// An overlay slot's pick: the part's skin cloned with the slot's texture for the actor's layout, blended over
	// it at the slot's alpha, tint and glow. Built and written on the main thread, a face clone's morph block
	// followed on the face pass, and taken off when the pick goes.
	class Overlay : public IApplier
	{
	public:
		// What our clones are named, so a walk of the actor's 3D can leave them out
		static constexpr auto kNodePrefix = "DCMF Overlay "sv;

		[[nodiscard]] Kind GetKind() const override { return Kind::kOverlay; }
		void Apply(Pass a_pass, ActorState::State& a_state) override;
	};
}
