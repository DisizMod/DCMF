#pragma once

namespace UIHooks
{
	class Hooks
	{
	public:
		static void Hook()
		{
			const REL::Relocation<uintptr_t> inputHook{ REL::VariantID(67315, 68617, 0xC519E0) };           // C150B0, C3B360, C519E0
			const REL::Relocation<uintptr_t> registerWindowHook{ REL::VariantID(75591, 77226, 0xDC4B90) };  // D71F00, DA3850, DC4B90
			const REL::Relocation<uintptr_t> created3d11Hook{ REL::VariantID(75595, 77226, 0xDC5530) };     // D72810, DA3850, DC5530
			const REL::Relocation<uintptr_t> presentHook{ REL::VariantID(75461, 77246, 0xDBBDD0) };         // D6A2B0, DA5BE0, DBBDD0

			auto& trampoline = SKSE::GetTrampoline();

			_InputFunc = trampoline.write_call<5>(inputHook.address() + REL::VariantOffset(0x7B, 0x7B, 0x81).offset(), InputFunc);
			_RegisterClassA_Hook = *(uintptr_t*)trampoline.write_call<6>(registerWindowHook.address() + REL::VariantOffset(0x8E, 0x15C, 0x99).offset(), RegisterClassA_Hook);
			_CreateD3D11 = trampoline.write_call<5>(created3d11Hook.address() + REL::VariantOffset(0x9, 0x275, 0x9).offset(), CreateD3D11);
			_Present = trampoline.write_call<5>(presentHook.address() + REL::VariantOffset(0x9, 0x9, 0x15).offset(), Present);
		}

	private:
		static void InputFunc(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent* const* a_events);
		static ATOM RegisterClassA_Hook(WNDCLASSA* a_wndClass);
		static LRESULT WndProcHandler(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam);

		static void CreateD3D11();
		static void Present(uint32_t a1);

		static inline REL::Relocation<decltype(InputFunc)> _InputFunc;
		static inline REL::Relocation<decltype(RegisterClassA_Hook)> _RegisterClassA_Hook;
		static inline REL::Relocation<decltype(CreateD3D11)> _CreateD3D11;
		static inline REL::Relocation<decltype(Present)> _Present;

		static inline REL::Relocation<decltype(WndProcHandler)> _WndProcHandler;
	};

	void Install();
}
