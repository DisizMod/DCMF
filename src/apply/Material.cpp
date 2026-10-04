#include "Material.h"

#include "ActorState.h"

namespace Apply
{
	namespace
	{
		RE::BSLightingShaderProperty* ShaderOf(RE::BSGeometry* a_geometry)
		{
			auto& runtime = a_geometry->GetGeometryRuntimeData();
			return runtime.shaderProperty ? netimmerse_cast<RE::BSLightingShaderProperty*>(runtime.shaderProperty.get()) : nullptr;
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

		// Every tintable, glowing or skinned shape under the actor, sorted by the head part it hangs under
		void Bind(MaterialData& a_data, RE::Actor* a_actor, RE::NiAVObject* a_root, RE::BSFaceGenNiNode* a_face)
		{
			a_data.targets.clear();
			auto* base = a_actor->GetActorBase();
			if (!base) {
				return;
			}

			using Type = RE::BGSHeadPart::HeadPartType;
			struct Owner
			{
				MaterialTarget::Kind kind;
				std::vector<RE::NiAVObject*> objects;
			};
			std::vector<Owner> owners;
			for (const auto [type, kind] : { std::pair{ Type::kHair, MaterialTarget::Kind::kHair }, std::pair{ Type::kEyebrows, MaterialTarget::Kind::kBrows }, std::pair{ Type::kFacialHair, MaterialTarget::Kind::kBeard } }) {
				auto* part = base->GetCurrentHeadPartByType(type);
				if (!part) {
					continue;
				}
				Owner owner{ kind, {} };
				const auto add = [&](const RE::BGSHeadPart* a_part) {
					if (auto* object = a_part ? a_face->GetObjectByName(a_part->formEditorID) : nullptr) {
						owner.objects.push_back(object);
					}
				};
				add(part);
				for (const auto* extra : part->extraParts) {
					add(extra);
				}
				if (!owner.objects.empty()) {
					owners.push_back(std::move(owner));
				}
			}

			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
				auto* shader = ShaderOf(a_geometry);
				auto* material = shader ? shader->material : nullptr;
				if (!material) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}

				using Feature = RE::BSShaderMaterial::Feature;
				MaterialTarget target;
				target.geometry = RE::NiPointer(a_geometry);
				auto* lighting = static_cast<RE::BSLightingShaderMaterialBase*>(material);
				target.specularPower = lighting->specularPower;
				target.specularColorScale = lighting->specularColorScale;
				target.subSurfaceLightRolloff = lighting->subSurfaceLightRolloff;
				target.rimLightPower = lighting->rimLightPower;
				target.bSpecular = shader->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kSpecular);
				switch (material->GetFeature()) {
				case Feature::kFaceGenRGBTint:
					target.kind = MaterialTarget::Kind::kSkin;
					break;
				case Feature::kFaceGen:
					target.kind = MaterialTarget::Kind::kFace;
					break;
				case Feature::kHairTint:
					// Hair, brows and beard all use the hair tint material; the part above says which
					if (!Under(a_geometry, a_face)) {
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					for (const auto& owner : owners) {
						if (std::ranges::any_of(owner.objects, [&](const RE::NiAVObject* a_object) { return Under(a_geometry, a_object); })) {
							target.kind = owner.kind;
							break;
						}
					}
					if (target.kind == MaterialTarget::Kind::kNone) {
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					target.tint = static_cast<RE::BSLightingShaderMaterialHairTint*>(material)->tintColor;
					break;
				case Feature::kEye:
					if (!Under(a_geometry, a_face)) {
						return RE::BSVisit::BSVisitControl::kContinue;
					}
					target.kind = MaterialTarget::Kind::kEyes;
					if (shader->emissiveColor) {
						target.emissive = *shader->emissiveColor;
					}
					target.emissiveMult = shader->emissiveMult;
					break;
				default:
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				a_data.targets.push_back(std::move(target));
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		// The colour or number held for a slot, or nothing
		const ColourSample* ColourFor(const std::vector<ColourSample>& a_samples, Material::Slot a_slot)
		{
			const auto it = std::ranges::find(a_samples, static_cast<std::uint32_t>(a_slot), &ColourSample::index);
			return it != a_samples.end() ? &*it : nullptr;
		}

		const NumberSample* NumberFor(const std::vector<NumberSample>& a_samples, Material::Slot a_slot)
		{
			const auto it = std::ranges::find(a_samples, static_cast<std::uint32_t>(a_slot), &NumberSample::index);
			return it != a_samples.end() ? &*it : nullptr;
		}

		// Writes what is held onto one target, or its original back
		void Write(MaterialTarget& a_target, const std::vector<ColourSample>& a_colours, const std::vector<NumberSample>& a_numbers, bool a_bLetGo)
		{
			auto* shader = ShaderOf(a_target.geometry.get());
			auto* material = shader ? shader->material : nullptr;
			if (!material) {
				return;
			}

			const auto tintSlot = a_target.kind == MaterialTarget::Kind::kHair ? Material::Slot::kHairTint :
			                      a_target.kind == MaterialTarget::Kind::kBrows ? Material::Slot::kBrowsTint :
			                                                                      Material::Slot::kBeardTint;

			switch (a_target.kind) {
			case MaterialTarget::Kind::kHair:
			case MaterialTarget::Kind::kBrows:
			case MaterialTarget::Kind::kBeard:
				if (material->GetFeature() == RE::BSShaderMaterial::Feature::kHairTint) {
					const auto* colour = a_bLetGo ? nullptr : ColourFor(a_colours, tintSlot);
					static_cast<RE::BSLightingShaderMaterialHairTint*>(material)->tintColor = colour ? RE::NiColor(colour->red, colour->green, colour->blue) : a_target.tint;
				}
				break;
			case MaterialTarget::Kind::kEyes: {
				const auto* glow = a_bLetGo ? nullptr : ColourFor(a_colours, Material::Slot::kEyesGlow);
				const auto* mult = a_bLetGo ? nullptr : NumberFor(a_numbers, Material::Slot::kEyesGlowMult);
				if (shader->emissiveColor) {
					*shader->emissiveColor = glow ? RE::NiColor(glow->red, glow->green, glow->blue) : a_target.emissive;
				}
				shader->emissiveMult = mult ? mult->value : a_target.emissiveMult;
				break;
			}
			case MaterialTarget::Kind::kSkin:
			case MaterialTarget::Kind::kFace: {
				const auto* gloss = a_bLetGo ? nullptr : NumberFor(a_numbers, Material::Slot::kGlossiness);
				const auto* specular = a_bLetGo ? nullptr : NumberFor(a_numbers, Material::Slot::kSpecular);
				const auto* soft = a_bLetGo ? nullptr : NumberFor(a_numbers, Material::Slot::kSoftLighting);
				const auto* rim = a_bLetGo ? nullptr : NumberFor(a_numbers, Material::Slot::kRimLighting);
				auto* lighting = static_cast<RE::BSLightingShaderMaterialBase*>(material);
				lighting->specularPower = gloss ? gloss->value : a_target.specularPower;
				lighting->specularColorScale = specular ? specular->value : a_target.specularColorScale;
				lighting->subSurfaceLightRolloff = soft ? soft->value : a_target.subSurfaceLightRolloff;
				lighting->rimLightPower = rim ? rim->value : a_target.rimLightPower;
				// The gloss only shows with the specular flag on; a skin shipped without it gets it while driven
				shader->SetFlags(RE::BSShaderProperty::EShaderPropertyFlag8::kSpecular, (gloss || specular) ? true : a_target.bSpecular);
				break;
			}
			default:
				break;
			}
		}
	}

	std::optional<RE::NiColor> Material::OwnColour(RE::Actor* a_actor, Slot a_slot)
	{
		if (!a_actor) {
			return std::nullopt;
		}
		// What was there before we wrote, or, nothing written, what is there now
		MaterialData data;
		if (const auto state = ActorState::Get(a_actor->GetHandle())) {
			state->material.With([&](const MaterialData& a_data) {
				if (a_data.bWritten) {
					data.targets = a_data.targets;
				}
			});
		}
		if (data.targets.empty()) {
			auto* root = a_actor->Get3D(false);
			if (!root) {
				return std::nullopt;
			}
			Bind(data, a_actor, root, a_actor->GetFaceNodeSkinned());
		}
		const auto kind = a_slot == Slot::kHairTint ? MaterialTarget::Kind::kHair :
		                  a_slot == Slot::kBrowsTint ? MaterialTarget::Kind::kBrows :
		                  a_slot == Slot::kBeardTint ? MaterialTarget::Kind::kBeard :
		                  a_slot == Slot::kEyesGlow  ? MaterialTarget::Kind::kEyes :
		                                               MaterialTarget::Kind::kNone;
		for (const auto& target : data.targets) {
			if (target.kind == kind) {
				return kind == MaterialTarget::Kind::kEyes ? target.emissive : target.tint;
			}
		}
		return std::nullopt;
	}

	void Material::Apply(Pass, ActorState::State& a_state)
	{
		const auto refr = a_state.handle.get();
		auto* actor = refr ? refr->As<RE::Actor>() : nullptr;
		auto* face = actor ? actor->GetFaceNodeSkinned() : nullptr;
		auto* root = actor ? actor->Get3D(false) : nullptr;
		const auto parts = a_state.parts.With([](const auto& a_parts) { return a_parts; });
		const auto sampled = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		const bool bRetired = a_state.bRetired.load();

		a_state.material.With([&](MaterialData& a_data) {
			const auto& colours = sampled ? sampled->colours[static_cast<std::size_t>(Kind::kMaterial)] : std::vector<ColourSample>{};
			const auto& numbers = sampled ? sampled->numbers[static_cast<std::size_t>(Kind::kMaterial)] : std::vector<NumberSample>{};
			const bool bLetGo = bRetired || !root || !parts || !parts->bLoaded || (colours.empty() && numbers.empty());

			if (bLetGo) {
				if (a_data.bWritten) {
					for (auto& target : a_data.targets) {
						if (Under(target.geometry.get(), root)) {
							Write(target, {}, {}, true);
						}
					}
				}
				a_data = {};
				return;
			}

			const bool bRebind = a_data.boundGeneration != parts->generation;
			if (bRebind) {
				Bind(a_data, actor, root, face);
				a_data.boundGeneration = parts->generation;
				a_data.appliedColours.clear();
				a_data.appliedNumbers.clear();
			}

			const auto sameColours = a_data.appliedColours.size() == colours.size() &&
			                         std::equal(a_data.appliedColours.begin(), a_data.appliedColours.end(), colours.begin(), [](const ColourSample& a, const ColourSample& b) {
										 return a.index == b.index && a.red == b.red && a.green == b.green && a.blue == b.blue;
									 });
			const auto sameNumbers = a_data.appliedNumbers.size() == numbers.size() &&
			                         std::equal(a_data.appliedNumbers.begin(), a_data.appliedNumbers.end(), numbers.begin(), [](const NumberSample& a, const NumberSample& b) { return a.index == b.index && a.value == b.value; });
			if (!bRebind && sameColours && sameNumbers) {
				return;
			}

			for (auto& target : a_data.targets) {
				Write(target, colours, numbers, false);
			}
			a_data.appliedColours = colours;
			a_data.appliedNumbers = numbers;
			a_data.bWritten = true;
		});
	}
}
