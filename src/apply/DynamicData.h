#pragma once

// The vertex blocks of the face's dynamic meshes, counted as RaceMenu's skee counts them, so a head and the overlays
// drawn on it can share one block and the last of them frees it
namespace Apply::DynamicData
{
	// Post load, once every plugin has patched: RaceMenu's count when its hooks are at the engine's allocate and free for
	// those blocks, else our own hooks there. SE and AE; false on VR or when a site is not the call expected
	bool Install();
	[[nodiscard]] bool IsInstalled();

	// One more owner of a block RaceMenu or this counted; false for any other block, which may not be shared
	bool Retain(void* a_block);
	// One owner fewer, freed with the last; a block this did not count is freed as it was
	void Release(void* a_block);
}
