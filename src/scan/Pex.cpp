#include "Pex.h"

#include <array>
#include <cstring>
#include <format>

namespace Scan::Pex
{
	namespace
	{
		// Fixed arguments per opcode, and whether a counted list follows them
		struct OpcodeShape
		{
			uint8_t fixed;
			bool variadic;
		};

		constexpr std::array<OpcodeShape, 36> kOpcodes{ {
			{ 0, false },                                                                                                 // nop
			{ 3, false }, { 3, false }, { 3, false }, { 3, false }, { 3, false }, { 3, false }, { 3, false }, { 3, false },  // iadd .. fdiv
			{ 3, false },                                                                                                 // imod
			{ 2, false }, { 2, false }, { 2, false }, { 2, false }, { 2, false },                                          // not, ineg, fneg, assign, cast
			{ 3, false }, { 3, false }, { 3, false }, { 3, false }, { 3, false },                                          // cmp_eq .. cmp_gte
			{ 1, false }, { 2, false }, { 2, false },                                                                     // jmp, jmpt, jmpf
			{ 3, true }, { 2, true }, { 3, true },                                                                        // callmethod, callparent, callstatic
			{ 1, false }, { 3, false }, { 3, false }, { 3, false },                                                       // return, strcat, propget, propset
			{ 2, false }, { 2, false }, { 3, false }, { 3, false }, { 4, false }, { 4, false }                             // array_create .. array_rfindelement
		} };

		class Reader
		{
		public:
			Reader(std::span<const std::byte> a_bytes, const std::vector<std::string>& a_strings) :
				_bytes(a_bytes), _strings(a_strings) {}

			[[nodiscard]] bool Ok() const { return _bOk; }
			void Fail() { _bOk = false; }

			template <class T>
			T Read()
			{
				T value{};
				if (_bytes.size() - _offset < sizeof(T)) {
					_bOk = false;
					_offset = _bytes.size();
					return value;
				}
				std::array<std::byte, sizeof(T)> raw;
				std::memcpy(raw.data(), _bytes.data() + _offset, sizeof(T));
				std::reverse(raw.begin(), raw.end());
				std::memcpy(&value, raw.data(), sizeof(T));
				_offset += sizeof(T);
				return value;
			}

			std::string ReadWString()
			{
				const auto length = Read<uint16_t>();
				if (_bytes.size() - _offset < length) {
					_bOk = false;
					_offset = _bytes.size();
					return {};
				}
				std::string out(reinterpret_cast<const char*>(_bytes.data() + _offset), length);
				_offset += length;
				return out;
			}

			std::string_view ReadStringRef()
			{
				const auto index = Read<uint16_t>();
				if (index >= _strings.size()) {
					_bOk = false;
					return {};
				}
				return _strings[index];
			}

			Value ReadValue()
			{
				Value value;
				switch (Read<uint8_t>()) {
				case 0:
					break;
				case 1:
					value.type = Value::Type::kIdentifier;
					value.text = ReadStringRef();
					break;
				case 2:
					value.type = Value::Type::kString;
					value.text = ReadStringRef();
					break;
				case 3:
					value.type = Value::Type::kInt;
					value.integer = Read<int32_t>();
					break;
				case 4:
					value.type = Value::Type::kFloat;
					value.real = Read<float>();
					break;
				case 5:
					value.type = Value::Type::kBool;
					value.integer = Read<uint8_t>();
					break;
				default:
					_bOk = false;
					break;
				}
				return value;
			}

		private:
			std::span<const std::byte> _bytes;
			const std::vector<std::string>& _strings;
			std::size_t _offset = 0;
			bool _bOk = true;
		};

		void ReadFunction(Reader& a_in, Function& a_out)
		{
			a_in.Read<uint16_t>();  // return type
			a_in.Read<uint16_t>();  // doc
			a_in.Read<uint32_t>();  // user flags
			a_in.Read<uint8_t>();   // flags
			for (auto count = a_in.Read<uint16_t>(); count > 0 && a_in.Ok(); --count) {
				a_in.Read<uint16_t>();  // parameter name
				a_in.Read<uint16_t>();  // parameter type
			}
			for (auto count = a_in.Read<uint16_t>(); count > 0 && a_in.Ok(); --count) {
				a_in.Read<uint16_t>();  // local name
				a_in.Read<uint16_t>();  // local type
			}
			for (auto count = a_in.Read<uint16_t>(); count > 0 && a_in.Ok(); --count) {
				Instruction instruction;
				instruction.opcode = a_in.Read<uint8_t>();
				if (instruction.opcode >= kOpcodes.size()) {
					a_in.Fail();
					return;
				}
				const auto shape = kOpcodes[instruction.opcode];
				for (uint8_t i = 0; i < shape.fixed; ++i) {
					instruction.arguments.push_back(a_in.ReadValue());
				}
				if (shape.variadic) {
					const auto extra = a_in.ReadValue();
					for (int32_t i = 0; i < extra.integer && a_in.Ok(); ++i) {
						instruction.arguments.push_back(a_in.ReadValue());
					}
				}
				a_out.code.push_back(std::move(instruction));
			}
		}
	}

	bool Read(std::span<const std::byte> a_bytes, Script& a_out, std::string& a_error)
	{
		a_out = {};

		Reader in(a_bytes, a_out.strings);
		if (in.Read<uint32_t>() != 0xFA57C0DE) {
			a_error = "not a compiled Papyrus script";
			return false;
		}
		in.Read<uint8_t>();   // major version
		in.Read<uint8_t>();   // minor version
		in.Read<uint16_t>();  // game
		in.Read<uint64_t>();  // compilation time
		in.ReadWString();     // source
		in.ReadWString();     // user
		in.ReadWString();     // machine

		for (auto count = in.Read<uint16_t>(); count > 0 && in.Ok(); --count) {
			a_out.strings.push_back(in.ReadWString());
		}

		if (in.Read<uint8_t>() != 0) {
			in.Read<uint64_t>();  // modification time
			for (auto count = in.Read<uint16_t>(); count > 0 && in.Ok(); --count) {
				in.Read<uint16_t>();  // object
				in.Read<uint16_t>();  // state
				in.Read<uint16_t>();  // function
				in.Read<uint8_t>();   // type
				for (auto lines = in.Read<uint16_t>(); lines > 0 && in.Ok(); --lines) {
					in.Read<uint16_t>();
				}
			}
		}

		for (auto count = in.Read<uint16_t>(); count > 0 && in.Ok(); --count) {
			in.Read<uint16_t>();  // user flag name
			in.Read<uint8_t>();   // user flag index
		}

		for (auto objects = in.Read<uint16_t>(); objects > 0 && in.Ok(); --objects) {
			a_out.name = in.ReadStringRef();
			in.Read<uint32_t>();  // size
			a_out.parent = in.ReadStringRef();
			in.Read<uint16_t>();  // doc
			in.Read<uint32_t>();  // user flags
			in.Read<uint16_t>();  // auto state

			for (auto count = in.Read<uint16_t>(); count > 0 && in.Ok(); --count) {
				in.Read<uint16_t>();  // name
				in.Read<uint16_t>();  // type
				in.Read<uint32_t>();  // user flags
				in.ReadValue();
			}

			for (auto count = in.Read<uint16_t>(); count > 0 && in.Ok(); --count) {
				Function getter;
				Function setter;
				getter.name = in.ReadStringRef();
				setter.name = getter.name;
				in.Read<uint16_t>();  // type
				in.Read<uint16_t>();  // doc
				in.Read<uint32_t>();  // user flags
				const auto flags = in.Read<uint8_t>();
				if (flags & 4) {
					in.Read<uint16_t>();  // auto variable
					continue;
				}
				if (flags & 1) {
					ReadFunction(in, getter);
					a_out.functions.push_back(std::move(getter));
				}
				if (flags & 2) {
					ReadFunction(in, setter);
					a_out.functions.push_back(std::move(setter));
				}
			}

			for (auto states = in.Read<uint16_t>(); states > 0 && in.Ok(); --states) {
				const auto state = in.ReadStringRef();
				for (auto count = in.Read<uint16_t>(); count > 0 && in.Ok(); --count) {
					Function function;
					function.state = state;
					function.name = in.ReadStringRef();
					ReadFunction(in, function);
					a_out.functions.push_back(std::move(function));
				}
			}
		}

		if (!in.Ok()) {
			a_error = "the script ends early or is not laid out as expected";
			return false;
		}
		return true;
	}
}
