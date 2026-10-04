#pragma once

#include <string_view>

namespace Plugin
{
	using namespace std::literals;

	inline constexpr auto NAME = "Dynamic Character Modifier Framework"sv;
	inline constexpr auto SHORT_NAME = "DCMF"sv;

	inline constexpr auto VERSION = REL::Version{ DCMF_VERSION_MAJOR, DCMF_VERSION_MINOR, DCMF_VERSION_PATCH };
	inline constexpr std::string_view VERSION_STRING = DCMF_VERSION;
}
