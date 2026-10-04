#include "Entries.h"

#include "ActorProperty.h"
#include "HeadPart.h"
#include "Mfg.h"
#include "Material.h"
#include "scan/Scan.h"

#include <mutex>
#include <unordered_set>

namespace Apply
{
	namespace
	{
		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			return out;
		}

		constexpr std::size_t Slot(Kind a_kind)
		{
			return static_cast<std::size_t>(a_kind);
		}
	}

	void Entries::Sync()
	{
		if (!_bFixedAdded) {
			_bFixedAdded = true;
			AddFixed();
		}

		const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue();
		if (!catalogue || catalogue.get() == _populatedFrom) {
			return;
		}
		Populate(*catalogue);
		_populatedFrom = catalogue.get();
	}

	// The kinds the code knows whole; their indices are what the appliers count on
	void Entries::AddFixed()
	{
		for (const auto& slot : Mfg::Slots()) {
			Entry entry;
			entry.name = slot.name;
			entry.display = slot.name;
			entry.group = slot.set == 0 ? "Engine expression" : slot.set == 1 ? "Engine modifier" : slot.set == 2 ? "Engine phoneme" : "Engine custom";
			entry.description = slot.set == 0 ? "An engine expression, as the mfg console command sets it." :
			                    slot.set == 1 ? "An engine face modifier, as the mfg console command sets it." :
			                    slot.set == 2 ? "An engine phoneme mouth shape, as the mfg console command sets it." :
			                                    "An engine custom face slot, as the mfg console command sets it.";
			Add(Kind::kMfg, std::move(entry));
		}
		// The engine's own face animation, stopped by a switch: what it does to the tiers is taken out at the source,
		// and a channel still writes on top
		struct Control
		{
			const char* name;
			const char* display;
			const char* description;
		};
		for (const auto& [name, display, description] : { Control{ "StopBlink", "Stop blink", "Stops the engine's own blinking." }, Control{ "StopEyeMovement", "Stop eye movement", "Stops the engine's own eye movement." },
				 Control{ "StopLipSync", "Stop lip sync", "Stops the engine's lip sync while talking." }, Control{ "StopDialogueEmotion", "Stop dialogue emotion", "Stops the expression the engine plays with dialogue." } }) {
			Entry entry;
			entry.name = name;
			entry.display = display;
			entry.description = description;
			entry.group = "Engine control";
			entry.type = ValueType::kSwitch;
			Add(Kind::kMfg, std::move(entry));
		}

		// Bones: the spine and the head, four entries each, in the order Bone::Property counts; the group is the node.
		// Out of the pickers: the pose channels below drive them
		for (const auto* bone : { "NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]", "NPC Neck [Neck]", "NPC Head [Head]" }) {
			for (const auto* property : { "scale", "x", "y", "z" }) {
				Entry entry;
				entry.name = std::format("{}.{}", bone, property);
				entry.display = property;
				entry.group = bone;
				entry.bHidden = true;
				if (property == "scale"sv) {
					entry.low = 0.25f;
					entry.high = 4.f;
					entry.neutral = 1.f;
				} else {
					entry.low = -1.5f;
					entry.high = 1.5f;
				}
				Add(Kind::kBone, std::move(entry));
			}
		}

		// Grouped by what they belong to: a head part with its own material, the skin's material with the skin set
		const auto partGroup = [](std::string_view a_part) {
			std::string group(a_part);
			if (!group.empty()) {
				group[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(group[0])));
			}
			return group;
		};

		for (const auto& slot : HeadPart::Slots()) {
			Entry entry;
			entry.name = slot.name;
			entry.display = slot.name;
			entry.group = partGroup(slot.name);
			entry.description = std::format("Which {} head part the actor wears.", slot.name);
			entry.type = ValueType::kReference;
			entry.bRebuilds = true;
			Add(Kind::kHeadPart, std::move(entry));
		}

		for (const auto* name : { "hair.tint", "brows.tint", "beard.tint", "eyes.glow" }) {
			Entry entry;
			entry.name = name;
			entry.display = name;
			entry.group = partGroup(std::string_view(name).substr(0, std::string_view(name).find('.')));
			entry.description = name == "eyes.glow"sv ? "The eyes' glow colour." : std::format("Tints the {}.", std::string_view(name).substr(0, std::string_view(name).find('.')));
			entry.type = ValueType::kColour;
			Add(Kind::kMaterial, std::move(entry));
		}
		// Ranges and starting points as legacy had them: a wet skin is glossiness in the hundreds and specular up several times
		struct Number
		{
			const char* name;
			float low;
			float high;
			float start;
			const char* description;
		};
		for (const auto& number : { Number{ "eyes.glowstrength", 0.f, 20.f, 1.f, "How strongly the eyes glow." }, Number{ "skin.glossiness", 1.f, 1000.f, 30.f, "How sharp the skin's shine is." },
				 Number{ "skin.specular", 0.f, 30.f, 1.f, "How strong the skin's shine is." }, Number{ "skin.softlighting", 0.f, 5.f, 0.3f, "How much light passes through the skin." },
				 Number{ "skin.rimlighting", 0.f, 5.f, 2.f, "How brightly the skin's edges catch light from behind." } }) {
			Entry entry;
			entry.name = number.name;
			entry.display = number.name;
			entry.description = number.description;
			entry.group = partGroup(std::string_view(number.name).substr(0, std::string_view(number.name).find('.')));
			entry.low = number.low;
			entry.high = number.high;
			entry.start = number.start;
			entry.neutral = -1.f;  // no value means not driven; every real value is
			Add(Kind::kMaterial, std::move(entry));
		}

		{
			Entry weight;
			weight.name = "weight";
			weight.display = "Weight";
			weight.group = "Actor";
			weight.description = "The actor's own weight; changing it rebuilds the actor.";
			weight.high = 100.f;
			weight.type = ValueType::kSteppedNumber;
			weight.neutral = -1.f;  // no weight means not driven; every real value is
			weight.bRebuilds = true;
			Add(Kind::kActorProperty, std::move(weight));

			Entry alpha;
			alpha.name = "alpha";
			alpha.display = "Alpha";
			alpha.group = "Actor";
			alpha.description = "How opaque the actor is.";
			alpha.neutral = 1.f;
			Add(Kind::kActorProperty, std::move(alpha));
		}

		// The skin set worn: one channel, picked from every set the mods define; nothing holds two
		{
			Entry set;
			set.name = "set";
			set.display = "Skin set";
			set.group = "Skin";
			set.description = "Which of the mods' skin sets the actor wears.";
			set.type = ValueType::kReference;
			Add(Kind::kSkin, std::move(set));
		}

		// The body preset: a BodySlide preset by name, the shape the body morphs add onto
		{
			Entry preset;
			preset.name = "preset";
			preset.display = "Body preset";
			preset.group = "Body preset";
			preset.description = "A BodySlide preset the body morphs add onto.";
			preset.type = ValueType::kBlendedPick;
			Add(Kind::kBodyMorph, std::move(preset));

			// The weight the preset is read at, moving smoothly where the actor's own rebuilds the 3D; the actor's own
			// when nothing drives it
			Entry weight;
			weight.name = "weight";
			weight.display = "Body weight";
			weight.group = "Body preset";
			weight.description = "The weight the body preset is read at, without rebuilding the actor.";
			weight.high = 100.f;
			weight.start = 50.f;
			weight.neutral = -1.f;  // no value means not driven; every real value is
			Add(Kind::kBodyMorph, std::move(weight));
		}

		// The three segments, one channel per part: a mode first, then what the mode reads, a target and its points for
		// Track, angles on top of any mode. A part nobody drives is at its rest
		for (const auto [kind, segment] : { std::pair{ Kind::kSpine, "spine"sv }, std::pair{ Kind::kHead, "head"sv }, std::pair{ Kind::kGaze, "gaze"sv } }) {
			const auto add = [&](const char* a_name, const char* a_display, std::string a_description, ValueType a_type, float a_low = 0.f, float a_high = 1.f, float a_start = 0.f, std::vector<std::string> a_modes = {}) {
				Entry entry;
				entry.name = a_name;
				entry.display = a_display;
				entry.description = std::move(a_description);
				entry.group = segment == "spine"sv ? "Spine pose" : segment == "head"sv ? "Head pose" : "Gaze";
				entry.type = a_type;
				entry.low = a_low;
				entry.high = a_high;
				entry.start = a_start;
				entry.neutral = -1.f;  // no value means not driven; every real value, the first mode included, is
				entry.modes = std::move(a_modes);
				Add(kind, std::move(entry));
			};
			std::vector<std::string> modes = segment == "gaze"sv ? std::vector<std::string>{ "Engine", "Fixed", "Track", "Offset" } : std::vector<std::string>{ "Offset", "Fixed", "Track" };
			const std::string part = segment == "gaze"sv ? "eyes" : std::string(segment);
			add("mode", "Mode", segment == "gaze"sv ? "Engine leaves the eyes to the game, Fixed holds the angles, Track looks at the target, Offset adds the angles to where the game looks." : "Offset adds the angles to the animation, Fixed holds them, Track turns toward the target.",
				ValueType::kMode, 0.f, 1.f, 0.f, std::move(modes));
			add("target", "Target", "What Track looks at: the engine's own target, the camera, the player or a chosen reference.", ValueType::kMode, 0.f, 1.f, 0.f, { "Engine", "Camera", "Player", "Form" });
			add("form", "Form", "The reference Track looks at when the target is Form.", ValueType::kForm);
			add("targetBone", "Target bone", "The bone on the target to look at; its eyes when empty.", ValueType::kText);
			add("headTail", "Head/tail value", "Where on the target bone to look, from its head (0) to its tail (1).", ValueType::kNumber, 0.f, 1.f, 0.f);
			add("pairedBone", "Paired bone", "A second bone; the look aims halfway between the two.", ValueType::kText);
			add("pairedHeadTail", "Paired head/tail value", "Where on the paired bone to look, from its head (0) to its tail (1).", ValueType::kNumber, 0.f, 1.f, 0.f);
			add("weight", "Weight", std::format("How much of this pose the {} takes over the animation.", part), ValueType::kNumber, 0.f, 1.f, 1.f);
			add("pitch", "Pitch", std::format("Turns the {} up or down, in degrees.", part), ValueType::kNumber, -90.f, 90.f, 0.f);
			add("yaw", "Yaw", std::format("Turns the {} left or right, in degrees.", part), ValueType::kNumber, -90.f, 90.f, 0.f);
			if (segment != "gaze"sv) {
				add("roll", "Roll", std::format("Tilts the {} sideways, in degrees.", part), ValueType::kNumber, -90.f, 90.f, 0.f);
			}
		}
	}

	void Entries::Populate(const Scan::Catalogue& a_catalogue)
	{
		// Body morphs: every name any BodySlide project, output TRI or RaceMenu script knows
		for (const auto& morph : a_catalogue.bodyMorphNames) {
			if (Find(Kind::kBodyMorph, morph.name).IsValid()) {
				continue;
			}
			Entry entry;
			entry.name = morph.name;
			entry.display = morph.display.empty() ? morph.name : morph.display;
			entry.group = morph.category;
			entry.low = morph.min;
			entry.high = morph.max;
			entry.bScanned = true;
			Add(Kind::kBodyMorph, std::move(entry));
		}

		// Face morphs: every name any head TRI carries that the mfg sets cannot reach. The four Look morphs are kept as
		// well: UBE's extension TRI carries a stronger version of them, which its patch writes through the base
		const auto isLook = [](std::string_view a_name) { return a_name == "LookUp"sv || a_name == "LookDown"sv || a_name == "LookLeft"sv || a_name == "LookRight"sv; };
		for (const auto& morph : a_catalogue.faceMorphNames) {
			if ((morph.category.starts_with("Engine") && !isLook(morph.name)) || Find(Kind::kFaceMorph, morph.name).IsValid()) {
				continue;
			}
			Entry entry;
			entry.name = morph.name;
			entry.display = morph.display.empty() ? morph.name : morph.display;
			entry.group = morph.category;
			entry.low = morph.min;
			entry.high = morph.max;
			entry.bScanned = true;
			Add(Kind::kFaceMorph, std::move(entry));
		}

		// TODO: overlays and skins, once their appliers exist
	}

	ChannelId Entries::Add(Kind a_kind, Entry a_entry)
	{
		std::unique_lock lock(_lock);
		auto& entries = _entries[Slot(a_kind)];
		const auto index = static_cast<std::uint32_t>(entries.size());
		_byName[Slot(a_kind)].emplace(Lower(a_entry.name), index);
		entries.push_back(std::move(a_entry));
		++_generation;
		return { a_kind, index };
	}

	namespace
	{
		struct SlotSub
		{
			const char* suffix;
			const char* display;
			ValueType type;
			float high;
			float start;
			const char* description;
		};

		constexpr std::array kSlotSubs{ SlotSub{ "", "Overlay", ValueType::kReference, 1.f, 0.f, "Which overlay of the slot's pool is shown." }, SlotSub{ ".alpha", "Alpha", ValueType::kNumber, 1.f, 1.f, "How opaque the overlay is." },
			SlotSub{ ".tint", "Tint", ValueType::kColour, 1.f, 0.f, "Tints the overlay." }, SlotSub{ ".glow", "Glow", ValueType::kColour, 1.f, 0.f, "The overlay's glow colour." },
			SlotSub{ ".glowstrength", "Glow strength", ValueType::kNumber, 20.f, 1.f, "How strongly the overlay glows." } };
	}

	void Entries::AddOverlaySlot(std::string_view a_name)
	{
		for (const auto& sub : kSlotSubs) {
			const auto name = std::format("{}{}", a_name, sub.suffix);
			// A slot of this name was here before: its rows come back as they were
			if (const auto id = Find(Kind::kOverlay, name); id.IsValid()) {
				std::unique_lock lock(_lock);
				_entries[Slot(Kind::kOverlay)][id.GetIndex()].bRetired = false;
				++_generation;
				continue;
			}
			Entry entry;
			entry.name = name;
			entry.display = sub.display;
			entry.description = sub.description;
			entry.group = a_name;
			entry.type = sub.type;
			entry.high = sub.high;
			entry.start = sub.start;
			if (sub.type == ValueType::kNumber) {
				entry.neutral = -1.f;  // no value means not driven; every real value is
			}
			Add(Kind::kOverlay, std::move(entry));
		}
	}

	void Entries::RetireOverlaySlot(std::string_view a_name)
	{
		for (const auto& sub : kSlotSubs) {
			if (const auto id = Find(Kind::kOverlay, std::format("{}{}", a_name, sub.suffix)); id.IsValid()) {
				std::unique_lock lock(_lock);
				_entries[Slot(Kind::kOverlay)][id.GetIndex()].bRetired = true;
				++_generation;
			}
		}
	}

	std::uint32_t Entries::Generation() const
	{
		std::shared_lock lock(_lock);
		return _generation;
	}

	ChannelId Entries::Find(Kind a_kind, std::string_view a_name) const
	{
		std::shared_lock lock(_lock);
		const auto& byName = _byName[Slot(a_kind)];
		auto it = byName.find(Lower(a_name));
		// A pose channel by the name it had before, as older files write it
		if (it == byName.end() && (a_kind == Kind::kSpine || a_kind == Kind::kHead || a_kind == Kind::kGaze)) {
			const auto lower = Lower(a_name);
			const auto renamed = lower == "point"sv ? "targetbone"sv : lower == "along"sv ? "headtail"sv : lower == "second"sv ? "pairedbone"sv : lower == "secondalong"sv ? "pairedheadtail"sv : ""sv;
			if (!renamed.empty()) {
				it = byName.find(std::string(renamed));
			}
		}
		return it != byName.end() ? ChannelId{ a_kind, it->second } : ChannelId{};
	}

	ChannelId Entries::Require(Kind a_kind, std::string_view a_name) const
	{
		const auto id = Find(a_kind, a_name);
		if (!id.IsValid()) {
			static std::mutex lock;
			static std::unordered_set<std::string> said;
			std::scoped_lock guard(lock);
			if (said.insert(std::format("{}:{}", KindName(a_kind), a_name)).second) {
				logger::error("entries: the channel {}:{} is not registered, though this plugin registers it; nothing that drives it is applied", KindName(a_kind), a_name);
			}
		}
		return id;
	}

	ChannelId Entries::Find(std::string_view a_identifier) const
	{
		const auto colon = a_identifier.find(':');
		if (colon == std::string_view::npos) {
			return {};
		}
		const auto kindName = a_identifier.substr(0, colon);
		for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(Kind::kTotal); ++i) {
			const auto kind = static_cast<Kind>(i);
			if (KindName(kind) == kindName) {
				return Find(kind, a_identifier.substr(colon + 1));
			}
		}
		return {};
	}

	std::string Entries::Identifier(ChannelId a_id) const
	{
		if (!a_id.IsValid()) {
			return {};
		}
		std::shared_lock lock(_lock);
		const auto& entries = _entries[Slot(a_id.GetKind())];
		if (a_id.GetIndex() >= entries.size()) {
			logger::error("entries: channel {}:{} is past the {} entries of its table", KindName(a_id.GetKind()), a_id.GetIndex(), entries.size());
			return {};
		}
		return std::format("{}:{}", KindName(a_id.GetKind()), entries[a_id.GetIndex()].name);
	}

	Entry Entries::Get(ChannelId a_id) const
	{
		std::shared_lock lock(_lock);
		if (!a_id.IsValid()) {
			return {};
		}
		const auto& entries = _entries[Slot(a_id.GetKind())];
		if (a_id.GetIndex() >= entries.size()) {
			logger::error("entries: channel {}:{} is past the {} entries of its table", KindName(a_id.GetKind()), a_id.GetIndex(), entries.size());
			return {};
		}
		return entries[a_id.GetIndex()];
	}

	std::size_t Entries::Count(Kind a_kind) const
	{
		std::shared_lock lock(_lock);
		return _entries[Slot(a_kind)].size();
	}

	void Entries::ForEach(Kind a_kind, const std::function<void(ChannelId, const Entry&)>& a_function) const
	{
		std::shared_lock lock(_lock);
		const auto& entries = _entries[Slot(a_kind)];
		for (std::uint32_t i = 0; i < entries.size(); ++i) {
			if (!entries[i].bRetired && !entries[i].bHidden) {
				a_function({ a_kind, i }, entries[i]);
			}
		}
	}
}
