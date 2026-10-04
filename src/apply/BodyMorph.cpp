#include "BodyMorph.h"

#include "ActorState.h"
#include "Entries.h"
#include "scan/Scan.h"
#include "Timing.h"

#include <bit>

namespace Apply
{
	namespace
	{
		std::string Lower(std::string_view a_text)
		{
			std::string out(a_text);
			std::ranges::transform(out, out.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			return out;
		}

		bool SameName(std::string_view a_left, std::string_view a_right)
		{
			return a_left.size() == a_right.size() && std::ranges::equal(a_left, a_right, [](unsigned char l, unsigned char r) { return std::tolower(l) == std::tolower(r); });
		}

		// One TRI, read once per path and shared by every actor wearing it
		std::shared_ptr<const std::vector<Scan::Tri::BodyTriShapeDeltas>> LoadTri(const std::string& a_path)
		{
			static std::mutex lock;
			static std::unordered_map<std::string, std::shared_ptr<const std::vector<Scan::Tri::BodyTriShapeDeltas>>> loaded;

			std::scoped_lock guard(lock);
			if (const auto it = loaded.find(a_path); it != loaded.end()) {
				return it->second;
			}

			auto shapes = std::make_shared<std::vector<Scan::Tri::BodyTriShapeDeltas>>();
			const auto bytes = Scan::Files::Read(a_path, false);
			std::string error;
			if (bytes.empty()) {
				error = "not found";
			} else if (!Scan::Tri::ReadBodyTriDeltas(bytes, *shapes, error)) {
				shapes->clear();
			}
			std::size_t deltas = 0;
			for (const auto& shape : *shapes) {
				for (const auto& morph : shape.morphs) {
					deltas += morph.deltas.size();
				}
			}
			logger::info("bodymorph: read {}: {} shape(s), {} delta(s){}{}", a_path, shapes->size(), deltas, error.empty() ? "" : " -- ", error);

			loaded.emplace(a_path, shapes);
			return shapes;
		}

		// The stamp on the object or the nearest node above it
		std::string BodyTriPathOf(const RE::NiAVObject* a_object)
		{
			for (auto* at = a_object; at; at = at->parent) {
				if (const auto* stamp = at->GetExtraData<RE::NiStringExtraData>("BODYTRI"); stamp && stamp->value && *stamp->value) {
					return "meshes/" + Scan::Files::Normalize(stamp->value);
				}
			}
			return {};
		}

		// The TRI of each head part the actor wears, by its editor ID, which names its shape in the face: a part built by
		// BodySlide, as an SMP hair's collision body, has its TRI beside the mesh, named without the weight's _0 or _1
		std::unordered_map<std::string, std::string> HeadPartTris(RE::FormID a_actor)
		{
			std::unordered_map<std::string, std::string> out;
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actor);
			auto* base = actor ? actor->GetActorBase() : nullptr;
			if (!base || !base->headParts) {
				return out;
			}
			const std::function<void(RE::BGSHeadPart*)> add = [&](RE::BGSHeadPart* a_part) {
				if (!a_part) {
					return;
				}
				if (const char* model = a_part->GetModel(); model && *model && !a_part->formEditorID.empty()) {
					auto path = "meshes/" + Scan::Files::Normalize(model);
					if (path.size() > 4 && Lower(path.substr(path.size() - 4)) == ".nif") {
						path.resize(path.size() - 4);
						if (path.size() > 2 && path[path.size() - 2] == '_' && (path.back() == '0' || path.back() == '1')) {
							path.resize(path.size() - 2);
						}
						out.emplace(Lower(a_part->formEditorID.c_str()), path + ".tri");
					}
				}
				for (auto* extra : a_part->extraParts) {
					add(extra);
				}
			};
			for (std::int8_t i = 0; i < base->numHeadParts; ++i) {
				add(base->headParts[i]);
			}
			return out;
		}

		// Positions sit first in the vertex, as halves unless the next attribute starts past 8 bytes
		bool PositionsAreHalves(RE::BSGraphics::VertexDesc& a_desc)
		{
			using A = RE::BSGraphics::Vertex::Attribute;
			std::uint32_t next = a_desc.GetSize();
			for (const auto attribute : { A::VA_TEXCOORD0, A::VA_TEXCOORD1, A::VA_NORMAL, A::VA_BINORMAL, A::VA_COLOR, A::VA_SKINNING, A::VA_EYEDATA }) {
				const auto offset = a_desc.GetAttributeOffset(attribute);
				if (offset != 0 && offset < next) {
					next = offset;
				}
			}
			return next <= 8;
		}

		std::uint16_t FloatToHalf(float a_value)
		{
			const auto bits = std::bit_cast<std::uint32_t>(a_value);
			const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
			const auto exponent = static_cast<int>((bits >> 23) & 0xFFu) - 127 + 15;
			auto mantissa = bits & 0x7FFFFFu;

			if (((bits >> 23) & 0xFFu) == 0xFFu) {
				return static_cast<std::uint16_t>(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
			}
			if (exponent >= 0x1F) {
				return static_cast<std::uint16_t>(sign | 0x7BFFu);
			}
			if (exponent <= 0) {
				if (exponent < -10) {
					return sign;
				}
				mantissa |= 0x800000u;
				const auto shift = static_cast<std::uint32_t>(14 - exponent);
				auto half = mantissa >> shift;
				const auto remainder = mantissa & ((1u << shift) - 1u);
				const auto halfway = 1u << (shift - 1);
				if (remainder > halfway || (remainder == halfway && (half & 1u))) {
					++half;
				}
				return static_cast<std::uint16_t>(sign | half);
			}
			auto half = static_cast<std::uint32_t>(exponent) << 10 | (mantissa >> 13);
			const auto remainder = mantissa & 0x1FFFu;
			if (remainder > 0x1000u || (remainder == 0x1000u && (half & 1u))) {
				++half;
			}
			return static_cast<std::uint16_t>(sign | half);
		}

		float HalfToFloat(std::uint16_t a_half)
		{
			const auto sign = static_cast<std::uint32_t>(a_half & 0x8000u) << 16;
			const auto exponent = (a_half >> 10) & 0x1Fu;
			const auto mantissa = static_cast<std::uint32_t>(a_half & 0x3FFu);

			if (exponent == 0) {
				if (mantissa == 0) {
					return std::bit_cast<float>(sign);
				}
				auto e = -1;
				auto m = mantissa;
				do {
					++e;
					m <<= 1;
				} while ((m & 0x400u) == 0);
				return std::bit_cast<float>(sign | ((127 - 15 - e) << 23) | ((m & 0x3FFu) << 13));
			}
			if (exponent == 0x1F) {
				return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13));
			}
			return std::bit_cast<float>(sign | ((exponent + 127 - 15) << 23) | (mantissa << 13));
		}

		void WritePosition(std::uint8_t* a_vertex, bool a_bHalves, float x, float y, float z)
		{
			if (a_bHalves) {
				const std::uint16_t packed[3]{ FloatToHalf(x), FloatToHalf(y), FloatToHalf(z) };
				std::memcpy(a_vertex, packed, sizeof(packed));
			} else {
				const float unpacked[3]{ x, y, z };
				std::memcpy(a_vertex, unpacked, sizeof(unpacked));
			}
		}

		void ReadPosition(const std::uint8_t* a_vertex, bool a_bHalves, float& x, float& y, float& z)
		{
			if (a_bHalves) {
				std::uint16_t packed[3];
				std::memcpy(packed, a_vertex, sizeof(packed));
				x = HalfToFloat(packed[0]);
				y = HalfToFloat(packed[1]);
				z = HalfToFloat(packed[2]);
			} else {
				float unpacked[3];
				std::memcpy(unpacked, a_vertex, sizeof(unpacked));
				x = unpacked[0];
				y = unpacked[1];
				z = unpacked[2];
			}
		}

		std::uint32_t ByteWidthOf(REX::W32::ID3D11Buffer* a_buffer)
		{
			if (!a_buffer) {
				return 0;
			}
			REX::W32::D3D11_BUFFER_DESC desc{};
			a_buffer->GetDesc(&desc);
			return desc.byteWidth;
		}

		// A working copy that changed since its last upload; filled here, drained by Upload on the render thread
		struct Pending
		{
			RE::NiPointer<RE::NiSkinPartition> partition;
			std::shared_ptr<std::vector<std::uint8_t>> bytes;
		};
		std::mutex g_pendingLock;
		std::vector<Pending> g_pending;

		// Every stamped, skinned geometry under the root whose name a TRI knows
		void Bind(BodyMorphData& a_data, RE::NiAVObject* a_root, RE::FormID a_actor)
		{
			a_data.shapes.clear();

			// Every TRI stamped anywhere under the root: BodySlide stamps the body, but its file names the clothes too
			std::vector<std::string> paths;
			RE::BSVisit::TraverseScenegraphObjects(a_root, [&](RE::NiAVObject* a_object) {
				if (const auto* stamp = a_object->GetExtraData<RE::NiStringExtraData>("BODYTRI"); stamp && stamp->value && *stamp->value) {
					const auto path = BodyTriPathOf(a_object);
					if (std::ranges::find(paths, path) == paths.end()) {
						paths.push_back(path);
					}
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (paths.empty() && HeadPartTris(a_actor).empty()) {
				return;
			}

			auto& entries = Entries::GetSingleton();
			const auto headPartTris = HeadPartTris(a_actor);

			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
				std::vector<std::string> candidates;
				if (auto own = BodyTriPathOf(a_geometry); !own.empty()) {
					candidates.push_back(std::move(own));
				}
				// A head part in the face goes by its editor ID, its TRI beside its mesh; one the face morphs is the face's
				std::string headPartTri;
				if (const auto it = headPartTris.find(Lower(a_geometry->name.c_str())); it != headPartTris.end() && !a_geometry->AsDynamicTriShape()) {
					headPartTri = it->second;
					candidates.push_back(headPartTri);
				}
				for (const auto& other : paths) {
					if (std::ranges::find(candidates, other) == candidates.end()) {
						candidates.push_back(other);
					}
				}

				auto& runtime = a_geometry->GetGeometryRuntimeData();
				if (!runtime.skinInstance || !runtime.skinInstance->skinPartition) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				auto& partition = runtime.skinInstance->skinPartition;
				if (partition->numPartitions == 0 || !partition->partitions[0].buffData || !partition->partitions[0].buffData->rawVertexData) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}

				std::shared_ptr<const std::vector<Scan::Tri::BodyTriShapeDeltas>> tri;
				std::size_t shapeIndex = 0;
				bool bFound = false;
				for (const auto& candidate : candidates) {
					auto loaded = LoadTri(candidate);
					for (std::size_t i = 0; i < loaded->size() && !bFound; ++i) {
						if (SameName((*loaded)[i].name, a_geometry->name.c_str())) {
							tri = loaded;
							shapeIndex = i;
							bFound = true;
						}
					}
					if (bFound) {
						break;
					}
				}
				if (!bFound) {
					// The engine renames a naked body's skin shapes after attaching them, and a head part's after its
					// record, so a file with one shape can only mean that one; the vertex count says whether it fits
					const auto own = headPartTri.empty() ? BodyTriPathOf(a_geometry) : headPartTri;
					if (!own.empty()) {
						auto loaded = LoadTri(own);
						if (loaded->size() == 1) {
							std::uint32_t highest = 0;
							for (const auto& morph : loaded->front().morphs) {
								for (const auto& delta : morph.deltas) {
									highest = (std::max)(highest, delta.index);
								}
							}
							if (highest < partition->vertexCount) {
								tri = loaded;
								shapeIndex = 0;
								bFound = true;
							}
						}
					}
				}
				if (!bFound) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}

				BodyMorphShape shape;
				shape.geometry = RE::NiPointer(a_geometry);
				shape.partition = partition;
				shape.tri = tri;
				shape.shape = shapeIndex;
				shape.vertexCount = partition->vertexCount;
				auto& first = partition->partitions[0];
				shape.stride = first.buffData->vertexDesc.GetSize();
				shape.bHalves = PositionsAreHalves(first.buffData->vertexDesc);

				const auto bytes = static_cast<std::size_t>(shape.vertexCount) * shape.stride;
				shape.base.assign(first.buffData->rawVertexData, first.buffData->rawVertexData + bytes);
				shape.work[0] = std::make_shared<std::vector<std::uint8_t>>(shape.base);
				shape.work[1] = std::make_shared<std::vector<std::uint8_t>>(shape.base);
				shape.moved[0].assign(shape.vertexCount, 0);
				shape.moved[1].assign(shape.vertexCount, 0);

				const auto& morphs = (*tri)[shapeIndex].morphs;
				for (std::uint32_t i = 0; i < morphs.size(); ++i) {
					if (const auto id = entries.Find(Kind::kBodyMorph, morphs[i].name); id.IsValid()) {
						shape.morphs.emplace_back(id.GetIndex(), i);
					}
				}
				std::ranges::sort(shape.morphs);

				a_data.shapes.push_back(std::move(shape));
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		// Sums the held weights into the next working copy and queues it; an empty set puts the base back
		void WriteShape(BodyMorphShape& a_shape, const std::vector<NumberSample>& a_weights)
		{
			const auto slot = a_shape.next;
			auto& work = *a_shape.work[slot];
			auto& moved = a_shape.moved[slot];
			a_shape.next ^= 1;

			// Every vertex moved in this copy goes back to base first, then each morph adds its share
			for (std::uint32_t v = 0; v < a_shape.vertexCount; ++v) {
				if (moved[v]) {
					std::memcpy(work.data() + v * a_shape.stride, a_shape.base.data() + v * a_shape.stride, a_shape.bHalves ? 6 : 12);
					moved[v] = 0;
				}
			}

			const auto& morphs = (*a_shape.tri)[a_shape.shape].morphs;
			auto weight = a_weights.begin();
			for (const auto& [index, morph] : a_shape.morphs) {
				while (weight != a_weights.end() && weight->index < index) {
					++weight;
				}
				if (weight == a_weights.end()) {
					break;
				}
				if (weight->index != index || weight->value == 0.f) {
					continue;
				}
				for (const auto& delta : morphs[morph].deltas) {
					if (delta.index >= a_shape.vertexCount) {
						continue;
					}
					auto* at = work.data() + delta.index * a_shape.stride;
					float x, y, z;
					ReadPosition(at, a_shape.bHalves, x, y, z);
					moved[delta.index] = 1;
					WritePosition(at, a_shape.bHalves, x + delta.x * weight->value, y + delta.y * weight->value, z + delta.z * weight->value);
				}
			}

			std::scoped_lock lock(g_pendingLock);
			g_pending.push_back({ a_shape.partition, a_shape.work[slot] });
		}

		// The base plus the weighted morphs into the CPU copy every partition keeps, which SMP builds its collision from
		void WriteRaw(BodyMorphShape& a_shape, const std::vector<NumberSample>& a_weights)
		{
			const auto bytes = a_shape.base.size();
			std::vector<std::uint8_t*> targets;
			for (std::uint32_t i = 0; i < a_shape.partition->numPartitions; ++i) {
				auto* data = a_shape.partition->partitions[i].buffData;
				if (data && data->rawVertexData && std::ranges::find(targets, data->rawVertexData) == targets.end()) {
					targets.push_back(data->rawVertexData);
				}
			}
			for (auto* target : targets) {
				std::memcpy(target, a_shape.base.data(), bytes);
			}

			const auto& morphs = (*a_shape.tri)[a_shape.shape].morphs;
			auto weight = a_weights.begin();
			for (const auto& [index, morph] : a_shape.morphs) {
				while (weight != a_weights.end() && weight->index < index) {
					++weight;
				}
				if (weight == a_weights.end()) {
					break;
				}
				if (weight->index != index || weight->value == 0.f) {
					continue;
				}
				for (const auto& delta : morphs[morph].deltas) {
					if (delta.index >= a_shape.vertexCount) {
						continue;
					}
					for (auto* target : targets) {
						auto* at = target + delta.index * a_shape.stride;
						float x, y, z;
						ReadPosition(at, a_shape.bHalves, x, y, z);
						WritePosition(at, a_shape.bHalves, x + delta.x * weight->value, y + delta.y * weight->value, z + delta.z * weight->value);
					}
				}
			}
		}

		// FSMP rebuilds the actor's physics from the meshes, keeping the bones where they are
		void ResetSmp(RE::Actor* a_actor)
		{
			auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm || !a_actor) {
				return;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback;
			vm->DispatchStaticCall("DynamicHDT", "ResetPhysics", RE::MakeFunctionArguments(std::move(a_actor), false), callback);
		}

		bool SameWeights(const std::vector<NumberSample>& a_left, const std::vector<NumberSample>& a_right)
		{
			return a_left.size() == a_right.size() &&
			       std::equal(a_left.begin(), a_left.end(), a_right.begin(), [](const NumberSample& a, const NumberSample& b) { return a.index == b.index && a.value == b.value; });
		}

		// Whether a shape still hangs under the root it was found under
		bool StillAttached(const BodyMorphShape& a_shape, const RE::NiAVObject* a_root)
		{
			for (const RE::NiAVObject* at = a_shape.geometry.get(); at; at = at->parent) {
				if (at == a_root) {
					return true;
				}
			}
			return false;
		}
	}

	void BodyMorph::Apply(Pass, ActorState::State& a_state)
	{
		const auto refr = a_state.handle.get();
		auto* root = refr ? refr->Get3D(false) : nullptr;
		const auto parts = a_state.parts.With([](const auto& a_parts) { return a_parts; });
		const auto sampled = a_state.sampled.With([](const auto& a_sampled) { return a_sampled; });
		const bool bRetired = a_state.bRetired.load();

		a_state.bodyMorph.With([&](BodyMorphData& a_data) {
			const auto& weights = sampled ? sampled->numbers[static_cast<std::size_t>(Kind::kBodyMorph)] : std::vector<NumberSample>{};
			const bool bLetGo = bRetired || !root || !parts || !parts->bLoaded || weights.empty();

			auto* actor = refr ? refr->As<RE::Actor>() : nullptr;

			if (bLetGo) {
				// The base goes back onto whatever is still drawn; a shape that left with its armor needs nothing
				if (a_data.bWritten) {
					for (auto& shape : a_data.shapes) {
						if (StillAttached(shape, root)) {
							WriteShape(shape, {});
						}
					}
				}
				// SMP's copy back to base, and SMP told while the actor is still there to read it
				if (!a_data.smpApplied.empty()) {
					for (auto& shape : a_data.shapes) {
						WriteRaw(shape, {});
					}
					if (!bRetired && root) {
						ResetSmp(actor);
					}
				}
				a_data = {};
				return;
			}

			const bool bRebind = a_data.boundGeneration != parts->generation;
			if (bRebind) {
				// SMP's copy back to base first, so a shape that stays is bound from its base and not from our morphs
				if (!a_data.smpApplied.empty()) {
					for (auto& shape : a_data.shapes) {
						WriteRaw(shape, {});
					}
					a_data.smpApplied.clear();
				}
				// What was written to a shape that is gone stays on nothing; a shape that stays is summed again from its base
				Bind(a_data, root, refr->GetFormID());
				a_data.boundGeneration = parts->generation;
				a_data.applied.clear();
			}

			// SMP's copy: the modifiers' body when the patch is on, the base when it is off
			static const std::vector<NumberSample> none;
			const auto& smpBody = sampled && sampled->smpBody ? *sampled->smpBody : none;
			if (!SameWeights(a_data.smpApplied, smpBody)) {
				for (auto& shape : a_data.shapes) {
					WriteRaw(shape, smpBody);
				}
				a_data.smpApplied = smpBody;
				ResetSmp(actor);
			}

			if (!bRebind && SameWeights(a_data.applied, weights)) {
				return;
			}

			float summed = 0.f;
			{
				Timing::Scope scope(summed);
				for (auto& shape : a_data.shapes) {
					WriteShape(shape, weights);
				}
			}
			Timing::RecordPerFrame([&](Timing::PerFrame& a_frame) {
				a_frame.bodyMorphSum.Add(summed);
				++a_frame.bodyMorphWrites;
			});
			a_data.applied = weights;
			a_data.bWritten = true;
		});
	}

	void BodyMorph::Upload()
	{
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		auto* context = renderer ? renderer->GetRuntimeData().context : nullptr;
		if (!context) {
			if (static bool bSaid = false; !bSaid) {
				bSaid = true;
				logger::error("bodymorph: no D3D context to upload with; the writes wait in their queue");
			}
			return;
		}

		std::vector<Pending> pending;
		{
			std::scoped_lock lock(g_pendingLock);
			if (g_pending.empty()) {
				return;
			}
			pending.swap(g_pending);
		}

		float uploaded = 0.f;
		Timing::Scope scope(uploaded);
		struct Record
		{
			float& milliseconds;
			~Record() { Timing::RecordPerFrame([&](Timing::PerFrame& a_frame) { a_frame.upload.Add(milliseconds); }); }
		} record{ uploaded };

		for (const auto& item : pending) {
			for (std::uint32_t i = 0; i < item.partition->numPartitions; ++i) {
				auto* data = item.partition->partitions[i].buffData;
				if (!data || !data->vertexBuffer) {
					continue;
				}
				// Never more than the buffer holds
				const auto bytes = (std::min)(ByteWidthOf(data->vertexBuffer), static_cast<std::uint32_t>(item.bytes->size()));
				if (bytes == 0) {
					continue;
				}
				REX::W32::D3D11_BOX box{ 0, 0, 0, bytes, 1, 1 };
				context->UpdateSubresource(data->vertexBuffer, 0, &box, item.bytes->data(), 0, 0);
			}
		}
	}
}
