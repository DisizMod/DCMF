#pragma once

#include <limits>

// For modders: copy this file into your own project to register values DCMF's conditions and channels can read
namespace DCMF_API::Resources
{
	enum class InterfaceVersion : std::uint8_t
	{
		V1,

		Latest = V1
	};

	// What a resource holds
	enum class ValueType : std::uint8_t
	{
		Number,
		Form,

		// Several forms at once: what an actor wears, its keywords, its factions
		FormSet,

		// Several texts at once, matched exactly and whatever their case: the names of what an actor wears
		TextSet
	};

	// Whose value it is: each actor's own, or one for every actor
	enum class Scope : std::uint8_t
	{
		Actor,
		World
	};

	// How its value stays current
	enum class Refresh : std::uint8_t
	{
		// Written by its plugin through SetNumber or SetForm, and held until written again
		Stored,

		// Read through its gatherer once per DCMF tick
		EveryTick,

		// Read through its gatherer at most once every intervalSeconds
		Interval,

		// Read through its gatherer, and held until its plugin calls Invalidate
		Invalidated
	};

	enum class APIResult : std::uint8_t
	{
		// Done
		OK,

		// No resource of that key is registered
		NotFound,

		// The key is empty, holds a character other than letters, digits, '.', '_' and '-', or is already registered
		InvalidKey,

		// The resource holds another type of value
		WrongType,

		// An actor was given for a resource for every actor, or none for a per-actor one
		WrongScope,

		// A value set on a resource DCMF gathers, or Invalidate called on a stored one
		WrongRefresh,

		// A gathered resource registered without a gatherer
		NoGatherer,

		// A condition with no name, or with a name a condition already has
		InvalidName
	};

	// What a condition a plugin adds asks of one of its resources
	enum class ConditionTest : std::uint8_t
	{
		// A number resource compared with a number the author writes
		Compare,

		// A form or form set resource holding a form the author picks
		HasForm,

		// A text set resource holding a text the author writes, whatever its case
		HasText,

		// A number resource that is not 0: a yes/no row with nothing to fill in, as IsFemale is, negated for "not"
		IsTrue
	};

	// A condition a plugin adds as a test of one of its resources: DCMF draws its row, saves it, and compiles it as it
	// does its own, merged with the rows beside it that test the same resource
	struct ConditionInfo
	{
		const char* name = nullptr;         // as files write it and the picker lists it, "Arousal"
		const char* description = nullptr;  // one sentence
		const char* resource = nullptr;     // the key of the resource it tests
		ConditionTest test = ConditionTest::Compare;
		const char* operandName = nullptr;           // what the author fills in, "Arousal"
		RE::FormType formType = RE::FormType::None;  // HasForm: the forms the picker offers; any when None
	};

	// Where a set resource writes its members, one call each, so no container crosses between the two plugins
	class ISetSink
	{
	public:
		virtual void AddForm(RE::FormID a_form) noexcept = 0;
		virtual void AddText(const char* a_text) noexcept = 0;
	};

	// How DCMF reads a gathered resource: on the main thread, during its tick; the subject is the actor, or nullptr for
	// a resource for every actor
	class IGatherer
	{
	public:
		virtual ~IGatherer() = default;

		/// <summary>The number now; NaN when there is none.</summary>
		virtual float GatherNumber([[maybe_unused]] RE::TESObjectREFR* a_subject) noexcept { return std::numeric_limits<float>::quiet_NaN(); }

		/// <summary>The form now; nullptr when there is none.</summary>
		virtual RE::TESForm* GatherForm([[maybe_unused]] RE::TESObjectREFR* a_subject) noexcept { return nullptr; }

		/// <summary>The set now, member by member into the sink; nothing when it is empty.</summary>
		virtual void GatherSet([[maybe_unused]] RE::TESObjectREFR* a_subject, [[maybe_unused]] ISetSink& a_out) noexcept {}
	};

	// Told on the main thread, during DCMF's tick, as DCMF starts and stops tracking an actor; what DCMF held for an
	// actor, a stored resource's values too, is dropped when it stops, so the plugin writes them again when it starts
	class IActorListener
	{
	public:
		virtual ~IActorListener() = default;

		/// <summary>DCMF tracks the actor from now on: it came within range, or is being inspected.</summary>
		virtual void OnActorTracked([[maybe_unused]] RE::TESObjectREFR* a_actor) noexcept {}

		/// <summary>DCMF no longer tracks the actor; what it held for it is gone.</summary>
		virtual void OnActorUntracked([[maybe_unused]] RE::FormID a_actor) noexcept {}
	};

	struct ResourceInfo
	{
		const char* key = nullptr;          // what conditions and channels name it by, "mymod.wetness"
		const char* displayName = nullptr;  // optional
		const char* description = nullptr;  // optional, one sentence
		ValueType type = ValueType::Number;
		Scope scope = Scope::Actor;
		Refresh refresh = Refresh::Stored;
		float intervalSeconds = 1.f;  // Interval only
		bool bFeed = false;           // offered to channels as a value to drive them by
		float defaultNumber = 0.f;    // what it holds before anything is set or gathered
		RE::FormID defaultForm = 0;
	};

	class IResourcesInterface1
	{
	public:
		/// <summary>
		/// Registers a resource; call it once the game's data is loaded. DCMF copies the info and keeps the gatherer,
		/// which has to live as long as the game does.
		/// </summary>
		/// <param name="a_gatherer">How DCMF reads it; nullptr for a stored resource</param>
		/// <returns>OK, InvalidKey, NoGatherer</returns>
		[[nodiscard]] virtual APIResult RegisterResource(const ResourceInfo& a_info, IGatherer* a_gatherer) noexcept = 0;

		/// <summary>
		/// Sets a stored number resource. Any thread.
		/// </summary>
		/// <param name="a_subject">The actor, for a per-actor resource; nullptr for a resource for every actor</param>
		/// <returns>OK, NotFound, WrongType, WrongScope, WrongRefresh</returns>
		[[nodiscard]] virtual APIResult SetNumber(const char* a_key, RE::TESObjectREFR* a_subject, float a_value) noexcept = 0;

		/// <summary>
		/// Sets a stored form resource. Any thread.
		/// </summary>
		/// <param name="a_subject">The actor, for a per-actor resource; nullptr for a resource for every actor</param>
		/// <param name="a_form">The form; nullptr for none</param>
		/// <returns>OK, NotFound, WrongType, WrongScope, WrongRefresh</returns>
		[[nodiscard]] virtual APIResult SetForm(const char* a_key, RE::TESObjectREFR* a_subject, RE::TESForm* a_form) noexcept = 0;

		/// <summary>
		/// Drops what DCMF holds of a gathered resource, so it is read again when next needed. Any thread.
		/// </summary>
		/// <param name="a_subject">The actor; nullptr for every actor</param>
		/// <returns>OK, NotFound, WrongRefresh</returns>
		[[nodiscard]] virtual APIResult Invalidate(const char* a_key, RE::TESObjectREFR* a_subject) noexcept = 0;

		/// <summary>
		/// Adds a condition testing one of the plugin's resources; call it once the game's data is loaded, before any
		/// DCMF mod is read. DCMF copies the info.
		/// </summary>
		/// <returns>OK, InvalidName, NotFound when the resource is not registered yet</returns>
		[[nodiscard]] virtual APIResult RegisterCondition(const ConditionInfo& a_info) noexcept = 0;

		/// <summary>
		/// Adds a listener told as DCMF starts and stops tracking an actor. DCMF keeps it, so it has to live as long as
		/// the game does. Any thread.
		/// </summary>
		/// <returns>OK</returns>
		[[nodiscard]] virtual APIResult AddActorListener(IActorListener* a_listener) noexcept = 0;
	};

	using IResourcesInterface = IResourcesInterface1;

	using _RequestPluginAPI_Resources = IResourcesInterface* (*)(InterfaceVersion a_interfaceVersion, const char* a_pluginName, REL::Version a_pluginVersion);

	/// <summary>
	/// Requests DCMF's resources interface; call it once the game's data is loaded.
	/// </summary>
	/// <param name="a_interfaceVersion">The interface version to request</param>
	/// <returns>The interface, or nullptr when DCMF is not loaded or the version is not supported</returns>
	inline IResourcesInterface* GetAPI(InterfaceVersion a_interfaceVersion = InterfaceVersion::Latest)
	{
		static IResourcesInterface* api = nullptr;
		if (api) {
			return api;
		}
		const auto module = GetModuleHandleW(L"DCMF.dll");
		const auto request = module ? reinterpret_cast<_RequestPluginAPI_Resources>(GetProcAddress(module, "RequestPluginAPI_Resources")) : nullptr;
		if (!request) {
			return nullptr;
		}
		const auto plugin = SKSE::PluginDeclaration::GetSingleton();
		api = request(a_interfaceVersion, plugin->GetName().data(), plugin->GetVersion());
		return api;
	}
}
