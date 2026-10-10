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

	// TODO: temporary, the player's fitted colliders written beside the log, to compare with a hand-made config
	void WriteComparison();

	// TODO: temporary, a HotSwap preset's colliders drawn on the player beside the fit: the presets found, which is drawn
	// (0 none), and the drawing kept in step each frame
	[[nodiscard]] const std::vector<std::string>& HotSwapPresets();
	[[nodiscard]] int HotSwapPick();
	void SetHotSwapPick(int a_pick);
	void DrawHotSwap();
}
