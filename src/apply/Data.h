#pragma once

#include "Kind.h"
#include "scan/Scan.h"
#include "scan/Tri.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

// What each applier keeps per actor; one ActorState subobject each, touched only through it
namespace Apply
{
	struct NumberSample
	{
		std::uint32_t index = 0;  // into the kind's table
		float value = 0.f;
	};

	struct ColourSample
	{
		std::uint32_t index = 0;
		float red = 1.f;
		float green = 1.f;
		float blue = 1.f;
	};

	struct ReferenceSample
	{
		std::uint32_t index = 0;
		std::uint64_t value = 0;  // a form id, or later a path's hash
	};

	struct TextSample
	{
		std::uint32_t index = 0;
		std::string value;
	};

	// A tracked pose, resolved to a direction: the bone pass turns the segment's chain towards it from where the
	// animation left it, the shortest way, which no angle convention can get backwards
	struct AimSample
	{
		std::uint8_t segment = 0;  // 0 spine, 1 head
		RE::NiPoint3 goal;         // unit, world
		float weight = 1.f;        // of the angle to close; for the rest pose, how far onto it from the animation's
		bool bRest = false;        // the bones below put on their rest pose first, the animation's turn of them taken off
		std::vector<std::pair<std::string, RE::NiMatrix3>> rests;  // when bRest: each bone by name with the skeleton's rest rotation, down the chain
		float armsDown = 0.f;  // when bRest: the upper arms then turned down this many degrees about the body's forward, the A pose
	};

	// A pose channel's value: what the segment does and, tracking, what it looks at
	struct PoseSample
	{
		enum class Mode : std::uint8_t
		{
			kOffset,  // the angles on the animation's pose
			kFixed,   // the rest pose, the angles on top
			kTrack,   // aimed at the target, the angles on top
			kEngine   // the gaze only: the engine's own eye tracking
		};

		enum class Target : std::uint8_t
		{
			kEngine,  // whoever the AI would have the actor look at
			kCamera,
			kPlayer,
			kForm
		};

		std::uint32_t index = 0;
		Mode mode = Mode::kOffset;
		Target target = Target::kEngine;
		RE::FormID form = 0;
		std::array<std::string, 2> bones;  // a point each: a bone by name, empty for the eyes; the second empty for one point
		std::array<float, 2> along{};      // how far along each bone, 0 its start to 1 its end
		float weight = 1.f;                // of the turn to the target this segment takes
		float pitch = 0.f;                 // degrees, on top
		float yaw = 0.f;
		float roll = 0.f;
		float strength = 1.f;  // the modifier's transition weight
	};

	// One geometry a body morph writes: where its vertices are, what they were, and the TRI shape that moves them
	struct BodyMorphShape
	{
		RE::NiPointer<RE::BSGeometry> geometry;
		RE::NiPointer<RE::NiSkinPartition> partition;
		std::shared_ptr<const std::vector<Scan::Tri::BodyTriShapeDeltas>> tri;
		std::size_t shape = 0;  // into *tri
		std::uint32_t vertexCount = 0;
		std::uint32_t stride = 0;
		bool bHalves = true;
		std::vector<std::uint8_t> base;  // the whole vertex array, as loaded

		// Two working copies: the one the render thread uploads is left alone while the next is summed into the other
		std::shared_ptr<std::vector<std::uint8_t>> work[2];
		std::vector<std::uint8_t> moved[2];  // per copy, the vertices away from base
		int next = 0;

		// Channel index to morph in the TRI shape, for the morphs this shape has
		std::vector<std::pair<std::uint32_t, std::uint32_t>> morphs;
	};

	struct BodyMorphData
	{
		std::uint32_t boundGeneration = 0;  // the parts tree the shapes were found from
		std::vector<BodyMorphShape> shapes;
		std::vector<NumberSample> applied;  // the weights last summed in
		bool bWritten = false;              // something other than the base is on the GPU
		std::vector<NumberSample> smpApplied;  // the weights last written into the CPU copy SMP reads; none is the base
	};

	// One face shape with base morph data: the original the engine built, and the TRI morphs that move it
	struct FaceMorphShape
	{
		struct Morph
		{
			std::uint32_t index;  // into the kind's table
			std::shared_ptr<const Scan::Tri::FaceTriDeltas> tri;
			std::uint32_t morph;  // into tri->morphs
		};

		RE::NiPointer<RE::BSGeometry> geometry;
		RE::NiPointer<RE::BSFaceGenBaseMorphExtraData> base;
		std::uint32_t writable = 0;
		std::vector<RE::NiPoint3> original;
		std::vector<Morph> morphs;  // sorted by index
	};

	struct FaceMorphData
	{
		std::uint32_t boundGeneration = 0;
		std::vector<FaceMorphShape> shapes;
		std::vector<NumberSample> applied;
		bool bWritten = false;
	};

	// One bone turned or scaled after animation. The pass runs more than once a frame on a root, and the
	// engine only rewrites the pose between frames, so what was written is kept to be taken back off first.
	struct BoneWrite
	{
		RE::NiPointer<RE::NiAVObject> node;
		RE::NiTransform animated;  // the pose found under our write
		RE::NiTransform written;   // what we left there
		RE::NiMatrix3 base;        // this pass: what the bone sits on, the animation's or a rest put in its place
		bool bWritten = false;
	};

	struct BoneData
	{
		std::uint32_t boundGeneration = 0;
		std::vector<std::pair<std::uint32_t, BoneWrite>> bones;  // by bone slot in the table
		std::vector<std::pair<std::string, BoneWrite>> named;    // bones the table does not have, by name: the A pose's
	};

	// The mfg arrays: which indices are held, and which were just let go of and need zeroing for a few passes
	struct MfgData
	{
		static constexpr std::uint32_t kReleasePasses = 10;

		std::array<std::uint32_t, 4> held{};       // bitmask per mfg set: expression, modifier, phoneme, custom
		std::array<std::uint32_t, 4> releasing{};  // indices being zeroed
		std::uint32_t releasePassesLeft = 0;
		bool bEyesHeld = false;  // the eye angles are ours: zeroed on release, since the engine never goes back to straight ahead on its own
		std::array<float, 2> engineEyes{};   // the engine's own aim last seen, heading and pitch, for the gaze's Offset
		std::array<float, 2> writtenEyes{};  // the base angles written last pass: the engine has not aimed since while they are still there

		// The eyes' wander: where they are off their aim, where they are going, and when the next glance is drawn
		float wanderTimer = 0.f;
		float wanderHeading = 0.f;
		float wanderPitch = 0.f;
		float wanderTargetHeading = 0.f;
		float wanderTargetPitch = 0.f;
	};

	// A head part slot swapped: the part the actor had, and the one put there
	struct HeadPartData
	{
		struct Held
		{
			bool bHeld = false;
			RE::FormID original = 0;
			RE::FormID applied = 0;
		};
		std::array<Held, 8> held{};
		bool bRefused = false;  // said once: not the player
	};

	// One shape whose material is tinted or glows, with what it had
	struct MaterialTarget
	{
		enum class Kind : std::uint8_t
		{
			kNone,
			kHair,
			kBrows,
			kBeard,
			kEyes,
			kSkin,  // body, hands and feet
			kFace
		};

		RE::NiPointer<RE::BSGeometry> geometry;
		Kind kind = Kind::kNone;
		RE::NiColor tint;
		RE::NiColor emissive;
		float emissiveMult = 1.f;
		float specularPower = 1.f;
		float specularColorScale = 1.f;
		float subSurfaceLightRolloff = 0.f;
		float rimLightPower = 0.f;
		bool bSpecular = false;
	};

	struct MaterialData
	{
		std::uint32_t boundGeneration = 0;
		std::vector<MaterialTarget> targets;
		std::vector<ColourSample> appliedColours;
		std::vector<NumberSample> appliedNumbers;
		bool bWritten = false;
	};

	// One overlay on the actor: a clone of the part's skin drawing the slot's texture, and for a face the skin's
	// morph block it shares or copies
	struct OverlayLayer
	{
		std::uint32_t slot = 0;  // the pick row's index in the table
		std::string item;  // what is drawn: an overlay of the slot's pool, by name
		std::uint32_t alpha = 0;  // the value rows' indices
		std::uint32_t tint = 0;
		std::uint32_t glow = 0;
		std::uint32_t glowStrength = 0;
		RE::NiPointer<RE::BSGeometry> clone;
		RE::NiPointer<RE::BSGeometry> skin;
		bool bShares = false;   // a face clone drawing from the skin's own block, one owner of it
		bool bOwnNormal = false;   // the texture brought a normal map; else the skin's is followed
	};

	struct OverlayData
	{
		std::uint32_t boundGeneration = 0;
		std::vector<OverlayLayer> layers;
		std::string lastError;  // said once
	};

	// One skin or face shape whose maps a skin set replaces: what it held, and what we put there
	struct SkinTarget
	{
		RE::NiPointer<RE::BSGeometry> geometry;
		Scan::SkinPart part = Scan::SkinPart::kBody;
		bool bFace = false;  // the FaceGen shape: its diffuse is the bake and stays
		std::array<RE::NiPointer<RE::NiSourceTexture>, 4> own;   // diffuse, normal, specular, subsurface as found
		std::array<RE::NiPointer<RE::NiSourceTexture>, 4> ours;  // what the set put there; null where it stated nothing
	};

	struct SkinData
	{
		std::uint32_t boundGeneration = 0;
		std::vector<SkinTarget> targets;
		std::string applied;  // the set worn, by name; empty for none
		bool bWritten = false;
	};

	// The look-at's gates on the actor's graph, as they were before we closed them
	struct HeadTrackData
	{
		struct Gate
		{
			std::string name;
			bool prior = false;
		};
		bool bHeld = false;
		std::array<Gate, 3> gates{};
		std::size_t count = 0;
	};

	// Weight and alpha: the actor's own values, kept to be put back
	struct ActorPropertyData
	{
		bool bWeightHeld = false;
		float originalWeight = 0.f;
		float appliedWeight = 0.f;
		bool bAlphaHeld = false;
		float originalAlpha = 1.f;
		float appliedAlpha = 1.f;
	};
}
