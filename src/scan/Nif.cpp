#include "Nif.h"

#include <cstring>

namespace Scan::Nif
{
	namespace
	{
		class Reader
		{
		public:
			explicit Reader(std::span<const std::byte> a_bytes) :
				_bytes(a_bytes) {}

			template <class T>
			bool Read(T& a_out)
			{
				if (_at + sizeof(T) > _bytes.size()) {
					return false;
				}
				std::memcpy(&a_out, _bytes.data() + _at, sizeof(T));
				_at += sizeof(T);
				return true;
			}

			bool Skip(std::size_t a_count)
			{
				if (_at + a_count > _bytes.size()) {
					return false;
				}
				_at += a_count;
				return true;
			}

			// A length-prefixed string, the prefix a byte or a uint32
			template <class Length>
			bool ReadString(std::string& a_out)
			{
				Length length = 0;
				if (!Read(length) || _at + length > _bytes.size()) {
					return false;
				}
				a_out.assign(reinterpret_cast<const char*>(_bytes.data() + _at), length);
				_at += length;
				return true;
			}

			[[nodiscard]] std::size_t At() const { return _at; }

		private:
			std::span<const std::byte> _bytes;
			std::size_t _at = 0;
		};

		constexpr std::string_view kHeader = "Gamebryo File Format, Version 20.2.0.7\n"sv;

		bool IsGeometryType(std::string_view a_type)
		{
			return a_type == "BSTriShape" || a_type == "BSDynamicTriShape" || a_type == "BSSubIndexTriShape" || a_type == "BSMeshLODTriShape" || a_type == "NiTriShape" || a_type == "NiTriStrips";
		}

		bool IsNodeType(std::string_view a_type)
		{
			return a_type == "NiNode" || a_type == "BSFadeNode";
		}

		// The names of the blocks of the wanted types, in block order: every such block starts with its name
		bool ReadNamesOf(std::span<const std::byte> a_bytes, bool (*a_isWanted)(std::string_view), std::vector<std::string>& a_out, std::string& a_error)
		{
		a_out.clear();
		if (a_bytes.size() < kHeader.size() || std::memcmp(a_bytes.data(), kHeader.data(), kHeader.size()) != 0) {
			a_error = "not a Skyrim SE mesh (20.2.0.7)";
			return false;
		}

		Reader reader(a_bytes);
		reader.Skip(kHeader.size());
		uint32_t version = 0;
		uint8_t endian = 0;
		uint32_t userVersion = 0;
		uint32_t blockCount = 0;
		uint32_t bsVersion = 0;
		if (!reader.Read(version) || !reader.Read(endian) || !reader.Read(userVersion) || !reader.Read(blockCount) || !reader.Read(bsVersion)) {
			a_error = "truncated header";
			return false;
		}
		if (userVersion != 12 || bsVersion != 100) {
			a_error = std::format("not a Skyrim SE mesh (user version {}, BS version {})", userVersion, bsVersion);
			return false;
		}
		// Author, process script, export script
		std::string text;
		for (int i = 0; i < 3; ++i) {
			if (!reader.ReadString<uint8_t>(text)) {
				a_error = "truncated header strings";
				return false;
			}
		}

		uint16_t typeCount = 0;
		if (!reader.Read(typeCount)) {
			a_error = "truncated block types";
			return false;
		}
		std::vector<bool> geometryTypes(typeCount);
		for (uint16_t i = 0; i < typeCount; ++i) {
			if (!reader.ReadString<uint32_t>(text)) {
				a_error = "truncated block types";
				return false;
			}
			geometryTypes[i] = a_isWanted(text);
		}
		std::vector<uint16_t> blockTypes(blockCount);
		std::vector<uint32_t> blockSizes(blockCount);
		for (auto& type : blockTypes) {
			if (!reader.Read(type)) {
				a_error = "truncated block index";
				return false;
			}
		}
		for (auto& size : blockSizes) {
			if (!reader.Read(size)) {
				a_error = "truncated block sizes";
				return false;
			}
		}

		uint32_t stringCount = 0;
		uint32_t longestString = 0;
		if (!reader.Read(stringCount) || !reader.Read(longestString)) {
			a_error = "truncated string table";
			return false;
		}
		std::vector<std::string> strings(stringCount);
		for (auto& string : strings) {
			if (!reader.ReadString<uint32_t>(string)) {
				a_error = "truncated string table";
				return false;
			}
		}
		uint32_t groupCount = 0;
		if (!reader.Read(groupCount) || !reader.Skip(groupCount * 4)) {
			a_error = "truncated groups";
			return false;
		}

		// A geometry block starts with its name, an index into the string table
		for (uint32_t i = 0; i < blockCount; ++i) {
			const auto start = reader.At();
			if (blockTypes[i] < geometryTypes.size() && geometryTypes[blockTypes[i]]) {
				uint32_t name = 0;
				if (reader.Read(name) && name < strings.size() && !strings[name].empty()) {
					a_out.push_back(strings[name]);
				}
			}
			if (!reader.Skip(start + blockSizes[i] - reader.At())) {
				a_error = std::format("truncated at block {} of {}", i, blockCount);
				return false;
			}
		}
		return true;
	}
	}

	bool ReadShapeNames(std::span<const std::byte> a_bytes, std::vector<std::string>& a_out, std::string& a_error)
	{
		return ReadNamesOf(a_bytes, IsGeometryType, a_out, a_error);
	}

	bool ReadNodeNames(std::span<const std::byte> a_bytes, std::vector<std::string>& a_out, std::string& a_error)
	{
		return ReadNamesOf(a_bytes, IsNodeType, a_out, a_error);
	}
}
