#include "Preview.h"

#include "ClipPlayer.h"
#include "RegisteredMods.h"
#include "Resolve.h"
#include "Settings.h"
#include "apply/Entries.h"

#include <mutex>

namespace Preview
{
	namespace
	{
		enum class Kind : std::uint8_t
		{
			kNone,
			kModifier,
			kClip,
			kFrame,
			kTime
		};

		struct Previewed
		{
			Kind kind = Kind::kNone;
			RE::ObjectRefHandle target;
			SubMod* modifier = nullptr;
			Clip* clip = nullptr;
			std::uint16_t variant = 0;
			const Keyframe* frame = nullptr;
			float time = 0.f;
			bool bStarted = false;
			ActorState::RunningClip running;
		};

		std::mutex g_lock;
		Previewed g_previewed;

		// The actor kept by the actor state, its Actor window holds let go
		void Begin(RE::TESObjectREFR* a_refr, Previewed a_previewed)
		{
			if (!a_refr) {
				return;
			}
			ActorState::Watch(a_refr->GetHandle());
			if (const auto state = ActorState::Get(a_refr->GetHandle())) {
				state->manual.With([](ActorState::Manual& a_manual) { a_manual = {}; });
			}
			a_previewed.target = a_refr->GetHandle();
			std::scoped_lock lock(g_lock);
			g_previewed = std::move(a_previewed);
		}

		void Clear(ActorState::Sampled& a_out)
		{
			for (auto& list : a_out.numbers) {
				list.clear();
			}
			for (auto& list : a_out.colours) {
				list.clear();
			}
			for (auto& list : a_out.references) {
				list.clear();
			}
			for (auto& list : a_out.texts) {
				list.clear();
			}
		}

		template <class T>
		void Merge(std::vector<T>& a_into, const std::vector<T>& a_from)
		{
			for (const auto& sample : a_from) {
				if (auto it = std::ranges::find(a_into, sample.index, &T::index); it != a_into.end()) {
					*it = sample;
				} else {
					a_into.push_back(sample);
				}
			}
		}

		// Where a variant's run starts: from what the actor has on each claimed channel now, or its rest when cleared
		void StartRun(Previewed& a_previewed, float a_now, const ActorState::Sampled& a_under, RE::TESObjectREFR* a_refr)
		{
			auto& running = a_previewed.running;
			running = {};
			running.clip = a_previewed.clip;
			running.priority = INT32_MAX;
			running.variant = a_previewed.variant;
			running.startedAt = a_now;
			const auto& tracks = a_previewed.clip->GetTracks();
			running.length = a_previewed.variant < tracks.size() && !tracks[a_previewed.variant].keyframes.empty() ?
			                     tracks[a_previewed.variant].keyframes.back()->time + (tracks[a_previewed.variant].keyframes.back()->bHold ? tracks[a_previewed.variant].keyframes.back()->holdTime : 0.f) :
			                     0.f;
			ClipPlayer::CaptureStart(running, a_under, a_refr);
			a_previewed.bStarted = true;
		}
	}

	void StartModifier(RE::TESObjectREFR* a_refr, SubMod* a_modifier)
	{
		Begin(a_refr, { .kind = Kind::kModifier, .modifier = a_modifier });
	}

	void StartClip(RE::TESObjectREFR* a_refr, Clip* a_clip, std::uint16_t a_variant)
	{
		Begin(a_refr, { .kind = Kind::kClip, .clip = a_clip, .variant = a_variant });
	}

	void StartFrame(RE::TESObjectREFR* a_refr, Clip* a_clip, std::uint16_t a_variant, const Keyframe* a_frame)
	{
		Begin(a_refr, { .kind = Kind::kFrame, .clip = a_clip, .variant = a_variant, .frame = a_frame });
	}

	void HoldAt(RE::TESObjectREFR* a_refr, Clip* a_clip, std::uint16_t a_variant, float a_time)
	{
		// Moved along an item already held: the time alone changes, the run kept
		{
			std::scoped_lock lock(g_lock);
			if (g_previewed.kind == Kind::kTime && g_previewed.clip == a_clip && g_previewed.variant == a_variant && a_refr && g_previewed.target == a_refr->GetHandle()) {
				g_previewed.time = a_time;
				return;
			}
		}
		Begin(a_refr, { .kind = Kind::kTime, .clip = a_clip, .variant = a_variant, .time = a_time });
	}

	std::optional<float> HeldAt(const Clip* a_clip, std::uint16_t a_variant)
	{
		std::scoped_lock lock(g_lock);
		if (g_previewed.kind == Kind::kTime && g_previewed.clip == a_clip && g_previewed.variant == a_variant) {
			return g_previewed.time;
		}
		return std::nullopt;
	}

	std::optional<float> Playhead(const Clip* a_clip, std::uint16_t a_variant)
	{
		std::scoped_lock lock(g_lock);
		if (g_previewed.kind == Kind::kClip && g_previewed.clip == a_clip && g_previewed.variant == a_variant && g_previewed.bStarted) {
			return Resolve::Now() - g_previewed.running.startedAt;
		}
		return std::nullopt;
	}

	void Stop()
	{
		std::scoped_lock lock(g_lock);
		g_previewed = {};
	}

	bool IsPreviewing(const void* a_item)
	{
		std::scoped_lock lock(g_lock);
		switch (g_previewed.kind) {
		case Kind::kModifier:
			return a_item == g_previewed.modifier;
		case Kind::kClip:
			return g_previewed.variant < g_previewed.clip->GetTracks().size() && a_item == &g_previewed.clip->GetTracks()[g_previewed.variant];
		case Kind::kFrame:
			return a_item == g_previewed.frame;
		default:
			return false;
		}
	}

	void Apply(ActorState::State& a_state, RE::TESObjectREFR* a_refr, float a_now, ActorState::Sampled& a_out)
	{
		std::scoped_lock lock(g_lock);
		if (g_previewed.kind == Kind::kNone || g_previewed.target != a_state.handle) {
			return;
		}

		// A hold made in the Actor window ends the preview
		const bool bHolding = a_state.manual.With([](const ActorState::Manual& a_manual) {
			return !a_manual.values.empty() || !a_manual.colours.empty() || !a_manual.references.empty() || !a_manual.texts.empty() || a_manual.bodyPreset.has_value();
		});
		if (bHolding) {
			g_previewed = {};
			return;
		}

		if (Settings::bPreviewClearChannels) {
			Clear(a_out);
		}

		// Lock spine: the spine, the neck and head and the eyes on their rest, so the animation does not move what is
		// being judged; whatever else drives them dropped, the previewed item's own poses going on top below
		if (Settings::bPreviewLockSpine) {
			auto& entries = Apply::Entries::GetSingleton();
			for (const auto kind : { Apply::Kind::kSpine, Apply::Kind::kHead, Apply::Kind::kGaze }) {
				const auto k = static_cast<std::size_t>(kind);
				a_out.numbers[k].clear();
				a_out.texts[k].clear();
				a_out.references[k].clear();
				const auto mode = entries.Require(kind, "mode"sv);
				const auto modes = entries.Get(mode).modes;
				const auto fixed = std::ranges::find(modes, "Fixed"sv);
				if (!mode.IsValid() || fixed == modes.end()) {
					continue;
				}
				a_out.numbers[k].push_back({ mode.GetIndex(), static_cast<float>(fixed - modes.begin()) });
				if (const auto weight = entries.Require(kind, "weight"sv); weight.IsValid()) {
					a_out.numbers[k].push_back({ weight.GetIndex(), 1.f });
				}
				std::ranges::sort(a_out.numbers[k], {}, &Apply::NumberSample::index);
			}
		}
		a_out.bAPose = Settings::bPreviewHoldAPose;

		switch (g_previewed.kind) {
		case Kind::kModifier: {
			// The modifier alone at full weight, its channels over what is there
			ActorState::Sampled own;
			Resolve::FoldModifier(g_previewed.modifier, a_refr, own);
			for (std::size_t k = 0; k < own.numbers.size(); ++k) {
				Merge(a_out.numbers[k], own.numbers[k]);
				Merge(a_out.colours[k], own.colours[k]);
				Merge(a_out.references[k], own.references[k]);
				Merge(a_out.texts[k], own.texts[k]);
			}
			if (!own.presetSliders.empty()) {
				a_out.presetSliders = std::move(own.presetSliders);
			}
			break;
		}
		case Kind::kClip: {
			// Played from its start and again once it is over, half a second between
			if (!g_previewed.bStarted || a_now >= g_previewed.running.startedAt + g_previewed.running.length + 0.5f) {
				StartRun(g_previewed, a_now, a_out, a_refr);
			}
			ClipPlayer::FoldRun(g_previewed.running, a_refr, a_now, a_out, nullptr);
			break;
		}
		case Kind::kFrame:
		case Kind::kTime: {
			// The clip as it is at the frame's time, or at the time scrubbed to, held there
			if (!g_previewed.bStarted) {
				StartRun(g_previewed, a_now, a_out, a_refr);
			}
			g_previewed.running.startedAt = a_now - (g_previewed.kind == Kind::kFrame ? g_previewed.frame->time : g_previewed.time);
			ClipPlayer::FoldRun(g_previewed.running, a_refr, a_now, a_out, nullptr);
			break;
		}
		default:
			break;
		}
	}
}
