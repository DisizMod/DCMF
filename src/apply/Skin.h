#pragma once

#include "Applier.h"

namespace Apply
{
	// The skin set picked: its maps loaded and put on the body, hands, feet and face in place of the meshes' own,
	// only the maps it states, put back on again where something replaced them, and the originals put back by the
	// pointers held when the set goes. Main thread.
	class Skin : public IApplier
	{
	public:
		[[nodiscard]] Kind GetKind() const override { return Kind::kSkin; }
		void Apply(Pass a_pass, ActorState::State& a_state) override;
	};
}
