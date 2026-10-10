#pragma once

namespace ActorState
{
	class State;
}

namespace Apply::Cbpc
{
	// At plugin load, before CBPC reads its configs: DCMF's collision config written while the patch is on, removed while off
	void SyncConfigFile();

	// Main thread, once a frame before the actors: the config written or removed and CBPC told when the patch is toggled
	void Frame();

	// Main thread, each frame per actor: its colliders fitted again when its body changed, and given back when CBPC dropped them
	void Update(ActorState::State& a_state);

	// Main thread: an actor let go, its colliders taken off
	void Release(ActorState::State& a_state);
}
