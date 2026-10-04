#include "MfgFix.h"

#include <Windows.h>

#include <chrono>
#include <mutex>
#include <vector>

namespace MfgFix
{
	namespace
	{
		bool g_bFixLoaded = false;

		constexpr std::uint32_t kPhonemes = 16;
		constexpr std::uint32_t kModifiers = 14;
		constexpr std::uint32_t kExpressions = 17;

		// The modes MfgConsoleFunc's natives take, as the script comments say
		constexpr int kModeReset = -1;
		constexpr int kModePhoneme = 0;
		constexpr int kModeModifier = 1;
		constexpr int kModeExpressionValue = 2;
		constexpr int kModeExpressionID = 3;

		// A value as the scripts write it, 0 to 100 the normal range and up to 200 allowed; as the engine reads it, a weight
		float WeightOf(int a_value)
		{
			return static_cast<float>(std::clamp(a_value, 0, 200)) / 100.f;
		}

		int ValueOf(float a_weight)
		{
			return static_cast<int>(std::lround(std::clamp(a_weight, 0.f, 2.f) * 100.f));
		}

		RE::BSFaceGenAnimationData* DataOf(RE::Actor* a_actor)
		{
			auto* face = a_actor ? a_actor->GetFaceNodeSkinned() : nullptr;
			return face ? face->GetRuntimeData().animationData.get() : nullptr;
		}

		void SetAt(RE::BSFaceGenKeyframeMultiple& a_keyframe, std::uint32_t a_index, float a_value)
		{
			if (a_keyframe.values && a_index < a_keyframe.count) {
				a_keyframe.values[a_index] = a_value;
			}
		}

		float At(const RE::BSFaceGenKeyframeMultiple& a_keyframe, std::uint32_t a_index)
		{
			return a_keyframe.values && a_index < a_keyframe.count ? a_keyframe.values[a_index] : 0.f;
		}

		// A phoneme or a modifier written where the console writes it, the input tier the merge carries into the
		// finals. Nothing recomputes a final nobody drives, so a value taken to nothing clears the final too
		void Write(RE::BSFaceGenAnimationData* a_data, int a_mode, std::uint32_t a_index, float a_weight)
		{
			if (a_mode == kModePhoneme) {
				SetAt(a_data->phenomeKeyFrame, a_index, a_weight);
				if (a_weight <= 0.f) {
					SetAt(a_data->phoneme3, a_index, 0.f);
				}
			} else if (a_mode == kModeModifier) {
				SetAt(a_data->modifierKeyFrame, a_index, a_weight);
				if (a_weight <= 0.f) {
					SetAt(a_data->modifier3, a_index, 0.f);
				}
			}
		}

		// One value on its way on one head, keyed by the animation data the face update has in hand; a head that is
		// gone drops its moves by their deadline
		struct Move
		{
			const RE::BSFaceGenAnimationData* data;
			int mode;
			std::uint32_t index;
			float target;
			float speed;
			std::chrono::steady_clock::time_point deadline;
		};

		std::mutex g_movesMutex;
		std::vector<Move> g_moves;

		void Aim(RE::BSFaceGenAnimationData* a_data, int a_mode, std::uint32_t a_index, float a_weight, float a_speed)
		{
			// A speed at or under nothing is the instant write; the scripts say 0.1 is close to instant and 0.75 smooth
			if (a_speed <= 0.f) {
				Write(a_data, a_mode, a_index, a_weight);
				return;
			}
			std::lock_guard<std::mutex> lock(g_movesMutex);
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
			for (auto& move : g_moves) {
				if (move.data == a_data && move.mode == a_mode && move.index == a_index) {
					move.target = a_weight;
					move.speed = a_speed;
					move.deadline = deadline;
					return;
				}
			}
			g_moves.push_back(Move{ a_data, a_mode, a_index, a_weight, std::clamp(a_speed, 0.01f, 0.99f), deadline });
		}

		// ---- the natives ----

		bool SetPhonemeModifier(RE::StaticFunctionTag*, RE::Actor* a_actor, int a_mode, int a_id, int a_value)
		{
			auto* data = DataOf(a_actor);
			if (!data) {
				return false;
			}
			switch (a_mode) {
			case kModeReset:
				for (std::uint32_t i = 0; i < kPhonemes; ++i) {
					Write(data, kModePhoneme, i, 0.f);
				}
				for (std::uint32_t i = 0; i < kModifiers; ++i) {
					Write(data, kModeModifier, i, 0.f);
				}
				return true;
			case kModePhoneme:
				if (a_id < 0 || a_id >= static_cast<int>(kPhonemes)) {
					return false;
				}
				Write(data, a_mode, static_cast<std::uint32_t>(a_id), WeightOf(a_value));
				return true;
			case kModeModifier:
				if (a_id < 0 || a_id >= static_cast<int>(kModifiers)) {
					return false;
				}
				Write(data, a_mode, static_cast<std::uint32_t>(a_id), WeightOf(a_value));
				return true;
			case kModeExpressionValue:
				if (a_id < 0 || a_id >= static_cast<int>(kExpressions)) {
					return false;
				}
				if (a_value <= 0) {
					data->ClearExpressionOverride();
					SetAt(data->expression3, static_cast<std::uint32_t>(a_id), 0.f);
				} else {
					data->SetExpressionOverride(static_cast<std::uint32_t>(a_id), WeightOf(a_value));
				}
				return true;
			default:
				return false;
			}
		}

		int GetPhonemeModifier(RE::StaticFunctionTag*, RE::Actor* a_actor, int a_mode, int a_id)
		{
			auto* data = DataOf(a_actor);
			if (!data) {
				return -1;
			}
			switch (a_mode) {
			case kModePhoneme:
				return a_id >= 0 && a_id < static_cast<int>(kPhonemes) ? ValueOf(At(data->phoneme3, static_cast<std::uint32_t>(a_id))) : -1;
			case kModeModifier:
				return a_id >= 0 && a_id < static_cast<int>(kModifiers) ? ValueOf(At(data->modifier3, static_cast<std::uint32_t>(a_id))) : -1;
			case kModeExpressionValue:
				return a_id >= 0 && a_id < static_cast<int>(kExpressions) ? ValueOf(At(data->expression3, static_cast<std::uint32_t>(a_id))) : -1;
			case kModeExpressionID: {
				// The one expression the face holds: the strongest of the finals, -1 when none is up
				int best = -1;
				float strongest = 0.f;
				for (std::uint32_t i = 0; i < kExpressions; ++i) {
					if (const auto value = At(data->expression3, i); value > strongest) {
						strongest = value;
						best = static_cast<int>(i);
					}
				}
				return best;
			}
			default:
				return -1;
			}
		}

		bool SetPhonemeModifierSmooth(RE::StaticFunctionTag*, RE::Actor* a_actor, int a_mode, int a_id, int a_value, float a_speed)
		{
			auto* data = DataOf(a_actor);
			if (!data) {
				return false;
			}
			switch (a_mode) {
			case kModeReset:
				for (std::uint32_t i = 0; i < kPhonemes; ++i) {
					Aim(data, kModePhoneme, i, 0.f, a_speed);
				}
				for (std::uint32_t i = 0; i < kModifiers; ++i) {
					Aim(data, kModeModifier, i, 0.f, a_speed);
				}
				return true;
			case kModePhoneme:
				if (a_id < 0 || a_id >= static_cast<int>(kPhonemes)) {
					return false;
				}
				Aim(data, a_mode, static_cast<std::uint32_t>(a_id), WeightOf(a_value), a_speed);
				return true;
			case kModeModifier:
				if (a_id < 0 || a_id >= static_cast<int>(kModifiers)) {
					return false;
				}
				Aim(data, a_mode, static_cast<std::uint32_t>(a_id), WeightOf(a_value), a_speed);
				return true;
			case kModeExpressionValue:
				// The engine eases an expression override on its own
				return SetPhonemeModifier(nullptr, a_actor, a_mode, a_id, a_value);
			default:
				return false;
			}
		}

		bool ResetMFGSmooth(RE::StaticFunctionTag*, RE::Actor* a_actor, int a_mode, float a_speed)
		{
			auto* data = DataOf(a_actor);
			if (!data) {
				return false;
			}
			if (a_mode == kModeReset || a_mode == kModePhoneme) {
				for (std::uint32_t i = 0; i < kPhonemes; ++i) {
					Aim(data, kModePhoneme, i, 0.f, a_speed);
				}
			}
			if (a_mode == kModeReset || a_mode == kModeModifier) {
				for (std::uint32_t i = 0; i < kModifiers; ++i) {
					Aim(data, kModeModifier, i, 0.f, a_speed);
				}
			}
			return a_mode == kModeReset || a_mode == kModePhoneme || a_mode == kModeModifier;
		}

		// The 32-float construct MfgConsoleFuncExt documents: sixteen phonemes, fourteen modifiers, an expression id and its strength
		bool ApplyExpressionPreset(RE::StaticFunctionTag*, RE::Actor* a_actor, std::vector<float> a_expression, bool a_bOpenMouth, int a_exprPower, float a_exprStrength, float a_modStrength, float a_phStrength, float a_speed)
		{
			auto* data = DataOf(a_actor);
			if (!data || a_expression.size() < kPhonemes + kModifiers + 2) {
				return false;
			}
			const auto scaled = [](float a_value, float a_by) { return std::clamp(a_value * a_by, 0.f, 2.f); };
			if (!a_bOpenMouth) {
				for (std::uint32_t i = 0; i < kPhonemes; ++i) {
					Aim(data, kModePhoneme, i, scaled(a_expression[i], a_phStrength), a_speed);
				}
			}
			for (std::uint32_t i = 0; i < kModifiers; ++i) {
				Aim(data, kModeModifier, i, scaled(a_expression[kPhonemes + i], a_modStrength), a_speed);
			}
			const auto id = static_cast<int>(std::lround(a_expression[kPhonemes + kModifiers]));
			float strength = a_expression[kPhonemes + kModifiers + 1];
			if (strength <= 0.f && a_exprPower > 0) {
				strength = static_cast<float>(a_exprPower) / 100.f;
			}
			if (id >= 0 && id < static_cast<int>(kExpressions)) {
				const auto weight = scaled(strength, a_exprStrength);
				if (weight <= 0.f) {
					data->ClearExpressionOverride();
				} else {
					data->SetExpressionOverride(static_cast<std::uint32_t>(id), weight);
				}
			}
			return true;
		}

		RE::Actor* GetPlayerSpeechTarget(RE::StaticFunctionTag*)
		{
			auto* topics = RE::MenuTopicManager::GetSingleton();
			const auto speaker = topics ? topics->speaker.get() : nullptr;
			return speaker ? speaker->As<RE::Actor>() : nullptr;
		}

		bool IsInDialogue(RE::StaticFunctionTag*, RE::Actor* a_actor)
		{
			auto* data = DataOf(a_actor);
			return data && data->dialogueData != nullptr;
		}

		// Every non-zero of the source over the destination
		bool MergeNonZero(const RE::BSFaceGenKeyframeMultiple& a_source, RE::BSFaceGenKeyframeMultiple& a_destination)
		{
			bool bAny = false;
			if (a_source.values && a_destination.values) {
				for (std::uint32_t i = 0; i < a_source.count && i < a_destination.count; ++i) {
					if (a_source.values[i] != 0.f) {
						a_destination.values[i] = a_source.values[i];
						bAny = true;
					}
				}
			}
			return bAny;
		}

		// Expressions are exclusive, one at a time: the whole tier replaced when the source holds anything
		bool MergeExclusive(const RE::BSFaceGenKeyframeMultiple& a_source, RE::BSFaceGenKeyframeMultiple& a_destination)
		{
			if (!a_source.values || !a_destination.values) {
				return false;
			}
			const auto count = (std::min)(a_source.count, a_destination.count);
			bool bAny = false;
			for (std::uint32_t i = 0; i < count; ++i) {
				bAny = bAny || a_source.values[i] != 0.f;
			}
			if (bAny) {
				for (std::uint32_t i = 0; i < count; ++i) {
					a_destination.values[i] = a_source.values[i];
				}
			}
			return bAny;
		}
	}

	void Detect()
	{
		g_bFixLoaded = GetModuleHandleA("mfgfix.dll") != nullptr;
		if (g_bFixLoaded) {
			logger::warn("mfg: Mfg Fix is loaded (mfgfix.dll). This plugin replaces it: the two hook the same function and blink and ease the face twice over. Remove mfgfix.dll and keep its scripts; the MfgConsoleFunc natives are provided here");
		}
	}

	bool FixLoaded()
	{
		return g_bFixLoaded;
	}

	bool Register(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			return false;
		}
		if (g_bFixLoaded) {
			logger::info("mfg: Mfg Fix's own MfgConsoleFunc natives stand; ours are not registered");
			return true;
		}
		a_vm->RegisterFunction("SetPhonemeModifier", "MfgConsoleFunc", SetPhonemeModifier);
		a_vm->RegisterFunction("GetPhonemeModifier", "MfgConsoleFunc", GetPhonemeModifier);
		a_vm->RegisterFunction("ApplyExpressionPreset", "MfgConsoleFuncExt", ApplyExpressionPreset);
		a_vm->RegisterFunction("ResetMFGSmooth", "MfgConsoleFuncExt", ResetMFGSmooth);
		a_vm->RegisterFunction("SetPhonemeModifierSmooth", "MfgConsoleFuncExt", SetPhonemeModifierSmooth);
		a_vm->RegisterFunction("GetPlayerSpeechTarget", "MfgConsoleFuncExt", GetPlayerSpeechTarget);
		a_vm->RegisterFunction("IsInDialogue", "MfgConsoleFuncExt", IsInDialogue);
		logger::info("mfg: MfgConsoleFunc and MfgConsoleFuncExt natives registered");
		return true;
	}

	void Smooth(RE::BSFaceGenAnimationData* a_data, float a_timeDelta)
	{
		if (!a_data) {
			return;
		}
		std::unique_lock<std::mutex> lock(g_movesMutex, std::try_to_lock);
		if (!lock.owns_lock() || g_moves.empty()) {
			return;
		}
		// The scripts' speed is per frame at sixty: 0.1 is close to instant, 0.75 a smooth few tenths of a second;
		// taken to the time that has passed, so a slow frame moves further
		const auto now = std::chrono::steady_clock::now();
		for (auto it = g_moves.begin(); it != g_moves.end();) {
			if (it->data != a_data) {
				if (now > it->deadline) {
					it = g_moves.erase(it);
				} else {
					++it;
				}
				continue;
			}
			auto& channel = it->mode == kModePhoneme ? a_data->phenomeKeyFrame : a_data->modifierKeyFrame;
			const float current = At(channel, it->index);
			const float keep = std::pow(it->speed, (std::max)(a_timeDelta, 0.f) * 60.f);
			float next = it->target + (current - it->target) * keep;
			if (std::fabs(next - it->target) < 0.002f) {
				next = it->target;
			}
			Write(a_data, it->mode, it->index, next);
			if (next == it->target) {
				it = g_moves.erase(it);
			} else {
				++it;
			}
		}
	}

	bool Merge(RE::BSFaceGenAnimationData* a_data)
	{
		if (!a_data) {
			return false;
		}
		bool bAny = MergeExclusive(a_data->expressionKeyFrame, a_data->expression3);
		bAny = MergeExclusive(a_data->expressionKeyFrame2, a_data->expression3) || bAny;
		bAny = MergeNonZero(a_data->modifierKeyFrame, a_data->modifier3) || bAny;
		bAny = MergeNonZero(a_data->phenomeKeyFrame, a_data->phoneme3) || bAny;
		bAny = MergeNonZero(a_data->customKeyFrame, a_data->custom3) || bAny;

		// NG's Look-group rule: once any of the four Look modifiers is set on the console tier all four carry, zeros
		// included, so a script aiming the eyes right also means "and not left"
		bool bAnyLook = false;
		for (std::uint32_t i = 8; i <= 11; ++i) {
			bAnyLook = bAnyLook || At(a_data->modifierKeyFrame, i) != 0.f;
		}
		if (bAnyLook) {
			for (std::uint32_t i = 8; i <= 11; ++i) {
				SetAt(a_data->modifier3, i, At(a_data->modifierKeyFrame, i));
			}
		}
		return bAny;
	}
}
