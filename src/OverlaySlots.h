#pragma once

#include "scan/Scan.h"

#include <rapidjson/document.h>

#include <string>
#include <string_view>
#include <vector>

// An overlay slot a mod defines: a named layer on one part, and the pool of overlays that can be put in it, each
// painted for the UV layouts it serves
namespace Overlays
{
	// One overlay's texture for one layout; a layout left off serves no actor of it
	struct Texture
	{
		std::string uv;
		bool bEnabled = false;
		std::string diffuse;  // relative to Data/Textures
		std::string normal;   // optional; empty borrows the skin's
	};

	// One overlay of the pool, by name, with a texture per layout of the slot's part
	struct Item
	{
		std::string name;
		bool bDisabled = false;  // kept in the file, out of the pool
		std::vector<Texture> textures;

		[[nodiscard]] bool IsValid() const;
		// The layouts set, as written, joined; "(none)" when none is
		[[nodiscard]] std::string LayoutsText() const;
		// The texture set for a layout, if one is
		[[nodiscard]] Texture* FindTexture(std::string_view a_uv);
		// The texture for a layout, added off when the item has none for it yet
		[[nodiscard]] Texture& TextureFor(std::string_view a_uv);
	};

	class Slot
	{
	public:
		explicit Slot(std::string_view a_name);

		[[nodiscard]] std::string_view GetName() const { return _name; }
		void SetName(std::string_view a_name) { _name = a_name; }

		[[nodiscard]] std::string_view GetDescription() const { return _description; }
		void SetDescription(std::string_view a_description) { _description = a_description; }

		[[nodiscard]] Scan::SkinPart GetPart() const { return _part; }
		// The part decides which layouts are listed; a texture set for another stays, flagged, until it is turned off
		void SetPart(Scan::SkinPart a_part) { _part = a_part; }

		[[nodiscard]] std::vector<Item>& GetItems() { return _items; }
		[[nodiscard]] const std::vector<Item>& GetItems() const { return _items; }
		[[nodiscard]] bool HasItem(std::string_view a_name) const;
		Item& AddItem(std::string_view a_name);
		void RemoveItem(std::string_view a_name);

		// A slot serves someone: an item with a layout checked and a texture named
		[[nodiscard]] bool IsValid() const;

		void Parse(const rapidjson::Value& a_value);
		[[nodiscard]] rapidjson::Value Serialize(rapidjson::Document::AllocatorType& a_allocator) const;

	private:
		std::string _name;
		std::string _description;
		Scan::SkinPart _part = Scan::SkinPart::kBody;
		std::vector<Item> _items;
	};

	// The layouts a part's textures are painted for: a head's own, and the body's for the body, hands and feet
	[[nodiscard]] std::vector<std::string> LayoutsOf(Scan::SkinPart a_part);

	// Every overlay of the slot of this name, across the mods defining one: the first defining mod's slot stands for it
	// (its part), its overlays first, then each other mod's in the registry's order; a_func gets the standing slot, the
	// mod an overlay comes from and the overlay, and returns false to stop
	void ForEachItem(std::string_view a_slot, const std::function<bool(Slot& a_slot, std::string_view a_mod, Item& a_item)>& a_func);
	// The slot standing for a name: the first registered mod's that defines one; nullptr when none does
	[[nodiscard]] Slot* SlotNamed(std::string_view a_name);
	// Once the mods are read: each name defined for different parts, said, the first mod's part used
	void WarnOfPartMismatches();

	// Why the item is not drawn on the actor, as the applier picks its texture: no texture for the layout of the slot's
	// part, or one turned off or naming nothing; empty when it is drawn
	[[nodiscard]] std::string WhySkipped(const Slot& a_slot, const Item& a_item, RE::Actor* a_actor);
}
