#pragma once

#include "Applier.h"

namespace Apply
{
	// BodySlide morphs, summed from each shape's TRI into its skin partition and sent to the GPU.
	// The CPU copy the engine keeps holds only the SMP patch's body, the modifiers' alone, which FSMP builds its collision from.
	class BodyMorph : public IApplier
	{
	public:
		[[nodiscard]] Kind GetKind() const override { return Kind::kBodyMorph; }
		void Apply(Pass a_pass, ActorState::State& a_state) override;

		// Render thread: the working copies that changed since the last frame
		static void Upload();
	};
}
