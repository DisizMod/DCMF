#include "Mfg.h"

#include "ActorState.h"
#include "Entries.h"
#include "mfgfix/MfgFix.h"

#include "Settings.h"

#include <MinHook.h>
#include <random>

namespace Apply
{
	namespace
	{
		// A member function taking (float, bool) and returning bool; as a free function with an explicit this
		// the registers line up: RCX = this, XMM1 = timeDelta, R8 = the flag
		using KeyframesUpdate_t = bool(RE::BSFaceGenAnimationData*, float, bool);
		KeyframesUpdate_t* g_original = nullptr;

		// The frame's seconds, for the blink and the wander; the pass has no other clock
		thread_local float t_timeDelta = 0.f;

		bool KeyframesUpdateHook(RE::BSFaceGenAnimationData* a_this, float a_timeDelta, bool a_flag)
		{
			const bool result = g_original(a_this, a_timeDelta, a_flag);
			// Every face, driven or not: the scripts' smooth moves stepped, and the console tier carried into the
			// finals, which vanilla loses on this very update; the engine is then asked to put the sets on the mesh
			if (a_this) {
				MfgFix::Smooth(a_this, a_timeDelta);
				if (MfgFix::Merge(a_this)) {
					a_this->unk217 = true;
				}
			}
			if (const auto state = ActorState::FindByAnimationData(a_this)) {
				t_timeDelta = std::clamp(a_timeDelta, 0.f, 0.1f);
				Run(Pass::kFaceAnimation, *state);
			}
			return result;
		}

		float RandomUnit()
		{
			thread_local std::mt19937 engine{ std::random_device{}() };
			thread_local std::uniform_real_distribution<float> distribution{ 0.f, 1.f };
			return distribution(engine);
		}

		// The eyelids, NG's machine on the engine's own stage and timer, since vanilla's never blinks the player
		// outside dialogue: the value for this frame, 0 open to 1 shut. Ported from Mfg Fix NG (GPL-3.0)
		float AdvanceBlink(RE::BSFaceGenAnimationData* a_this, float a_timeDelta, bool a_bHoldClosed)
		{
			using Stage = RE::BSFaceGenAnimationData::EyesBlinkingStage;
			const float down = Settings::fBlinkDownTime;
			const float up = Settings::fBlinkUpTime;
			// The gap squared towards the short end, so blinks cluster rather than spread over the window
			const auto gap = [] {
				const float unit = RandomUnit();
				return Settings::fBlinkDelayMin + (Settings::fBlinkDelayMax - Settings::fBlinkDelayMin) * unit * unit;
			};
			auto& timer = a_this->eyesBlinkingTimer;
			auto& stage = a_this->eyesBlinkingStage;
			timer = (std::max)(timer - a_timeDelta, 0.f);
			float value = 0.f;
			switch (stage) {
			case Stage::BlinkDelay:
				if (timer == 0.f) {
					stage = Stage::BlinkDown;
					timer = down;
				}
				break;
			case Stage::BlinkDown:
				value = down != 0.f ? 1.f - timer / down : 1.f;
				if (timer == 0.f) {
					stage = Stage::BlinkUp;
					timer = up;
				}
				break;
			case Stage::BlinkUp:
				value = up != 0.f ? timer / up : 0.f;
				if (timer == 0.f) {
					stage = Stage::BlinkDelay;
					timer = gap();
				}
				break;
			case Stage::BlinkDownAndWait1:
				if (a_bHoldClosed) {
					value = down != 0.f ? 1.f - timer / down : 1.f;
				} else {
					// Released: finish shut, then open normally rather than being stranded closed
					value = 1.f;
					stage = Stage::BlinkUp;
					timer = up;
				}
				break;
			case Stage::BlinkDownAndWait2:
				stage = Stage::BlinkDownAndWait1;
				break;
			case Stage::WaitForLookDown:
			default:
				stage = Stage::BlinkDelay;
				timer = gap();
				break;
			}
			return std::clamp(value, 0.f, 1.f);
		}

		void ZeroAll(RE::BSFaceGenKeyframeMultiple& a_set)
		{
			if (a_set.values) {
				std::fill_n(a_set.values, a_set.count, 0.f);
			}
		}

		RE::BSFaceGenKeyframeMultiple* SetOf(RE::BSFaceGenAnimationData* a_data, std::uint32_t a_set)
		{
			switch (a_set) {
			case 0:
				return &a_data->expression3;
			case 1:
				return &a_data->modifier3;
			case 2:
				return &a_data->phoneme3;
			default:
				return &a_data->custom3;
			}
		}
	}

	const std::vector<Mfg::Slot>& Mfg::Slots()
	{
		// Names and order are the engine's own enums
		static const std::vector<Slot> slots = [] {
			std::vector<Slot> out;
			std::uint32_t i = 0;
			for (const auto* name : { "DialogueAnger", "DialogueFear", "DialogueHappy", "DialogueSad", "DialogueSurprise", "DialoguePuzzled", "DialogueDisgusted",
					 "MoodNeutral", "MoodAnger", "MoodFear", "MoodHappy", "MoodSad", "MoodSurprise", "MoodPuzzled", "MoodDisgusted", "CombatAnger", "CombatShout" }) {
				out.push_back({ name, 0, i++ });
			}
			i = 0;
			// HeadPitch, HeadRoll and HeadYaw are left out: the engine turns the head bone from them by another route,
			// which an mfg write does not reach; the bone kind turns the head
			for (const auto* name : { "BlinkLeft", "BlinkRight", "BrowDownLeft", "BrowDownRight", "BrowInLeft", "BrowInRight", "BrowUpLeft", "BrowUpRight",
					 "LookDown", "LookLeft", "LookRight", "LookUp", "SquintLeft", "SquintRight" }) {
				out.push_back({ name, 1, i++ });
			}
			i = 0;
			for (const auto* name : { "Aah", "BigAah", "BMP", "ChJSh", "DST", "Eee", "Eh", "FV", "I", "K", "N", "Oh", "OohQ", "R", "Th", "W" }) {
				out.push_back({ name, 2, i++ });
			}
			// SkinnyMorph is left out: the engine folds the custom set into the base each update, so a held value grows without end
			return out;
		}();
		return slots;
	}

	bool Mfg::Install()
	{
		// MinHook relocates the prologue; the SKSE trampoline cannot patch a function entry
		if (const auto status = MH_Initialize(); status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
			logger::error("mfg: MinHook did not initialise: {}", MH_StatusToString(status));
			return false;
		}
		const REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(25983, 26598) };
		void* trampoline = nullptr;
		if (const auto status = MH_CreateHook(reinterpret_cast<void*>(target.address()), reinterpret_cast<void*>(&KeyframesUpdateHook), &trampoline); status != MH_OK) {
			logger::error("mfg: could not hook BSFaceGenAnimationData::KeyframesUpdate: {}", MH_StatusToString(status));
			return false;
		}
		g_original = reinterpret_cast<KeyframesUpdate_t*>(trampoline);
		if (const auto status = MH_EnableHook(reinterpret_cast<void*>(target.address())); status != MH_OK) {
			logger::error("mfg: could not enable the hook: {}", MH_StatusToString(status));
			return false;
		}
		logger::info("keyframe: hooked BSFaceGenAnimationData::KeyframesUpdate");
		return true;
	}

	void Mfg::Apply(Pass, ActorState::State& a_state)
	{
		const auto refr = a_state.handle.get();
		auto* faceNode = refr ? refr->GetFaceNodeSkinned() : nullptr;
		auto* animation = faceNode ? faceNode->GetRuntimeData().animationData.get() : nullptr;
		if (!animation) {
			return;
		}
		const auto sampled = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		const bool bRetired = a_state.bRetired.load();
		const auto& slots = Slots();

		a_state.mfg.With([&](MfgData& a_data) {
			const auto& samples = sampled ? sampled->numbers[static_cast<std::size_t>(Kind::kMfg)] : std::vector<NumberSample>{};

			// The switches first: what the engine's own animation put in the tiers is taken out at the source, and
			// the held values then go on top
			auto& entries = Entries::GetSingleton();
			static const auto stopBlink = entries.Require(Kind::kMfg, "StopBlink"sv).GetIndex();
			static const auto stopEyeMovement = entries.Require(Kind::kMfg, "StopEyeMovement"sv).GetIndex();
			static const auto stopLipSync = entries.Require(Kind::kMfg, "StopLipSync"sv).GetIndex();
			static const auto stopDialogueEmotion = entries.Require(Kind::kMfg, "StopDialogueEmotion"sv).GetIndex();
			const auto on = [&](std::uint32_t a_index) {
				const auto it = std::ranges::find(samples, a_index, &NumberSample::index);
				return !bRetired && it != samples.end() && it->value > 0.5f;
			};
			const bool bStopBlink = on(stopBlink);
			const bool bStopEyeMovement = on(stopEyeMovement);
			bool bTouched = false;
			if (on(stopLipSync)) {
				ZeroAll(animation->phoneme1);
				ZeroAll(animation->phoneme3);
				bTouched = true;
			}
			if (on(stopDialogueEmotion)) {
				ZeroAll(animation->expression3);
				bTouched = true;
			}
			if (bStopBlink && animation->modifier1.values && animation->modifier1.count > 1) {
				animation->modifier1.values[0] = 0.f;
				animation->modifier1.values[1] = 0.f;
			}

			std::array<std::uint32_t, 4> held{};
			std::array<float, 4> look{};  // down, left, right, up
			bool bLook = false;
			if (!bRetired) {
				for (const auto& sample : samples) {
					// The Stop switches follow the slots in the table, and were read above
					if (sample.index >= slots.size()) {
						continue;
					}
					const auto& slot = slots[sample.index];
					auto* set = SetOf(animation, slot.set);
					if (!set->values || slot.index >= set->count) {
						continue;
					}
					set->values[slot.index] = sample.value;
					// The Look morphs are re-derived into the final tier from the dialogue tier's value, so both are written
					if (slot.set == 1 && slot.index >= 8 && slot.index <= 11) {
						if (animation->modifier1.values && slot.index < animation->modifier1.count) {
							animation->modifier1.values[slot.index] = sample.value;
						}
						look[slot.index - 8] = sample.value;
						bLook = true;
					}
					held[slot.set] |= 1u << slot.index;
					bTouched = true;
				}
			}

			// The gaze proper: the engine rotates the eye bones from its heading and pitch and re-derives the Look morphs
			// from them every frame, so a Look value alone is written over. While any Look is ours the angles are too,
			// the morphs' weight over the engine's own bounds, in radians; let go, they are zeroed, since the engine holds
			// the eyes wherever it was last handed them
			constexpr float kEyeHeadingLimit = 0.489f;
			constexpr float kEyePitchLimit = 0.349f;
			const auto eyes = sampled && !bRetired ? sampled->eyes : std::nullopt;
			if (bLook || eyes || a_data.bEyesHeld) {
				if (animation->eyesHeadingBase != a_data.writtenEyes[0] || animation->eyesPitchBase != a_data.writtenEyes[1]) {
					a_data.engineEyes = { animation->eyesHeadingBase, animation->eyesPitchBase };
				}
				const auto [engineHeading, enginePitch] = a_data.engineEyes;
				float heading = eyes ? (*eyes)[0] * kEyeHeadingLimit : bLook ? std::clamp(look[2] - look[1], -1.f, 1.f) * kEyeHeadingLimit : 0.f;
				float pitch = eyes ? (*eyes)[1] * kEyePitchLimit : bLook ? std::clamp(look[3] - look[0], -1.f, 1.f) * kEyePitchLimit : 0.f;
				// Easing to or from the engine's own tracking: partway from where vanilla put the eyes this update
				if (eyes && sampled->eyesWeight < 1.f) {
					heading = std::lerp(animation->eyesHeading, heading, sampled->eyesWeight);
					pitch = std::lerp(animation->eyesPitch, pitch, sampled->eyesWeight);
				}
				// Offset: the angles on where the engine aims the eyes this update
				if (eyes && sampled->eyesOffset > 0.f) {
					heading += engineHeading * sampled->eyesOffset;
					pitch += enginePitch * sampled->eyesOffset;
				}
				animation->eyesHeading = heading;
				animation->eyesPitch = pitch;
				animation->eyesHeadingBase = heading;
				animation->eyesPitchBase = pitch;
				a_data.writtenEyes = { heading, pitch };
				animation->eyesHeadingOffset = 0.f;
				animation->eyesPitchOffset = 0.f;
				bTouched = true;
			}
			a_data.bEyesHeld = bLook || eyes.has_value();

			// The blink is ours, on the engine's own stage and timer, unless a channel holds a lid; stopped, the lids
			// stay open and the stage waits where it was, so letting go resumes mid-cycle
			if (!bRetired && animation->modifier3.values && animation->modifier3.count > 1 && (held[1] & 0x3) == 0) {
				const float lid = bStopBlink ? 0.f : AdvanceBlink(animation, t_timeDelta, animation->unk21A != 0);
				animation->modifier3.values[0] = lid;
				animation->modifier3.values[1] = lid;
				bTouched = true;
			}

			// The eyes' wander, ours in place of the engine's: a small glance off the aim drawn every so often and
			// settled to, on top of whatever aims the eyes, the engine's tracking or a held gaze; stopped, they sit
			// on their aim
			if (!bRetired) {
				const float dt = t_timeDelta;
				if (bStopEyeMovement) {
					a_data.wanderHeading = 0.f;
					a_data.wanderPitch = 0.f;
					a_data.wanderTargetHeading = 0.f;
					a_data.wanderTargetPitch = 0.f;
				} else {
					a_data.wanderTimer -= dt;
					if (a_data.wanderTimer <= 0.f) {
						a_data.wanderTimer = Settings::fEyeMovementGapMin + (Settings::fEyeMovementGapMax - Settings::fEyeMovementGapMin) * RandomUnit();
						a_data.wanderTargetHeading = (RandomUnit() * 2.f - 1.f) * Settings::fEyeMovementHeading * kEyeHeadingLimit;
						a_data.wanderTargetPitch = (RandomUnit() * 2.f - 1.f) * Settings::fEyeMovementPitch * kEyePitchLimit;
					}
					const float closing = 1.f - std::exp(-Settings::fEyeMovementSpeed * dt);
					a_data.wanderHeading += (a_data.wanderTargetHeading - a_data.wanderHeading) * closing;
					a_data.wanderPitch += (a_data.wanderTargetPitch - a_data.wanderPitch) * closing;
				}
				animation->eyesHeadingOffset = a_data.wanderHeading;
				animation->eyesPitchOffset = a_data.wanderPitch;
				animation->eyesHeading = std::clamp(animation->eyesHeadingBase + a_data.wanderHeading, -kEyeHeadingLimit, kEyeHeadingLimit);
				animation->eyesPitch = std::clamp(animation->eyesPitchBase + a_data.wanderPitch, -kEyePitchLimit, kEyePitchLimit);
			}

			// What was held last pass and is not now starts a release
			for (std::size_t set = 0; set < 4; ++set) {
				if (const auto dropped = a_data.held[set] & ~held[set]; dropped != 0) {
					a_data.releasing[set] |= dropped;
					a_data.releasePassesLeft = MfgData::kReleasePasses;
				}
				a_data.releasing[set] &= ~held[set];
			}
			a_data.held = held;

			if (a_data.releasePassesLeft > 0) {
				for (std::uint32_t set = 0; set < 4; ++set) {
					auto* keys = SetOf(animation, set);
					if (!keys->values) {
						continue;
					}
					for (std::uint32_t i = 0; i < keys->count && i < 32; ++i) {
						if (a_data.releasing[set] & (1u << i)) {
							keys->values[i] = 0.f;
							if (set == 1 && i >= 8 && i <= 11 && animation->modifier1.values && i < animation->modifier1.count) {
								animation->modifier1.values[i] = 0.f;
							}
							bTouched = true;
						}
					}
				}
				if (--a_data.releasePassesLeft == 0) {
					a_data.releasing = {};
				}
			}

			// Asks the engine to put the mfg sets on the mesh; what Mfg Fix sets on every face, here only on a driven one
			if (bTouched) {
				animation->unk217 = true;
			}
		});
	}
}
