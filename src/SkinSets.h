#pragma once

#include "scan/Scan.h"

#include <rapidjson/document.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

// A skin set a mod defines: the skin's own maps replaced on the parts it states, as written, the layout being the
// author's to condition on; a part or a map left off is the actor's own
namespace Skins
{
	// The four maps of a skin part, in the order the applier holds them
	enum class Map : std::uint8_t
	{
		kDiffuse,
		kNormal,      // _msn
		kSpecular,    // _s
		kSubsurface,  // _sk

		kTotal
	};

	[[nodiscard]] std::string_view MapName(Map a_map);

	// One map of a part, relative to Data/Textures; off, or empty, it is not touched
	struct Texture
	{
		bool bEnabled = false;
		std::string path;

		[[nodiscard]] bool IsStated() const { return bEnabled && !path.empty(); }
	};

	// One part's maps
	struct Textures
	{
		std::array<Texture, static_cast<std::size_t>(Map::kTotal)> maps;

		[[nodiscard]] Texture& Get(Map a_map) { return maps[static_cast<std::size_t>(a_map)]; }
		[[nodiscard]] const Texture& Get(Map a_map) const { return maps[static_cast<std::size_t>(a_map)]; }
		[[nodiscard]] bool Any() const;
		// The maps stated, joined: "diffuse, normal"
		[[nodiscard]] std::string MapsText() const;
	};

	class Set
	{
	public:
		explicit Set(std::string_view a_name);

		[[nodiscard]] std::string_view GetName() const { return _name; }
		void SetName(std::string_view a_name) { _name = a_name; }

		[[nodiscard]] std::string_view GetDescription() const { return _description; }
		void SetDescription(std::string_view a_description) { _description = a_description; }

		[[nodiscard]] bool IsDisabled() const { return _bDisabled; }
		void SetDisabled(bool a_bDisabled) { _bDisabled = a_bDisabled; }

		[[nodiscard]] Textures& GetTextures(Scan::SkinPart a_part) { return _textures[static_cast<std::size_t>(a_part)]; }
		[[nodiscard]] const Textures& GetTextures(Scan::SkinPart a_part) const { return _textures[static_cast<std::size_t>(a_part)]; }

		// A set says something: a part on with a map named
		[[nodiscard]] bool IsValid() const;
		// The parts stated, each with its maps: "Body: diffuse, normal; Hands: diffuse"; "(none)" when none
		[[nodiscard]] std::string StatedText() const;

		void Parse(const rapidjson::Value& a_value);
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const;

	private:
		std::string _name;
		std::string _description;
		bool _bDisabled = false;  // kept in the file, out of the pickers
		std::array<Textures, static_cast<std::size_t>(Scan::SkinPart::kTotal)> _textures;
	};

	// A set as the pickers list it, with the mod that defines it
	struct Listed
	{
		std::string_view mod;
		Set* set;
	};

	// Every enabled set of every registered mod, in the order the pickers list them; a pick counts that order
	[[nodiscard]] std::vector<Listed> ListSets();
}
