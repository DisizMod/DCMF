#pragma once

#include "Applier.h"

namespace Apply
{
	// Weight and alpha, on the main thread, only on change; weight rebuilds the 3D and is never blended
	class ActorProperty : public IApplier
	{
	public:
		[[nodiscard]] Kind GetKind() const override { return Kind::kActorProperty; }
		void Apply(Pass a_pass, ActorState::State& a_state) override;

		enum class Slot : std::uint32_t
		{
			kWeight,
			kAlpha,
			kTotal
		};
	};
}
