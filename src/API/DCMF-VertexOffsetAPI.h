#pragma once

// For modders: copy this file into your own project to move an actor's skin on top of the morphs DCMF writes
namespace DCMF_API::VertexOffset
{
	enum class InterfaceVersion : std::uint8_t
	{
		V1,

		Latest = V1
	};

	// Per-vertex offsets added to a body shape's positions at upload, DCMF staying the only writer of its vertex buffer:
	// final position = base + morphs + every owner's offsets. Positions are in the shape's skin space, in game units
	class IVertexOffsetInterface1
	{
	public:
		/// <summary>Offsets for one of the actor's shapes, replacing what this owner set for it before. Every shape drawing
		/// from the same vertex buffer shows them, RaceMenu's overlays included. Any thread; the data is copied at once.</summary>
		/// <param name="a_owner">Who sets them; owners' offsets add up</param>
		/// <param name="a_vertices">The vertex indices, a_count of them</param>
		/// <param name="a_xyz">The offsets, x, y and z for each index</param>
		/// <returns>False, storing nothing, when DCMF does not write the shape: the caller may write the buffer itself</returns>
		virtual bool SetOffsets(RE::Actor* a_actor, RE::BSGeometry* a_shape, const char* a_owner, const std::uint32_t* a_vertices, const float* a_xyz, std::uint32_t a_count) noexcept = 0;

		/// <summary>This owner's offsets and rotations taken off every shape of the actor; the skin goes back to base and morphs.</summary>
		virtual void ClearOffsets(RE::Actor* a_actor, const char* a_owner) noexcept = 0;

		/// <summary>The shape's positions as DCMF last wrote them, base and morphs without offsets, x, y and z per vertex.</summary>
		/// <param name="a_count">How many vertices a_outXyz holds; at most that many are written</param>
		/// <returns>False when DCMF does not write the shape</returns>
		virtual bool CopyPositions(RE::Actor* a_actor, RE::BSGeometry* a_shape, float* a_outXyz, std::uint32_t a_count) noexcept = 0;

		/// <summary>A number that goes up each time DCMF writes new morphs on the actor or binds its shapes again.</summary>
		[[nodiscard]] virtual std::uint32_t MorphGeneration(RE::Actor* a_actor) noexcept = 0;

		/// <summary>Rotations of the vertices' frames for one of the actor's shapes, replacing what this owner set for it before:
		/// each vertex's normal, tangent and bitangent (as the shape has them, base and morphs) turned by its rotation at upload,
		/// so lighting -- a tangent-space normal map's -- follows the skin as offsets moved it. Owners' rotations compose.
		/// Any thread; the data is copied at once.</summary>
		/// <param name="a_owner">Who sets them</param>
		/// <param name="a_vertices">The vertex indices, a_count of them</param>
		/// <param name="a_xyzw">The rotations, unit quaternions x, y, z, w for each index, in the shape's skin space</param>
		/// <returns>False, storing nothing, when DCMF does not write the shape</returns>
		virtual bool SetRotations(RE::Actor* a_actor, RE::BSGeometry* a_shape, const char* a_owner, const std::uint32_t* a_vertices, const float* a_xyzw, std::uint32_t a_count) noexcept = 0;
	};

	using IVertexOffsetInterface = IVertexOffsetInterface1;

	using _RequestPluginAPI_VertexOffset = IVertexOffsetInterface* (*)(InterfaceVersion a_interfaceVersion, const char* a_pluginName, REL::Version a_pluginVersion);

	/// <summary>
	/// Requests DCMF's vertex offset interface; call it once the game's data is loaded.
	/// </summary>
	/// <returns>The interface, or nullptr when DCMF is not loaded or the version is not supported</returns>
	inline IVertexOffsetInterface* GetAPI(InterfaceVersion a_interfaceVersion = InterfaceVersion::Latest)
	{
		static IVertexOffsetInterface* api = nullptr;
		if (api) {
			return api;
		}
		const auto module = GetModuleHandleW(L"DCMF.dll");
		const auto request = module ? reinterpret_cast<_RequestPluginAPI_VertexOffset>(GetProcAddress(module, "RequestPluginAPI_VertexOffset")) : nullptr;
		if (!request) {
			return nullptr;
		}
		const auto plugin = SKSE::PluginDeclaration::GetSingleton();
		api = request(a_interfaceVersion, plugin->GetName().data(), plugin->GetVersion());
		return api;
	}
}
