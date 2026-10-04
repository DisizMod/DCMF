#include "ActorState.h"

#include "ClipPlayer.h"
#include "Pose.h"
#include "Preview.h"
#include "Resources.h"
#include "Resolve.h"
#include "Settings.h"
#include "apply/Entries.h"
#include "apply/Overlay.h"
#include "scan/Scan.h"
#include "scan/Xml.h"

namespace ActorState
{
	namespace
	{
		// What the main thread keeps to decide when to read an actor again; touched only there
		struct Tracking
		{
			std::shared_ptr<State> state;
			const void* actor = nullptr;  // matched against what is marked dirty, never dereferenced
			const void* root = nullptr;
			std::size_t signature = 0;
			bool bDirty = true;
			bool bTracked = false;
			RE::FormID formID = 0;
		};

		std::shared_mutex g_lock;
		std::unordered_map<uint32_t, Tracking> g_states;

		// Tracking begun and ended since the last frame, in order, for Resources to tell its listeners on the main thread
		struct Change
		{
			RE::ObjectRefHandle handle;
			RE::FormID formID;
			bool bTracked;
		};
		std::vector<Change> g_changes;

		// A preset's sliders read at the body weight, added onto the body's morphs
		void AddPreset(std::vector<Apply::NumberSample>& a_body, const PresetMix& a_mix, float a_bodyWeight)
		{
			for (const auto& [index, ends] : a_mix) {
				const float value = std::lerp(ends[0], ends[1], a_bodyWeight);
				if (auto it = std::ranges::find(a_body, index, &Apply::NumberSample::index); it != a_body.end()) {
					it->value += value;
				} else {
					a_body.push_back({ index, value });
				}
			}
		}
		std::mutex g_dirtyLock;
		std::unordered_set<const void*> g_dirty;
		uint32_t g_watched = 0;
		uint32_t g_generation = 0;
		uint32_t g_sampleGeneration = 0;
		std::vector<std::shared_ptr<State>> g_retired;

		// For the hooks, rebuilt each frame; a retired actor lingers a few frames
		struct Lookup
		{
			std::shared_ptr<State> state;
			const void* root = nullptr;
			const void* animationData = nullptr;
			uint32_t framesRetired = 0;
		};
		std::shared_mutex g_lookupLock;
		std::vector<Lookup> g_lookups;
		constexpr uint32_t kRetiredFrames = 4;

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			return out;
		}

		void Combine(std::size_t& a_seed, std::size_t a_value)
		{
			a_seed ^= a_value + 0x9e3779b97f4a7c15ull + (a_seed << 6) + (a_seed >> 2);
		}

		// Cheap enough for every frame: what hangs off the root and the face, and whether it is shown.
		// Armor attaches under the root and head parts under the face, so a swap changes this.
		std::size_t SignatureOf(RE::NiAVObject* a_root, RE::NiAVObject* a_face)
		{
			std::size_t seed = 0;
			const auto addChildren = [&](RE::NiAVObject* a_object, int a_depth, const auto& a_self) -> void {
				auto* node = a_object ? a_object->AsNode() : nullptr;
				if (!node) {
					return;
				}
				for (const auto& child : node->GetChildren()) {
					// Our own overlay clones come and go under the skin, and are not a change of the actor
					if (child && child->name.c_str() && std::string_view(child->name.c_str()).starts_with(Apply::Overlay::kNodePrefix)) {
						continue;
					}
					Combine(seed, reinterpret_cast<std::size_t>(child.get()));
					if (child) {
						Combine(seed, child->GetFlags().any(RE::NiAVObject::Flag::kHidden));
						if (a_depth > 0) {
							a_self(child.get(), a_depth - 1, a_self);
						}
					}
				}
			};
			Combine(seed, reinterpret_cast<std::size_t>(a_root));
			addChildren(a_root, 1, addChildren);
			Combine(seed, reinterpret_cast<std::size_t>(a_face));
			addChildren(a_face, 0, addChildren);
			return seed;
		}

		// The shapes an SMP config collides with, by config; read once each
		const std::unordered_set<std::string>& CollisionShapes(const std::string& a_config)
		{
			static std::unordered_map<std::string, std::unordered_set<std::string>> cache;
			if (const auto it = cache.find(a_config); it != cache.end()) {
				return it->second;
			}
			auto& shapes = cache[a_config];
			const auto bytes = Scan::Files::Read(a_config);
			Scan::Xml::Node root;
			std::string error;
			if (!bytes.empty() && Scan::Xml::Parse(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), root, error)) {
				const std::function<void(const Scan::Xml::Node&)> visit = [&](const Scan::Xml::Node& a_node) {
					if (a_node.name == "per-vertex-shape" || a_node.name == "per-triangle-shape") {
						shapes.insert(Lower(a_node.Attribute("name")));
					}
					for (const auto& child : a_node.children) {
						visit(child);
					}
				};
				visit(root);
			}
			return shapes;
		}

		std::string ExtraString(const RE::NiAVObject* a_object, const char* a_key)
		{
			const auto* extra = a_object->GetExtraData<RE::NiStringExtraData>(a_key);
			return extra && extra->value ? std::string(extra->value) : std::string{};
		}

		// Walks the 3D; main thread
		std::shared_ptr<Parts> Read(RE::TESObjectREFR* a_refr, RE::NiAVObject* a_root, const Scan::Catalogue& a_catalogue)
		{
			auto parts = std::make_shared<Parts>();
			if (!a_root) {
				return parts;
			}
			parts->bLoaded = true;

			std::unordered_map<std::string, const Scan::HeadPart*> headParts;
			for (const auto& part : a_catalogue.headParts) {
				headParts.emplace(Lower(part.editorID), &part);
			}
			constexpr std::array kinds{ "race"sv, "expression"sv, "chargen"sv };

			struct Context
			{
				bool bFace = false;
				std::string bodyTri;
				std::string physics;
			};

			// Keeps a node only if a shape under it has a TRI
			const std::function<bool(RE::NiAVObject*, Context, Part&)> collect = [&](RE::NiAVObject* a_object, Context a_context, Part& a_out) {
				a_out.name = a_object->name.empty() ? std::string("(unnamed)") : std::string(a_object->name.c_str());
				a_out.bHidden = a_object->GetFlags().any(RE::NiAVObject::Flag::kHidden);
				a_context.bFace = a_context.bFace || netimmerse_cast<RE::BSFaceGenNiNode*>(a_object);
				if (auto tri = ExtraString(a_object, "BODYTRI"); !tri.empty()) {
					a_context.bodyTri = "meshes/" + Scan::Files::Normalize(tri);
				}
				if (auto config = ExtraString(a_object, "HDT Skinned Mesh Physics Object"); !config.empty()) {
					a_context.physics = Scan::Files::Normalize(config);
				}

				if (a_object->AsGeometry()) {
					a_out.bGeometry = true;
					if (!a_context.physics.empty() && CollisionShapes(a_context.physics).contains(Lower(a_out.name))) {
						a_out.collision = a_context.physics;
					}
					// A face shape is named after its head part
					if (const auto it = a_context.bFace ? headParts.find(Lower(a_out.name)) : headParts.end(); it != headParts.end()) {
						const auto& part = *it->second;
						for (std::size_t i = 0; i < std::size(part.tris); ++i) {
							if (!part.tris[i].empty()) {
								a_out.tris.push_back({ part.tris[i], std::format("{} TRI of head part {}", kinds[i], part.editorID) });
							}
						}
						for (const auto& set : a_catalogue.faceMorphs) {
							if (!set.extends.empty() && std::ranges::find(set.headParts, part.formID) != set.headParts.end()) {
								a_out.tris.push_back({ set.tri, std::format("extends {}", set.extends) });
							}
						}
					}
					if (!a_context.bodyTri.empty()) {
						a_out.tris.push_back({ a_context.bodyTri, "BODYTRI stamp on the shape or a node above it" });
					}
					return !a_out.tris.empty();
				}

				if (auto* node = a_object->AsNode()) {
					for (const auto& child : node->GetChildren()) {
						if (!child) {
							continue;
						}
						Part part;
						if (collect(child.get(), a_context, part)) {
							a_out.children.push_back(std::move(part));
						}
					}
				}
				return !a_out.children.empty();
			};

			Part top;
			if (collect(a_root, {}, top)) {
				parts->roots.push_back(std::move(top));
			}

			// The morph names of every TRI found, from the sets the scan read
			std::unordered_map<std::string_view, std::pair<const std::vector<Scan::Morph>*, bool>> morphsByTri;  // and whether the body's
			for (const auto& set : a_catalogue.faceMorphs) {
				morphsByTri.emplace(set.tri, std::pair{ &set.morphs, false });
			}
			for (const auto& set : a_catalogue.bodyMorphs) {
				morphsByTri.emplace(set.tri, std::pair{ &set.morphs, true });
			}
			const std::function<void(const Part&)> gather = [&](const Part& a_part) {
				for (const auto& tri : a_part.tris) {
					if (const auto it = morphsByTri.find(tri.path); it != morphsByTri.end()) {
						auto& own = it->second.second ? parts->bodyMorphs : parts->faceMorphs;
						for (const auto& morph : *it->second.first) {
							parts->morphs.insert(morph.name);
							own.insert(morph.name);
						}
					}
				}
				for (const auto& child : a_part.children) {
					gather(child);
				}
			};
			for (const auto& root : parts->roots) {
				gather(root);
			}
			return parts;
		}

		Tracking& Ensure(RE::TESObjectREFR* a_refr)
		{
			const auto handle = a_refr->GetHandle();
			auto& tracking = g_states[handle.native_handle()];
			if (!tracking.state) {
				tracking.state = std::make_shared<State>(handle);
				tracking.actor = a_refr;
				tracking.formID = a_refr->GetFormID();
				g_changes.push_back({ handle, tracking.formID, true });
			}
			return tracking;
		}

		// Let go of an actor: anyone still holding it sees the flag
		void Erase(std::unordered_map<uint32_t, Tracking>::iterator a_it)
		{
			a_it->second.state->bRetired = true;
			g_retired.push_back(a_it->second.state);
			g_changes.push_back({ a_it->second.state->handle, a_it->second.formID, false });
			g_states.erase(a_it);
		}
	}

	void Update()
	{
		const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue();

		struct Pending
		{
			std::shared_ptr<State> state;
			RE::NiPointer<RE::TESObjectREFR> refr;
			RE::NiAVObject* root;
			std::size_t signature;
		};
		std::vector<Pending> pending;

		std::unordered_set<const void*> dirty;
		{
			std::scoped_lock lock(g_dirtyLock);
			dirty.swap(g_dirty);
		}

		std::vector<Lookup> lookups;
		{
			std::unique_lock lock(g_lock);
			for (auto it = g_states.begin(); it != g_states.end();) {
				auto& tracking = it->second;
				const auto refr = tracking.state->handle.get();
				if (!refr) {
					const auto next = std::next(it);
					Erase(it);
					it = next;
					continue;
				}
				if (dirty.contains(tracking.actor)) {
					tracking.bDirty = true;
				}
				auto* root = refr->Get3D(false);
				auto* actor = refr->As<RE::Actor>();
				auto* faceNode = actor ? actor->GetFaceNodeSkinned() : nullptr;
				lookups.push_back({ tracking.state, root, faceNode ? faceNode->GetRuntimeData().animationData.get() : nullptr, 0 });
				const auto signature = SignatureOf(root, faceNode);
				if (root != tracking.root || signature != tracking.signature) {
					tracking.bDirty = true;
					tracking.root = root;
					tracking.signature = signature;
				}
				if (tracking.bDirty && catalogue) {
					tracking.bDirty = false;
					pending.push_back({ tracking.state, refr, root, signature });
				}
				++it;
			}
		}

		// Tracking begun and ended, told outside the registry's lock
		std::vector<Change> changes;
		{
			std::unique_lock lock(g_lock);
			changes.swap(g_changes);
		}
		for (const auto& change : changes) {
			if (!change.bTracked) {
				Resources::OnUntracked(change.formID);
			} else if (const auto refr = change.handle.get()) {
				Resources::OnTracked(refr.get());
			}
		}

		// The retired keep their last root and face for a few frames, then go
		{
			std::unique_lock lock(g_lookupLock);
			for (auto& lookup : g_lookups) {
				if (lookup.state->bRetired.load() && ++lookup.framesRetired <= kRetiredFrames) {
					lookups.push_back(std::move(lookup));
				}
			}
			g_lookups = std::move(lookups);
		}

		// Read outside the registry's lock, so nothing waits on a walk
		for (auto& item : pending) {
			auto parts = Read(item.refr.get(), item.root, *catalogue);
			parts->generation = ++g_generation;
			item.state->parts.With([&](auto& a_parts) { a_parts = std::move(parts); });
		}
	}

	void Sample()
	{
		const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue();
		std::vector<std::shared_ptr<State>> states;
		{
			std::shared_lock lock(g_lock);
			for (const auto& [key, tracking] : g_states) {
				states.push_back(tracking.state);
			}
		}

		for (const auto& state : states) {
			auto sampled = std::make_shared<Sampled>();
			sampled->generation = ++g_sampleGeneration;

			// The modifiers first, then the Actor window's holds over them: what an author drags wins while held
			{
				const auto refr = state->handle.get();
				Resolve::Fold(*state, refr.get(), Resolve::Now(), *sampled);
				// SMP's body: the modifiers alone, taken once none is mid-transition, else what it was
				if (Settings::bPatchSmpBodyCollision && Settings::bSmpInstalled) {
					if (Resolve::Settled(*state, Resolve::Now())) {
						auto body = sampled->numbers[static_cast<std::size_t>(Apply::Kind::kBodyMorph)];
						AddPreset(body, sampled->presetSliders, Resolve::BodyWeight(*sampled, refr.get()));
						std::ranges::sort(body, {}, &Apply::NumberSample::index);
						sampled->smpBody = std::move(body);
					} else {
						const auto previous = state->sampled.With([](const auto& a_sampled) { return a_sampled; });
						sampled->smpBody = previous && previous->smpBody ? previous->smpBody : std::vector<Apply::NumberSample>{};
					}
				}
				ClipPlayer::Fold(*state, refr.get(), Resolve::Now(), *sampled);
			}
			state->manual.With([&](const Manual& a_manual) {
				const auto put = [](auto& a_list, auto a_sample) {
					if (auto it = std::ranges::find(a_list, a_sample.index, [](const auto& a_entry) { return a_entry.index; }); it != a_list.end()) {
						*it = a_sample;
					} else {
						a_list.push_back(a_sample);
					}
				};
				for (const auto& [raw, value] : a_manual.values) {
					Apply::ChannelId id;
					id.raw = raw;
					put(sampled->numbers[static_cast<std::size_t>(id.GetKind())], Apply::NumberSample{ id.GetIndex(), value });
				}
				for (const auto& [raw, colour] : a_manual.colours) {
					Apply::ChannelId id;
					id.raw = raw;
					put(sampled->colours[static_cast<std::size_t>(id.GetKind())], Apply::ColourSample{ id.GetIndex(), colour[0], colour[1], colour[2] });
				}
				for (const auto& [raw, value] : a_manual.references) {
					Apply::ChannelId id;
					id.raw = raw;
					put(sampled->references[static_cast<std::size_t>(id.GetKind())], Apply::ReferenceSample{ id.GetIndex(), value });
				}
				for (const auto& [raw, text] : a_manual.texts) {
					Apply::ChannelId id;
					id.raw = raw;
					put(sampled->texts[static_cast<std::size_t>(id.GetKind())], Apply::TextSample{ id.GetIndex(), text });
				}
			});
			// The preview over everything: the one item previewed on the reference actor
			{
				const auto refr = state->handle.get();
				Preview::Apply(*state, refr.get(), Resolve::Now(), *sampled);
			}
			// A pose is what it drives: its bones and look modifiers, added onto the sample
			{
				const auto refr = state->handle.get();
				Pose::Expand(*state, refr ? refr->As<RE::Actor>() : nullptr, Resolve::Now(), *sampled);
			}
			// A body preset is the shape under the sliders: its weights first, what the sliders hold added on top. The
			// Actor window's pick, else the preset channel's by name
			const Scan::BodyPreset* preset = nullptr;
			if (catalogue) {
				const auto held = state->manual.With([](const Manual& a_manual) { return a_manual.bodyPreset; });
				if (held && *held < catalogue->bodyPresets.size()) {
					preset = &catalogue->bodyPresets[*held];
				}
			}
			// The preset's sliders as the modifiers and the clips folded them, the Actor window's pick in their place
			const auto refr = state->handle.get();
			const auto mix = preset ? Resolve::PresetSliders(preset->name) : sampled->presetSliders;
			const float bodyWeight = Resolve::BodyWeight(*sampled, refr.get());
			AddPreset(sampled->numbers[static_cast<std::size_t>(Apply::Kind::kBodyMorph)], mix, bodyWeight);
			for (auto& numbers : sampled->numbers) {
				std::ranges::sort(numbers, {}, &Apply::NumberSample::index);
			}
			for (auto& colours : sampled->colours) {
				std::ranges::sort(colours, {}, &Apply::ColourSample::index);
			}
			for (auto& references : sampled->references) {
				std::ranges::sort(references, {}, &Apply::ReferenceSample::index);
			}
			state->sampled.With([&](auto& a_sampled) { a_sampled = std::move(sampled); });
		}
	}

	void ForEach(const std::function<void(State&)>& a_function)
	{
		std::vector<std::shared_ptr<State>> states;
		{
			std::shared_lock lock(g_lock);
			for (const auto& [key, tracking] : g_states) {
				states.push_back(tracking.state);
			}
		}
		for (const auto& state : states) {
			a_function(*state);
		}
	}

	std::vector<std::shared_ptr<State>> TakeRetired()
	{
		std::unique_lock lock(g_lock);
		return std::move(g_retired);
	}

	void Track(const std::vector<RE::ActorHandle>& a_handles)
	{
		std::unique_lock lock(g_lock);
		for (auto& [key, tracking] : g_states) {
			tracking.bTracked = false;
		}
		for (const auto& handle : a_handles) {
			if (const auto actor = handle.get()) {
				Ensure(actor.get()).bTracked = true;
			}
		}
		for (auto it = g_states.begin(); it != g_states.end();) {
			const auto next = std::next(it);
			if (!it->second.bTracked && it->first != g_watched) {
				Erase(it);
			}
			it = next;
		}
	}

	void Watch(RE::ObjectRefHandle a_refr)
	{
		std::unique_lock lock(g_lock);
		if (a_refr.native_handle() == g_watched) {
			return;
		}
		if (const auto it = g_states.find(g_watched); it != g_states.end() && !it->second.bTracked) {
			Erase(it);
		}
		g_watched = a_refr.native_handle();
		if (const auto refr = a_refr.get()) {
			Ensure(refr.get());
		}
	}

	void MarkDirty(const RE::TESObjectREFR* a_refr)
	{
		if (!a_refr) {
			return;
		}
		std::scoped_lock lock(g_dirtyLock);
		g_dirty.insert(a_refr);
	}

	std::shared_ptr<State> FindByRoot(const RE::NiAVObject* a_root)
	{
		std::shared_lock lock(g_lookupLock);
		for (const auto& lookup : g_lookups) {
			if (lookup.root == a_root) {
				return lookup.state;
			}
		}
		return nullptr;
	}

	std::shared_ptr<State> FindByAnimationData(const void* a_animationData)
	{
		if (!a_animationData) {
			return nullptr;
		}
		std::shared_lock lock(g_lookupLock);
		for (const auto& lookup : g_lookups) {
			if (lookup.animationData == a_animationData) {
				return lookup.state;
			}
		}
		return nullptr;
	}

	std::shared_ptr<State> Get(RE::ObjectRefHandle a_refr)
	{
		std::shared_lock lock(g_lock);
		const auto it = g_states.find(a_refr.native_handle());
		return it != g_states.end() ? it->second.state : nullptr;
	}
}
