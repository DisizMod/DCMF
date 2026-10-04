#include "Bone.h"

#include "ActorState.h"
#include "Entries.h"
#include "Pose.h"
#include "Settings.h"

#include <numbers>
#include <span>

namespace Apply
{
	namespace
	{
		constexpr std::size_t kUpdateDownwardPassVtableIndex = 0x2C;

		using UpdateDownwardPass_t = void(RE::BSFadeNode*, RE::NiUpdateData&, std::uint32_t);
		UpdateDownwardPass_t* g_original = nullptr;

		bool SameRotation(const RE::NiMatrix3& a_a, const RE::NiMatrix3& a_b)
		{
			for (int r = 0; r < 3; ++r) {
				for (int c = 0; c < 3; ++c) {
					if (a_a.entry[r][c] != a_b.entry[r][c]) {
						return false;
					}
				}
			}
			return true;
		}

		// A bone's frame as the animation and our earlier writes posed it this pass: the root's world times every
		// local down the chain. The bones' world transforms are stale here, this pass is what computes them
		bool AnimatedFrame(RE::NiAVObject* a_root, RE::NiAVObject* a_bone, RE::NiTransform& a_out)
		{
			std::vector<RE::NiAVObject*> chain;
			for (auto* at = a_bone; at && at != a_root; at = at->parent) {
				chain.push_back(at);
			}
			if (chain.empty() || chain.back()->parent != a_root) {
				return false;
			}
			a_out = a_root->world;
			for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
				a_out = a_out * (*it)->local;
			}
			return true;
		}

		// A rotation about a world axis, by Rodrigues
		RE::NiMatrix3 AboutAxis(const RE::NiPoint3& a_axis, float a_radians)
		{
			const float c = std::cos(a_radians);
			const float s = std::sin(a_radians);
			const float t = 1.f - c;
			const float x = a_axis.x;
			const float y = a_axis.y;
			const float z = a_axis.z;
			RE::NiMatrix3 m;
			m.entry[0][0] = t * x * x + c;
			m.entry[0][1] = t * x * y - s * z;
			m.entry[0][2] = t * x * z + s * y;
			m.entry[1][0] = t * x * y + s * z;
			m.entry[1][1] = t * y * y + c;
			m.entry[1][2] = t * y * z - s * x;
			m.entry[2][0] = t * x * z - s * y;
			m.entry[2][1] = t * y * z + s * x;
			m.entry[2][2] = t * z * z + c;
			return m;
		}

		// A rotation partway from one to another, along the one turn between them
		RE::NiMatrix3 Partway(const RE::NiMatrix3& a_from, const RE::NiMatrix3& a_to, float a_u)
		{
			if (a_u >= 1.f) {
				return a_to;
			}
			const auto between = a_from.Transpose() * a_to;
			const float cosine = std::clamp((between.entry[0][0] + between.entry[1][1] + between.entry[2][2] - 1.f) * 0.5f, -1.f, 1.f);
			const float angle = std::acos(cosine);
			RE::NiPoint3 axis{ between.entry[2][1] - between.entry[1][2], between.entry[0][2] - between.entry[2][0], between.entry[1][0] - between.entry[0][1] };
			const float length = axis.Length();
			if (a_u <= 0.f || angle < 1e-5f || length < 1e-5f) {
				return a_u < 0.5f ? a_from : a_to;
			}
			return a_from * AboutAxis(axis * (1.f / length), angle * a_u);
		}

		float AngleBetween(const RE::NiPoint3& a_a, const RE::NiPoint3& a_b)
		{
			const float la = a_a.Length();
			const float lb = a_b.Length();
			return la <= 0.f || lb <= 0.f ? 0.f : std::acos(std::clamp(a_a.Dot(a_b) / (la * lb), -1.f, 1.f));
		}

		// The rotation, world, that turns from towards to by an angle: the sense that closes the gap is kept,
		// whatever the matrix convention. Nothing when they are aligned or opposite
		bool ArcTowards(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_radians, RE::NiMatrix3& a_out)
		{
			const auto axis = a_from.Cross(a_to);
			const float sine = axis.Length();
			if (sine < 1e-4f || a_radians < 1e-5f) {
				return false;
			}
			const auto unit = axis * (1.f / sine);
			const auto forwards = AboutAxis(unit, a_radians);
			const auto backwards = AboutAxis(unit, -a_radians);
			a_out = AngleBetween(forwards * a_from, a_to) <= AngleBetween(backwards * a_from, a_to) ? forwards : backwards;
			return true;
		}

		// A turn towards a goal within a yaw and a pitch cone. The goal is first brought inside the cones, its yaw
		// about the vertical and its pitch about the horizontal right each cut to its reach, then the turn is the
		// one shortest arc from the forward to that, as legacy's was: the arc leans the head on a diagonal the way
		// a head leans. The sense per axis is the one that closes the gap. False when nothing turned
		bool TurnTowards(const RE::NiPoint3& a_from, const RE::NiPoint3& a_goal, float a_gain, float a_yawReach, float a_pitchReach, RE::NiMatrix3& a_out)
		{
			const auto flat = [](const RE::NiPoint3& a_v, const RE::NiPoint3& a_axis) { return a_v - a_axis * a_v.Dot(a_axis); };
			// The turn about an axis that closes the gap, cut to the reach
			const auto about = [&](const RE::NiPoint3& a_axis, const RE::NiPoint3& a_f, const RE::NiPoint3& a_g, float a_reach, RE::NiMatrix3& a_turn) {
				const auto f = flat(a_f, a_axis);
				const auto g = flat(a_g, a_axis);
				if (f.Length() < 1e-4f || g.Length() < 1e-4f) {
					return false;
				}
				const float turn = (std::min)(AngleBetween(f, g) * a_gain, a_reach);
				if (turn < 1e-5f) {
					return false;
				}
				const auto forwards = AboutAxis(a_axis, turn);
				const auto backwards = AboutAxis(a_axis, -turn);
				a_turn = AngleBetween(forwards * f, g) <= AngleBetween(backwards * f, g) ? forwards : backwards;
				return true;
			};
			const RE::NiPoint3 up{ 0.f, 0.f, 1.f };
			auto within = a_from;
			RE::NiMatrix3 yaw;
			if (about(up, within, a_goal, a_yawReach, yaw)) {
				within = yaw * within;
			}
			auto right = within.Cross(up);
			const float rightLength = right.Length();
			RE::NiMatrix3 pitch;
			if (rightLength > 1e-4f && about(right * (1.f / rightLength), within, a_goal, a_pitchReach, pitch)) {
				within = pitch * within;
			}
			return ArcTowards(a_from, within, AngleBetween(a_from, within), a_out);
		}

		void UpdateDownwardPassHook(RE::BSFadeNode* a_node, RE::NiUpdateData& a_data, std::uint32_t a_arg2)
		{
			// Only the actor's own skeleton: its user data is on its weapon's and first-person roots too
			if (a_node) {
				if (auto* refr = a_node->GetUserData(); refr && a_node == refr->Get3D(false)) {
					if (const auto state = ActorState::FindByRoot(a_node)) {
						Run(Pass::kFadeNode, *state);
					}
				}
			}
			g_original(a_node, a_data, a_arg2);
		}
	}

	bool Bone::Install()
	{
		REL::Relocation<std::uintptr_t> vtable{ RE::BSFadeNode::VTABLE[0] };
		const auto slot = vtable.address() + sizeof(void*) * kUpdateDownwardPassVtableIndex;
		if (*reinterpret_cast<std::uintptr_t*>(slot) == 0) {
			logger::error("bone: BSFadeNode vtable slot 0x{:X} is empty; nothing patched", kUpdateDownwardPassVtableIndex);
			return false;
		}
		g_original = reinterpret_cast<UpdateDownwardPass_t*>(vtable.write_vfunc(kUpdateDownwardPassVtableIndex, UpdateDownwardPassHook));
		logger::info("bone: hooked BSFadeNode::UpdateDownwardPass at vtable slot 0x{:X}", kUpdateDownwardPassVtableIndex);
		return true;
	}

	void Bone::Apply(Pass, ActorState::State& a_state)
	{
		const auto refr = a_state.handle.get();
		auto* actor = refr ? refr->As<RE::Actor>() : nullptr;
		auto* root = actor ? actor->Get3D(false) : nullptr;
		const auto parts = a_state.parts.With([](const auto& a_parts) { return a_parts; });
		const auto sampled = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		const bool bRetired = a_state.bRetired.load();
		auto& entries = Entries::GetSingleton();

		a_state.bone.With([&](BoneData& a_data) {
			const auto& samples = sampled ? sampled->numbers[static_cast<std::size_t>(Kind::kBone)] : std::vector<NumberSample>{};
			const auto& aims = sampled ? sampled->aims : std::vector<AimSample>{};

			// Havok poses a dead or fallen actor; a turn on top deforms the body
			const bool bLetGo = bRetired || !root || !parts || (samples.empty() && aims.empty()) || actor->IsDead() || actor->IsInRagdollState();

			// Whatever was written last pass comes off first, on every bone still holding it. Rotation and scale
			// are taken back separately: animation rewrites a bone's rotation every frame and leaves its scale,
			// so a scale still holding ours would otherwise be scaled again on top of itself
			const auto takeBack = [](BoneWrite& a_bone) {
				if (a_bone.bWritten && a_bone.node) {
					if (SameRotation(a_bone.node->local.rotate, a_bone.written.rotate)) {
						a_bone.node->local.rotate = a_bone.animated.rotate;
					}
					if (a_bone.node->local.scale == a_bone.written.scale) {
						a_bone.node->local.scale = a_bone.animated.scale;
					}
				}
				a_bone.bWritten = false;
			};
			for (auto& [slot, bone] : a_data.bones) {
				takeBack(bone);
			}
			for (auto& [name, bone] : a_data.named) {
				takeBack(bone);
			}

			if (bLetGo) {
				a_data = {};
				return;
			}

			if (a_data.boundGeneration != parts->generation) {
				a_data.bones.clear();
				a_data.named.clear();
				a_data.boundGeneration = parts->generation;
			}

			constexpr auto perBone = static_cast<std::uint32_t>(Property::kTotal);

			// The aims, the spine's before the head's, since the head closes what the spine left. Every frame is
			// walked from the root, the world transforms being what this pass computes, so a bone sees the turns
			// before it. The write goes through the same record as a number's, so the take-back finds it
			constexpr float kToRadians = std::numbers::pi_v<float> / 180.f;
			const RE::NiPoint3 kForward{ 0.f, 1.f, 0.f };
			const auto writeTurn = [&](RE::NiAVObject* a_node, const char* a_name, const RE::NiMatrix3& a_by, bool a_bReplace, float a_weight = 1.f) {
				// The table's bones by their slot, the A pose's others by name; one record each, so the take-back finds it
				BoneWrite* record = nullptr;
				if (const auto id = entries.Find(Kind::kBone, std::format("{}.x", a_name)); id.IsValid()) {
					const auto slot = id.GetIndex() / perBone;
					auto held = std::ranges::find(a_data.bones, slot, [](const auto& a_entry) { return a_entry.first; });
					if (held == a_data.bones.end()) {
						held = a_data.bones.insert(a_data.bones.end(), { slot, BoneWrite{ .node = RE::NiPointer(a_node) } });
					}
					record = &held->second;
				} else {
					auto held = std::ranges::find(a_data.named, std::string_view(a_name), [](const auto& a_entry) { return std::string_view(a_entry.first); });
					if (held == a_data.named.end()) {
						held = a_data.named.insert(a_data.named.end(), { std::string(a_name), BoneWrite{ .node = RE::NiPointer(a_node) } });
					}
					record = &held->second;
				}
				auto& write = *record;
				if (!write.bWritten) {
					write.animated = a_node->local;
					write.base = a_node->local.rotate;
				}
				if (a_bReplace) {
					// The rest pose in place of what the bone sits on, the turns made on top of that kept: a second rest
					// on the same bone, the A pose's then a fixed spine's, measured from the first and not the animation
					const auto turn = write.base.Transpose() * a_node->local.rotate;
					a_node->local.rotate = Partway(a_node->local.rotate, a_by * turn, a_weight);
					write.base = Partway(write.base, a_by, a_weight);
				} else {
					a_node->local.rotate = a_node->local.rotate * a_by;
				}
				write.written = a_node->local;
				write.bWritten = true;
			};
			// The rest pose goes on before any aim is taken, so the chain aims the head it ends with; then the spine
			auto sorted = aims;
			std::ranges::stable_sort(sorted, [](const AimSample& a, const AimSample& b) { return std::pair{ !a.bRest, a.segment } < std::pair{ !b.bRest, b.segment }; });
			const auto spineChain = Pose::SpineChain();
			const auto headChain = Pose::HeadChain();
			auto* headBone = root->GetObjectByName(headChain[0].name);
			for (const auto& aim : sorted) {
				if (aim.bRest) {
					for (const auto& [name, rest] : aim.rests) {
						if (auto* node = root->GetObjectByName(RE::BSFixedString(name))) {
							writeTurn(node, name.c_str(), rest, true, aim.weight);
						}
					}
					// The A pose: each upper arm, shoulder to elbow as the rest put it, turned to point out to its side
					// and this far below level, whatever the rest's own arms were; the turn made in the world and put in
					// the arm's own frame
					if (aim.armsDown != 0.f) {
						const auto& body = root->world.rotate;
						const auto up = body * RE::NiPoint3{ 0.f, 0.f, 1.f };
						const float down = aim.armsDown * kToRadians;
						constexpr std::pair<const char*, const char*> kArms[]{ { "NPC L UpperArm [LUar]", "NPC L Forearm [LLar]" }, { "NPC R UpperArm [RUar]", "NPC R Forearm [RLar]" } };
						for (std::size_t i = 0; i < std::size(kArms); ++i) {
							auto* arm = root->GetObjectByName(kArms[i].first);
							auto* elbow = root->GetObjectByName(kArms[i].second);
							RE::NiTransform parent;
							RE::NiTransform armFrame;
							RE::NiTransform elbowFrame;
							if (!arm || !elbow || !arm->parent || !AnimatedFrame(root, arm->parent, parent) || !AnimatedFrame(root, arm, armFrame) || !AnimatedFrame(root, elbow, elbowFrame)) {
								continue;
							}
							auto along = elbowFrame.translate - armFrame.translate;
							if (along.Length() < 1e-3f) {
								continue;
							}
							along = along * (1.f / along.Length());
							const auto side = body * RE::NiPoint3{ i == 0 ? -1.f : 1.f, 0.f, 0.f };
							const auto goal = side * std::cos(down) - up * std::sin(down);
							RE::NiMatrix3 turn;
							if (!ArcTowards(along, goal, AngleBetween(along, goal), turn)) {
								continue;
							}
							const auto& local = arm->local.rotate;
							const auto wanted = parent.rotate.Transpose() * turn * parent.rotate * local;
							writeTurn(arm, kArms[i].first, local.Transpose() * wanted, false);
						}
					}
					continue;
				}
				RE::NiTransform frame;
				if (!headBone || !AnimatedFrame(root, headBone, frame)) {
					continue;
				}
				// The head's forward is the measure for both chains. A target well behind it is not something a body
				// can face: eased off from 105 degrees off the forward to nothing at 160, measured from the head as
				// the animation posed it, so a twisted torso counts. The weight folded in once, the goal moved that
				// fraction of the way from the forward, so a fade is a fraction of the turn and not of every bone's gain
				auto forward = frame.rotate * kForward;
				auto goal = aim.goal;
				const float away = AngleBetween(forward, aim.goal) / kToRadians;
				const float weight = aim.weight * (away <= 105.f ? 1.f : std::clamp((160.f - away) / 55.f, 0.f, 1.f));
				if (weight < 1.f) {
					RE::NiMatrix3 part;
					goal = ArcTowards(forward, aim.goal, AngleBetween(forward, aim.goal) * weight, part) ? part * forward : forward;
				}
				if (aim.segment == 0) {
					// Sequential gains from the shares: each bone's share of what is left below the neck, the neck
					// closing the rest within its cone; the head's forward carried through each turn
					const auto& chain = spineChain;
					for (std::size_t i = 0; i < chain.size(); ++i) {
						auto* node = root->GetObjectByName(chain[i].name);
						if (!node || !AnimatedFrame(root, node, frame)) {
							continue;
						}
						float below = 0.f;
						for (std::size_t j = i + 1; j < chain.size(); ++j) {
							below += chain[j].share;
						}
						const float remaining = chain[i].share + below;
						const float gain = below <= 0.f ? 1.f : remaining > 0.f ? chain[i].share / remaining : 0.f;
						RE::NiMatrix3 world;
						if (TurnTowards(forward, goal, gain, chain[i].yawReach * kToRadians, chain[i].pitchReach * kToRadians, world)) {
							writeTurn(node, chain[i].name, frame.rotate.Transpose() * world * frame.rotate, false);
							forward = world * forward;
						}
					}
				} else {
					// The head closes whatever the spine left, from where the spine put it, within its cone
					RE::NiMatrix3 world;
					if (TurnTowards(forward, goal, 1.f, headChain[0].yawReach * kToRadians, headChain[0].pitchReach * kToRadians, world)) {
						writeTurn(headBone, headChain[0].name, frame.rotate.Transpose() * world * frame.rotate, false);
					}
				}
			}

			// Per bone: scale and three angles, gathered from the sorted samples, on top of the aims: an angle the author
			// sets is an offset on whatever the tracking did
			for (std::size_t i = 0; i < samples.size();) {
				const auto slot = samples[i].index / perBone;
				float scale = 1.f;
				float angles[3]{};
				for (; i < samples.size() && samples[i].index / perBone == slot; ++i) {
					switch (static_cast<Property>(samples[i].index % perBone)) {
					case Property::kScale:
						scale = samples[i].value;
						break;
					case Property::kX:
						angles[0] = samples[i].value;
						break;
					case Property::kY:
						angles[1] = samples[i].value;
						break;
					case Property::kZ:
						angles[2] = samples[i].value;
						break;
					default:
						break;
					}
				}
				auto bone = std::ranges::find(a_data.bones, slot, [](const auto& a_entry) { return a_entry.first; });
				if (bone == a_data.bones.end()) {
					// The node is the entry's group: the bone name the skeleton scan found
					BoneWrite write;
					const auto name = entries.Get({ Kind::kBone, slot * perBone }).group;
					if (!name.empty()) {
						write.node = RE::NiPointer(root->GetObjectByName(RE::BSFixedString(name)));
					}
					if (!write.node) {
						logger::info("bone: '{}' is not on this actor's skeleton", name);
					}
					bone = a_data.bones.insert(a_data.bones.end(), { slot, std::move(write) });
				}
				auto& write = bone->second;
				if (!write.node) {
					continue;
				}

				if (!write.bWritten) {
					write.animated = write.node->local;
				}
				RE::NiMatrix3 turn;
				turn.SetEulerAnglesXYZ(angles[0], angles[1], angles[2]);
				write.node->local.rotate = write.node->local.rotate * turn;
				write.node->local.scale = write.animated.scale * scale;
				write.written = write.node->local;
				write.bWritten = true;
			}

		});
	}
}
