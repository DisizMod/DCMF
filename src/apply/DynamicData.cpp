#include "DynamicData.h"

#include <atomic>
#include <mutex>
#include <unordered_set>

namespace Apply::DynamicData
{
	namespace
	{
		using AllocateFn = void* (*)(std::size_t);
		using FreeFn = void (*)(void*);

		// Who counts the blocks: RaceMenu's hooks when they are at the sites, ours otherwise
		enum class Counter : std::uint8_t
		{
			kNone,
			kOurs,
			kRaceMenu
		};
		Counter g_counter = Counter::kNone;

		// What each site called before us, called on through
		AllocateFn g_allocate1 = nullptr;
		AllocateFn g_allocate2 = nullptr;
		FreeFn g_free1 = nullptr;
		FreeFn g_free2 = nullptr;

		// RaceMenu's free at the sites: one owner fewer of a block it counted, the block freed with the last
		FreeFn g_raceMenuFree = nullptr;

		// Both counts sit 16 bytes in front of the block the engine is handed; ours knows its blocks by a set
		constexpr std::size_t kHeader = 0x10;
		std::mutex g_lock;
		std::unordered_set<void*> g_blocks;

		std::atomic<std::uint64_t>& CountOf(void* a_block)
		{
			return *reinterpret_cast<std::atomic<std::uint64_t>*>(static_cast<std::uint8_t*>(a_block) - kHeader);
		}

		void* Allocate(AllocateFn a_previous, std::size_t a_size)
		{
			auto* raw = static_cast<std::uint8_t*>(a_previous(a_size + kHeader));
			if (!raw) {
				return nullptr;
			}
			void* block = raw + kHeader;
			new (raw) std::atomic<std::uint64_t>(1);
			std::scoped_lock guard(g_lock);
			g_blocks.insert(block);
			return block;
		}

		void Free(FreeFn a_previous, void* a_block)
		{
			{
				std::scoped_lock guard(g_lock);
				const auto it = a_block ? g_blocks.find(a_block) : g_blocks.end();
				if (it != g_blocks.end()) {
					if (--CountOf(a_block) > 0) {
						return;
					}
					g_blocks.erase(it);
					a_block = static_cast<std::uint8_t*>(a_block) - kHeader;
				}
			}
			a_previous(a_block);
		}

		void* Allocate1(std::size_t a_size) { return Allocate(g_allocate1, a_size); }
		void* Allocate2(std::size_t a_size) { return Allocate(g_allocate2, a_size); }
		void Free1(void* a_block) { Free(g_free1, a_block); }
		void Free2(void* a_block) { Free(g_free2, a_block); }

		// A site holds a call, or it is not the code this was written against
		bool IsCall(std::uintptr_t a_address)
		{
			return *reinterpret_cast<const std::uint8_t*>(a_address) == 0xE8;
		}

		// Where a site's call ends up: through the jump a plugin's trampoline puts near the game, to the function itself
		std::uintptr_t TargetOf(std::uintptr_t a_call)
		{
			auto target = a_call + 5 + *reinterpret_cast<const std::int32_t*>(a_call + 1);
			for (int hop = 0; hop < 4; ++hop) {
				const auto* code = reinterpret_cast<const std::uint8_t*>(target);
				if (code[0] != 0xFF || code[1] != 0x25) {
					break;
				}
				target = *reinterpret_cast<const std::uintptr_t*>(target + 6 + *reinterpret_cast<const std::int32_t*>(target + 2));
			}
			return target;
		}

		// skee64.dll's span in memory, empty when RaceMenu is not loaded
		std::pair<std::uintptr_t, std::uintptr_t> SkeeSpan()
		{
			const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"skee64.dll"));
			if (!base) {
				return {};
			}
			const auto headers = base + *reinterpret_cast<const std::uint32_t*>(base + 0x3C);
			return { base, base + *reinterpret_cast<const std::uint32_t*>(headers + 0x50) };
		}
	}

	bool Install()
	{
		if (REL::Module::IsVR()) {
			logger::error("dynamic data: VR is not supported; face overlays will not be drawn");
			return false;
		}
		// The sites skee64 patches: SE 1.5.97 found by where its calls land, AE as RaceMenu's own source names them,
		// checked against 1.7.104
		const auto site = [](std::uint64_t a_se, std::uint64_t a_ae, std::uintptr_t a_seOffset, std::uintptr_t a_aeOffset) {
			return REL::Module::IsAE() ? REL::ID(a_ae).address() + a_aeOffset : REL::ID(a_se).address() + a_seOffset;
		};
		const auto free1 = site(69554, 70939, 0x24B, 0x140);
		const auto allocate1 = site(69554, 70939, 0x252, 0x147);
		const auto allocate2 = site(69562, 70947, 0x146, 0x76);
		const auto free2 = site(69573, 70958, 0x2F, 0x2F);
		if (!IsCall(free1) || !IsCall(allocate1) || !IsCall(allocate2) || !IsCall(free2)) {
			logger::error("dynamic data: a site is not the call expected on runtime {}; not hooked, face overlays will not be drawn", REL::Module::get().version().string());
			return false;
		}

		// RaceMenu counting the blocks already: shared through its count, so its own face overlays still know the face's block
		const auto [skeeBegin, skeeEnd] = SkeeSpan();
		const auto inSkee = [&](std::uintptr_t a_site) {
			const auto target = TargetOf(a_site);
			return skeeBegin <= target && target < skeeEnd;
		};
		const int counted = inSkee(free1) + inSkee(allocate1) + inSkee(allocate2) + inSkee(free2);
		if (counted == 4) {
			g_raceMenuFree = reinterpret_cast<FreeFn>(TargetOf(free2));
			g_counter = Counter::kRaceMenu;
			logger::info("dynamic data: RaceMenu counts the face's blocks; shared through its count");
			return true;
		}
		if (counted != 0) {
			logger::error("dynamic data: RaceMenu hooks {} of the 4 sites; not hooked, face overlays will not be drawn", counted);
			return false;
		}

		auto& trampoline = SKSE::GetTrampoline();
		g_free1 = reinterpret_cast<FreeFn>(trampoline.write_call<5>(free1, reinterpret_cast<std::uintptr_t>(&Free1)));
		g_allocate1 = reinterpret_cast<AllocateFn>(trampoline.write_call<5>(allocate1, reinterpret_cast<std::uintptr_t>(&Allocate1)));
		g_allocate2 = reinterpret_cast<AllocateFn>(trampoline.write_call<5>(allocate2, reinterpret_cast<std::uintptr_t>(&Allocate2)));
		g_free2 = reinterpret_cast<FreeFn>(trampoline.write_call<5>(free2, reinterpret_cast<std::uintptr_t>(&Free2)));
		g_counter = Counter::kOurs;
		logger::info("dynamic data: face blocks counted by our hooks; RaceMenu {}", skeeBegin ? "loaded without its face overlay hooks" : "not loaded");
		return true;
	}

	bool IsInstalled()
	{
		return g_counter != Counter::kNone;
	}

	bool Retain(void* a_block)
	{
		if (!a_block) {
			return false;
		}
		if (g_counter == Counter::kRaceMenu) {
			// RaceMenu's count, one or more, and the word after it zero, as its allocate leaves them; else not a block it counted
			auto* header = reinterpret_cast<std::uint64_t*>(static_cast<std::uint8_t*>(a_block) - kHeader);
			if (header[0] == 0 || header[0] > 0xFFFF || header[1] != 0) {
				return false;
			}
			std::atomic_ref<std::uint64_t>(header[0]).fetch_add(1);
			return true;
		}
		std::scoped_lock guard(g_lock);
		if (!g_blocks.contains(a_block)) {
			return false;
		}
		++CountOf(a_block);
		return true;
	}

	void Release(void* a_block)
	{
		if (g_counter == Counter::kRaceMenu) {
			g_raceMenuFree(a_block);
		} else if (g_counter == Counter::kOurs) {
			Free(g_free2, a_block);
		}
	}
}
