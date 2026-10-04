#pragma once

#include "Applier.h"

namespace Apply
{
	// Swaps a head part of a type for another, with the reset of the 3D that takes it; player only, since
	// an NPC's face is baked and a swapped part on it goes grey
	class HeadPart : public IApplier
	{
	public:
		[[nodiscard]] Kind GetKind() const override { return Kind::kHeadPart; }
		void Apply(Pass a_pass, ActorState::State& a_state) override;

		// The table's entries, in index order
		struct Slot
		{
			std::string_view name;
			RE::BGSHeadPart::HeadPartType type;
		};
		[[nodiscard]] static const std::vector<Slot>& Slots();
	};
}
