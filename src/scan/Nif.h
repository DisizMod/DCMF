#pragma once

#include <span>
#include <string>
#include <vector>

namespace Scan::Nif
{
	// The names of the geometry blocks of a Skyrim SE mesh, in block order; false with why when the bytes are not one
	bool ReadShapeNames(std::span<const std::byte> a_bytes, std::vector<std::string>& a_out, std::string& a_error);

	// The names of its node blocks, a skeleton's bones, in block order
	bool ReadNodeNames(std::span<const std::byte> a_bytes, std::vector<std::string>& a_out, std::string& a_error);
}
