#pragma once

#include "Kind.h"

#include <array>
#include <functional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Scan
{
	struct Catalogue;
}

namespace Apply
{
	enum class ValueType : std::uint8_t
	{
		kNumber,
		kColour,
		kMode,
		kReference,
		kSwitch,
		kText,  // a name, typed or picked: a bone
		kForm,  // a plugin name and an id
		kSteppedNumber,  // a number only ever set at once, never eased: the actor's weight
		kBlendedPick     // a name picked from a list, eased through what it names: the body preset, as its sliders
	};

	// One thing a kind can drive
	struct Entry
	{
		std::string name;     // within its kind; what a file writes after "kind:"
		std::string display;  // what the picker shows; the name when nothing else is known
		std::string group;    // what the picker sorts it under: a BodySlide category, "Engine phoneme"
		std::string description;  // one sentence on what it does, for a channel the code defines; none for one read from the load order
		ValueType type = ValueType::kNumber;
		float low = 0.f;
		float high = 1.f;
		float neutral = 0.f;
		float start = 0.f;  // what a slider shows before it is held, for an entry whose neutral means not driven
		bool bScanned = false;   // from the load order rather than the code
		bool bRebuilds = false;  // taking it rebuilds the actor's 3D, so it is never blended
		bool bRetired = false;   // its overlay slot was removed: kept so the id stays, out of the pickers
		bool bHidden = false;    // driven only through another kind's channels: out of the pickers, still in the table
		std::vector<std::string> modes;  // a mode's names, the first its rest
	};

	// One table per kind. A table only ever grows within a session, so an id handed
	// out before a rescan still names the same entry after it; a removed slot's rows retire instead.
	class Entries
	{
	public:
		static Entries& GetSingleton()
		{
			static Entries singleton;
			return singleton;
		}

		// Main thread, once a tick: takes the scan's newest catalogue into the scanned kinds
		void Sync();

		// An overlay slot's five channels, as legacy had them: the pick, alpha, tint, glow and glow strength
		void AddOverlaySlot(std::string_view a_name);
		void RetireOverlaySlot(std::string_view a_name);

		// Bumped by every add and retire, for a list that caches the tables
		[[nodiscard]] std::uint32_t Generation() const;

		[[nodiscard]] ChannelId Find(Kind a_kind, std::string_view a_name) const;
		[[nodiscard]] ChannelId Find(std::string_view a_identifier) const;  // "body:BreastsSH"
		// A channel this plugin registers itself: not found is a bug, logged once per name, and the id is invalid
		[[nodiscard]] ChannelId Require(Kind a_kind, std::string_view a_name) const;
		[[nodiscard]] std::string Identifier(ChannelId a_id) const;

		// A copy: the table can grow under a reader
		[[nodiscard]] Entry Get(ChannelId a_id) const;
		[[nodiscard]] std::size_t Count(Kind a_kind) const;
		void ForEach(Kind a_kind, const std::function<void(ChannelId, const Entry&)>& a_function) const;

	private:
		Entries() = default;

		ChannelId Add(Kind a_kind, Entry a_entry);
		void AddFixed();
		void Populate(const Scan::Catalogue& a_catalogue);

		mutable std::shared_mutex _lock;
		std::array<std::vector<Entry>, static_cast<std::size_t>(Kind::kTotal)> _entries;
		std::array<std::unordered_map<std::string, std::uint32_t>, static_cast<std::size_t>(Kind::kTotal)> _byName;  // lowercased
		const void* _populatedFrom = nullptr;
		bool _bFixedAdded = false;
		std::uint32_t _generation = 0;
	};
}
