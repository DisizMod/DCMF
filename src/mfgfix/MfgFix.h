#pragma once

#include <cstdint>

// What Mfg Fix did, so it can be removed: its presence is detected and said, the seven Papyrus natives its scripts
// declare, MfgConsoleFunc and MfgConsoleFuncExt, are answered here, writing the console tier as the mfg command
// does, and the smooth forms are stepped every face update. The scripts stay; the natives behind them are ours
namespace MfgFix
{
	// Looks for mfgfix.dll in the process, at load; SKSE loads plugins alphabetically, so it is in by then
	void Detect();
	[[nodiscard]] bool FixLoaded();

	// Registers the natives, unless Mfg Fix is loaded, whose own then stand
	bool Register(RE::BSScript::IVirtualMachine* a_vm);

	// Steps every smooth move on this head toward its target; called from the face update, before the merge
	void Smooth(RE::BSFaceGenAnimationData* a_data, float a_timeDelta);

	// The console tier carried into the finals, what vanilla loses on its next update and Mfg Fix put back: an
	// expression replaces the whole tier, modifiers, phonemes and custom go over by value, and once any Look is on
	// the console tier all four carry, zeros included. True when anything carried
	bool Merge(RE::BSFaceGenAnimationData* a_data);
}
