#include "UIHooks.h"

#include "ModRegistry.h"
#include "apply/Applier.h"
#include "Settings.h"
#include "UI/MeshThumbs.h"
#include "UI/UIManager.h"

namespace UIHooks
{
	void Install()
	{
		if (Settings::bEnableUI) {
			Hooks::Hook();
		}
	}

	void Hooks::InputFunc(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events)
	{
		if (a_events) {
			UI::UIManager::GetSingleton().ProcessInputEvents(a_events);
		}

		if (UI::UIManager::GetSingleton().ShouldConsumeInput()) {
			constexpr RE::InputEvent* const dummy[] = { nullptr };
			_InputFunc(a_dispatcher, dummy);
		} else {
			_InputFunc(a_dispatcher, a_events);
		}
	}

	ATOM Hooks::RegisterClassA_Hook(WNDCLASSA* a_wndClass)
	{
		_WndProcHandler = reinterpret_cast<uintptr_t>(a_wndClass->lpfnWndProc);
		a_wndClass->lpfnWndProc = &WndProcHandler;

		return _RegisterClassA_Hook(a_wndClass);
	}

	LRESULT Hooks::WndProcHandler(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
	{
		if (a_msg == WM_KILLFOCUS) {
			UI::UIManager::GetSingleton().OnFocusLost();
		}

		return _WndProcHandler(a_hwnd, a_msg, a_wParam, a_lParam);
	}

	void Hooks::CreateD3D11()
	{
		_CreateD3D11();

		UI::UIManager::GetSingleton().Init();
	}

	void Hooks::Present(uint32_t a1)
	{
		_Present(a1);

		// The queued jobs, once a frame
		ModRegistry::GetSingleton().RunJobs();
		Apply::Upload();
		if (const auto* renderManager = RE::BSGraphics::Renderer::GetSingleton()) {
			UI::MeshThumbs::Advance(reinterpret_cast<ID3D11Device*>(renderManager->GetRuntimeData().forwarder), reinterpret_cast<ID3D11DeviceContext*>(renderManager->GetRuntimeData().context));
		}
		UI::UIManager::GetSingleton().Render();
	}
}
