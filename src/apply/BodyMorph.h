#pragma once

#include "Applier.h"
#include "Data.h"

namespace Apply
{
	// BodySlide morphs, summed from each shape's TRI into its skin partition and sent to the GPU.
	// The CPU copy the engine keeps holds only the SMP patch's body, the modifiers' alone, which FSMP builds its collision from.
	class BodyMorph : public IApplier
	{
	public:
		[[nodiscard]] Kind GetKind() const override { return Kind::kBodyMorph; }
		void Apply(Pass a_pass, ActorState::State& a_state) override;

		// Render thread: the working copies that changed since the last frame
		static void Upload();

		// A stored half as a float
		[[nodiscard]] static float Half(std::uint16_t a_half);
		// A vertex's position as stored, as halves or as floats
		static void ReadVertexPosition(const std::uint8_t* a_vertex, bool a_bHalves, float& a_x, float& a_y, float& a_z);
		// A shape's TRI and which of its morphs each body morph channel drives, as a bind finds them; none when no TRI it is
		// stamped with names it
		struct Morphable
		{
			std::shared_ptr<const std::vector<Scan::Tri::BodyTriShapeDeltas>> tri;
			std::size_t shape = 0;
			std::vector<std::pair<std::uint32_t, std::uint32_t>> morphs;
		};
		[[nodiscard]] static std::optional<Morphable> MorphableOf(const RE::BSGeometry* a_geometry, std::uint32_t a_vertexCount);
	};
}
