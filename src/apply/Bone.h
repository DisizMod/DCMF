#pragma once

#include "Applier.h"

namespace Apply
{
	// Turns and scales bones after animation, in BSFadeNode::UpdateDownwardPass on the actor's own skeleton
	class Bone : public IApplier
	{
	public:
		[[nodiscard]] Kind GetKind() const override { return Kind::kBone; }
		void Apply(Pass a_pass, ActorState::State& a_state) override;

		// The vtable hook the pass runs from; false when nothing was patched
		static bool Install();

		// Four entries per bone, in this order
		enum class Property : std::uint32_t
		{
			kScale,
			kX,
			kY,
			kZ,
			kTotal
		};
	};
}
