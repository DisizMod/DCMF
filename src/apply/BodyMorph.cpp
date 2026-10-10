#include "BodyMorph.h"

#include "API/DCMF-VertexOffsetAPI.h"
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
			RE::NiPointer<RE::BSGeometry> geometry;  // whose offsets go on top; none for a write that takes no offsets
		};
		std::mutex g_pendingLock;
		std::vector<Pending> g_pending;

		// ---- vertex offsets: other plugins' per-vertex moves, added on top of the morphs at upload ----

		struct OffsetSet
		{
			std::vector<std::uint32_t> vertices;
			std::vector<float> xyz;
		};

		// Rotations of the vertices' frames, a unit quaternion each
		struct TurnSet
		{
			std::vector<std::uint32_t> vertices;
			std::vector<float> xyzw;
		};

		// The write side: what each owner set on a shape, swapped to the upload under a short lock
		struct ShapeOffsets
		{
			RE::FormID actor = 0;
			RE::NiPointer<RE::BSGeometry> geometry;
			RE::NiPointer<RE::NiSkinPartition> partition;
			std::uint32_t vertexCount = 0;
			std::uint32_t stride = 0;
			bool bHalves = true;
			std::uint32_t normalOffset = 0;   // bytes into a vertex; 0, none
			std::uint32_t tangentOffset = 0;  // bytes into a vertex; 0, none
			std::map<std::string, std::shared_ptr<const OffsetSet>, std::less<>> owners;
			std::map<std::string, std::shared_ptr<const TurnSet>, std::less<>> turners;
			std::shared_ptr<const std::vector<std::uint8_t>> seed;  // base and morphs as the shape had them at its first offsets
			bool bDirty = true;
		};
		std::mutex g_offsetLock;
		std::unordered_map<const RE::BSGeometry*, ShapeOffsets> g_offsets;
		std::vector<RE::NiPointer<RE::BSGeometry>> g_droppedShapes;  // gone: the upload forgets them without a write
		std::unordered_map<RE::FormID, std::uint32_t> g_morphGenerations;
		std::atomic<bool> g_bOffsetsChanged = false;

		// The read side, the upload's alone: what the GPU holds of a shape with offsets, and the base and morphs under them
		struct Offset
		{
			std::uint32_t vertex;
			float x, y, z;
			float qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
			bool bTurn = false;
		};
		struct Shown
		{
			RE::NiPointer<RE::BSGeometry> geometry;
			RE::NiPointer<RE::NiSkinPartition> partition;
			std::uint32_t vertexCount = 0;
			std::uint32_t stride = 0;
			bool bHalves = true;
			std::uint32_t normalOffset = 0;
			std::uint32_t tangentOffset = 0;
			std::vector<std::uint8_t> morphed;
			std::vector<std::uint8_t> shown;
			std::vector<Offset> offsets;            // every owner's summed, by vertex
			std::vector<std::uint32_t> lastOffset;  // the vertices the GPU has offsets on
			bool bFull = false;
			bool bOffsetsChanged = false;
		};
		std::unordered_map<const RE::BSGeometry*, Shown> g_shown;

		// The shapes of an actor that are not among those kept lose their offsets
		void ForgetOffsets(RE::FormID a_actor, const std::vector<BodyMorphShape>& a_kept)
		{
			std::scoped_lock lock(g_offsetLock);
			for (auto it = g_offsets.begin(); it != g_offsets.end();) {
				const bool bKept = std::ranges::any_of(a_kept, [&](const BodyMorphShape& a_shape) { return a_shape.geometry.get() == it->first; });
				if (it->second.actor == a_actor && !bKept) {
					g_droppedShapes.push_back(it->second.geometry);
					it = g_offsets.erase(it);
					g_bOffsetsChanged = true;
				} else {
					++it;
				}
			}
		}

		bool HasOffsets(RE::FormID a_actor)
		{
			std::scoped_lock lock(g_offsetLock);
			return std::ranges::any_of(g_offsets, [&](const auto& a_entry) { return a_entry.second.actor == a_actor; });
		}

		void NextMorphGeneration(RE::FormID a_actor)
		{
			std::scoped_lock lock(g_offsetLock);
			++g_morphGenerations[a_actor];
		}

		// A vertex's frame turned by a rotation, read from the base and morphs and written over them: the normal's bytes
		// xyz with the bitangent's y as its w, the tangent's xyz with the bitangent's z, the bitangent's x after the position.
		// A byte maps 0-255 onto -1..1
		void TurnFrame(std::uint8_t* a_out, const std::uint8_t* a_in, bool a_bHalves, std::uint32_t a_normal, std::uint32_t a_tangent, float qx, float qy, float qz, float qw)
		{
			const auto rotate = [&](float& x, float& y, float& z) {
				const float tx = 2.f * (qy * z - qz * y);
				const float ty = 2.f * (qz * x - qx * z);
				const float tz = 2.f * (qx * y - qy * x);
				const float rx = x + qw * tx + (qy * tz - qz * ty);
				const float ry = y + qw * ty + (qz * tx - qx * tz);
				const float rz = z + qw * tz + (qx * ty - qy * tx);
				x = rx;
				y = ry;
				z = rz;
			};
			const auto unpack = [](std::uint8_t a_byte) { return a_byte / 255.f * 2.f - 1.f; };
			const auto pack = [](float a_value) { return static_cast<std::uint8_t>(std::clamp(std::lround((a_value * .5f + .5f) * 255.f), 0l, 255l)); };
			const std::uint8_t* normalIn = a_in + a_normal;
			std::uint8_t* normalOut = a_out + a_normal;
			float nx = unpack(normalIn[0]), ny = unpack(normalIn[1]), nz = unpack(normalIn[2]);
			rotate(nx, ny, nz);
			normalOut[0] = pack(nx);
			normalOut[1] = pack(ny);
			normalOut[2] = pack(nz);
			if (!a_tangent) {
				return;
			}
			const std::uint8_t* tangentIn = a_in + a_tangent;
			std::uint8_t* tangentOut = a_out + a_tangent;
			float tx = unpack(tangentIn[0]), ty = unpack(tangentIn[1]), tz = unpack(tangentIn[2]);
			rotate(tx, ty, tz);
			tangentOut[0] = pack(tx);
			tangentOut[1] = pack(ty);
			tangentOut[2] = pack(tz);
			float bx = 0.f;
			if (a_bHalves) {
				std::uint16_t half;
				std::memcpy(&half, a_in + 6, sizeof(half));
				bx = HalfToFloat(half);
			} else {
				std::memcpy(&bx, a_in + 12, sizeof(bx));
			}
			float by = unpack(normalIn[3]), bz = unpack(tangentIn[3]);
			rotate(bx, by, bz);
			normalOut[3] = pack(by);
			tangentOut[3] = pack(bz);
			if (a_bHalves) {
				const auto half = FloatToHalf(bx);
				std::memcpy(a_out + 6, &half, sizeof(half));
			} else {
				std::memcpy(a_out + 12, &bx, sizeof(bx));
			}
		}

		// Where a shape's vertices keep their normal and tangent, from the partition it draws with; 0 for none
		std::pair<std::uint32_t, std::uint32_t> FrameOffsets(const BodyMorphShape& a_shape);

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

		// The partition the shape draws with: our copy once there is one
		RE::NiSkinPartition* Active(const BodyMorphShape& a_shape)
		{
			return a_shape.own ? a_shape.own.get() : a_shape.partition.get();
		}

		RE::NiSkinInstance* SkinOf(const BodyMorphShape& a_shape)
		{
			return a_shape.geometry ? a_shape.geometry->GetGeometryRuntimeData().skinInstance.get() : nullptr;
		}

		std::pair<std::uint32_t, std::uint32_t> FrameOffsets(const BodyMorphShape& a_shape)
		{
			auto* partition = Active(a_shape);
			if (!partition || partition->numPartitions == 0 || !partition->partitions[0].buffData) {
				return { 0, 0 };
			}
			auto desc = partition->partitions[0].buffData->vertexDesc;
			using A = RE::BSGraphics::Vertex::Attribute;
			using F = RE::BSGraphics::Vertex::Flags;
			if (!desc.HasFlag(F::VF_NORMAL)) {
				return { 0, 0 };
			}
			return { desc.GetAttributeOffset(A::VA_NORMAL), desc.HasFlag(F::VF_TANGENT) ? desc.GetAttributeOffset(A::VA_BINORMAL) : 0u };
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
			g_pending.push_back({ RE::NiPointer(Active(a_shape)), a_shape.work[slot], a_shape.geometry });
		}

		// The base plus the weighted morphs into the CPU copy SMP builds its collision from, in the shape's own copy of its
		// partition, as skee does: the partition the engine built is never written, so a rebuilt shape starts clean. An
		// empty set gives the shape its partition back
		void WriteRaw(BodyMorphShape& a_shape, const std::vector<NumberSample>& a_weights)
		{
			auto* skin = SkinOf(a_shape);
			if (a_weights.empty()) {
				if (a_shape.own) {
					if (skin && skin->skinPartition.get() == a_shape.own.get()) {
						skin->skinPartition = a_shape.partition;
						// What the GPU of the bound partition last had of ours goes back to base
						std::scoped_lock lock(g_pendingLock);
						g_pending.push_back({ a_shape.partition, std::make_shared<std::vector<std::uint8_t>>(a_shape.base) });
					}
					a_shape.own.reset();
				}
				return;
			}
			if (!a_shape.own) {
				// The first write since the bind: a copy, swapped in only while the shape still draws with what was bound
				if (!skin || skin->skinPartition.get() != a_shape.partition.get()) {
					return;
				}
				RE::NiPointer<RE::NiObject> copy;
				a_shape.partition->CreateDeepCopy(copy);
				auto* own = copy ? netimmerse_cast<RE::NiSkinPartition*>(copy.get()) : nullptr;
				if (!own) {
					logger::error("bodymorph: '{}': its partition would not copy; SMP's copy left as it is", a_shape.geometry->name.c_str());
					return;
				}
				a_shape.own = RE::NiPointer(own);
				// The copy draws from the original's GPU buffer, which holds our morphs already: one it came with would
				// show the mesh as loaded until our next upload, a frame of the body without them
				for (std::uint32_t i = 0; i < own->numPartitions && i < a_shape.partition->numPartitions; ++i) {
					auto* ownData = own->partitions[i].buffData;
					auto* originalData = a_shape.partition->partitions[i].buffData;
					if (ownData && originalData && ownData->vertexBuffer != originalData->vertexBuffer) {
						if (originalData->vertexBuffer) {
							originalData->vertexBuffer->AddRef();
						}
						if (ownData->vertexBuffer) {
							ownData->vertexBuffer->Release();
						}
						ownData->vertexBuffer = originalData->vertexBuffer;
					}
				}
				skin->skinPartition = a_shape.own;
			}
			const auto bytes = a_shape.base.size();
			std::vector<std::uint8_t*> targets;
			auto* partition = a_shape.own.get();
			for (std::uint32_t i = 0; i < partition->numPartitions; ++i) {
				auto* data = partition->partitions[i].buffData;
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
				// Offsets go with the actor, and the base goes back onto whatever is still drawn; a shape that left with its armor
				// needs nothing
				const bool bHadOffsets = refr && HasOffsets(refr->GetFormID());
				if (refr) {
					ForgetOffsets(refr->GetFormID(), {});
				}
				if (a_data.bWritten || bHadOffsets) {
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
				if (refr && (a_data.bWritten || bHadOffsets)) {
					NextMorphGeneration(refr->GetFormID());
				}
				a_data = {};
				return;
			}

			// Bound again when the parts change, or when another plugin, skee re-applying its morphs, swapped a partition in
			const bool bRebind = a_data.boundGeneration != parts->generation || std::ranges::any_of(a_data.shapes, [](const BodyMorphShape& a_shape) {
				const auto* skin = SkinOf(a_shape);
				return skin && skin->skinPartition.get() != Active(a_shape);
			});
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
				ForgetOffsets(refr->GetFormID(), a_data.shapes);
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
				// A copy swapped in or out draws from its own GPU buffer: the morphs uploaded to it again
				a_data.applied.clear();
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
			NextMorphGeneration(refr->GetFormID());
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

		// The offsets set since the last upload, and the shapes gone
		struct Changed
		{
			ShapeOffsets offsets;
			bool bRemoved = false;
		};
		std::vector<Changed> changed;
		std::vector<RE::NiPointer<RE::BSGeometry>> dropped;
		if (g_bOffsetsChanged.exchange(false)) {
			std::scoped_lock lock(g_offsetLock);
			dropped.swap(g_droppedShapes);
			for (auto it = g_offsets.begin(); it != g_offsets.end();) {
				if (!it->second.bDirty) {
					++it;
					continue;
				}
				it->second.bDirty = false;
				const bool bRemoved = it->second.owners.empty() && it->second.turners.empty();
				changed.push_back({ it->second, bRemoved });
				it->second.seed.reset();
				it = bRemoved ? g_offsets.erase(it) : std::next(it);
			}
		}

		std::vector<Pending> pending;
		{
			std::scoped_lock lock(g_pendingLock);
			pending.swap(g_pending);
		}
		if (pending.empty() && changed.empty() && dropped.empty()) {
			return;
		}

		float uploaded = 0.f;
		Timing::Scope scope(uploaded);
		struct Record
		{
			float& milliseconds;
			~Record() { Timing::RecordPerFrame([&](Timing::PerFrame& a_frame) { a_frame.upload.Add(milliseconds); }); }
		} record{ uploaded };

		// Bytes of a copy into each of the partition's buffers, never more than a buffer holds
		const auto send = [&](RE::NiSkinPartition* a_partition, const std::uint8_t* a_bytes, std::uint32_t a_from, std::uint32_t a_to) {
			for (std::uint32_t i = 0; i < a_partition->numPartitions; ++i) {
				auto* data = a_partition->partitions[i].buffData;
				if (!data || !data->vertexBuffer) {
					continue;
				}
				const auto to = (std::min)(ByteWidthOf(data->vertexBuffer), a_to);
				if (to <= a_from) {
					continue;
				}
				REX::W32::D3D11_BOX box{ a_from, 0, 0, to, 1, 1 };
				context->UpdateSubresource(data->vertexBuffer, 0, &box, a_bytes + a_from, 0, 0);
			}
		};

		for (const auto& geometry : dropped) {
			g_shown.erase(geometry.get());
		}
		for (auto& item : changed) {
			auto& offsets = item.offsets;
			auto it = g_shown.find(offsets.geometry.get());
			if (it == g_shown.end()) {
				if (item.bRemoved || !offsets.seed) {
					continue;
				}
				Shown shown{ offsets.geometry, offsets.partition, offsets.vertexCount, offsets.stride, offsets.bHalves, offsets.normalOffset, offsets.tangentOffset, *offsets.seed, *offsets.seed };
				it = g_shown.emplace(offsets.geometry.get(), std::move(shown)).first;
			}
			// Every owner's offsets summed by vertex
			auto& summed = it->second.offsets;
			summed.clear();
			for (const auto& [owner, set] : offsets.owners) {
				for (std::size_t i = 0; i < set->vertices.size(); ++i) {
					summed.push_back({ set->vertices[i], set->xyz[i * 3], set->xyz[i * 3 + 1], set->xyz[i * 3 + 2] });
				}
			}
			std::ranges::sort(summed, {}, &Offset::vertex);
			std::size_t kept = 0;
			for (std::size_t i = 0; i < summed.size(); ++i) {
				if (kept > 0 && summed[kept - 1].vertex == summed[i].vertex) {
					summed[kept - 1].x += summed[i].x;
					summed[kept - 1].y += summed[i].y;
					summed[kept - 1].z += summed[i].z;
				} else {
					summed[kept++] = summed[i];
				}
			}
			summed.resize(kept);
			// Every owner's rotations composed by vertex, a turned vertex with no offset given a zero one
			for (const auto& [owner, set] : offsets.turners) {
				for (std::size_t i = 0; i < set->vertices.size(); ++i) {
					auto at = std::ranges::lower_bound(summed, set->vertices[i], {}, &Offset::vertex);
					if (at == summed.end() || at->vertex != set->vertices[i]) {
						at = summed.insert(at, Offset{ set->vertices[i], 0.f, 0.f, 0.f });
					}
					const float* q = set->xyzw.data() + i * 4;
					const float x = q[3] * at->qx + q[0] * at->qw + q[1] * at->qz - q[2] * at->qy;
					const float y = q[3] * at->qy - q[0] * at->qz + q[1] * at->qw + q[2] * at->qx;
					const float z = q[3] * at->qz + q[0] * at->qy - q[1] * at->qx + q[2] * at->qw;
					const float w = q[3] * at->qw - q[0] * at->qx - q[1] * at->qy - q[2] * at->qz;
					at->qx = x;
					at->qy = y;
					at->qz = z;
					at->qw = w;
					at->bTurn = true;
				}
			}
			it->second.bOffsetsChanged = true;
		}

		for (const auto& item : pending) {
			// A shape with offsets takes the new morphs into its mirror and goes up whole below
			if (item.geometry) {
				if (const auto it = g_shown.find(item.geometry.get()); it != g_shown.end()) {
					it->second.morphed.assign(item.bytes->begin(), item.bytes->end());
					it->second.partition = item.partition;
					it->second.bFull = true;
					continue;
				}
			}
			send(item.partition.get(), item.bytes->data(), 0, static_cast<std::uint32_t>(item.bytes->size()));
		}

		for (auto it = g_shown.begin(); it != g_shown.end();) {
			auto& shown = it->second;
			if (!shown.bFull && !shown.bOffsetsChanged) {
				++it;
				continue;
			}
			const auto stride = shown.stride;
			// The position with the bitangent's x after it, and the normal and tangent with the bitangent's y and z
			const auto positionBytes = shown.bHalves ? 8u : 16u;
			const auto frameBytes = shown.tangentOffset ? 8u : 4u;
			const auto put = [&](std::uint32_t a_vertex, const Offset* a_offset) {
				const auto at = static_cast<std::size_t>(a_vertex) * stride;
				auto* out = shown.shown.data() + at;
				const auto* in = shown.morphed.data() + at;
				// Back to base and morphs first, so neither offsets nor rotations build up
				std::memcpy(out, in, positionBytes);
				if (shown.normalOffset) {
					std::memcpy(out + shown.normalOffset, in + shown.normalOffset, frameBytes);
				}
				if (!a_offset) {
					return;
				}
				float x, y, z;
				ReadPosition(in, shown.bHalves, x, y, z);
				WritePosition(out, shown.bHalves, x + a_offset->x, y + a_offset->y, z + a_offset->z);
				if (a_offset->bTurn && shown.normalOffset) {
					TurnFrame(out, in, shown.bHalves, shown.normalOffset, shown.tangentOffset, a_offset->qx, a_offset->qy, a_offset->qz, a_offset->qw);
				}
			};

			std::vector<std::uint32_t> current;
			current.reserve(shown.offsets.size());
			for (const auto& offset : shown.offsets) {
				if (offset.vertex < shown.vertexCount) {
					current.push_back(offset.vertex);
				}
			}

			if (shown.bFull) {
				shown.shown = shown.morphed;
				for (const auto& offset : shown.offsets) {
					if (offset.vertex < shown.vertexCount) {
						put(offset.vertex, &offset);
					}
				}
				send(shown.partition.get(), shown.shown.data(), 0, static_cast<std::uint32_t>(shown.shown.size()));
			} else {
				// Only the vertices with offsets now or before: those that lost theirs back to base and morphs
				std::vector<std::uint32_t> touched;
				std::ranges::set_union(shown.lastOffset, current, std::back_inserter(touched));
				auto offset = shown.offsets.begin();
				for (const auto vertex : touched) {
					while (offset != shown.offsets.end() && offset->vertex < vertex) {
						++offset;
					}
					put(vertex, offset != shown.offsets.end() && offset->vertex == vertex ? &*offset : nullptr);
				}
				// Runs of touched vertices, small gaps bridged, each sent as one range
				constexpr std::uint32_t kGap = 16;
				for (std::size_t i = 0; i < touched.size();) {
					auto last = touched[i];
					std::size_t j = i + 1;
					while (j < touched.size() && touched[j] - last <= kGap) {
						last = touched[j++];
					}
					send(shown.partition.get(), shown.shown.data(), touched[i] * stride, (last + 1) * stride);
					i = j;
				}
			}
			shown.lastOffset = std::move(current);
			shown.bFull = false;
			shown.bOffsetsChanged = false;
			// No offsets left: the shape is back on the plain path
			it = shown.offsets.empty() ? g_shown.erase(it) : std::next(it);
		}
	}
}

namespace
{
	class VertexOffsetInterface : public DCMF_API::VertexOffset::IVertexOffsetInterface2
	{
	public:
		static VertexOffsetInterface* GetSingleton() noexcept
		{
			static VertexOffsetInterface singleton;
			return std::addressof(singleton);
		}

		bool SetOffsets(RE::Actor* a_actor, RE::BSGeometry* a_shape, const char* a_owner, const std::uint32_t* a_vertices, const float* a_xyz, std::uint32_t a_count) noexcept override
		{
			return Store<Apply::OffsetSet, 3>(a_actor, a_shape, a_owner, a_vertices, a_xyz, a_count, &Apply::ShapeOffsets::owners, &Apply::OffsetSet::xyz);
		}

		bool SetRotations(RE::Actor* a_actor, RE::BSGeometry* a_shape, const char* a_owner, const std::uint32_t* a_vertices, const float* a_xyzw, std::uint32_t a_count) noexcept override
		{
			return Store<Apply::TurnSet, 4>(a_actor, a_shape, a_owner, a_vertices, a_xyzw, a_count, &Apply::ShapeOffsets::turners, &Apply::TurnSet::xyzw);
		}

		void ClearOffsets(RE::Actor* a_actor, const char* a_owner) noexcept override
		{
			if (!a_actor || !a_owner) {
				return;
			}
			std::scoped_lock lock(Apply::g_offsetLock);
			for (auto& [geometry, offsets] : Apply::g_offsets) {
				if (offsets.actor == a_actor->GetFormID() && (offsets.owners.erase(std::string(a_owner)) + offsets.turners.erase(std::string(a_owner))) > 0) {
					offsets.bDirty = true;
					Apply::g_bOffsetsChanged = true;
				}
			}
		}

		bool CopyPositions(RE::Actor* a_actor, RE::BSGeometry* a_shape, float* a_outXyz, std::uint32_t a_count) noexcept override
		{
			if (!a_actor || !a_shape || (a_count > 0 && !a_outXyz)) {
				return false;
			}
			const auto state = ActorState::Get(a_actor->GetHandle());
			if (!state) {
				return false;
			}
			return state->bodyMorph.With([&](const Apply::BodyMorphData& a_data) {
				const auto shape = std::ranges::find_if(a_data.shapes, [&](const Apply::BodyMorphShape& a_bound) { return a_bound.geometry.get() == a_shape; });
				if (shape == a_data.shapes.end()) {
					return false;
				}
				const auto& bytes = *shape->work[shape->next ^ 1];
				const auto count = (std::min)(a_count, shape->vertexCount);
				for (std::uint32_t v = 0; v < count; ++v) {
					Apply::ReadPosition(bytes.data() + static_cast<std::size_t>(v) * shape->stride, shape->bHalves, a_outXyz[v * 3], a_outXyz[v * 3 + 1], a_outXyz[v * 3 + 2]);
				}
				return true;
			});
		}

	private:
		// An owner's set for a shape DCMF writes, an entry made for the shape at its first: its layout and what the GPU
		// holds now. False, storing nothing, for a shape DCMF does not write
		template <class Set, std::size_t N>
		static bool Store(RE::Actor* a_actor, RE::BSGeometry* a_shape, const char* a_owner, const std::uint32_t* a_vertices, const float* a_values, std::uint32_t a_count,
			std::map<std::string, std::shared_ptr<const Set>, std::less<>> Apply::ShapeOffsets::*a_owners, std::vector<float> Set::*a_floats)
		{
			if (!a_actor || !a_shape || !a_owner || (a_count > 0 && (!a_vertices || !a_values))) {
				return false;
			}
			const auto state = ActorState::Get(a_actor->GetHandle());
			if (!state) {
				return false;
			}
			auto set = std::make_shared<Set>();
			return state->bodyMorph.With([&](Apply::BodyMorphData& a_data) {
				const auto shape = std::ranges::find_if(a_data.shapes, [&](const Apply::BodyMorphShape& a_bound) { return a_bound.geometry.get() == a_shape; });
				if (shape == a_data.shapes.end()) {
					return false;
				}
				set->vertices.reserve(a_count);
				auto& floats = (*set).*a_floats;
				floats.reserve(static_cast<std::size_t>(a_count) * N);
				for (std::uint32_t i = 0; i < a_count; ++i) {
					if (a_vertices[i] < shape->vertexCount) {
						set->vertices.push_back(a_vertices[i]);
						floats.insert(floats.end(), a_values + static_cast<std::size_t>(i) * N, a_values + static_cast<std::size_t>(i) * N + N);
					}
				}
				std::scoped_lock lock(Apply::g_offsetLock);
				auto [entry, bNew] = Apply::g_offsets.try_emplace(a_shape);
				auto& offsets = entry->second;
				if (bNew) {
					offsets.actor = a_actor->GetFormID();
					offsets.geometry = shape->geometry;
					offsets.partition = RE::NiPointer(Apply::Active(*shape));
					offsets.vertexCount = shape->vertexCount;
					offsets.stride = shape->stride;
					offsets.bHalves = shape->bHalves;
					std::tie(offsets.normalOffset, offsets.tangentOffset) = Apply::FrameOffsets(*shape);
					// What the GPU holds now: the copy last summed
					offsets.seed = std::make_shared<const std::vector<std::uint8_t>>(*shape->work[shape->next ^ 1]);
				}
				auto& owners = offsets.*a_owners;
				if (set->vertices.empty()) {
					owners.erase(std::string(a_owner));
				} else {
					owners.insert_or_assign(std::string(a_owner), std::move(set));
				}
				offsets.bDirty = true;
				Apply::g_bOffsetsChanged = true;
				return true;
			});
		}

	public:
		std::uint32_t MorphGeneration(RE::Actor* a_actor) noexcept override
		{
			if (!a_actor) {
				return 0;
			}
			std::scoped_lock lock(Apply::g_offsetLock);
			const auto it = Apply::g_morphGenerations.find(a_actor->GetFormID());
			return it != Apply::g_morphGenerations.end() ? it->second : 0;
		}
	};
}

extern "C" __declspec(dllexport) DCMF_API::VertexOffset::IVertexOffsetInterface* RequestPluginAPI_VertexOffset(const DCMF_API::VertexOffset::InterfaceVersion a_interfaceVersion, const char* a_pluginName, REL::Version a_pluginVersion)
{
	if (!a_pluginName) {
		logger::warn("vertexoffset: RequestPluginAPI_VertexOffset called with no plugin name");
		return nullptr;
	}
	logger::info("vertexoffset: RequestPluginAPI_VertexOffset called by {} {}, interface version {}", a_pluginName, a_pluginVersion, static_cast<std::uint8_t>(a_interfaceVersion) + 1);
	// Version 2 extends version 1's table, so one object answers both
	if (a_interfaceVersion == DCMF_API::VertexOffset::InterfaceVersion::V1 || a_interfaceVersion == DCMF_API::VertexOffset::InterfaceVersion::V2) {
		return VertexOffsetInterface::GetSingleton();
	}
	logger::warn("vertexoffset: RequestPluginAPI_VertexOffset asked for an interface version this build does not have");
	return nullptr;
}
