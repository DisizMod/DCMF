#pragma once

// For modders: copy this file into your own project to ask what DCMF knows of an actor
namespace DCMF_API::Query
{
	enum class InterfaceVersion : std::uint8_t
	{
		V1,

		Latest = V1
	};

	// Called once per name; the text lives only for the call
	using NameCallback = void (*)(const char* a_name, void* a_userData);
	// Called once per shape: its name, whether it is hidden, and the SMP config colliding with it, empty when none
	using ShapeCallback = void (*)(const char* a_name, bool a_bHidden, const char* a_collision, void* a_userData);

	// What DCMF knows of an actor. Any thread. An actor DCMF does not track answers false or calls nothing; its 3D is
	// never read on demand
	class IQueryInterface1
	{
	public:
		/// <summary>Whether DCMF keeps state for the actor: one a mod drives, or the one its UI inspects.</summary>
		[[nodiscard]] virtual bool IsActorTracked(RE::Actor* a_actor) noexcept = 0;

		/// <summary>Whether a modifier is active on the actor, its exit transition included.</summary>
		/// <param name="a_modifier">The DCMF mod's folder name and the modifier's name, "mod:modifier"</param>
		[[nodiscard]] virtual bool IsModifierActive(RE::Actor* a_actor, const char* a_modifier) noexcept = 0;

		/// <summary>Whether a clip is playing on the actor.</summary>
		/// <param name="a_clip">The DCMF mod's folder name and the clip's name, "mod:clip"</param>
		[[nodiscard]] virtual bool IsClipPlaying(RE::Actor* a_actor, const char* a_clip) noexcept = 0;

		/// <summary>Each modifier active on the actor, "mod:modifier", highest priority first.</summary>
		virtual void ForEachActiveModifier(RE::Actor* a_actor, NameCallback a_callback, void* a_userData) noexcept = 0;

		/// <summary>Each clip playing on the actor, "mod:clip", highest priority first.</summary>
		virtual void ForEachPlayingClip(RE::Actor* a_actor, NameCallback a_callback, void* a_userData) noexcept = 0;

		/// <summary>A number channel's value sent to the actor this frame, modifiers, clips and holds included.</summary>
		/// <param name="a_channel">The channel as DCMF's files name it, "body:weight"</param>
		/// <returns>False when nothing drives the channel on the actor</returns>
		[[nodiscard]] virtual bool GetNumberChannel(RE::Actor* a_actor, const char* a_channel, float& a_outValue) noexcept = 0;

		/// <summary>A colour channel's value sent to the actor this frame, red, green, blue, each 0-1.</summary>
		/// <returns>False when nothing drives the channel on the actor</returns>
		[[nodiscard]] virtual bool GetColourChannel(RE::Actor* a_actor, const char* a_channel, float* a_outRgb) noexcept = 0;

		/// <summary>A head part or form channel's form sent to the actor this frame.</summary>
		/// <returns>False when nothing drives the channel on the actor</returns>
		[[nodiscard]] virtual bool GetFormChannel(RE::Actor* a_actor, const char* a_channel, RE::TESForm*& a_outForm) noexcept = 0;

		/// <summary>Each morph name in the body TRIs on the actor's shapes, outfits included.</summary>
		virtual void ForEachBodyMorph(RE::Actor* a_actor, NameCallback a_callback, void* a_userData) noexcept = 0;

		/// <summary>Each morph name in the TRIs on the actor's head shapes.</summary>
		virtual void ForEachFaceMorph(RE::Actor* a_actor, NameCallback a_callback, void* a_userData) noexcept = 0;

		/// <summary>Whether any TRI on the actor has a morph of this name, face or body.</summary>
		[[nodiscard]] virtual bool HasMorph(RE::Actor* a_actor, const char* a_morph) noexcept = 0;

		/// <summary>Each shape of the actor's 3D.</summary>
		virtual void ForEachShape(RE::Actor* a_actor, ShapeCallback a_callback, void* a_userData) noexcept = 0;
	};

	using IQueryInterface = IQueryInterface1;

	using _RequestPluginAPI_Query = IQueryInterface* (*)(InterfaceVersion a_interfaceVersion, const char* a_pluginName, REL::Version a_pluginVersion);

	/// <summary>
	/// Requests DCMF's query interface; call it once the game's data is loaded.
	/// </summary>
	/// <returns>The interface, or nullptr when DCMF is not loaded or the version is not supported</returns>
	inline IQueryInterface* GetAPI(InterfaceVersion a_interfaceVersion = InterfaceVersion::Latest)
	{
		static IQueryInterface* api = nullptr;
		if (api) {
			return api;
		}
		const auto module = GetModuleHandleW(L"DCMF.dll");
		const auto request = module ? reinterpret_cast<_RequestPluginAPI_Query>(GetProcAddress(module, "RequestPluginAPI_Query")) : nullptr;
		if (!request) {
			return nullptr;
		}
		const auto plugin = SKSE::PluginDeclaration::GetSingleton();
		api = request(a_interfaceVersion, plugin->GetName().data(), plugin->GetVersion());
		return api;
	}
}
