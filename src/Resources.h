#pragma once

#include "API/DCMF-ResourcesAPI.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Values other plugins register for DCMF to read: stored, which they write, or gathered, which DCMF reads through them
namespace Resources
{
	using ValueType = DCMF_API::Resources::ValueType;
	using Scope = DCMF_API::Resources::Scope;
	using Refresh = DCMF_API::Resources::Refresh;
	using Result = DCMF_API::Resources::APIResult;
	using Gatherer = DCMF_API::Resources::IGatherer;

	struct Info
	{
		std::string key;
		std::string displayName;
		std::string description;
		std::string plugin;  // the plugin that registered it
		ValueType type = ValueType::Number;
		Scope scope = Scope::Actor;
		Refresh refresh = Refresh::Stored;
		float intervalSeconds = 1.f;
		bool bFeed = false;
		float defaultNumber = 0.f;
		RE::FormID defaultForm = 0;
	};

	[[nodiscard]] std::string_view TypeName(ValueType a_type);
	[[nodiscard]] std::string_view ScopeName(Scope a_scope);
	[[nodiscard]] std::string RefreshName(const Info& a_info);

	// Any thread: a resource added, its gatherer kept; refused when the key is malformed or taken
	Result Register(Info a_info, Gatherer* a_gatherer);

	[[nodiscard]] bool Exists(std::string_view a_key);
	// Every resource registered, by key
	[[nodiscard]] std::vector<Info> List();
	// The keys of the resources of a type flagged as feeds, which channels may be driven by
	[[nodiscard]] std::vector<std::string> FeedKeys(ValueType a_type);

	// Any thread: a stored resource's value, for an actor or for every actor by its scope
	Result SetNumber(std::string_view a_key, RE::TESObjectREFR* a_subject, float a_value);
	Result SetForm(std::string_view a_key, RE::TESObjectREFR* a_subject, RE::TESForm* a_form);
	// Any thread: a gathered resource read again when next needed, for an actor or, with none, for everyone
	Result Invalidate(std::string_view a_key, RE::TESObjectREFR* a_subject);

	// What a number resource holds for a reference: gathered when due on the main thread, what was last held anywhere
	// else, its default when nothing is; none when no number resource has the key
	[[nodiscard]] std::optional<float> GetNumber(std::string_view a_key, RE::TESObjectREFR* a_refr);
	// What a form resource holds for a reference, as a number resource's; nullptr when no form resource has the key
	[[nodiscard]] RE::TESForm* GetForm(std::string_view a_key, RE::TESObjectREFR* a_refr);

	// What a form, form set or text set resource holds for a reference, sorted: form ids, or text keys; false when no
	// such resource has the key
	bool GetSet(std::string_view a_key, RE::TESObjectREFR* a_refr, std::vector<std::uint64_t>& a_out);
	// A text as a text set holds it: lowercased and hashed, so a row's text and a gathered one meet
	[[nodiscard]] std::uint64_t TextKey(std::string_view a_text);
	// Every gathered value dropped, as when another game is loaded over this one
	void InvalidateAll();

	// Any thread: a listener told as actors are tracked and untracked
	void AddActorListener(DCMF_API::Resources::IActorListener* a_listener);
	// Main thread: an actor DCMF began tracking, the listeners told
	void OnTracked(RE::TESObjectREFR* a_actor);
	// Main thread: an actor DCMF stopped tracking, every value held for it dropped, stored ones too, the listeners told
	void OnUntracked(RE::FormID a_actor);

	// Main thread, every frame: the thread gathering may run on
	void SetMainThread();
	// Main thread, at the start of each DCMF tick: what is gathered every tick is due again
	void BeginTick();
}
