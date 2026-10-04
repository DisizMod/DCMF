#pragma once

#include <cstdint>
#include <string_view>

namespace Apply
{
	// One applier each; the list is fixed, only what some kinds hold comes from the load order
	enum class Kind : std::uint8_t
	{
		kMfg,            // BSFaceGenAnimationData: expressions, phonemes, modifiers, gaze, the stops, as the mfg console command sets them
		kFaceMorph,      // morphs with no mfg slot, added into the base morph
		kBodyMorph,      // BodySlide morphs, summed into the skin partition
		kBone,           // spine and head turns, node scales
		kHeadTrack,      // holding off the engine's own look-at
		kHeadPart,       // hair, brows, beard, scars, eyes
		kActorProperty,  // weight, alpha
		kMaterial,       // tints, glow, gloss
		kOverlay,        // a texture cloned onto the skin, and its alpha, tint and glow
		kSkin,           // the skin's own textures
		kSpine,          // a segment's pose, one channel per part, expanded into bone and mfg numbers; no applier of its own
		kHead,
		kGaze,

		kTotal
	};

	// The engine passes an applier runs in; a kind may need more than one
	enum class Pass : std::uint8_t
	{
		kUpdate = 1 << 0,     // the actor update, main thread
		kFaceAnimation = 1 << 1,  // BSFaceGenAnimationData::KeyframesUpdate
		kFaceNode = 1 << 2,   // BSFaceGenNiNode::UpdateDownwardPass
		kFadeNode = 1 << 3    // BSFadeNode::UpdateDownwardPass, after animation
	};

	[[nodiscard]] constexpr std::uint8_t PassesOf(Kind a_kind)
	{
		constexpr auto bit = [](Pass a_pass) { return static_cast<std::uint8_t>(a_pass); };
		switch (a_kind) {
		case Kind::kMfg:
			return bit(Pass::kFaceAnimation);
		case Kind::kFaceMorph:
			return bit(Pass::kFaceNode);
		case Kind::kBone:
			return bit(Pass::kFadeNode);
		case Kind::kOverlay:
			return bit(Pass::kUpdate) | bit(Pass::kFaceNode);  // built and written on the main thread, a face clone's block followed after the face animates
		default:
			return bit(Pass::kUpdate);
		}
	}

	[[nodiscard]] constexpr bool RunsIn(Kind a_kind, Pass a_pass)
	{
		return (PassesOf(a_kind) & static_cast<std::uint8_t>(a_pass)) != 0;
	}

	// What a file writes before the ':' of a channel, and what the pickers group by
	[[nodiscard]] constexpr std::string_view KindName(Kind a_kind)
	{
		switch (a_kind) {
		case Kind::kMfg:
			return "mfg";
		case Kind::kFaceMorph:
			return "face";
		case Kind::kBodyMorph:
			return "body";
		case Kind::kBone:
			return "bone";
		case Kind::kHeadTrack:
			return "headtrack";
		case Kind::kHeadPart:
			return "headpart";
		case Kind::kActorProperty:
			return "actor";
		case Kind::kMaterial:
			return "material";
		case Kind::kOverlay:
			return "overlay";
		case Kind::kSkin:
			return "skin";
		case Kind::kSpine:
			return "spine";
		case Kind::kHead:
			return "head";
		case Kind::kGaze:
			return "gaze";
		default:
			return "";
		}
	}

	// A kind and an index into its table, packed so a frame passes it around as one word.
	// Indices are handed out once and never reused within a session.
	struct ChannelId
	{
		static constexpr std::uint32_t kNone = 0xFFFFFFFF;
		static constexpr std::uint32_t kIndexBits = 24;

		std::uint32_t raw = kNone;

		constexpr ChannelId() = default;
		constexpr ChannelId(Kind a_kind, std::uint32_t a_index) :
			raw((static_cast<std::uint32_t>(a_kind) << kIndexBits) | (a_index & ((1u << kIndexBits) - 1))) {}

		[[nodiscard]] constexpr bool IsValid() const { return raw != kNone; }
		[[nodiscard]] constexpr Kind GetKind() const { return static_cast<Kind>(raw >> kIndexBits); }
		[[nodiscard]] constexpr std::uint32_t GetIndex() const { return raw & ((1u << kIndexBits) - 1); }

		[[nodiscard]] constexpr bool operator==(const ChannelId&) const = default;
	};
}
