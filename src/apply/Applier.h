#pragma once

#include "Kind.h"

namespace ActorState
{
	class State;
}

namespace Apply
{
	// Writes one kind of channel into the engine. It binds when the actor's parts change, writes what
	// changed, and puts back what it wrote when the actor is retired or its channels go.
	class IApplier
	{
	public:
		virtual ~IApplier() = default;

		[[nodiscard]] virtual Kind GetKind() const = 0;

		// The pass is one of PassesOf(GetKind()); the state's sampled values are read inside
		virtual void Apply(Pass a_pass, ActorState::State& a_state) = 0;
	};

	void Install();

	// Every applier that runs in the pass, for one actor
	void Run(Pass a_pass, ActorState::State& a_state);

	// Main thread, each frame after Sample: the Update pass for every kept and every just-retired actor
	void RunUpdate();

	// Render thread, each frame: whatever the appliers left to send to the GPU
	void Upload();
}
