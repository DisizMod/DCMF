#pragma once

#include "SharedTypes.h"

#include "Resources.h"

class SubMod;

#include <functional>
#include <string>
#include <string_view>
#include <vector>

// DCMF's own resources: what its conditions read, registered and cached as any plugin's are
namespace BuiltinResources
{
	// What an actor wears, read again on an equip and every few seconds besides: the forms, their keywords, their names,
	// the plugins that add them
	inline constexpr auto kWorn = "dcmf.worn"sv;
	inline constexpr auto kWornKeywords = "dcmf.wornKeywords"sv;
	inline constexpr auto kWornNames = "dcmf.wornNames"sv;
	inline constexpr auto kWornPlugins = "dcmf.wornPlugins"sv;
	// What an actor is, read every tick
	inline constexpr auto kRace = "dcmf.race"sv;
	inline constexpr auto kActorBase = "dcmf.actorBase"sv;

	// Main thread, at load: the targets a TARGET row reads its subject from, what an actor wears and is, and the equip
	// events that make what it wears read again
	void Register();

	// A number, a form or a set of forms read by a function, registered under the key the first time it is asked for. The
	// registry keeps the function for the game's life: it holds nothing a row owns
	[[nodiscard]] std::string Number(std::string a_key, std::string_view a_description, std::function<float(RE::TESObjectREFR*)> a_read, Resources::Scope a_scope = Resources::Scope::Actor);
	[[nodiscard]] std::string Form(std::string a_key, std::string_view a_description, std::function<RE::TESForm*(RE::TESObjectREFR*)> a_read, Resources::Scope a_scope = Resources::Scope::Actor);
	[[nodiscard]] std::string FormSet(std::string a_key, std::string_view a_description, std::function<void(RE::TESObjectREFR*, std::vector<RE::FormID>&)> a_read, float a_intervalSeconds = 0.f);
	[[nodiscard]] std::string TextSet(std::string a_key, std::string_view a_description, std::function<void(RE::TESObjectREFR*, std::vector<std::string>&)> a_read);
	// A number drawn between the two for each actor, held until the key is let go
	void Drawn(const std::string& a_key, float a_low, float a_high);
	// How long the actor has stood idle, counted from when it was first read
	[[nodiscard]] std::string IdleTime();
	// The angle of the ground under the actor to its facing, the surface's normal smoothed by the factor
	[[nodiscard]] std::string SurfaceAngle(float a_smoothing, bool a_bNavmesh, bool a_bDegrees);
	// The slots worn and the keywords of what is worn in one, read again on an equip like what is worn
	inline constexpr auto kWornSlots = "dcmf.wornSlots"sv;
	[[nodiscard]] std::string WornSlotKeywordsKey(std::uint32_t a_slot);

	// A resource read again for an actor when the clip loops or echoes on it, as a Random row that asks to
	void ResetWith(const SubMod* a_clip, std::string a_key);
	void OnLoopOrEcho(const SubMod* a_clip, RE::TESObjectREFR* a_refr);

	// The key of an actor value as a number component reads it, registered the first time it is asked for
	[[nodiscard]] std::string ActorValueKey(const Components::NumericValue& a_value);
	// The key of a global variable's value, registered as an actor value's is
	[[nodiscard]] std::string GlobalKey(const Components::NumericValue& a_value);
	// The key of an actor's target of a kind, Utils::TargetType
	[[nodiscard]] std::string TargetKey(std::int32_t a_targetType);
}
