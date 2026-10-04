#pragma once

// The precompiled header: the "sv" literals, the lock aliases, and IMGUI_DEFINE_MATH_OPERATORS, which UICommon.cpp's marks need

#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#define IMGUI_DEFINE_MATH_OPERATORS

#pragma warning(push)
#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>
#pragma warning(pop)

#include <boost/container_hash/hash.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

using namespace std::literals;

namespace logger = SKSE::log;

namespace util
{
	using SKSE::stl::report_and_fail;
}

namespace std
{
	template <class T>
	struct hash<RE::BSPointerHandle<T>>
	{
		uint32_t operator()(const RE::BSPointerHandle<T>& a_handle) const
		{
			const uint32_t nativeHandle = const_cast<RE::BSPointerHandle<T>*>(&a_handle)->native_handle();
			return nativeHandle;
		}
	};

	template <class T>
	struct hash<RE::hkRefPtr<T>>
	{
		uintptr_t operator()(const RE::hkRefPtr<T>& a_ptr) const
		{
			return reinterpret_cast<uintptr_t>(a_ptr.get());
		}
	};
}

namespace RE
{
	template <class T>
	bool operator<(const RE::BSPointerHandle<T>& a_lhs, const RE::BSPointerHandle<T>& a_rhs)
	{
		return a_lhs.native_handle() < a_rhs.native_handle();
	}

	template <class T>
	std::size_t hash_value(const BSPointerHandle<T>& a_handle)
	{
		boost::hash<uint32_t> hasher;
		return hasher(a_handle.native_handle());
	}
}

struct CaseInsensitiveHash
{
	size_t operator()(const std::string& a_key) const
	{
		std::string lowerStr = a_key;
		std::ranges::transform(lowerStr, lowerStr.begin(), [](char c) {
			return static_cast<char>(std::tolower(c));
		});
		return std::hash<std::string>()(lowerStr);
	}
};

struct CaseInsensitiveEqual
{
	bool operator()(const std::string& a, const std::string& b) const
	{
		return std::equal(a.begin(), a.end(), b.begin(), b.end(),
			[](char l, char r) {
				return std::tolower(l) == std::tolower(r);
			});
	}
};

struct CaseInsensitiveCompare
{
	bool operator()(const std::string& a, const std::string& b) const
	{
		return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
			[](char l, char r) {
				return std::tolower(l) < std::tolower(r);
			});
	}
};

#include "Plugin.h"

using ExclusiveLock = std::mutex;
using Locker = std::lock_guard<ExclusiveLock>;

using SharedLock = std::shared_mutex;
using ReadLocker = std::shared_lock<SharedLock>;
using WriteLocker = std::unique_lock<SharedLock>;
