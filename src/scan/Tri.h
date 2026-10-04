#pragma once

#include "Scan.h"

namespace Scan::Tri
{
	// A FaceGen FRTRI003 file, without its deltas
	struct FaceTri
	{
		uint32_t vertexCount = 0;
		std::vector<Vec3> sample;  // the base vertices at SampleIndices
		std::vector<std::string> morphs;
	};

	// Fails with a reason when the bytes are not a readable FRTRI003
	[[nodiscard]] bool ReadFaceTri(std::span<const std::byte> a_bytes, FaceTri& a_out, std::string& a_error);

	// The same file with its deltas: one move per vertex per morph, in model units
	struct FaceTriMorphDeltas
	{
		std::string name;
		std::vector<Vec3> deltas;  // one per vertex
	};

	struct FaceTriDeltas
	{
		uint32_t vertexCount = 0;
		std::vector<FaceTriMorphDeltas> morphs;
	};

	[[nodiscard]] bool ReadFaceTriDeltas(std::span<const std::byte> a_bytes, FaceTriDeltas& a_out, std::string& a_error);

	// A BodySlide TRI, "\0IRT" or "PIRT": each shape's morph names
	struct BodyTriShape
	{
		std::string name;
		std::vector<std::string> morphs;
	};

	[[nodiscard]] bool ReadBodyTri(std::span<const std::byte> a_bytes, std::vector<BodyTriShape>& a_out, std::string& a_error);

	// The same file with its deltas: one vertex's move under one morph at full weight, in the mesh's units
	struct BodyTriDelta
	{
		uint32_t index = 0;
		float x = 0.f;
		float y = 0.f;
		float z = 0.f;
	};

	struct BodyTriMorphDeltas
	{
		std::string name;
		std::vector<BodyTriDelta> deltas;
	};

	struct BodyTriShapeDeltas
	{
		std::string name;
		std::vector<BodyTriMorphDeltas> morphs;
	};

	[[nodiscard]] bool ReadBodyTriDeltas(std::span<const std::byte> a_bytes, std::vector<BodyTriShapeDeltas>& a_out, std::string& a_error);

	// Spread over the mesh, so two meshes with the same vertex count can be told apart
	[[nodiscard]] std::vector<uint32_t> SampleIndices(uint32_t a_vertexCount);

	// Zero for the same mesh; the samples are compared relative to their first vertex
	[[nodiscard]] float ShapeDistance(const std::vector<Vec3>& a_a, const std::vector<Vec3>& a_b);
}
