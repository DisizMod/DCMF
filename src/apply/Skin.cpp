#include "Skin.h"

#include "ActorState.h"
#include "Overlay.h"
#include "SkinSets.h"

namespace Apply
{
	namespace
	{
		constexpr std::size_t kMaps = static_cast<std::size_t>(Skins::Map::kTotal);

		RE::BSLightingShaderMaterialBase* MaterialOf(RE::BSGeometry* a_geometry)
		{
			auto& runtime = a_geometry->GetGeometryRuntimeData();
			auto* shader = runtime.shaderProperty ? netimmerse_cast<RE::BSLightingShaderProperty*>(runtime.shaderProperty.get()) : nullptr;
			return shader ? static_cast<RE::BSLightingShaderMaterialBase*>(shader->material) : nullptr;
		}

		bool Under(const RE::NiAVObject* a_object, const RE::NiAVObject* a_ancestor)
		{
			for (const auto* at = a_object; at; at = at->parent) {
				if (at == a_ancestor) {
					return true;
				}
			}
			return false;
		}

		bool IsOverlayName(const RE::NiAVObject* a_object)
		{
			const std::string_view name = a_object && a_object->name.c_str() ? a_object->name.c_str() : "";
			return name.find("[Ovl") != std::string_view::npos || name.find("[SOvl") != std::string_view::npos || name.starts_with(Overlay::kNodePrefix);
		}

		// The material's slot for a map: the skin's four on the base, the face's subsurface on its own material
		RE::NiPointer<RE::NiSourceTexture>* SlotOf(RE::BSLightingShaderMaterialBase* a_material, bool a_bFace, Skins::Map a_map)
		{
			switch (a_map) {
			case Skins::Map::kDiffuse:
				return a_bFace ? nullptr : &a_material->diffuseTexture;  // the face's diffuse is the FaceGen bake
			case Skins::Map::kNormal:
				return &a_material->normalTexture;
			case Skins::Map::kSpecular:
				return &a_material->specularBackLightingTexture;
			case Skins::Map::kSubsurface:
				return a_bFace ? &static_cast<RE::BSLightingShaderMaterialFacegen*>(a_material)->subsurfaceTexture : &a_material->rimSoftLightingTexture;
			default:
				return nullptr;
			}
		}

		// Every skin shape by the biped slot it hangs under, and the FaceGen shape under the face node, with what each holds
		void Bind(SkinData& a_data, RE::Actor* a_actor)
		{
			a_data.targets.clear();
			auto* faceNode = a_actor->GetFaceNodeSkinned();

			const auto hold = [&](SkinTarget& a_target) {
				auto* material = MaterialOf(a_target.geometry.get());
				for (std::size_t m = 0; m < kMaps; ++m) {
					if (auto* slot = material ? SlotOf(material, a_target.bFace, static_cast<Skins::Map>(m)) : nullptr) {
						a_target.own[m] = *slot;
					}
				}
			};

			const auto visit = [&](auto& a_self, RE::NiAVObject* a_object, Scan::SkinPart a_part) -> void {
				if (!a_object || a_object == faceNode) {
					return;
				}
				if (auto* geometry = a_object->AsGeometry()) {
					auto* material = MaterialOf(geometry);
					if (!material || IsOverlayName(a_object) || !geometry->GetGeometryRuntimeData().skinInstance || material->GetFeature() != RE::BSShaderMaterial::Feature::kFaceGenRGBTint) {
						return;
					}
					SkinTarget target;
					target.geometry = RE::NiPointer(geometry);
					target.part = a_part;
					hold(target);
					a_data.targets.push_back(std::move(target));
					return;
				}
				if (auto* node = a_object->AsNode()) {
					for (const auto& child : node->GetChildren()) {
						a_self(a_self, child.get(), a_part);
					}
				}
			};
			if (const auto& biped = a_actor->GetCurrentBiped()) {
				for (const auto [slot, part] : { std::pair{ RE::BIPED_OBJECT::kBody, Scan::SkinPart::kBody }, std::pair{ RE::BIPED_OBJECT::kHands, Scan::SkinPart::kHands }, std::pair{ RE::BIPED_OBJECT::kFeet, Scan::SkinPart::kFeet } }) {
					visit(visit, biped->objects[slot].partClone.get(), part);
				}
			}

			if (faceNode) {
				for (const auto& child : faceNode->GetChildren()) {
					auto* geometry = child ? child->AsGeometry() : nullptr;
					auto* material = geometry ? MaterialOf(geometry) : nullptr;
					if (!material || IsOverlayName(child.get()) || material->GetFeature() != RE::BSShaderMaterial::Feature::kFaceGen) {
						continue;
					}
					SkinTarget target;
					target.geometry = RE::NiPointer(geometry);
					target.part = Scan::SkinPart::kHead;
					target.bFace = true;
					hold(target);
					a_data.targets.push_back(std::move(target));
				}
			}
		}

		// A map loaded the way the engine loads a set's; null when it would not, and the slot is left alone
		RE::NiPointer<RE::NiSourceTexture> Load(const std::string& a_path)
		{
			RE::NiPointer<RE::NiSourceTexture> loaded;
			std::string full = a_path;
			std::ranges::replace(full, '/', '\\');
			if (full.size() <= 9 || _strnicmp(full.c_str(), "textures\\", 9) != 0) {
				full = "textures\\" + full;
			}
			auto set = RE::NiPointer<RE::BSTextureSet>{ RE::BSShaderTextureSet::Create() };
			set->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, full.c_str());
			set->SetTexture(RE::BSTextureSet::Texture::kDiffuse, loaded);
			return loaded;
		}

		// The set's stated maps loaded and put on every target of the part; a map it leaves gets the original back
		void Wear(SkinData& a_data, const Skins::Set& a_set, RE::FormID a_actor)
		{
			std::array<std::array<RE::NiPointer<RE::NiSourceTexture>, kMaps>, static_cast<std::size_t>(Scan::SkinPart::kTotal)> loaded;
			for (std::uint8_t p = 0; p < static_cast<std::uint8_t>(Scan::SkinPart::kTotal); ++p) {
				const auto& textures = a_set.GetTextures(static_cast<Scan::SkinPart>(p));
				for (std::size_t m = 0; m < kMaps; ++m) {
					const auto& [bEnabled, path] = textures.maps[m];
					if (!bEnabled || path.empty()) {
						continue;
					}
					loaded[p][m] = Load(path);
					if (!loaded[p][m]) {
						logger::warn("skin: {:08X} '{}': {} {} '{}' would not load; the slot is left as it is", a_actor, a_set.GetName(), Scan::SkinPartName(static_cast<Scan::SkinPart>(p)), Skins::MapName(static_cast<Skins::Map>(m)), path);
					}
				}
			}
			for (auto& target : a_data.targets) {
				auto* material = MaterialOf(target.geometry.get());
				if (!material) {
					continue;
				}
				for (std::size_t m = 0; m < kMaps; ++m) {
					auto* slot = SlotOf(material, target.bFace, static_cast<Skins::Map>(m));
					if (!slot) {
						continue;
					}
					target.ours[m] = loaded[static_cast<std::size_t>(target.part)][m];
					*slot = target.ours[m] ? target.ours[m] : target.own[m];
				}
			}
		}

		// A normal Mu Dynamic NormalMap baked from ours is ours: it is named after its source, and putting the raw
		// one back would only have it bake again
		bool DerivedFromOurs(const RE::NiPointer<RE::NiSourceTexture>& a_found, const RE::NiPointer<RE::NiSourceTexture>& a_ours)
		{
			if (!a_found || !a_ours || !a_found->name.c_str() || !a_ours->name.c_str()) {
				return false;
			}
			const auto lower = [](std::string a_text) {
				for (auto& c : a_text) {
					c = c == '/' ? '\\' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				return a_text;
			};
			const auto found = lower(a_found->name.c_str());
			const auto ours = lower(a_ours->name.c_str());
			return !ours.empty() && found != ours && found.find(ours) != std::string::npos;
		}

		// Ours put on again where the material no longer has them: the FaceGen refresh puts the face's own back between frames
		void Reassert(SkinData& a_data)
		{
			for (auto& target : a_data.targets) {
				auto* material = MaterialOf(target.geometry.get());
				if (!material) {
					continue;
				}
				for (std::size_t m = 0; m < kMaps; ++m) {
					auto* slot = target.ours[m] ? SlotOf(material, target.bFace, static_cast<Skins::Map>(m)) : nullptr;
					if (slot && slot->get() != target.ours[m].get() && !DerivedFromOurs(*slot, target.ours[m])) {
						*slot = target.ours[m];
					}
				}
			}
		}

		// The meshes' own maps back where ours were, by the pointers held; the set's paths are never written, since
		// the engine would stream ours into a rebuilt material and draw the unloaded original purple
		void Restore(SkinData& a_data, const RE::NiAVObject* a_root)
		{
			for (auto& target : a_data.targets) {
				auto* material = Under(target.geometry.get(), a_root) ? MaterialOf(target.geometry.get()) : nullptr;
				for (std::size_t m = 0; m < kMaps; ++m) {
					if (auto* slot = material && target.ours[m] ? SlotOf(material, target.bFace, static_cast<Skins::Map>(m)) : nullptr) {
						*slot = target.own[m];
					}
					target.ours[m].reset();
				}
			}
		}
	}

	void Skin::Apply(Pass, ActorState::State& a_state)
	{
		const auto refr = a_state.handle.get();
		auto* actor = refr ? refr->As<RE::Actor>() : nullptr;
		auto* root = actor ? actor->Get3D(false) : nullptr;
		const auto parts = a_state.parts.With([](const auto& a_parts) { return a_parts; });
		const auto sampled = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		const bool bRetired = a_state.bRetired.load();

		a_state.skin.With([&](SkinData& a_data) {
			const auto& picks = sampled ? sampled->texts[static_cast<std::size_t>(Kind::kSkin)] : std::vector<TextSample>{};
			const auto pick = picks.empty() ? std::string{} : picks.front().value;
			const bool bLetGo = bRetired || !root || !parts || !parts->bLoaded || pick.empty();

			if (bLetGo) {
				if (a_data.bWritten) {
					Restore(a_data, root);
				}
				a_data = {};
				return;
			}

			// A rebuilt 3D: what was worn is put back on the shapes that remain, and the new ones are found
			const bool bRebind = a_data.boundGeneration != parts->generation;
			if (bRebind) {
				if (a_data.bWritten) {
					Restore(a_data, root);
				}
				Bind(a_data, actor);
				a_data.boundGeneration = parts->generation;
				a_data.applied.clear();
			}

			if (a_data.applied != pick) {
				// The set by its name, the first mod's that defines it
				const auto sets = Skins::ListSets();
				const auto set = std::ranges::find_if(sets, [&](const Skins::Listed& a_listed) { return a_listed.set->GetName() == pick; });
				if (set == sets.end()) {
					if (a_data.bWritten) {
						Restore(a_data, root);
						a_data.bWritten = false;
					}
					a_data.applied = pick;
					logger::warn("skin: {:08X} asks for '{}', which no mod defines", actor->GetFormID(), pick);
					return;
				}
				if (a_data.bWritten) {
					Restore(a_data, root);
				}
				Wear(a_data, *set->set, actor->GetFormID());
				a_data.applied = pick;
				a_data.bWritten = true;
				logger::info("skin: {:08X} wears '{}' from {}", actor->GetFormID(), pick, set->mod);
				return;
			}

			Reassert(a_data);
		});
	}
}
