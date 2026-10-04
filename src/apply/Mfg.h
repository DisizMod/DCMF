#pragma once

#include "Applier.h"

namespace Apply
{
	// The engine's own face channels, written into the final mfg sets where BSFaceGenAnimationData::KeyframesUpdate
	// has merged dialogue, blink and look-at; only the indices held are touched, and one just let go of is zeroed for a
	// few passes because the engine does not recompute an index nothing drives
	class Mfg : public IApplier
	{
	public:
		[[nodiscard]] Kind GetKind() const override { return Kind::kMfg; }
		void Apply(Pass a_pass, ActorState::State& a_state) override;

		// The function hook the pass runs from; false when nothing was patched
		static bool Install();

		// The table's entries, in index order: which mfg set and which index in it
		struct Slot
		{
			std::string_view name;
			std::uint32_t set;  // 0 expression, 1 modifier, 2 phoneme, 3 custom
			std::uint32_t index;
		};
		[[nodiscard]] static const std::vector<Slot>& Slots();
	};
}
