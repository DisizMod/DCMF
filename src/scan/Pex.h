#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Scan::Pex
{
	struct Value
	{
		enum class Type : uint8_t
		{
			kNone,
			kIdentifier,
			kString,
			kInt,
			kFloat,
			kBool
		};

		Type type = Type::kNone;
		std::string_view text;  // an identifier or a string, into the script's string table
		int32_t integer = 0;
		float real = 0.f;
	};

	struct Instruction
	{
		enum Opcode : uint8_t
		{
			kAssign = 13,
			kCallMethod = 23,
			kCallParent = 24,
			kCallStatic = 25,
			kArraySetElement = 33
		};

		uint8_t opcode = 0;
		std::vector<Value> arguments;
	};

	struct Function
	{
		std::string_view state;
		std::string_view name;
		std::vector<Instruction> code;
	};

	// A compiled Papyrus script, Skyrim's big-endian layout
	struct Script
	{
		std::vector<std::string> strings;
		std::string_view name;
		std::string_view parent;
		std::vector<Function> functions;
	};

	[[nodiscard]] bool Read(std::span<const std::byte> a_bytes, Script& a_out, std::string& a_error);
}
