#include "OverlaySlots.h"

#include "BodyTypes.h"
#include "ModRegistry.h"
#include "Utils.h"

namespace Overlays
{
	std::vector<std::string> LayoutsOf(Scan::SkinPart a_part)
	{
		return BodyTypes::Layouts(a_part == Scan::SkinPart::kHead ? Scan::SkinPart::kHead : Scan::SkinPart::kBody);
	}

	bool Item::IsValid() const
	{
		return std::ranges::any_of(textures, [](const Texture& a_texture) { return a_texture.bEnabled && !a_texture.diffuse.empty(); });
	}

	std::string Item::LayoutsText() const
	{
		std::string out;
		for (const auto& texture : textures) {
			if (texture.bEnabled) {
				out += out.empty() ? texture.uv : ", " + texture.uv;
			}
		}
		return out.empty() ? "(none)" : out;
	}

	Texture* Item::FindTexture(std::string_view a_uv)
	{
		const auto it = std::ranges::find_if(textures, [&](const Texture& a_texture) { return Utils::CompareStringsIgnoreCase(a_texture.uv, a_uv); });
		return it != textures.end() ? &*it : nullptr;
	}

	std::string WhySkipped(const Slot& a_slot, const Item& a_item, RE::Actor* a_actor)
	{
		auto* base = a_actor ? a_actor->GetActorBase() : nullptr;
		if (!base) {
			return {};
		}
		const bool bHead = a_slot.GetPart() == Scan::SkinPart::kHead;
		const auto path = bHead ? BodyTypes::HeadPathOf(a_actor) : BodyTypes::BodyPathOf(a_actor);
		const auto uv = BodyTypes::UvOf(bHead ? Scan::SkinPart::kHead : Scan::SkinPart::kBody, path, base->GetSex() == RE::SEX::kFemale);
		const auto it = std::ranges::find_if(a_item.textures, [&](const Texture& a_texture) { return Utils::CompareStringsIgnoreCase(a_texture.uv, uv); });
		const auto* who = a_actor->GetDisplayFullName();
		if (it == a_item.textures.end()) {
			return std::format("Not drawn on {}: it has no texture for the {} layout of {}'s {}. '{}' has textures for: {}.", who, uv, who, bHead ? "head" : "body", a_item.name, a_item.LayoutsText());
		}
		if (!it->bEnabled) {
			return std::format("Not drawn on {}: its {} texture, the layout of {}'s {}, is turned off.", who, uv, who, bHead ? "head" : "body");
		}
		if (it->diffuse.empty()) {
			return std::format("Not drawn on {}: its {} texture, the layout of {}'s {}, names no diffuse.", who, uv, who, bHead ? "head" : "body");
		}
		return {};
	}

	Texture& Item::TextureFor(std::string_view a_uv)
	{
		if (auto* texture = FindTexture(a_uv)) {
			return *texture;
		}
		return textures.emplace_back(Texture{ .uv = std::string(a_uv) });
	}

	Slot::Slot(std::string_view a_name) :
		_name(a_name)
	{}

	bool Slot::HasItem(std::string_view a_name) const
	{
		return std::ranges::find(_items, a_name, &Item::name) != _items.end();
	}

	Item& Slot::AddItem(std::string_view a_name)
	{
		return _items.emplace_back(Item{ .name = std::string(a_name) });
	}

	void Slot::RemoveItem(std::string_view a_name)
	{
		std::erase_if(_items, [&](const Item& a_item) { return a_item.name == a_name; });
	}

	bool Slot::IsValid() const
	{
		return std::ranges::any_of(_items, [](const Item& a_item) { return !a_item.bDisabled && a_item.IsValid(); });
	}

	void Slot::Parse(const rapidjson::Value& a_value)
	{
		if (!a_value.IsObject()) {
			return;
		}
		const auto object = a_value.GetObj();
		if (const auto it = object.FindMember("part"); it != object.MemberEnd() && it->value.IsString()) {
			const std::string_view name = it->value.GetString();
			for (std::uint8_t p = 0; p < static_cast<std::uint8_t>(Scan::SkinPart::kTotal); ++p) {
				if (Scan::SkinPartName(static_cast<Scan::SkinPart>(p)) == name) {
					_part = static_cast<Scan::SkinPart>(p);
				}
			}
		}
		if (const auto it = object.FindMember("description"); it != object.MemberEnd() && it->value.IsString()) {
			_description = it->value.GetString();
		}
		if (const auto it = object.FindMember("overlays"); it != object.MemberEnd() && it->value.IsArray()) {
			for (const auto& entry : it->value.GetArray()) {
				if (!entry.IsObject()) {
					continue;
				}
				const auto name = entry.FindMember("name");
				if (name == entry.MemberEnd() || !name->value.IsString()) {
					continue;
				}
				auto& item = AddItem(name->value.GetString());
				if (const auto disabled = entry.FindMember("disabled"); disabled != entry.MemberEnd() && disabled->value.IsBool()) {
					item.bDisabled = disabled->value.GetBool();
				}
				if (const auto textures = entry.FindMember("textures"); textures != entry.MemberEnd() && textures->value.IsObject()) {
					for (const auto& member : textures->value.GetObj()) {
						if (!member.value.IsObject()) {
							continue;
						}
						auto& texture = item.TextureFor(member.name.GetString());
						texture.bEnabled = true;
						if (const auto diffuse = member.value.FindMember("diffuse"); diffuse != member.value.MemberEnd() && diffuse->value.IsString()) {
							texture.diffuse = diffuse->value.GetString();
						}
						if (const auto normal = member.value.FindMember("normal"); normal != member.value.MemberEnd() && normal->value.IsString()) {
							texture.normal = normal->value.GetString();
						}
					}
				}
			}
		}
	}

	rapidjson::Value Slot::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
	{
		rapidjson::Value object(rapidjson::kObjectType);
		object.AddMember("name", rapidjson::Value(_name.data(), a_allocator), a_allocator);
		if (!_description.empty()) {
			object.AddMember("description", rapidjson::Value(_description.data(), a_allocator), a_allocator);
		}
		const auto part = Scan::SkinPartName(_part);
		object.AddMember("part", rapidjson::Value(part.data(), static_cast<rapidjson::SizeType>(part.size()), a_allocator), a_allocator);
		rapidjson::Value overlays(rapidjson::kArrayType);
		for (const auto& item : _items) {
			rapidjson::Value entry(rapidjson::kObjectType);
			entry.AddMember("name", rapidjson::Value(item.name.data(), a_allocator), a_allocator);
			if (item.bDisabled) {
				entry.AddMember("disabled", true, a_allocator);
			}
			rapidjson::Value textures(rapidjson::kObjectType);
			for (const auto& texture : item.textures) {
				if (!texture.bEnabled) {
					continue;
				}
				rapidjson::Value one(rapidjson::kObjectType);
				one.AddMember("diffuse", rapidjson::Value(texture.diffuse.data(), a_allocator), a_allocator);
				if (!texture.normal.empty()) {
					one.AddMember("normal", rapidjson::Value(texture.normal.data(), a_allocator), a_allocator);
				}
				textures.AddMember(rapidjson::Value(texture.uv.data(), a_allocator), one, a_allocator);
			}
			entry.AddMember("textures", textures, a_allocator);
			overlays.PushBack(entry, a_allocator);
		}
		object.AddMember("overlays", overlays, a_allocator);
		return object;
	}

	Slot* SlotNamed(std::string_view a_name)
	{
		Slot* found = nullptr;
		ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_mod) {
			if (!found) {
				found = a_mod->GetOverlaySlot(a_name);
			}
		});
		return found;
	}

	void ForEachItem(std::string_view a_slot, const std::function<bool(Slot& a_slot, std::string_view a_mod, Item& a_item)>& a_func)
	{
		auto* standing = SlotNamed(a_slot);
		if (!standing) {
			return;
		}
		bool bStopped = false;
		ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_mod) {
			auto* slot = bStopped ? nullptr : a_mod->GetOverlaySlot(a_slot);
			if (!slot) {
				return;
			}
			for (auto& item : slot->GetItems()) {
				if (!a_func(*standing, a_mod->GetName(), item)) {
					bStopped = true;
					return;
				}
			}
		});
	}

	void WarnOfPartMismatches()
	{
		std::unordered_map<std::string, std::pair<std::string, Scan::SkinPart>> first;
		ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_mod) {
			a_mod->ForEachOverlaySlot([&](Slot* a_slot) {
				const auto [it, bAdded] = first.try_emplace(std::string(a_slot->GetName()), std::string(a_mod->GetName()), a_slot->GetPart());
				if (!bAdded && it->second.second != a_slot->GetPart()) {
					logger::warn("overlay slot '{}': '{}' defines it on {}, '{}' on {}; one slot of a name is one slot, on the first mod's part, {}",
						a_slot->GetName(), it->second.first, Scan::SkinPartName(it->second.second), a_mod->GetName(), Scan::SkinPartName(a_slot->GetPart()), Scan::SkinPartName(it->second.second));
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		});
	}
}
