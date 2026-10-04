#pragma once

// The stock Win32 backend; UIManager::Render corrects its DisplaySize and cursor position after its NewFrame
#include <imgui_impl_win32.h>

// The ratio between the swap chain and the window's client area, computed by UIManager::Init and applied by UIManager::Render
struct ImGuiUserData
{
	struct
	{
		float x = 1.f;
		float y = 1.f;
	} screenScaleRatio;
};
