#include "Hook.h"
#include "Resources.h"

#include "ActorState.h"
#include "apply/Applier.h"
#include "Scheduler.h"
#include "Timing.h"

#include <format>
#include <optional>

namespace Hook
{
	namespace
	{
		bool g_installed = false;

		// Said in the panel, not only in a log: a log under Mod Organizer is
		// behind its virtual file system and hard to reach.
		std::string g_status = "Install() has not been called";

		REL::Relocation<void()> g_original;

		void Frame()
		{
			g_original();
			Resources::SetMainThread();

			// The wall clock rather than the game's delta: conditions ask about AI state,
			// which does not slow with the world, and a paused menu still has to blend.
			float realDelta = 0.f;
			if (const auto* timer = RE::BSTimer::GetSingleton()) {
				realDelta = timer->realTimeDelta;
			} else if (static bool bSaid = false; !bSaid) {
				bSaid = true;
				logger::error("hook: no BSTimer; the frame's time does not advance");
			}

			float total = 0.f;
			float actorState = 0.f;
			float sample = 0.f;
			float applyUpdate = 0.f;
			std::optional<Timing::Scope> totalScope(std::in_place, total);
			{
				Timing::Scope scope(actorState);
				ActorState::Update();
			}
			Scheduler::Advance(realDelta);
			{
				Timing::Scope scope(sample);
				ActorState::Sample();
			}
			{
				Timing::Scope scope(applyUpdate);
				Apply::RunUpdate();
			}
			totalScope.reset();
			Timing::RecordPerFrame([&](Timing::PerFrame& a_frame) {
				a_frame.total.Add(total);
				a_frame.actorState.Add(actorState);
				a_frame.sample.Add(sample);
				a_frame.applyUpdate.Add(applyUpdate);
			});
		}
	}

	bool Install()
	{
		if (g_installed) {
			return true;
		}

		// Main::Update, and the call inside it that runs once a frame
		const REL::Relocation<std::uintptr_t> mainUpdate{ REL::VariantID(35565, 36564, 0x5BAB10) };
		const auto callOffset = REL::VariantOffset(0x748, REL::Module::IsAtLeast(SKSE::RUNTIME_SSE_1_7_99) ? 0xC38 : 0xC26, 0x7EE).offset();
		const auto call = mainUpdate.address() + callOffset;

		// A call is E8 and four bytes of displacement; anything else means the offset is not this build's
		if (*reinterpret_cast<const std::uint8_t*>(call) != 0xE8) {
			g_status = std::format("Main::Update+0x{:X} is not a call (0x{:02X}); nothing patched", callOffset, *reinterpret_cast<const std::uint8_t*>(call));
			return false;
		}

		auto& trampoline = SKSE::GetTrampoline();
		g_original = trampoline.write_call<5>(call, Frame);
		g_installed = true;
		g_status = std::format("hooked the call at Main::Update+0x{:X}", callOffset);
		logger::info("{}", g_status);
		return true;
	}

	bool Installed() { return g_installed; }

	const std::string& Status() { return g_status; }
}
