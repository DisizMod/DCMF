#include "AnimationPlay.h"

#include "AnimationGraph.h"
#include "SharedTypes.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace AnimationPlay
{
	namespace
	{
		// The engine functions the clip needs, as OAR's Offsets.h has them (SE, AE, VR); taken inside functions
		using AnimationControlCtor_t = void(RE::hkaDefaultAnimationControl*, RE::hkaAnimationBinding*, bool, std::int32_t);
		using ClipUnkUpdate_t = void(RE::hkbClipGenerator*);
		using ClipCheckAtEnd_t = bool(RE::hkbClipGenerator*);
		using ClipComputeTimes_t = void(RE::hkbClipGenerator*, float, float*, float*, std::int32_t*, bool*);
		using BlendPoses_t = RE::hkVector4&(std::uint32_t, const RE::hkQsTransform*, const RE::hkQsTransform*, float, RE::hkQsTransform*);
		using NumTransformTracks_t = std::int32_t(const RE::hkArray<std::int16_t>&, void*, std::int16_t, std::int32_t);
		using ConvertPose_t = void(std::int32_t, RE::hkQsTransform*, std::int32_t, RE::hkQsTransform*, void*, const RE::hkArray<std::int16_t>&, bool, RE::hkaMirroredSkeleton*, bool);
		using MirroredSkeleton_t = RE::hkaMirroredSkeleton*(RE::hkbCharacterSetup*);

		AnimationControlCtor_t* AnimationControlCtor()
		{
			static REL::Relocation<AnimationControlCtor_t> fn{ REL::VariantID(63230, 64170, 0xB51CB0) };
			return fn.get();
		}
		ClipUnkUpdate_t* ClipUnkUpdate()
		{
			static REL::Relocation<ClipUnkUpdate_t> fn{ REL::VariantID(58628, 59279, 0xA49FD0) };
			return fn.get();
		}
		ClipCheckAtEnd_t* ClipCheckAtEnd()
		{
			static REL::Relocation<ClipCheckAtEnd_t> fn{ REL::VariantID(58630, 59281, 0xA4A170) };
			return fn.get();
		}
		ClipComputeTimes_t* ClipComputeTimes()
		{
			static REL::Relocation<ClipComputeTimes_t> fn{ REL::VariantID(58622, 59273, 0xA48D60) };
			return fn.get();
		}
		BlendPoses_t* BlendPoses()
		{
			static REL::Relocation<BlendPoses_t> fn{ REL::VariantID(63192, 64112, 0xB4DD80) };
			return fn.get();
		}
		NumTransformTracks_t* NumTransformTracks()
		{
			static REL::Relocation<NumTransformTracks_t> fn{ REL::VariantID(63171, 64091, 0xB4C690) };
			return fn.get();
		}
		ConvertPose_t* ConvertPose()
		{
			static REL::Relocation<ConvertPose_t> fn{ REL::VariantID(59332, 60005, 0xA7C160) };
			return fn.get();
		}
		MirroredSkeleton_t* MirroredSkeleton()
		{
			static REL::Relocation<MirroredSkeleton_t> fn{ REL::VariantID(58817, 59480, 0xA5E8C0) };
			return fn.get();
		}

		// Havok's heap, through the thread's memory router
		void* HavokHeapAlloc(int a_bytes)
		{
			static REL::Relocation<std::uint32_t*> tlsIndex{ REL::VariantID(520865, 407383, 0x30A8C04) };
			auto* router = static_cast<RE::hkMemoryRouter*>(REX::W32::TlsGetValue(*tlsIndex));
			return router && router->Heap ? router->Heap->BlockAlloc(a_bytes) : nullptr;
		}

		// A binding with its triggers: hkReferencedObject, then the binding; CommonLibSSE-NG declares it without a body
		struct BindingWithTriggers
		{
			std::uint8_t base[0x10];
			RE::hkRefPtr<RE::hkaAnimationBinding> binding;  // 10
		};

		// hkArray's three words, for the scratch buffer ConvertPose takes
		struct RawArray
		{
			float* data;
			std::int32_t size;
			std::int32_t capacityAndFlags;
		};

		// OAR's FakeClipGenerator: hkbClipGenerator's layout, so the engine's own clip functions take it, with only the
		// fields its update and sampling read
		struct FakeClip
		{
			std::uint64_t pad00 = 0;
			std::uint64_t pad08 = 0;
			std::uint64_t pad10 = 0;
			std::uint64_t pad18 = 0;
			std::uint64_t pad20 = 0;
			std::uint64_t pad28 = 0;
			std::uint64_t userData = 0;
			std::uint64_t pad38 = 0;
			std::uint64_t pad40 = 0;
			std::uint64_t pad48 = 0;
			RE::hkRefPtr<RE::hkbClipTriggerArray> triggers;                      // 050
			float cropStartAmountLocalTime = 0.f;                                // 058
			float cropEndAmountLocalTime = 0.f;                                  // 05C
			float startTime = 0.f;                                               // 060
			float playbackSpeed = 1.f;                                           // 064
			float enforcedDuration = 0.f;                                        // 068
			float userControlledTimeFraction = 0.f;                              // 06C
			std::uint16_t animationBindingIndex = 0;                             // 070
			REX::EnumSet<RE::hkbClipGenerator::PlaybackMode, std::uint8_t> mode;  // 072
			std::uint8_t flags = 0;                                              // 073
			std::uint32_t pad74 = 0;
			std::uint64_t pad78 = 0;
			std::uint64_t pad80 = 0;
			RE::hkRefPtr<RE::hkaDefaultAnimationControl> animationControl;  // 088
			std::uint64_t pad90 = 0;
			RE::hkaDefaultAnimationControlMapperData* mapperData = nullptr;  // 098
			RE::hkaAnimationBinding* binding = nullptr;                      // 0A0
			std::uint64_t padA8 = 0;
			RE::hkQsTransform extractedMotion;  // 0B0
			std::uint64_t padE0 = 0;
			std::uint64_t padE8 = 0;
			float localTime = 0.f;                           // 0F0
			float time = 0.f;                                // 0F4
			float previousUserControlledTimeFraction = 0.f;  // 0F8
			std::uint32_t padFC = 0;
			std::uint32_t pad100 = 0;
			bool atEnd = false;             // 104
			bool ignoreStartTime = false;   // 105
			bool pingPongBackward = false;  // 106

			RE::hkRefPtr<RE::hkbBehaviorGraph> behaviorGraph;
			bool bPaired = false;  // a paired file: its root lines it up with a partner there is not, so the graph's is kept

			RE::hkbClipGenerator* AsClip() { return reinterpret_cast<RE::hkbClipGenerator*>(this); }

			// hkbClipGenerator::Activate's part that matters: the file queued, the animation bound, a control made for it
			void Activate(const RE::hkbContext& a_context)
			{
				behaviorGraph = a_context.character->behaviorGraph;
				if (!userData) {
					RE::AnimationFileManagerSingleton::GetSingleton()->Queue(a_context, AsClip(), nullptr);
				}
				if (userData == 8) {
					return;  // still loading; Update tries again
				}
				const auto* graph = reinterpret_cast<RE::BShkbAnimationGraph*>(behaviorGraph->userData);
				const auto& setup = graph->characterInstance.setup;
				auto* bindingSet = graph->characterInstance.animationBindingSet.get();
				if (!bindingSet) {
					bindingSet = setup->animationBindingSet.get();
				}
				if (!bindingSet || animationBindingIndex >= bindingSet->bindings.size()) {
					return;
				}
				auto* withTriggers = reinterpret_cast<BindingWithTriggers*>(bindingSet->bindings[animationBindingIndex]);
				if (!withTriggers) {
					return;
				}
				binding = withTriggers->binding.get();
				if (!animationControl && binding) {
					auto* control = static_cast<RE::hkaDefaultAnimationControl*>(HavokHeapAlloc(sizeof(RE::hkaDefaultAnimationControl)));
					if (!control) {
						return;
					}
					AnimationControlCtor()(control, binding, true, 2);
					animationControl = RE::hkRefPtr<RE::hkaDefaultAnimationControl>(control);
				}
			}

			// hkbClipGenerator::Update's part that matters: the local time advanced by the engine's own arithmetic
			void Update(const RE::hkbContext& a_context, float a_delta)
			{
				if (userData == 8 && RE::AnimationFileManagerSingleton::GetSingleton()->Load(a_context, AsClip(), nullptr)) {
					Activate(a_context);
				}
				if (!animationControl) {
					return;
				}
				if (std::fabs(playbackSpeed) <= 0.001f) {
					playbackSpeed = playbackSpeed <= 0.f ? 0.001f : -0.001f;
				}
				animationControl->localTime = localTime;
				if (atEnd && mode.get() != RE::hkbClipGenerator::PlaybackMode::kModeSinglePlay) {
					ClipUnkUpdate()(AsClip());
				}
				ClipCheckAtEnd()(AsClip());
				float previous = 0.f;
				float next = 0.f;
				std::int32_t loops = 0;
				bool bEnded = false;
				ClipComputeTimes()(AsClip(), a_delta, &previous, &next, &loops, &bEnded);
				if (!atEnd) {
					localTime = next;
					animationControl->localTime = next;
					atEnd = bEnded;
					if ((loops & 1) != 0) {
						pingPongBackward = !pingPongBackward;
					}
				}
				time += a_delta * animationControl->playbackSpeed;
				previousUserControlledTimeFraction = userControlledTimeFraction;
			}

			// OAR's ActiveAnimationPreview::OnGenerate: the clip sampled at its local time and blended over the graph's pose
			void Generate(const RE::hkbContext& a_context, RE::hkbGeneratorOutput& a_output)
			{
				if (!animationControl || !animationControl->binding || !animationControl->binding->animation) {
					return;
				}
				constexpr auto kPose = static_cast<std::int32_t>(RE::hkbGeneratorOutput::StandardTracks::kPose);
				if (!a_output.tracks || a_output.tracks->masterHeader.numTracks <= kPose) {
					return;
				}
				auto& header = a_output.tracks->trackHeaders[kPose];
				if (header.onFraction <= 0.f) {
					return;
				}
				auto* poseData = reinterpret_cast<float*>(reinterpret_cast<char*>(a_output.tracks) + header.dataOffset);
				auto* poseOut = reinterpret_cast<RE::hkQsTransform*>(poseData);
				const auto poseCount = header.numData;

				auto* bind = animationControl->binding;
				const auto& animation = bind->animation;
				auto tracks = NumTransformTracks()(bind->transformTrackToBoneIndices, nullptr, poseCount, animation->numberOfTransformTracks) + 1;
				if (a_context.character->numTracksInLOD > -1 && a_context.character->numTracksInLOD < tracks) {
					tracks = a_context.character->numTracksInLOD;
				}
				if (tracks <= 0) {
					return;
				}

				std::vector<RE::hkQsTransform> sampled(static_cast<std::size_t>(tracks));
				std::vector<float> floats(static_cast<std::size_t>((std::max)(animation->numberOfFloatTracks, 0)));
				animation->SamplePartialTracks(localTime, static_cast<std::uint32_t>(tracks), sampled.data(), static_cast<std::uint32_t>(floats.size()), floats.data(), nullptr);

				RE::hkaMirroredSkeleton* mirrored = nullptr;
				if ((flags & 4) != 0) {
					mirrored = MirroredSkeleton()(a_context.character->setup.get());
				}

				// The scratch words after the pose data, zeroed, as the engine hands them to the conversion
				RawArray scratch;
				scratch.data = &poseData[0x30 * poseCount];
				scratch.size = (poseCount + 0x20) >> 5;
				scratch.capacityAndFlags = scratch.size | static_cast<std::int32_t>(1u << 31);
				for (std::int32_t i = 0; i < scratch.size; ++i) {
					scratch.data[i] = 0.f;
				}

				std::vector<RE::hkQsTransform> converted(static_cast<std::size_t>(poseCount));
				ConvertPose()(tracks, sampled.data(), poseCount, converted.data(), &scratch, bind->transformTrackToBoneIndices, bind->blendHint.get() == RE::hkaAnimationBinding::BlendHint::kAdditive, mirrored, true);

				if (bPaired && poseCount > 0) {
					converted[0] = poseOut[0];
				}
				const auto blend = (std::min)(static_cast<std::int32_t>(poseCount), tracks);
				BlendPoses()(static_cast<std::uint32_t>(blend), converted.data(), poseOut, 0.f, poseOut);
			}

			void Unload(const RE::hkbContext& a_context)
			{
				if (userData) {
					RE::AnimationFileManagerSingleton::GetSingleton()->Unload(a_context, AsClip(), nullptr);
				}
			}
		};

		constexpr std::size_t kUpdateIndex = 0x5;
		constexpr std::size_t kGenerateIndex = 0x17;

		using Update_t = void(RE::hkbBehaviorGraph*, const RE::hkbContext&, float);
		using Generate_t = void(RE::hkbBehaviorGraph*, const RE::hkbContext&, const RE::hkbGeneratorOutput**, RE::hkbGeneratorOutput&, float);
		Update_t* g_update = nullptr;
		Generate_t* g_generate = nullptr;

		// What plays on a graph, and what is asked of it from the main thread
		struct Playing
		{
			std::unique_ptr<FakeClip> clip;
			std::optional<std::uint16_t> next;  // a Play waiting for the graph's thread
			std::string file;                   // what next plays, and as which replacer, for the tracker
			std::string replacer;
			bool bStop = false;
		};

		std::mutex g_lock;
		std::unordered_map<RE::hkbBehaviorGraph*, Playing> g_playing;  // by the root graph the clip plays on

		// Behavior threads: the clip made, stepped and unmade on the graph's own thread, with the context its file wants
		void UpdateHook(RE::hkbBehaviorGraph* a_this, const RE::hkbContext& a_context, float a_timestep)
		{
			g_update(a_this, a_context, a_timestep);

			std::scoped_lock guard(g_lock);
			const auto it = g_playing.find(a_this);
			if (it == g_playing.end()) {
				return;
			}
			auto& playing = it->second;
			if (playing.clip && (playing.bStop || playing.next || playing.clip->atEnd)) {
				AnimationGraph::Ended(a_context, playing.clip.get());
				playing.clip->Unload(a_context);
				playing.clip.reset();
			}
			if (playing.next) {
				playing.clip = std::make_unique<FakeClip>();
				playing.clip->animationBindingIndex = *playing.next;
				playing.clip->mode = RE::hkbClipGenerator::PlaybackMode::kModeSinglePlay;
				playing.clip->bPaired = std::filesystem::path(playing.file).filename().string().starts_with("paired_");
				playing.clip->Activate(a_context);
				AnimationGraph::Started(a_context, playing.clip.get(), playing.file, playing.replacer);
				playing.next.reset();
			}
			if (!playing.clip) {
				g_playing.erase(it);
				return;
			}
			playing.clip->Update(a_context, a_timestep);
		}

		void GenerateHook(RE::hkbBehaviorGraph* a_this, const RE::hkbContext& a_context, const RE::hkbGeneratorOutput** a_activeChildrenOutput, RE::hkbGeneratorOutput& a_output, float a_timeOffset)
		{
			g_generate(a_this, a_context, a_activeChildrenOutput, a_output, a_timeOffset);

			std::scoped_lock guard(g_lock);
			const auto it = g_playing.find(a_context.character ? a_context.character->behaviorGraph.get() : nullptr);
			if (it != g_playing.end() && it->second.clip && !it->second.bStop) {
				it->second.clip->Generate(a_context, a_output);
			}
		}

		RE::hkbBehaviorGraph* RootOf(RE::Actor* a_actor)
		{
			auto* graph = AnimationGraph::GraphOn(a_actor);
			return graph ? graph->behaviorGraph : nullptr;
		}
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_hkbBehaviorGraph[0] };
		g_update = reinterpret_cast<Update_t*>(vtable.write_vfunc(kUpdateIndex, UpdateHook));
		g_generate = reinterpret_cast<Generate_t*>(vtable.write_vfunc(kGenerateIndex, GenerateHook));
		logger::info("animations: hooked hkbBehaviorGraph::Update and Generate, for Play");
	}

	bool Play(RE::Actor* a_actor, const Components::AnimationValue& a_value, std::string* a_outWhy)
	{
		const auto binding = AnimationGraph::BindingOf(a_actor, a_value, a_outWhy);
		auto* root = binding ? binding->graph->behaviorGraph : nullptr;
		if (!root) {
			return false;
		}
		std::scoped_lock guard(g_lock);
		auto& playing = g_playing[root];
		playing.next = binding->index;
		playing.file = Components::AnimationValue::Normalize(a_value.file.GetValue());
		playing.replacer = a_value.bReplacer ? std::string(a_value.replacer.GetValue()) : std::string{};
		playing.bStop = false;
		logger::info("animations: playing '{}' (binding {}) on {:08X}", a_value.GetArgument(), binding->index, a_actor->GetFormID());
		return true;
	}

	void Stop(RE::Actor* a_actor)
	{
		auto* root = RootOf(a_actor);
		std::scoped_lock guard(g_lock);
		if (const auto it = root ? g_playing.find(root) : g_playing.end(); it != g_playing.end()) {
			it->second.bStop = true;
		}
	}

	bool IsPlaying(RE::Actor* a_actor)
	{
		auto* root = RootOf(a_actor);
		std::scoped_lock guard(g_lock);
		const auto it = root ? g_playing.find(root) : g_playing.end();
		return it != g_playing.end() && !it->second.bStop;
	}
}
