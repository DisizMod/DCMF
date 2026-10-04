#include "SkinSets.h"

#include "ModRegistry.h"
#include "RegisteredMods.h"

namespace Skins
{
	std::string_view MapName(Map a_map)
	{
		switch (a_map) {
		case Map::kDiffuse:
			return "diffuse"sv;
		case Map::kNormal:
			return "normal"sv;
		case Map::kSpecular:
			return "specular"sv;
		case Map::kSubsurface:
			return "subsurface"sv;
		default:
			return ""sv;
		}
	}

	bool Textures::Any() const
	{
		return std::ranges::any_of(maps, &Texture::IsStated);
	}

	std::string Textures::MapsText() const
	{
		std::string out;
		for (std::uint8_t m = 0; m < static_cast<std::uint8_t>(Map::kTotal); ++m) {
			if (maps[m].IsStated()) {
				out += (out.empty() ? "" : ", ") + std::string(MapName(static_cast<Map>(m)));
			}
		}
		return out;
	}

	Set::Set(std::string_view a_name) :
		_name(a_name)
	{}

	bool Set::IsValid() const
	{
		return std::ranges::any_of(_textures, &Textures::Any);
	}

	std::string Set::StatedText() const
	{
		std::string out;
		for (std::uint8_t p = 0; p < static_cast<std::uint8_t>(Scan::SkinPart::kTotal); ++p) {
			const auto& textures = _textures[p];
			if (textures.Any()) {
				out += std::format("{}{}: {}", out.empty() ? "" : "; ", Scan::SkinPartName(static_cast<Scan::SkinPart>(p)), textures.MapsText());
			}
		}
		return out.empty() ? "(none)" : out;
	}

	void Set::Parse(const rapidjson::Value& a_value)
	{
		if (!a_value.IsObject()) {
			return;
		}
		const auto object = a_value.GetObj();
		if (const auto it = object.FindMember("description"); it != object.MemberEnd() && it->value.IsString()) {
			_description = it->value.GetString();
		}
		if (const auto it = object.FindMember("disabled"); it != object.MemberEnd() && it->value.IsBool()) {
			_bDisabled = it->value.GetBool();
		}
		if (const auto it = object.FindMember("textures"); it != object.MemberEnd() && it->value.IsObject()) {
			for (const auto& member : it->value.GetObj()) {
				if (!member.value.IsObject()) {
					continue;
				}
				const std::string_view partName = member.name.GetString();
				for (std::uint8_t p = 0; p < static_cast<std::uint8_t>(Scan::SkinPart::kTotal); ++p) {
					if (Scan::SkinPartName(static_cast<Scan::SkinPart>(p)) != partName) {
						continue;
					}
					auto& textures = _textures[p];
					for (std::uint8_t m = 0; m < static_cast<std::uint8_t>(Map::kTotal); ++m) {
						const auto name = MapName(static_cast<Map>(m));
						if (const auto path = member.value.FindMember(name.data()); path != member.value.MemberEnd() && path->value.IsString()) {
							textures.maps[m] = { .bEnabled = true, .path = path->value.GetString() };
						}
					}
				}
			}
		}
	}

	rapidjson::Value Set::Serialize(rapidjson::Document::AllocatorType& a_allocator) const
	{
		rapidjson::Value object(rapidjson::kObjectType);
		object.AddMember("name", rapidjson::Value(_name.data(), a_allocator), a_allocator);
		if (!_description.empty()) {
			object.AddMember("description", rapidjson::Value(_description.data(), a_allocator), a_allocator);
		}
		if (_bDisabled) {
			object.AddMember("disabled", true, a_allocator);
		}
		rapidjson::Value textures(rapidjson::kObjectType);
		for (std::uint8_t p = 0; p < static_cast<std::uint8_t>(Scan::SkinPart::kTotal); ++p) {
			const auto& part = _textures[p];
			if (!part.Any()) {
				continue;
			}
			rapidjson::Value maps(rapidjson::kObjectType);
			for (std::uint8_t m = 0; m < static_cast<std::uint8_t>(Map::kTotal); ++m) {
				if (part.maps[m].IsStated()) {
					const auto name = MapName(static_cast<Map>(m));
					maps.AddMember(rapidjson::Value(name.data(), static_cast<rapidjson::SizeType>(name.size()), a_allocator), rapidjson::Value(part.maps[m].path.data(), a_allocator), a_allocator);
				}
			}
			const auto partName = Scan::SkinPartName(static_cast<Scan::SkinPart>(p));
			textures.AddMember(rapidjson::Value(partName.data(), static_cast<rapidjson::SizeType>(partName.size()), a_allocator), maps, a_allocator);
		}
		object.AddMember("textures", textures, a_allocator);
		return object;
	}

	std::vector<Listed> ListSets()
	{
		std::vector<Listed> out;
		ModRegistry::GetSingleton().ForEachRegisteredMod([&](RegisteredMod* a_mod) {
			a_mod->ForEachSkinSet([&](Set* a_set) {
				if (!a_set->IsDisabled()) {
					out.push_back({ a_mod->GetName(), a_set });
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		});
		return out;
	}
}
