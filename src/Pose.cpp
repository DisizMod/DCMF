#include "Pose.h"

#include "Settings.h"
#include "apply/Entries.h"

#include <numbers>
#include <optional>
#include <span>

namespace Pose
{
	namespace
	{
		constexpr float kToDegrees = 180.f / std::numbers::pi_v<float>;
		constexpr float kToRadians = std::numbers::pi_v<float> / 180.f;

		constexpr float kGazeReach = 30.f;  // degrees off the head's forward that fills a look modifier
		constexpr float kSmoothing = 8.f;   // per second, how fast an aim closes on its target
		constexpr float kEyesAlong = 0.6f;  // the eyes, so far up the head bone

		// Where a point is along a bone: from the node towards the child that carries the chain on, or straight
		// on from its parent when it is the last
		RE::NiPoint3 Along(RE::NiAVObject* a_node, float a_along)
		{
			const auto start = a_node->world.translate;
			RE::NiPoint3 end = start;
			bool bFound = false;
			if (auto* node = a_node->AsNode()) {
				for (const auto& child : node->GetChildren()) {
					if (child && child->AsNode() && child->name.c_str() && *child->name.c_str()) {
						end = child->world.translate;
						bFound = true;
						break;
					}
				}
			}
			if (!bFound && a_node->parent) {
				end = start + (start - a_node->parent->world.translate);
			}
			return start + (end - start) * std::clamp(a_along, 0.f, 1.f);
		}

		// A point on a reference: so far along the named bone, the eyes when none is named or found
		RE::NiPoint3 PointOn(RE::TESObjectREFR* a_ref, const std::string& a_bone, float a_along)
		{
			auto* root = a_ref->Get3D();
			if (root) {
				if (!a_bone.empty()) {
					if (auto* node = root->GetObjectByName(RE::BSFixedString(a_bone))) {
						return Along(node, a_along);
					}
				}
				if (auto* head = root->GetObjectByName("NPC Head [Head]")) {
					return Along(head, kEyesAlong);
				}
			}
			auto point = a_ref->GetPosition();
			point.z += a_ref->GetHeight() * 0.9f;
			return point;
		}

		// What a sample looks at: a reference's point, or the camera
		std::optional<RE::NiPoint3> TargetPoint(RE::Actor* a_actor, const Apply::PoseSample& a_sample)
		{
			RE::TESObjectREFR* ref = nullptr;
			switch (a_sample.target) {
			case Apply::PoseSample::Target::kCamera:
				if (auto* camera = RE::Main::WorldRootCamera()) {
					return camera->world.translate;
				}
				return std::nullopt;
			case Apply::PoseSample::Target::kPlayer:
				ref = RE::PlayerCharacter::GetSingleton();
				break;
			case Apply::PoseSample::Target::kForm:
				ref = a_sample.form ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a_sample.form) : nullptr;
				break;
			case Apply::PoseSample::Target::kEngine:
			default:
				if (auto* process = a_actor->GetActorRuntimeData().currentProcess) {
					ref = process->GetHeadtrackTarget().get().get();
				}
				break;
			}
			if (!ref || ref == a_actor || !ref->Is3DLoaded()) {
				return std::nullopt;
			}
			auto point = PointOn(ref, a_sample.bones[0], a_sample.along[0]);
			if (!a_sample.bones[1].empty()) {
				const auto second = PointOn(ref, a_sample.bones[1], a_sample.along[1]);
				point = (point + second) * 0.5f;
			}
			return point;
		}

		// Where the actor looks from: its head bone, its face, or a guess up from its feet
		RE::NiPoint3 EyesOf(RE::Actor* a_actor, RE::NiAVObject* a_root)
		{
			if (auto* head = a_root->GetObjectByName("NPC Head [Head]")) {
				return Along(head, kEyesAlong);
			}
			if (auto* face = a_actor->GetFaceNodeSkinned()) {
				return face->world.translate;
			}
			auto point = a_actor->GetPosition();
			point.z += a_actor->GetHeight() * 0.9f;
			return point;
		}

		// The skeleton's bones' local rotations at rest, the race's skeleton model's before any animation, read once
		// per skeleton on the main thread; what a fixed segment, and the A pose, are put on in place of the animation's
		struct Rest
		{
			std::unordered_map<std::string, RE::NiMatrix3> bones;  // by bone name, every biped bone the skeleton has
		};

		// A biped bone of the skeleton, "NPC ... [Code]": the physics and helper bones skeletons add are named otherwise
		bool IsBipedBone(std::string_view a_name)
		{
			return a_name.starts_with("NPC "sv) && a_name.ends_with("]"sv);
		}

		std::optional<Rest> RestFor(RE::Actor* a_actor)
		{
			static std::unordered_map<std::string, std::optional<Rest>> bySkeleton;
			auto* base = a_actor->GetActorBase();
			auto* race = a_actor->GetRace();
			if (!base || !race) {
				return std::nullopt;
			}
			const auto sex = base->GetSex() == RE::SEX::kFemale ? RE::SEXES::kFemale : RE::SEXES::kMale;
			const char* model = race->skeletonModels[sex].GetModel();
			if (!model || !*model) {
				return std::nullopt;
			}
			const std::string path(model);
			if (const auto it = bySkeleton.find(path); it != bySkeleton.end()) {
				return it->second;
			}
			std::optional<Rest> rest;
			RE::NiPointer<RE::NiNode> loaded;
			RE::BSModelDB::DBTraits::ArgsType args;
			if (const auto code = RE::BSModelDB::Demand(path.c_str(), loaded, args); code == RE::BSResource::ErrorCode::kNone && loaded) {
				Rest read;
				RE::BSVisit::TraverseScenegraphObjects(loaded.get(), [&](RE::NiAVObject* a_object) {
					if (const auto* name = a_object->name.c_str(); name && IsBipedBone(name)) {
						read.bones.emplace(name, a_object->local.rotate);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				});
				if (!read.bones.empty()) {
					rest = std::move(read);
				}
			}
			logger::info("pose: {} bones at rest {} '{}'", rest ? rest->bones.size() : 0, rest ? "read off" : "not found in", path);
			bySkeleton[path] = rest;
			return rest;
		}

		// A world direction as yaw and pitch, degrees, in a frame whose forward is +y and up +z
		void Aim(const RE::NiMatrix3& a_frame, const RE::NiPoint3& a_direction, float& a_yaw, float& a_pitch)
		{
			const auto local = a_frame.Transpose() * a_direction;
			a_yaw = std::atan2(local.x, local.y) * kToDegrees;
			a_pitch = std::atan2(local.z, std::hypot(local.x, local.y)) * kToDegrees;
		}

		void AddNumber(ActorState::Sampled& a_out, Apply::Kind a_kind, std::string_view a_name, float a_value, float a_low, float a_high)
		{
			// A face morph is the load order's, and may not be there; the bones and the mfg look channels are this plugin's
			auto& entries = Apply::Entries::GetSingleton();
			const auto id = a_kind == Apply::Kind::kFaceMorph ? entries.Find(a_kind, a_name) : entries.Require(a_kind, a_name);
			if (!id.IsValid()) {
				return;
			}
			auto& numbers = a_out.numbers[static_cast<std::size_t>(a_kind)];
			if (auto it = std::ranges::find(numbers, id.GetIndex(), &Apply::NumberSample::index); it != numbers.end()) {
				it->value = std::clamp(it->value + a_value, a_low, a_high);
			} else {
				numbers.push_back({ id.GetIndex(), std::clamp(a_value, a_low, a_high) });
			}
		}

		// The angles shared down a chain: each bone its share of the yaw, pitch and roll. The cones are the look-at's;
		// an angle the author sets is theirs, to the slider's end. Yaw is about the bone's z, pitch about its x, roll
		// about its y, radians, the way the bone applier takes them
		void Turn(ActorState::Sampled& a_out, std::span<const ChainBone> a_chain, float a_yaw, float a_pitch, float a_roll)
		{
			constexpr float kLimit = std::numbers::pi_v<float>;
			float total = 0.f;
			for (const auto& bone : a_chain) {
				total += bone.share;
			}
			for (const auto& bone : a_chain) {
				const float share = total > 0.f ? bone.share / total : 0.f;
				AddNumber(a_out, Apply::Kind::kBone, std::format("{}.z", bone.name), -a_yaw * share * kToRadians, -kLimit, kLimit);
				AddNumber(a_out, Apply::Kind::kBone, std::format("{}.x", bone.name), a_pitch * share * kToRadians, -kLimit, kLimit);
				AddNumber(a_out, Apply::Kind::kBone, std::format("{}.y", bone.name), a_roll * share * kToRadians, -kLimit, kLimit);
			}
		}
	}

	std::array<ChainBone, 3> SpineChain()
	{
		return { ChainBone{ "NPC Spine1 [Spn1]", Settings::fTrackSpine1Share, Settings::fTrackSpine1Yaw, Settings::fTrackSpine1Pitch }, ChainBone{ "NPC Spine2 [Spn2]", Settings::fTrackSpine2Share, Settings::fTrackSpine2Yaw, Settings::fTrackSpine2Pitch },
			ChainBone{ "NPC Neck [Neck]", Settings::fTrackNeckShare, Settings::fTrackNeckYaw, Settings::fTrackNeckPitch } };
	}

	std::array<ChainBone, 1> HeadChain()
	{
		return { ChainBone{ "NPC Head [Head]", 1.f, Settings::fTrackHeadYaw, Settings::fTrackHeadPitch } };
	}

	std::vector<std::string> ModesFor(std::string_view a_segment)
	{
		if (a_segment == kGaze) {
			return { std::string(ModeName(Apply::PoseSample::Mode::kEngine)), std::string(ModeName(Apply::PoseSample::Mode::kFixed)), std::string(ModeName(Apply::PoseSample::Mode::kTrack)), std::string(ModeName(Apply::PoseSample::Mode::kOffset)) };
		}
		return { std::string(ModeName(Apply::PoseSample::Mode::kOffset)), std::string(ModeName(Apply::PoseSample::Mode::kFixed)), std::string(ModeName(Apply::PoseSample::Mode::kTrack)) };
	}

	std::string_view ModeName(Apply::PoseSample::Mode a_mode)
	{
		switch (a_mode) {
		case Apply::PoseSample::Mode::kFixed:
			return "Fixed"sv;
		case Apply::PoseSample::Mode::kTrack:
			return "Track"sv;
		case Apply::PoseSample::Mode::kEngine:
			return "Engine"sv;
		case Apply::PoseSample::Mode::kOffset:
		default:
			return "Offset"sv;
		}
	}

	Apply::PoseSample::Mode ModeOf(std::string_view a_name)
	{
		for (const auto mode : { Apply::PoseSample::Mode::kOffset, Apply::PoseSample::Mode::kFixed, Apply::PoseSample::Mode::kTrack, Apply::PoseSample::Mode::kEngine }) {
			if (ModeName(mode) == a_name) {
				return mode;
			}
		}
		if (static bool bSaid = false; !bSaid) {
			bSaid = true;
			logger::error("pose: mode '{}' is not one of the pose modes, though the loader checks them; it is Offset", a_name);
		}
		return Apply::PoseSample::Mode::kOffset;
	}

	std::vector<std::string> Targets()
	{
		return { "Engine", "Camera", "Player", "Form" };
	}

	std::string_view TargetName(Apply::PoseSample::Target a_target)
	{
		switch (a_target) {
		case Apply::PoseSample::Target::kCamera:
			return "Camera"sv;
		case Apply::PoseSample::Target::kPlayer:
			return "Player"sv;
		case Apply::PoseSample::Target::kForm:
			return "Form"sv;
		case Apply::PoseSample::Target::kEngine:
		default:
			return "Engine"sv;
		}
	}

	Apply::PoseSample::Target TargetOf(std::string_view a_name)
	{
		for (const auto target : { Apply::PoseSample::Target::kEngine, Apply::PoseSample::Target::kCamera, Apply::PoseSample::Target::kPlayer, Apply::PoseSample::Target::kForm }) {
			if (TargetName(target) == a_name) {
				return target;
			}
		}
		if (static bool bSaid = false; !bSaid) {
			bSaid = true;
			logger::error("pose: target '{}' is not one of the pose targets, though the loader checks them; it is Engine", a_name);
		}
		return Apply::PoseSample::Target::kEngine;
	}

	void Expand(ActorState::State& a_state, RE::Actor* a_actor, float a_now, ActorState::Sampled& a_out)
	{
		auto* root = a_actor ? a_actor->Get3D(false) : nullptr;
		if (!root) {
			return;
		}
		auto& entries = Apply::Entries::GetSingleton();
		const auto parts = a_state.parts.With([](const auto& a_parts) { return a_parts; });

		// The preview's A pose: every biped bone on its rest, then the arms down; the spine and head poses go on top
		constexpr float kAPoseArmsDown = 45.f;
		if (a_out.bAPose) {
			if (const auto rest = RestFor(a_actor)) {
				Apply::AimSample aim{ .segment = 3, .goal = {}, .weight = 1.f, .bRest = true, .armsDown = kAPoseArmsDown };
				aim.rests.assign(rest->bones.begin(), rest->bones.end());
				a_out.aims.push_back(std::move(aim));
			}
		}

		// Each segment's parts gathered from what is held of its kind; a segment none of whose parts is held is not
		// posed, and a part not held is at its rest
		std::vector<std::pair<std::size_t, Apply::PoseSample>> poses;
		constexpr std::pair<Apply::Kind, std::size_t> kSegments[]{ { Apply::Kind::kSpine, 0 }, { Apply::Kind::kHead, 1 }, { Apply::Kind::kGaze, 2 } };
		for (const auto& [kind, segment] : kSegments) {
			const auto k = static_cast<std::size_t>(kind);
			if (a_out.numbers[k].empty() && a_out.texts[k].empty() && a_out.references[k].empty()) {
				continue;
			}
			const auto number = [&](std::string_view a_name, float a_default) {
				const auto id = entries.Find(kind, a_name);
				const auto it = id.IsValid() ? std::ranges::find(a_out.numbers[k], id.GetIndex(), &Apply::NumberSample::index) : a_out.numbers[k].end();
				return it != a_out.numbers[k].end() ? it->value : a_default;
			};
			const auto text = [&](std::string_view a_name) {
				const auto id = entries.Find(kind, a_name);
				const auto it = id.IsValid() ? std::ranges::find(a_out.texts[k], id.GetIndex(), &Apply::TextSample::index) : a_out.texts[k].end();
				return it != a_out.texts[k].end() ? it->value : std::string{};
			};
			// Undriven, the first of the list; a place past the list is a bug
			const auto modeName = [&](std::string_view a_name) {
				const auto modes = entries.Get(entries.Require(kind, a_name)).modes;
				const auto at = static_cast<std::size_t>((std::max)(number(a_name, 0.f), 0.f));
				if (at >= modes.size()) {
					if (static bool bSaid = false; !bSaid) {
						bSaid = true;
						logger::error("pose: {} mode {} is past its {} modes", a_name, at, modes.size());
					}
					return std::string{};
				}
				return modes[at];
			};
			Apply::PoseSample sample;
			sample.mode = ModeOf(modeName("mode"sv));
			sample.target = TargetOf(modeName("target"sv));
			if (const auto id = entries.Find(kind, "form"sv); id.IsValid()) {
				const auto it = std::ranges::find(a_out.references[k], id.GetIndex(), &Apply::ReferenceSample::index);
				sample.form = it != a_out.references[k].end() ? static_cast<RE::FormID>(it->value) : 0;
			}
			sample.bones = { text("targetBone"sv), text("pairedBone"sv) };
			sample.along = { number("headTail"sv, 0.f), number("pairedHeadTail"sv, 0.f) };
			sample.weight = number("weight"sv, 1.f);
			sample.pitch = number("pitch"sv, 0.f);
			sample.yaw = number("yaw"sv, 0.f);
			sample.roll = number("roll"sv, 0.f);
			poses.emplace_back(segment, std::move(sample));
		}

		a_state.pose.With([&](ActorState::PoseMemory& a_memory) {
			const float dt = a_memory.lastNow >= 0.f ? std::clamp(a_now - a_memory.lastNow, 0.f, 0.25f) : 0.f;
			a_memory.lastNow = a_now;
			const float closing = 1.f - std::exp(-kSmoothing * dt);
			std::array<bool, 3> bSeen{};

			// A mode change eases over the setting's time from the mode before; a segment let go goes back to its
			// default, Offset or the engine's, the same way, posed until it is there
			std::array<bool, 3> bHeld{};
			for (const auto& [segment, sample] : poses) {
				bHeld[segment] = true;
			}
			for (std::size_t segment = 0; segment < 3; ++segment) {
				const auto fallback = segment == 2 ? Apply::PoseSample::Mode::kEngine : Apply::PoseSample::Mode::kOffset;
				auto mode = fallback;
				for (const auto& [held, sample] : poses) {
					if (held == segment) {
						mode = sample.mode;
					}
				}
				if (mode != a_memory.mode[segment]) {
					a_memory.previous[segment] = a_memory.mode[segment];
					a_memory.mode[segment] = mode;
					a_memory.modeBlend[segment] = 0.f;
				}
				a_memory.modeBlend[segment] = Settings::fPoseModeTransition > 0.f ? std::clamp(a_memory.modeBlend[segment] + dt / Settings::fPoseModeTransition, 0.f, 1.f) : 1.f;
				if (!bHeld[segment] && a_memory.modeBlend[segment] < 1.f) {
					Apply::PoseSample sample;
					sample.mode = mode;
					poses.emplace_back(segment, std::move(sample));
				}
			}
			// How much of a mode a segment has on this frame, eased in and out
			const auto modeWeight = [&](std::size_t a_segment, Apply::PoseSample::Mode a_mode) {
				const float u = a_memory.modeBlend[a_segment];
				const float eased = u * u * (3.f - 2.f * u);
				return (a_memory.mode[a_segment] == a_mode ? eased : 0.f) + (a_memory.previous[a_segment] == a_mode ? 1.f - eased : 0.f);
			};

			for (const auto& [segment, sample] : poses) {
				const float engine = modeWeight(segment, Apply::PoseSample::Mode::kEngine);
				if (engine >= 1.f) {
					continue;  // the engine's own eye tracking runs; nothing to add
				}
				bSeen[segment] = true;
				const float track = modeWeight(segment, Apply::PoseSample::Mode::kTrack);

				// A fixed spine sits on its rest pose, Spine1 to the neck, straight on the pelvis however the animation
				// bends it. A fixed or tracking head sits on its rest pose, the neck on its own under it unless the spine
				// already put it there, so the head is straight on the spine however the animation bends the neck
				const auto restOf = [&](std::initializer_list<const char*> a_bones, float a_weight) {
					const auto rest = RestFor(a_actor);
					if (!rest || a_weight <= 0.f) {
						return;
					}
					Apply::AimSample aim{ .segment = static_cast<std::uint8_t>(segment), .goal = {}, .weight = a_weight, .bRest = true };
					for (const auto* bone : a_bones) {
						if (const auto it = rest->bones.find(bone); it != rest->bones.end()) {
							aim.rests.emplace_back(bone, it->second);
						}
					}
					a_out.aims.push_back(std::move(aim));
				};
				if (segment == 0) {
					const auto chain = SpineChain();
					restOf({ chain[0].name, chain[1].name, chain[2].name }, modeWeight(0, Apply::PoseSample::Mode::kFixed));
				} else if (segment == 1) {
					const float rested = modeWeight(1, Apply::PoseSample::Mode::kFixed) + track;
					if (modeWeight(0, Apply::PoseSample::Mode::kFixed) >= 1.f) {
						restOf({ HeadChain()[0].name }, rested);
					} else {
						restOf({ SpineChain()[2].name, HeadChain()[0].name }, rested);
					}
				}

				// Where to aim: the angles given, and for a tracked target the direction to it. The spine and the head
				// leave the direction for the bone pass, which turns them from the animated frame; the gaze reads it
				// in the head's frame here
				float yaw = sample.yaw;
				float pitch = sample.pitch;
				float trackYaw = 0.f;
				float trackPitch = 0.f;
				if (track > 0.f) {
					if (const auto point = TargetPoint(a_actor, sample)) {
						auto to = *point - EyesOf(a_actor, root);
						const float length = to.Length();
						if (segment == 2) {
							auto* head = root->GetObjectByName("NPC Head [Head]");
							float toYaw = 0.f;
							float toPitch = 0.f;
							Aim(head ? head->world.rotate : root->world.rotate, to, toYaw, toPitch);
							trackYaw = toYaw * sample.weight * track;
							trackPitch = toPitch * sample.weight * track;
						} else if (length >= 1.f) {
							to = to * (1.f / length);
							// The direction closes on the target rather than jumping with it
							auto& direction = a_memory.direction[segment];
							if (!a_memory.bTracking[segment] || dt <= 0.f) {
								direction = to;
							} else {
								const auto mixed = direction + (to - direction) * closing;
								const float mixedLength = mixed.Length();
								direction = mixedLength > 0.001f ? mixed * (1.f / mixedLength) : to;
							}
							a_memory.bTracking[segment] = true;
							// A camera on top of the head is not something to face: nothing within 40 units, whole from 80.
							// A target behind is faded by the bone pass, which has the head's animated forward to measure from
							float wanted = 1.f;
							if (sample.target == Apply::PoseSample::Target::kCamera) {
								wanted = std::clamp((length - 40.f) / 40.f, 0.f, 1.f);
							}
							a_out.aims.push_back({ static_cast<std::uint8_t>(segment), direction, wanted * sample.weight * sample.strength * track });
						}
					} else if (segment < 2 && a_memory.bTracking[segment] && sample.mode != Apply::PoseSample::Mode::kTrack) {
						// Easing out of Track with the target gone: out from where it last looked
						a_out.aims.push_back({ static_cast<std::uint8_t>(segment), a_memory.direction[segment], sample.weight * sample.strength * track });
					}
				} else if (segment < 2) {
					a_memory.bTracking[segment] = false;
				}
				// TODO: Fixed puts the head on its rest pose; until the rest is read off the skeleton it is Offset
				// Smoothed: what follows a target closes on it rather than jumping with it. The angles an author sets are
				// taken as they are, so a keyed curve keeps its shape, a bounce's dips and a spring's overshoot included
				if (!a_memory.bAiming[segment] || dt <= 0.f) {
					a_memory.yaw[segment] = trackYaw;
					a_memory.pitch[segment] = trackPitch;
				} else {
					a_memory.yaw[segment] += (trackYaw - a_memory.yaw[segment]) * closing;
					a_memory.pitch[segment] += (trackPitch - a_memory.pitch[segment]) * closing;
				}
				a_memory.bAiming[segment] = true;
				yaw = (yaw + a_memory.yaw[segment]) * sample.strength;
				pitch = (pitch + a_memory.pitch[segment]) * sample.strength;
				const float roll = sample.roll * sample.strength;

				switch (segment) {
				case 0:
					Turn(a_out, SpineChain(), yaw, pitch, roll);
					break;
				case 1:
					Turn(a_out, HeadChain(), yaw, pitch, roll);
					break;
				default: {
					// The eyes: the angles as fractions of the engine's bounds, and the four look modifiers each filled
					// by the angle its way
					const float heading = std::clamp(yaw / kGazeReach, -1.f, 1.f);
					const float elevation = std::clamp(pitch / kGazeReach, -1.f, 1.f);
					a_out.eyes = std::array{ heading, elevation };
					a_out.eyesWeight = 1.f - engine;
					a_out.eyesOffset = modeWeight(2, Apply::PoseSample::Mode::kOffset);
					const auto positive = [&](float a_value) { return (std::max)(a_value, 0.f) * (1.f - engine); };
					AddNumber(a_out, Apply::Kind::kMfg, "LookRight"sv, positive(heading), 0.f, 1.f);
					AddNumber(a_out, Apply::Kind::kMfg, "LookLeft"sv, positive(-heading), 0.f, 1.f);
					AddNumber(a_out, Apply::Kind::kMfg, "LookUp"sv, positive(elevation), 0.f, 1.f);
					AddNumber(a_out, Apply::Kind::kMfg, "LookDown"sv, positive(-elevation), 0.f, 1.f);
					break;
				}
				}
			}
			for (std::size_t i = 0; i < 3; ++i) {
				if (!bSeen[i]) {
					a_memory.bAiming[i] = false;
					if (i < 2) {
						a_memory.bTracking[i] = false;
					}
				}
			}
		});

		// UBE's patch, on the Look channels themselves: on a head with the rotation morphs, every Look held, from a
		// channel, a slider or the gaze, keeps the engine's own and also moves the extension's Look morph at its weight,
		// and sideways turns the eyes by the rotation pair at the strength
		if (Settings::bPatchUbeEyeRotation && parts && parts->morphs.contains("Right_Eye_Rotation_Out")) {
			const auto lookOf = [&](std::string_view a_name) {
				const auto id = entries.Find(Apply::Kind::kMfg, a_name);
				const auto& numbers = a_out.numbers[static_cast<std::size_t>(Apply::Kind::kMfg)];
				const auto it = id.IsValid() ? std::ranges::find(numbers, id.GetIndex(), &Apply::NumberSample::index) : numbers.end();
				return it != numbers.end() ? std::clamp(it->value, 0.f, 1.f) : 0.f;
			};
			const float right = lookOf("LookRight"sv);
			const float left = lookOf("LookLeft"sv);
			const float up = lookOf("LookUp"sv);
			const float down = lookOf("LookDown"sv);
			if (right > 0.f || left > 0.f || up > 0.f || down > 0.f) {
				AddNumber(a_out, Apply::Kind::kFaceMorph, "LookRight"sv, right * Settings::fPatchUbeEyeRight, 0.f, 4.f);
				AddNumber(a_out, Apply::Kind::kFaceMorph, "LookLeft"sv, left * Settings::fPatchUbeEyeLeft, 0.f, 4.f);
				AddNumber(a_out, Apply::Kind::kFaceMorph, "LookUp"sv, up * Settings::fPatchUbeEyeUp, 0.f, 4.f);
				AddNumber(a_out, Apply::Kind::kFaceMorph, "LookDown"sv, down * Settings::fPatchUbeEyeDown, 0.f, 4.f);
				const float deflection = (right - left) * Settings::fPatchUbeEyeRotationStrength;
				AddNumber(a_out, Apply::Kind::kFaceMorph, "Right_Eye_Rotation_Out"sv, (std::max)(deflection, 0.f), 0.f, 4.f);
				AddNumber(a_out, Apply::Kind::kFaceMorph, "Left_Eye_Rotation_In"sv, (std::max)(deflection, 0.f), 0.f, 4.f);
				AddNumber(a_out, Apply::Kind::kFaceMorph, "Left_Eye_Rotation_Out"sv, (std::max)(-deflection, 0.f), 0.f, 4.f);
				AddNumber(a_out, Apply::Kind::kFaceMorph, "Right_Eye_Rotation_In"sv, (std::max)(-deflection, 0.f), 0.f, 4.f);
			}
		}
	}
}
