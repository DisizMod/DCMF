#pragma once

#include "Applier.h"

namespace Apply
{
	// Face morphs with no mfg slot, summed from the head's TRIs into each shape's base morph data, which the
	// engine then re-applies to the mesh under its own mfg morphs; runs in BSFaceGenNiNode::UpdateDownwardPass
	class FaceMorph : public IApplier
	{
	public:
		[[nodiscard]] Kind GetKind() const override { return Kind::kFaceMorph; }
		void Apply(Pass a_pass, ActorState::State& a_state) override;

		// The vtable hook the pass runs from; false when nothing was patched
		static bool Install();
	};
}
