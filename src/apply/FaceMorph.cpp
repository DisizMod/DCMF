#include "FaceMorph.h"

#include "ActorState.h"
#include "Entries.h"
#include "scan/Scan.h"

namespace Apply
{
	namespace
	{
		constexpr std::size_t kUpdateDownwardPassVtableIndex = 0x2C;

		// A head is a few thousand vertices; past this the count was misread and a write would be a heap overwrite
		constexpr std::uint32_t kMaxPlausibleVertices = 200'000;

		using UpdateDownwardPass_t = void(RE::BSFaceGenNiNode*, RE::NiUpdateData&, std::uint32_t);
		UpdateDownwardPass_t* g_original = nullptr;

		void UpdateDownwardPassHook(RE::BSFaceGenNiNode* a_node, RE::NiUpdateData& a_data, std::uint32_t a_arg2)
		{
			// Vanilla runs first: lip sync, blink, look-at and dialogue emotion are its to compute
			g_original(a_node, a_data, a_arg2);
			if (a_node) {
				if (const auto state = ActorState::FindByAnimationData(a_node->GetRuntimeData().animationData.get())) {
					Run(Pass::kFaceNode, *state);
				}
			}
		}

		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			return out;
		}

		// One TRI with its deltas, read once per path and shared by every head wearing it
		std::shared_ptr<const Scan::Tri::FaceTriDeltas> LoadTri(const std::string& a_path)
		{
			static std::mutex lock;
			static std::unordered_map<std::string, std::shared_ptr<const Scan::Tri::FaceTriDeltas>> loaded;

			std::scoped_lock guard(lock);
			if (const auto it = loaded.find(a_path); it != loaded.end()) {
				return it->second;
			}

			auto tri = std::make_shared<Scan::Tri::FaceTriDeltas>();
			const auto bytes = Scan::Files::Read(a_path);
			std::string error;
			if (bytes.empty()) {
				error = "not found";
			} else if (!Scan::Tri::ReadFaceTriDeltas(bytes, *tri, error)) {
				*tri = {};
			}
			logger::info("facemorph: read {}: {} vert(s), {} morph(s){}{}", a_path, tri->vertexCount, tri->morphs.size(), error.empty() ? "" : " -- ", error);

			loaded.emplace(a_path, tri);
			return tri;
		}

		RE::BSFaceGenBaseMorphExtraData* BaseMorphOf(RE::NiAVObject* a_object)
		{
			for (std::uint16_t i = 0; i < a_object->GetExtraDataSize(); ++i) {
				if (auto* base = netimmerse_cast<RE::BSFaceGenBaseMorphExtraData*>(a_object->GetExtraDataAt(i))) {
					return base;
				}
			}
			return nullptr;
		}

		const ActorState::Part* FindPart(const ActorState::Part& a_part, std::string_view a_name)
		{
			if (a_part.bGeometry && a_part.name == a_name) {
				return &a_part;
			}
			for (const auto& child : a_part.children) {
				if (const auto* found = FindPart(child, a_name)) {
					return found;
				}
			}
			return nullptr;
		}

		// Every face shape with base morph data whose TRIs the parts tree knows
		void Bind(FaceMorphData& a_data, RE::BSFaceGenNiNode* a_face, const ActorState::Parts& a_parts)
		{
			// A base morph block already bound keeps the original read the first time: what it holds now is our last write
			std::vector<FaceMorphShape> previous;
			previous.swap(a_data.shapes);
			auto& entries = Entries::GetSingleton();

			RE::BSVisit::TraverseScenegraphGeometries(a_face, [&](RE::BSGeometry* a_geometry) {
				auto* base = BaseMorphOf(a_geometry);
				if (!base || !base->vertexData) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}

				// vertexData is allocated against modelVertexCount, which the other counts need not agree with;
				// the smallest non-zero one stays inside every reading of the buffer
				std::uint32_t writable = base->modelVertexCount;
				const std::uint32_t shapeCount = a_geometry->AsTriShape() ? a_geometry->AsTriShape()->GetTrishapeRuntimeData().vertexCount : 0;
				for (const auto count : { base->vertexCount, shapeCount }) {
					if (count != 0) {
						writable = writable == 0 ? count : (std::min)(writable, count);
					}
				}
				if (writable == 0 || writable > kMaxPlausibleVertices) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}

				const ActorState::Part* part = nullptr;
				for (const auto& root : a_parts.roots) {
					if ((part = FindPart(root, a_geometry->name.c_str()))) {
						break;
					}
				}
				if (!part) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}

				FaceMorphShape shape;
				shape.geometry = RE::NiPointer(a_geometry);
				shape.base = RE::NiPointer(base);
				shape.writable = writable;
				if (const auto known = std::ranges::find_if(previous, [&](const FaceMorphShape& a_shape) { return a_shape.base.get() == base && a_shape.writable == writable; }); known != previous.end()) {
					shape.original = std::move(known->original);
				} else {
					shape.original.assign(base->vertexData, base->vertexData + writable);
				}

				// Every TRI of the shape's head part, and each of its morphs the table names; the first TRI to name a morph wins
				for (const auto& tri : part->tris) {
					auto loaded = LoadTri(tri.path);
					if (loaded->vertexCount == 0) {
						continue;
					}
					// A TRI for a different mesh moves the wrong vertices
					if (loaded->vertexCount != writable) {
						continue;
					}
					for (std::uint32_t i = 0; i < loaded->morphs.size(); ++i) {
						const auto id = entries.Find(Kind::kFaceMorph, loaded->morphs[i].name);
						if (!id.IsValid()) {
							continue;
						}
						if (std::ranges::any_of(shape.morphs, [&](const auto& a_entry) { return a_entry.index == id.GetIndex(); })) {
							continue;
						}
						shape.morphs.push_back({ id.GetIndex(), loaded, i });
					}
				}
				if (shape.morphs.empty()) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				std::ranges::sort(shape.morphs, {}, &FaceMorphShape::Morph::index);

				logger::info("facemorph: bound '{}': {} vert(s), {} morph(s)", a_geometry->name.c_str(), writable, shape.morphs.size());
				a_data.shapes.push_back(std::move(shape));
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		// The original plus every held morph's share, into the base morph data
		void WriteShape(FaceMorphShape& a_shape, const std::vector<NumberSample>& a_weights)
		{
			auto* data = a_shape.base->vertexData;
			std::memcpy(data, a_shape.original.data(), a_shape.writable * sizeof(RE::NiPoint3));

			auto weight = a_weights.begin();
			for (const auto& morph : a_shape.morphs) {
				while (weight != a_weights.end() && weight->index < morph.index) {
					++weight;
				}
				if (weight == a_weights.end()) {
					break;
				}
				if (weight->index != morph.index || weight->value == 0.f) {
					continue;
				}
				const auto& deltas = morph.tri->morphs[morph.morph].deltas;
				const auto count = (std::min)(static_cast<std::size_t>(a_shape.writable), deltas.size());
				for (std::size_t v = 0; v < count; ++v) {
					data[v].x += deltas[v].x * weight->value;
					data[v].y += deltas[v].y * weight->value;
					data[v].z += deltas[v].z * weight->value;
				}
			}
		}

		bool StillAttached(const FaceMorphShape& a_shape, const RE::NiAVObject* a_face)
		{
			for (const RE::NiAVObject* at = a_shape.geometry.get(); at; at = at->parent) {
				if (at == a_face) {
					return true;
				}
			}
			return false;
		}
	}

	bool FaceMorph::Install()
	{
		REL::Relocation<std::uintptr_t> vtable{ RE::BSFaceGenNiNode::VTABLE[0] };
		const auto slot = vtable.address() + sizeof(void*) * kUpdateDownwardPassVtableIndex;
		if (*reinterpret_cast<std::uintptr_t*>(slot) == 0) {
			logger::error("facemorph: BSFaceGenNiNode vtable slot 0x{:X} is empty; nothing patched", kUpdateDownwardPassVtableIndex);
			return false;
		}
		g_original = reinterpret_cast<UpdateDownwardPass_t*>(vtable.write_vfunc(kUpdateDownwardPassVtableIndex, UpdateDownwardPassHook));
		logger::info("facemorph: hooked BSFaceGenNiNode::UpdateDownwardPass at vtable slot 0x{:X}", kUpdateDownwardPassVtableIndex);
		return true;
	}

	void FaceMorph::Apply(Pass, ActorState::State& a_state)
	{
		const auto refr = a_state.handle.get();
		auto* face = refr ? refr->GetFaceNodeSkinned() : nullptr;
		const auto parts = a_state.parts.With([](const auto& a_parts) { return a_parts; });
		const auto sampled = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		const bool bRetired = a_state.bRetired.load();

		a_state.faceMorph.With([&](FaceMorphData& a_data) {
			const auto& weights = sampled ? sampled->numbers[static_cast<std::size_t>(Kind::kFaceMorph)] : std::vector<NumberSample>{};
			const bool bLetGo = bRetired || !face || !parts || !parts->bLoaded || weights.empty();

			const auto reapply = [&] {
				// The engine only puts the base on the mesh when it thinks the mfg sets changed
				if (auto animation = face ? face->GetRuntimeData().animationData : nullptr) {
					animation->unk217 = true;
				}
			};

			if (bLetGo) {
				if (a_data.bWritten) {
					bool bAny = false;
					for (auto& shape : a_data.shapes) {
						if (StillAttached(shape, face)) {
							WriteShape(shape, {});
							bAny = true;
						}
					}
					if (bAny) {
						reapply();
					}
				}
				a_data = {};
				return;
			}

			const bool bRebind = a_data.boundGeneration != parts->generation;
			if (bRebind) {
				Bind(a_data, face, *parts);
				a_data.boundGeneration = parts->generation;
				a_data.applied.clear();
			}

			if (!bRebind && a_data.applied.size() == weights.size() &&
				std::equal(a_data.applied.begin(), a_data.applied.end(), weights.begin(), [](const NumberSample& a, const NumberSample& b) { return a.index == b.index && a.value == b.value; })) {
				return;
			}

			for (auto& shape : a_data.shapes) {
				WriteShape(shape, weights);
			}
			if (!a_data.shapes.empty()) {
				reapply();
			}
			a_data.applied = weights;
			a_data.bWritten = true;
		});
	}
}
