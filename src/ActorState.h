#pragma once

#include "apply/Data.h"
#include "scan/Scan.h"

#include <atomic>
#include <cassert>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

class SubMod;
class Clip;

namespace ActorState
{
	struct Tri
	{
		std::string path;
		std::string via;
	};

	// A node of the actor's 3D that leads to a shape with a TRI
	struct Part
	{
		std::string name;
		bool bGeometry = false;
		bool bHidden = false;
		std::string collision;  // the SMP config that uses this shape as a collision shape
		std::vector<Tri> tris;
		std::vector<Part> children;
	};

	struct Parts
	{
		bool bLoaded = false;
		uint32_t generation = 0;  // counts every re-read, so a reader can tell its copy is old
		std::vector<Part> roots;
		std::unordered_set<std::string> morphs;  // every morph name on every TRI here, as the scan read them
		std::unordered_set<std::string> bodyMorphs;  // those of the body TRIs alone
		std::unordered_set<std::string> faceMorphs;  // and of the head's
	};

	// One modifier on the actor: since when, and since when it is leaving, on the resolver's clock
	struct Active
	{
		const SubMod* subMod = nullptr;
		int32_t priority = 0;
		float enteredAt = 0.f;
		float leftAt = -1.f;  // set once its conditions stop holding; it stays while its end transition runs
	};

	// The modifiers whose conditions hold for the actor, highest priority first; swapped each tick by Resolve
	struct Resolved
	{
		uint32_t generation = 0;
		std::vector<Active> active;
	};

	// A body preset mix by body morph index: each slider at weight 0 and at weight 100, read at the body weight last
	using PresetMix = std::unordered_map<std::uint32_t, std::array<float, 2>>;

	// Every channel's value on this frame, one list per kind, each sorted by index; swapped each frame by Sample
	struct Sampled
	{
		uint32_t generation = 0;
		std::array<std::vector<Apply::NumberSample>, static_cast<std::size_t>(Apply::Kind::kTotal)> numbers;
		std::array<std::vector<Apply::ColourSample>, static_cast<std::size_t>(Apply::Kind::kTotal)> colours;
		std::array<std::vector<Apply::ReferenceSample>, static_cast<std::size_t>(Apply::Kind::kTotal)> references;
		std::array<std::vector<Apply::TextSample>, static_cast<std::size_t>(Apply::Kind::kTotal)> texts;
		std::vector<Apply::AimSample> aims;  // what the tracked poses came to; the bone pass solves them on the animated frame
		std::optional<std::array<float, 2>> eyes;  // the gaze's heading and pitch as fractions of the engine's bounds, -1..1
		float eyesWeight = 1.f;                    // of the eyes over the engine's own, easing to or from the gaze's Engine mode
		float eyesOffset = 0.f;                    // of the engine's own aim kept under the eyes, the gaze's Offset mode
		PresetMix presetSliders;  // the body preset's sliders, weighted as a morph is; read at the body weight and added onto the body at the end
		bool bAPose = false;      // the preview's Hold A pose: every skeleton bone on its rest, the arms down
		std::string presetName;   // the highest modifier's body preset, as it was drawn; what the Actor window shows
		std::optional<std::vector<Apply::NumberSample>> smpBody;  // the body SMP collides with: the modifiers' morphs and preset alone, held while one is mid-transition; none when the patch is off
	};

	// What the Actor window holds on the actor, over what the modifiers set
	struct Manual
	{
		std::unordered_map<uint32_t, float> values;  // by ChannelId::raw
		std::unordered_map<uint32_t, std::array<float, 3>> colours;
		std::unordered_map<uint32_t, uint64_t> references;
		std::unordered_map<uint32_t, std::string> texts;
		std::optional<std::size_t> bodyPreset;  // into the catalogue's bodyPresets
	};

	// What the pose expansion smooths between frames: each segment's last aim, degrees
	struct PoseMemory
	{
		float lastNow = -1.f;
		std::array<float, 3> yaw{};  // spine, head, gaze
		std::array<float, 3> pitch{};
		std::array<bool, 3> bAiming{};
		std::array<RE::NiPoint3, 2> direction{};  // spine, head: the tracked direction, unit, world
		std::array<bool, 2> bTracking{};
		// Each segment's mode and the one it is easing from, 0 to 1 of the way
		std::array<Apply::PoseSample::Mode, 3> mode{ Apply::PoseSample::Mode::kOffset, Apply::PoseSample::Mode::kOffset, Apply::PoseSample::Mode::kEngine };
		std::array<Apply::PoseSample::Mode, 3> previous{ Apply::PoseSample::Mode::kOffset, Apply::PoseSample::Mode::kOffset, Apply::PoseSample::Mode::kEngine };
		std::array<float, 3> modeBlend{ 1.f, 1.f, 1.f };
	};

	// An event on the actor a clip's trigger may answer, queued for the next tick
	struct ClipFiring
	{
		std::uint8_t event = 0;  // ClipPlayer::Event
		RE::FormID topicInfo = 0;
		const Clip* except = nullptr;  // the clip whose PlayDialogue started the line, not fired by it
		std::string animation;         // the file an animation event is about, and the OAR replacer it played as
		std::string replacer;
		const SubMod* modifier = nullptr;  // the modifier a modifier event is about
	};

	// A topic a clip's PlayDialogue had the actor say, until its line begins or the wait runs out
	struct SaidTopic
	{
		RE::FormID topic = 0;
		const Clip* clip = nullptr;
		float until = 0.f;
	};

	// A clip playing on the actor: which variant, since when, and what its channels started from and last were
	struct RunningClip
	{
		Clip* clip = nullptr;
		int32_t priority = 0;
		std::uint16_t variant = 0;
		float startedAt = 0.f;
		float activatedAt = 0.f;  // when the run began, fixed: a random row drawn on activation draws from it
		float length = 0.f;
		bool bExiting = false;  // interrupted: its channels on their way back to what is underneath
		float exitStart = 0.f;
		float exitLength = 0.f;
		std::size_t nextFunctionFrame = 0;
		std::unordered_map<uint32_t, float> startNumbers;  // by ChannelId::raw: the previous frame's value at the start
		std::unordered_map<uint32_t, std::array<float, 3>> startColours;
		std::unordered_map<uint32_t, std::array<float, 3>> ownColours;  // the actor's own, for a colour no modifier holds
		PresetMix startPreset;  // the body preset's sliders it started from, and last wrote
		PresetMix lastPreset;
		std::unordered_map<uint32_t, float> lastNumbers;  // what the clip last wrote, for an interrupt to leave from
		std::unordered_map<uint32_t, std::array<float, 3>> lastColours;
	};

	// Per clip, per actor: where a sequence of variants stands, and the variants played once already
	struct ClipHistory
	{
		std::uint32_t next = 0;
		std::unordered_set<std::uint16_t> playedOnce;
	};

	// What the clips' tick did to a clip, and why, for the Modifier & Clip Log
	struct ClipReport
	{
		enum class Kind : std::uint8_t
		{
			kStart,
			kEcho,         // fired again while playing, and started over
			kEchoIgnored,  // fired again while playing, and let be
			kEnd,          // reached its last frame
			kInterrupted,
			kBlocked       // fired, and could not start
		};

		const Clip* clip = nullptr;
		Kind kind = Kind::kStart;
		std::string reason;
		std::uint16_t variant = 0;
		const void* trigger = nullptr;  // the trigger that fired it, when one did
	};

	// An Interval trigger's countdown on the actor: when it fires next, or none while its clip plays; and its roll when random
	struct IntervalTimer
	{
		std::optional<float> nextFire;
		std::optional<float> roll;
	};

	struct ClipState
	{
		std::unordered_map<const void*, IntervalTimer> intervals;  // by trigger
		std::vector<ClipFiring> pending;
		std::vector<SaidTopic> said;
		std::vector<ClipReport> report;  // what the last tick did to each clip
		std::vector<RunningClip> running;  // lowest priority first
		std::unordered_set<const Clip*> passingLast;  // whose conditions held on the previous tick
		std::unordered_map<const Clip*, ClipHistory> history;
		std::unordered_map<const void*, float> firedAt;  // by trigger: when it last fired on the actor, on the resolver's clock
	};

	namespace detail
	{
		inline thread_local bool tbInsideGuard = false;
	}

	// A value with a lock of its own, reached only through With; one at a time per thread
	template <class T>
	class Guarded
	{
	public:
		template <class F>
		decltype(auto) With(F&& a_function)
		{
			assert(!detail::tbInsideGuard && "one ActorState subobject at a time");
			std::scoped_lock lock(_lock);
			detail::tbInsideGuard = true;
			struct Reset
			{
				~Reset() { detail::tbInsideGuard = false; }
			} reset;
			return a_function(_value);
		}

	private:
		std::mutex _lock;
		T _value{};
	};

	// Everything kept about one actor, each part behind its own lock
	class State
	{
	public:
		explicit State(RE::ObjectRefHandle a_handle) :
			handle(a_handle) {}

		const RE::ObjectRefHandle handle;

		// Set when the actor is let go of: whoever holds a subobject restores what it wrote, then clears it
		std::atomic<bool> bRetired = false;

		Guarded<std::shared_ptr<const Parts>> parts;
		Guarded<std::shared_ptr<const Resolved>> resolved;
		Guarded<std::shared_ptr<const Sampled>> sampled;
		Guarded<Manual> manual;
		Guarded<PoseMemory> pose;

		Guarded<Apply::BodyMorphData> bodyMorph;
		Guarded<Apply::FaceMorphData> faceMorph;
		Guarded<Apply::BoneData> bone;
		Guarded<Apply::MfgData> mfg;
		Guarded<Apply::ActorPropertyData> actorProperty;
		Guarded<Apply::HeadPartData> headPart;
		Guarded<Apply::MaterialData> material;
		Guarded<Apply::OverlayData> overlay;
		Guarded<Apply::SkinData> skin;
		Guarded<Apply::HeadTrackData> headTrack;
		Guarded<ClipState> clips;
	};

	// Main thread, once a frame: re-reads every tracked actor whose fingerprint changed
	void Update();

	// Main thread, once a frame after Update: this frame's channel values for every kept actor
	void Sample();

	// Main thread: every kept actor
	void ForEach(const std::function<void(State&)>& a_function);

	// Main thread: the actors let go of since last asked, so what was written to them can be put back
	[[nodiscard]] std::vector<std::shared_ptr<State>> TakeRetired();

	// Main thread, each tick: the actors the scheduler drives; the rest are released unless watched
	void Track(const std::vector<RE::ActorHandle>& a_handles);

	// Kept even outside the scheduler's filter, for the panel
	void Watch(RE::ObjectRefHandle a_refr);

	// Any thread: read again on the next frame, whatever the fingerprint says
	void MarkDirty(const RE::TESObjectREFR* a_refr);

	// Any thread; null when the actor is not kept
	[[nodiscard]] std::shared_ptr<State> Get(RE::ObjectRefHandle a_refr);

	// Any thread, for the hooks: the actor whose skeleton root or face animation this is. A retired actor stays
	// findable for a few frames, so a pass that wrote to it can take its write back.
	[[nodiscard]] std::shared_ptr<State> FindByRoot(const RE::NiAVObject* a_root);
	[[nodiscard]] std::shared_ptr<State> FindByAnimationData(const void* a_animationData);
}
