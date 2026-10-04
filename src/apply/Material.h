#pragma once

#include "Applier.h"

namespace Apply
{
	// The tints and glows on the head's materials: hair, brows and beard tint, the eyes' emissive; written on
	// the main thread, only on change, with the originals kept to put back
	class Material : public IApplier
	{
	public:
		[[nodiscard]] Kind GetKind() const override { return Kind::kMaterial; }
		void Apply(Pass a_pass, ActorState::State& a_state) override;

		// The table's entries, in index order
		enum class Slot : std::uint32_t
		{
			kHairTint,      // colour
			kBrowsTint,     // colour
			kBeardTint,     // colour
			kEyesGlow,      // colour
			kEyesGlowMult,  // number
			kGlossiness,    // number: specular power, on the skin and the face
			kSpecular,      // number: specular strength
			kSoftLighting,  // number: subsurface rolloff
			kRimLighting,   // number: rim light power
			kTotal
		};

		// Main thread: the actor's own colour for a colour slot, what it has with nothing of ours on it; none without 3D
		[[nodiscard]] static std::optional<RE::NiColor> OwnColour(RE::Actor* a_actor, Slot a_slot);
	};
}
