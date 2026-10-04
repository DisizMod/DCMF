#include "Xml.h"

#include <format>

namespace Scan::Xml
{
	std::string_view Node::Attribute(std::string_view a_name) const
	{
		for (const auto& [name, value] : attributes) {
			if (name == a_name) {
				return value;
			}
		}
		return {};
	}

	const Node* Node::Child(std::string_view a_name) const
	{
		for (const auto& child : children) {
			if (child.name == a_name) {
				return &child;
			}
		}
		return nullptr;
	}

	namespace
	{
		class Parser
		{
		public:
			explicit Parser(std::string_view a_text) :
				_text(a_text) {}

			bool ParseDocument(Node& a_root)
			{
				SkipMisc();
				if (!ParseElement(a_root)) {
					return false;
				}
				return true;
			}

			[[nodiscard]] std::string Error() const
			{
				return std::format("{} at offset {}", _error, _pos);
			}

		private:
			[[nodiscard]] bool AtEnd() const { return _pos >= _text.size(); }
			[[nodiscard]] bool StartsWith(std::string_view a_prefix) const { return _text.substr(_pos).starts_with(a_prefix); }

			void SkipSpace()
			{
				while (!AtEnd() && std::isspace(static_cast<unsigned char>(_text[_pos]))) {
					++_pos;
				}
			}

			bool SkipPast(std::string_view a_end)
			{
				const auto at = _text.find(a_end, _pos);
				if (at == std::string_view::npos) {
					_pos = _text.size();
					return false;
				}
				_pos = at + a_end.size();
				return true;
			}

			// Declarations, comments and doctypes between elements
			void SkipMisc()
			{
				while (true) {
					SkipSpace();
					if (StartsWith("<?")) {
						SkipPast("?>");
					} else if (StartsWith("<!--")) {
						SkipPast("-->");
					} else if (StartsWith("<!")) {
						SkipPast(">");
					} else {
						return;
					}
				}
			}

			std::string ParseName()
			{
				const auto start = _pos;
				while (!AtEnd()) {
					const char c = _text[_pos];
					if (std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '/' || c == '=') {
						break;
					}
					++_pos;
				}
				return std::string(_text.substr(start, _pos - start));
			}

			static std::string Decode(std::string_view a_raw)
			{
				std::string out;
				out.reserve(a_raw.size());
				for (std::size_t i = 0; i < a_raw.size(); ++i) {
					if (a_raw[i] != '&') {
						out += a_raw[i];
						continue;
					}
					const auto end = a_raw.find(';', i);
					if (end == std::string_view::npos) {
						out += a_raw[i];
						continue;
					}
					const auto entity = a_raw.substr(i + 1, end - i - 1);
					if (entity == "amp") {
						out += '&';
					} else if (entity == "lt") {
						out += '<';
					} else if (entity == "gt") {
						out += '>';
					} else if (entity == "quot") {
						out += '"';
					} else if (entity == "apos") {
						out += '\'';
					} else if (entity.starts_with("#")) {
						const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
						const auto code = std::strtoul(std::string(entity.substr(hex ? 2 : 1)).c_str(), nullptr, hex ? 16 : 10);
						out += code < 0x80 ? static_cast<char>(code) : '?';
					} else {
						out += a_raw.substr(i, end - i + 1);
					}
					i = end;
				}
				return out;
			}

			bool ParseElement(Node& a_node)
			{
				if (AtEnd() || _text[_pos] != '<') {
					_error = "expected an element";
					return false;
				}
				++_pos;
				a_node.name = ParseName();
				if (a_node.name.empty()) {
					_error = "an element without a name";
					return false;
				}

				// Attributes
				while (true) {
					SkipSpace();
					if (AtEnd()) {
						_error = "the file ends inside a tag";
						return false;
					}
					if (StartsWith("/>")) {
						_pos += 2;
						return true;
					}
					if (_text[_pos] == '>') {
						++_pos;
						break;
					}
					auto name = ParseName();
					SkipSpace();
					if (AtEnd() || _text[_pos] != '=') {
						_error = std::format("attribute '{}' has no value", name);
						return false;
					}
					++_pos;
					SkipSpace();
					if (AtEnd() || (_text[_pos] != '"' && _text[_pos] != '\'')) {
						_error = std::format("attribute '{}' is not quoted", name);
						return false;
					}
					const char quote = _text[_pos++];
					const auto end = _text.find(quote, _pos);
					if (end == std::string_view::npos) {
						_error = "an attribute value runs to the end";
						return false;
					}
					a_node.attributes.emplace_back(std::move(name), Decode(_text.substr(_pos, end - _pos)));
					_pos = end + 1;
				}

				// Content
				while (true) {
					if (AtEnd()) {
						_error = std::format("'{}' is never closed", a_node.name);
						return false;
					}
					if (StartsWith("</")) {
						_pos += 2;
						const auto name = ParseName();
						if (name != a_node.name) {
							_error = std::format("'{}' is closed by '{}'", a_node.name, name);
							return false;
						}
						SkipSpace();
						if (AtEnd() || _text[_pos] != '>') {
							_error = "a closing tag is not closed";
							return false;
						}
						++_pos;
						const auto first = a_node.text.find_first_not_of(" \t\r\n");
						a_node.text = first == std::string::npos ? std::string{} : a_node.text.substr(first, a_node.text.find_last_not_of(" \t\r\n") - first + 1);
						return true;
					}
					if (StartsWith("<!--")) {
						SkipPast("-->");
					} else if (StartsWith("<![CDATA[")) {
						const auto start = _pos + 9;
						SkipPast("]]>");
						a_node.text += _text.substr(start, _pos - 3 - start);
					} else if (StartsWith("<?")) {
						SkipPast("?>");
					} else if (_text[_pos] == '<') {
						Node child;
						if (!ParseElement(child)) {
							return false;
						}
						a_node.children.push_back(std::move(child));
					} else {
						const auto end = _text.find('<', _pos);
						const auto raw = _text.substr(_pos, end == std::string_view::npos ? std::string_view::npos : end - _pos);
						a_node.text += Decode(raw);
						_pos = end == std::string_view::npos ? _text.size() : end;
					}
				}
			}

			std::string_view _text;
			std::size_t _pos = 0;
			std::string _error;
		};
	}

	bool Parse(std::string_view a_text, Node& a_root, std::string& a_error)
	{
		if (a_text.starts_with("\xEF\xBB\xBF")) {
			a_text.remove_prefix(3);
		}
		Parser parser(a_text);
		if (!parser.ParseDocument(a_root)) {
			a_error = parser.Error();
			return false;
		}
		return true;
	}
}
