#include "Overlay.h"
#include "DynamicData.h"

#include "ActorState.h"
#include "BodyTypes.h"
#include "Entries.h"
#include "ModRegistry.h"
#include "RegisteredMods.h"

namespace Apply
{
	namespace
	{
		constexpr auto kNodePrefix = Overlay::kNodePrefix;

		RE::BSLightingShaderProperty* ShaderOf(RE::BSGeometry* a_geometry)
		{
			auto& runtime = a_geometry->GetGeometryRuntimeData();
			return runtime.shaderProperty ? netimmerse_cast<RE::BSLightingShaderProperty*>(runtime.shaderProperty.get()) : nullptr;
		}

		RE::BSLightingShaderMaterialBase* MaterialOf(RE::BSGeometry* a_geometry)
		{
			auto* shader = ShaderOf(a_geometry);
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

		std::string_view NameOf(const RE::NiAVObject* a_object)
		{
			return a_object && a_object->name.c_str() ? a_object->name.c_str() : ""sv;
		}

		// RaceMenu's overlays and ours draw with the skin's shader too, and are not skin to build on
		bool IsOverlayName(std::string_view a_name)
		{
			return a_name.find("[Ovl") != std::string_view::npos || a_name.find("[SOvl") != std::string_view::npos || a_name.starts_with(kNodePrefix);
		}

		// The overlay of the name, when it is on, among every mod's overlays of the slot's name
		Overlays::Item* ItemNamed(Overlays::Slot* a_slot, std::string_view a_name)
		{
			Overlays::Item* found = nullptr;
			Overlays::ForEachItem(a_slot->GetName(), [&](Overlays::Slot&, std::string_view, Overlays::Item& a_item) {
				if (!a_item.bDisabled && a_item.name == a_name) {
					found = &a_item;
					return false;
				}
				return true;
			});
			return found;
		}

		// The item's texture for the actor's layout on the slot's part; hands and feet are painted for the body's
		const Overlays::Texture* TextureFor(RE::Actor* a_actor, Overlays::Slot* a_slot, Overlays::Item* a_item)
		{
			auto* base = a_actor->GetActorBase();
			const bool bFemale = base && base->GetSex() == RE::SEX::kFemale;
			const bool bHead = a_slot->GetPart() == Scan::SkinPart::kHead;
			const auto path = bHead ? BodyTypes::HeadPathOf(a_actor) : BodyTypes::BodyPathOf(a_actor);
			const auto uv = BodyTypes::UvOf(bHead ? Scan::SkinPart::kHead : Scan::SkinPart::kBody, path, bFemale);
			const auto* texture = a_item->FindTexture(uv);
			return texture && texture->bEnabled && !texture->diffuse.empty() ? texture : nullptr;
		}

		// Where a texture is, loose or in an archive, relative to Data/Textures; empty when nowhere
		std::string ResolveTexture(std::string_view a_path)
		{
			std::string relative(a_path);
			std::ranges::replace(relative, '/', '\\');
			if (relative.size() > 9 && _strnicmp(relative.c_str(), "textures\\", 9) == 0) {
				relative.erase(0, 9);
			}
			RE::BSResourceNiBinaryStream stream(std::format("textures\\{}", relative));
			return stream.good() ? relative : std::string{};
		}

		RE::BIPED_OBJECT BipedSlotOf(Scan::SkinPart a_part)
		{
			switch (a_part) {
			case Scan::SkinPart::kHands:
				return RE::BIPED_OBJECT::kHands;
			case Scan::SkinPart::kFeet:
				return RE::BIPED_OBJECT::kFeet;
			default:
				return RE::BIPED_OBJECT::kBody;
			}
		}

		// The skin to build on. Head: the largest dynamic shape under the face node drawing with the FaceGen
		// shader. Else the largest skinned shape drawing with FaceGenRGBTint under what the part's biped slot
		// holds; an empty slot, the body being re-equipped, means wait
		RE::BSGeometry* FindSkin(RE::Actor* a_actor, Scan::SkinPart a_part)
		{
			auto* faceNode = a_actor->GetFaceNodeSkinned();
			if (a_part == Scan::SkinPart::kHead) {
				if (!faceNode) {
					return nullptr;
				}
				RE::BSGeometry* best = nullptr;
				std::uint32_t bestSize = 0;
				for (const auto& child : faceNode->GetChildren()) {
					auto* dynamic = child ? child->AsDynamicTriShape() : nullptr;
					if (!dynamic || IsOverlayName(NameOf(child.get()))) {
						continue;
					}
					auto* material = MaterialOf(dynamic);
					if (!material || material->GetFeature() != RE::BSShaderMaterial::Feature::kFaceGen) {
						continue;
					}
					const auto size = dynamic->GetDynamicTrishapeRuntimeData().dataSize;
					if (size > bestSize) {
						best = dynamic;
						bestSize = size;
					}
				}
				return best;
			}

			RE::BSGeometry* found = nullptr;
			std::uint32_t foundVertices = 0;
			const auto visit = [&](auto& a_self, RE::NiAVObject* a_object) -> void {
				if (!a_object || a_object == faceNode) {
					return;
				}
				if (auto* geometry = a_object->AsGeometry()) {
					if (IsOverlayName(NameOf(a_object))) {
						return;
					}
					auto* material = MaterialOf(geometry);
					if (!material || !geometry->GetGeometryRuntimeData().skinInstance || material->GetFeature() != RE::BSShaderMaterial::Feature::kFaceGenRGBTint) {
						return;
					}
					auto* triShape = geometry->AsTriShape();
					const auto vertices = triShape ? triShape->GetTrishapeRuntimeData().vertexCount : 0;
					if (!found || vertices > foundVertices) {
						found = geometry;
						foundVertices = vertices;
					}
					return;
				}
				if (auto* node = a_object->AsNode()) {
					for (const auto& child : node->GetChildren()) {
						a_self(a_self, child.get());
					}
				}
			};
			if (const auto& biped = a_actor->GetCurrentBiped()) {
				visit(visit, biped->objects[BipedSlotOf(a_part)].partClone.get());
			}
			return found;
		}

		// An alpha property from nothing, the engine having no constructor to call: source alpha over inverse
		// source alpha, test greater than zero, RaceMenu's overlay defaults
		RE::NiAlphaProperty* MakeAlpha()
		{
			auto* alpha = static_cast<RE::NiAlphaProperty*>(RE::malloc(sizeof(RE::NiAlphaProperty)));
			if (!alpha) {
				return nullptr;
			}
			std::memset(static_cast<void*>(alpha), 0, sizeof(RE::NiAlphaProperty));
			if (!SKSE::stl::emplace_vtable(alpha)) {
				RE::free(alpha);
				return nullptr;
			}
			using Fn = RE::NiAlphaProperty::AlphaFunction;
			alpha->alphaFlags = static_cast<std::uint16_t>(1 | (static_cast<int>(Fn::kSrcAlpha) << 1) | (static_cast<int>(Fn::kInvSrcAlpha) << 5) | 0x200 | (4 << 10));
			alpha->alphaThreshold = 0;
			return alpha;
		}

		// A face clone drawing from the skin's own block: one more owner of it, the clone's own let go; the error says why not
		bool Share(OverlayLayer& a_layer, std::string& a_error)
		{
			auto* clone = a_layer.clone ? a_layer.clone->AsDynamicTriShape() : nullptr;
			auto* skin = a_layer.skin ? a_layer.skin->AsDynamicTriShape() : nullptr;
			if (!clone || !skin) {
				a_error = "the face's clone is not a dynamic shape";
				return false;
			}
			if (!DynamicData::IsInstalled()) {
				a_error = "the face's vertex blocks are not counted on this runtime";
				return false;
			}
			auto& to = clone->GetDynamicTrishapeRuntimeData();
			auto& from = skin->GetDynamicTrishapeRuntimeData();
			if (to.dataSize != from.dataSize) {
				a_error = std::format("the clone's vertex block is {} bytes, the face's {}", to.dataSize, from.dataSize);
				return false;
			}
			if (!DynamicData::Retain(from.dynamicData)) {
				a_error = "the face's vertex block is not one the hooks counted";
				return false;
			}
			from.lock.Lock();
			to.lock.Lock();
			void* own = to.dynamicData;
			to.dynamicData = from.dynamicData;
			to.frameCount = from.frameCount;
			to.unk178 = from.unk178;
			to.lock.Unlock();
			from.lock.Unlock();
			DynamicData::Release(own);
			return true;
		}

		void TearDown(OverlayLayer& a_layer);

		// Clones the skin and builds the rest; the error says why not
		bool Build(OverlayLayer& a_layer, RE::Actor* a_actor, Scan::SkinPart a_part, const Overlays::Texture& a_texture, std::string& a_error)
		{
			auto* skin = FindSkin(a_actor, a_part);
			auto* parent = skin ? skin->parent : nullptr;
			if (!skin || !parent) {
				a_error = std::format("nothing in the {} slot to build on yet", Scan::SkinPartName(a_part));
				return false;
			}
			auto* skinShader = ShaderOf(skin);
			auto* skinMaterial = MaterialOf(skin);
			if (!skinShader || !skinMaterial) {
				a_error = std::format("skin '{}' has no lighting shader", NameOf(skin));
				return false;
			}
			const auto diffuse = ResolveTexture(a_texture.diffuse);
			if (diffuse.empty()) {
				a_error = std::format("texture not found, loose or in an archive: '{}'", a_texture.diffuse);
				return false;
			}
			const auto normal = a_texture.normal.empty() ? std::string{} : ResolveTexture(a_texture.normal);
			if (!a_texture.normal.empty() && normal.empty()) {
				a_error = std::format("normal map not found: '{}'", a_texture.normal);
				return false;
			}

			auto* cloned = skin->Clone();
			auto* clone = cloned ? cloned->AsGeometry() : nullptr;
			if (!clone) {
				a_error = std::format("cloning skin '{}' gave no geometry", NameOf(skin));
				return false;
			}
			clone->name = std::format("{}{}.{}", kNodePrefix, a_layer.slot, a_layer.item);

			// The skin's extra data came along and none of it is about us; Mu Dynamic NormalMap is asked to leave the clone out
			while (clone->GetExtraDataSize() > 0) {
				clone->RemoveExtraDataAt(0);
			}
			if (auto* leaveOut = RE::NiIntegerExtraData::Create("NoDynamicNormalMap", 1)) {
				clone->AddExtraData(leaveOut);
			}

			// The skin instance is the skin's own: whatever reshapes the body swaps the partition there, and a clone of it would keep the old shape
			clone->GetGeometryRuntimeData().skinInstance = skin->GetGeometryRuntimeData().skinInstance;

			// A copy of the skin's own shader: one created from nothing is never drawn on AE
			auto* copied = skinShader->Clone();
			auto* shader = copied ? netimmerse_cast<RE::BSLightingShaderProperty*>(copied) : nullptr;
			if (!shader) {
				a_error = std::format("could not copy skin '{}''s shader", NameOf(skin));
				return false;
			}
			// The emissive colour is a pointer to something shared; one of this layer's own
			if (auto* colour = static_cast<RE::NiColor*>(RE::malloc(sizeof(RE::NiColor)))) {
				*colour = RE::NiColor{ 0.f, 0.f, 0.f };
				shader->emissiveColor = colour;
			}
			// What the geometry is comes from the skin; what an overlay is, a blended decal not writing depth, is ours
			using Flag = RE::BSShaderProperty::EShaderPropertyFlag8;
			for (const auto flag : { Flag::kSkinned, Flag::kModelSpaceNormals, Flag::kVertexColors, Flag::kVertexAlpha, Flag::kSpecular, Flag::kReceiveShadows, Flag::kCastShadows, Flag::kZBufferTest, Flag::kRemappableTextures }) {
				shader->SetFlags(flag, skinShader->flags.any(static_cast<RE::BSShaderProperty::EShaderPropertyFlag>(std::uint64_t{ 1 } << static_cast<std::uint8_t>(flag))));
			}
			shader->SetFlags(Flag::kDecal, true);
			shader->SetFlags(Flag::kDynamicDecal, true);
			shader->SetFlags(Flag::kOwnEmit, true);
			shader->SetFlags(Flag::kFaceGenRGBTint, true);
			shader->SetFlags(Flag::kZBufferWrite, false);

			// Tintable and lit like the skin; SetMaterial copies, so the shader's own is what gets the textures
			auto* material = RE::BSLightingShaderMaterialBase::CreateMaterial(RE::BSShaderMaterial::Feature::kFaceGenRGBTint);
			material->CopyBaseMembers(skinMaterial);
			material->materialAlpha = 0.f;  // nothing until the first write
			shader->SetMaterial(material, true);
			// CreateMaterial takes it from the thread's scrap heap, so it goes back there
			material->~BSLightingShaderMaterialBase();
			RE::MemoryManager::GetSingleton()->GetThreadScrapHeap()->Deallocate(material);
			auto* live = static_cast<RE::BSLightingShaderMaterialBase*>(shader->material);
			if (!live) {
				a_error = "the shader took no material";
				return false;
			}

			// Our diffuse over the skin's other maps, loaded before the material is given them
			using T = RE::BSTextureSet::Texture;
			auto textures = RE::NiPointer<RE::BSTextureSet>{ RE::BSShaderTextureSet::Create() };
			if (skinMaterial->textureSet) {
				for (std::uint32_t i = 1; i < static_cast<std::uint32_t>(T::kTotal); ++i) {
					if (const auto* path = skinMaterial->textureSet->GetTexturePath(static_cast<T>(i))) {
						textures->SetTexturePath(static_cast<T>(i), path);
					}
				}
			}
			textures->SetTexturePath(T::kDiffuse, diffuse.c_str());
			if (!normal.empty()) {
				textures->SetTexturePath(T::kNormal, normal.c_str());
			}
			RE::NiPointer<RE::NiSourceTexture> diffuseTexture, normalTexture;
			textures->SetTexture(T::kDiffuse, diffuseTexture);
			textures->SetTexture(T::kNormal, normalTexture);
			if (!diffuseTexture || !normalTexture) {
				a_error = std::format("could not load the textures (diffuse {}, normal {})", diffuseTexture ? "ok" : "missing", normalTexture ? "ok" : "missing");
				return false;
			}
			live->diffuseTexture = diffuseTexture;
			live->normalTexture = normalTexture;
			live->textureSet = textures;

			auto* alpha = MakeAlpha();
			if (!alpha) {
				a_error = "could not make an alpha property";
				return false;
			}
			auto& runtime = clone->GetGeometryRuntimeData();
			runtime.shaderProperty.reset(shader);
			runtime.alphaProperty.reset(alpha);
			shader->SetupGeometry(clone);
			shader->FinishSetupGeometry(clone);

			// Appended: same-depth decals draw in traversal order, and ours goes over what RaceMenu put on the skin
			parent->AttachChild(clone, false);

			a_layer.clone = RE::NiPointer(clone);
			a_layer.skin = RE::NiPointer(skin);
			a_layer.bOwnNormal = !normal.empty();
			// The face's vertices move every frame: a clone of a dynamic face draws from its block, or is not drawn
			if (skin->AsDynamicTriShape()) {
				if (!Share(a_layer, a_error)) {
					TearDown(a_layer);
					return false;
				}
				a_layer.bShares = true;
			}
			return true;
		}

		// Takes the clone off: the skin instance is the skin's, not ours to take down
		void TearDown(OverlayLayer& a_layer)
		{
			if (a_layer.clone) {
				a_layer.clone->GetGeometryRuntimeData().skinInstance = nullptr;
				if (auto* parent = a_layer.clone->parent) {
					parent->DetachChild2(a_layer.clone.get());
				}
			}
			a_layer.clone.reset();
			a_layer.skin.reset();
		}

		const NumberSample* NumberAt(const std::vector<NumberSample>& a_samples, std::uint32_t a_index)
		{
			const auto it = std::ranges::find(a_samples, a_index, &NumberSample::index);
			return it != a_samples.end() ? &*it : nullptr;
		}

		const ColourSample* ColourAt(const std::vector<ColourSample>& a_samples, std::uint32_t a_index)
		{
			const auto it = std::ranges::find(a_samples, a_index, &ColourSample::index);
			return it != a_samples.end() ? &*it : nullptr;
		}

		// This frame's values onto the clone's material; the normal follows the skin's, which Mu Dynamic NormalMap rebakes
		void Write(OverlayLayer& a_layer, const std::vector<NumberSample>& a_numbers, const std::vector<ColourSample>& a_colours)
		{
			auto* shader = ShaderOf(a_layer.clone.get());
			auto* material = shader ? static_cast<RE::BSLightingShaderMaterialBase*>(shader->material) : nullptr;
			if (!material) {
				return;
			}
			if (auto* skinMaterial = MaterialOf(a_layer.skin.get()); !a_layer.bOwnNormal && skinMaterial && skinMaterial->normalTexture.get() != material->normalTexture.get()) {
				material->normalTexture = skinMaterial->normalTexture;
			}
			const auto* alpha = NumberAt(a_numbers, a_layer.alpha);
			const auto* tint = ColourAt(a_colours, a_layer.tint);
			const auto* glow = ColourAt(a_colours, a_layer.glow);
			const auto* glowStrength = NumberAt(a_numbers, a_layer.glowStrength);
			material->materialAlpha = alpha ? std::clamp(alpha->value, 0.f, 1.f) : 1.f;
			static_cast<RE::BSLightingShaderMaterialFacegenTint*>(material)->tintColor = tint ? RE::NiColor{ tint->red, tint->green, tint->blue } : RE::NiColor{ 1.f, 1.f, 1.f };
			if (shader->emissiveColor) {
				*shader->emissiveColor = glow ? RE::NiColor{ glow->red, glow->green, glow->blue } : RE::NiColor{ 0.f, 0.f, 0.f };
			}
			shader->emissiveMult = glowStrength ? glowStrength->value : 1.f;

		}

	}

	void Overlay::Apply(Pass a_pass, ActorState::State& a_state)
	{
		const auto refr = a_state.handle.get();
		auto* actor = refr ? refr->As<RE::Actor>() : nullptr;
		auto* root = actor ? actor->Get3D(false) : nullptr;

		if (a_pass == Pass::kFaceNode) {
			a_state.overlay.With([&](OverlayData& a_data) {
				for (auto& layer : a_data.layers) {
					// The skin given a new block, its old one kept alive by our count: shared again
					if (layer.bShares) {
						auto* clone = layer.clone ? layer.clone->AsDynamicTriShape() : nullptr;
						auto* skin = layer.skin ? layer.skin->AsDynamicTriShape() : nullptr;
						if (clone && skin && clone->GetDynamicTrishapeRuntimeData().dynamicData != skin->GetDynamicTrishapeRuntimeData().dynamicData) {
							std::string error;
							layer.bShares = Share(layer, error);
							if (!layer.bShares) {
								logger::error("overlay: {:08X} '{}': the face's new vertex block could not be shared: {}", actor ? actor->GetFormID() : 0, layer.item, error);
							}
						}
					}
				}
			});
			return;
		}

		const auto parts = a_state.parts.With([](const auto& a_parts) { return a_parts; });
		const auto sampled = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		const bool bRetired = a_state.bRetired.load();
		auto& entries = Entries::GetSingleton();

		a_state.overlay.With([&](OverlayData& a_data) {
			const auto& picks = sampled ? sampled->texts[static_cast<std::size_t>(Kind::kOverlay)] : std::vector<TextSample>{};
			const auto& numbers = sampled ? sampled->numbers[static_cast<std::size_t>(Kind::kOverlay)] : std::vector<NumberSample>{};
			const auto& colours = sampled ? sampled->colours[static_cast<std::size_t>(Kind::kOverlay)] : std::vector<ColourSample>{};
			const bool bLetGo = bRetired || !root || !parts || !parts->bLoaded || picks.empty();
			const bool bRebind = !bLetGo && a_data.boundGeneration != parts->generation;

			// Everything off: the actor is going, its 3D was rebuilt, or nothing is picked
			if (bLetGo || bRebind) {
				for (auto& layer : a_data.layers) {
					if (layer.clone && Under(layer.clone.get(), root)) {
						TearDown(layer);
					}
				}
				a_data.layers.clear();
				if (bLetGo) {
					a_data = {};
					return;
				}
				a_data.boundGeneration = parts->generation;
			}

			// A layer whose pick went, or changed, comes off
			std::erase_if(a_data.layers, [&](OverlayLayer& a_layer) {
				const auto pick = std::ranges::find(picks, a_layer.slot, &TextSample::index);
				if (pick != picks.end() && pick->value == a_layer.item) {
					return false;
				}
				TearDown(a_layer);
				return true;
			});

			// A pick with no layer yet is built, when its slot has a texture for this actor and the skin is there
			for (const auto& pick : picks) {
				if (std::ranges::any_of(a_data.layers, [&](const OverlayLayer& a_layer) { return a_layer.slot == pick.index; })) {
					continue;
				}
				const auto entry = entries.Get({ Kind::kOverlay, pick.index });
				if (entry.type != ValueType::kReference) {
					continue;
				}
				auto* slot = Overlays::SlotNamed(entry.name);
				auto* item = slot ? ItemNamed(slot, pick.value) : nullptr;
				const auto* texture = item ? TextureFor(actor, slot, item) : nullptr;
				if (!texture) {
					continue;
				}
				OverlayLayer layer;
				layer.slot = pick.index;
				layer.item = pick.value;
				const auto alpha = entries.Require(Kind::kOverlay, std::format("{}.alpha", entry.name));
				const auto tint = entries.Require(Kind::kOverlay, std::format("{}.tint", entry.name));
				const auto glow = entries.Require(Kind::kOverlay, std::format("{}.glow", entry.name));
				const auto glowStrength = entries.Require(Kind::kOverlay, std::format("{}.glowstrength", entry.name));
				if (!alpha.IsValid() || !tint.IsValid() || !glow.IsValid() || !glowStrength.IsValid()) {
					continue;
				}
				layer.alpha = alpha.GetIndex();
				layer.tint = tint.GetIndex();
				layer.glow = glow.GetIndex();
				layer.glowStrength = glowStrength.GetIndex();
				std::string error;
				if (Build(layer, actor, slot->GetPart(), *texture, error)) {
					logger::info("overlay: {:08X} '{}' shows '{}' from '{}'", actor->GetFormID(), entry.name, item->name, texture->diffuse);
					a_data.layers.push_back(std::move(layer));
				} else if (error != a_data.lastError) {
					// Said once per reason; the build is tried again each frame, a body being re-equipped is back within a few
					a_data.lastError = error;
					logger::warn("overlay: {:08X} '{}' '{}': {}", actor->GetFormID(), entry.name, item->name, error);
				}
			}

			for (auto& layer : a_data.layers) {
				Write(layer, numbers, colours);
			}
		});
	}
}
