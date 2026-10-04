#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Scan::Xml
{
	// Only what BodySlide's files use: elements, attributes, text, comments and the declaration
	struct Node
	{
		std::string name;
		std::vector<std::pair<std::string, std::string>> attributes;
		std::string text;
		std::vector<Node> children;

		[[nodiscard]] std::string_view Attribute(std::string_view a_name) const;
		[[nodiscard]] const Node* Child(std::string_view a_name) const;
	};

	// The document's root element; false with a reason when the text is not well formed
	[[nodiscard]] bool Parse(std::string_view a_text, Node& a_root, std::string& a_error);
}
