#pragma once

#include <string>

namespace Hook
{
	// Hooks a call inside Main::Update, which drives everything that runs once a frame, on the main thread, menus open or
	// not; false when the patch could not be placed
	bool Install();

	[[nodiscard]] bool Installed();

	// What happened, in words, for the panel to show.
	[[nodiscard]] const std::string& Status();
}
