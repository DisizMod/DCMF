#include "MeshThumbs.h"

#include "BodyTypes.h"
#include "UICommon.h"
#include "Utils.h"
#include "apply/HeadPart.h"
#include "scan/Scan.h"

#include <d3d11.h>
#include <imgui_stdlib.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace UI::MeshThumbs
{
	namespace
	{
		using Microsoft::WRL::ComPtr;

		constexpr std::uint32_t kSize = 256;

		// How long a scene waits for its textures before drawing untextured
		constexpr int kTextureWaitFrames = 180;

		struct Vertex
		{
			float px, py, pz;
			float u, v;
			float nx, ny, nz;
		};

		// One geometry of a model, read off the renderer's CPU copies on the main thread
		struct Piece
		{
			std::vector<Vertex> vertices;
			std::vector<std::uint16_t> indices;
			RE::NiPointer<RE::NiSourceTexture> diffuse;
			bool bAlphaTest = false;
			bool bAlphaBlend = false;
			float cutoff = 0.f;
			bool bNeedsNormals = false;  // none in the buffer: computed from the triangles
			float materialAlpha = 1.f;
			bool bEyeShader = false;   // the piece the iris goes on
			bool bSkinShader = false;  // a facegen piece: its texture is the record's, not the file's
			float tint[3]{ 1.f, 1.f, 1.f };  // the texture multiplied
			float glow[3]{ 0.f, 0.f, 0.f };  // added, unlit
			bool bFxBlend = false;    // a blend the card cannot draw and nothing tests: skipped
		};

		enum class State : std::uint8_t
		{
			kLoading,
			kExtracted,
			kRendered,
			kFailed
		};

		// A face model, bald and untextured, loaded once and drawn under every card on it
		struct Mannequin
		{
			std::shared_ptr<std::vector<Piece>> pieces;
			RE::NiPointer<RE::NiNode> model;  // keeps the renderer data alive
			RE::NiPoint3 head;
			bool bHaveHead = false;
			std::array<float, 3> boundsMin{};
			std::array<float, 3> boundsMax{};
			int attempts = 0;
			std::string error;
		};

		struct Entry
		{
			State state = State::kLoading;
			int waited = 0;
			RE::NiPointer<RE::NiNode> model;
			std::vector<RE::NiPointer<RE::NiNode>> extraModels;
			bool bHavePieces = false;
			std::vector<Piece> pieces;
			std::shared_ptr<std::vector<Piece>> mannequin;
			RE::NiPointer<RE::NiSourceTexture> partDiffuse;  // an eye part's iris
			bool bEyes = false;
			std::array<float, 3> boundsMin{};
			std::array<float, 3> boundsMax{};
			ComPtr<ID3D11Texture2D> colour;
			ComPtr<ID3D11ShaderResourceView> view;
			std::string error;
		};

		std::mutex g_mutex;
		std::unordered_map<std::string, Entry> g_entries;  // by scene key: a part's id, or an overlay's texture, part and layout
		std::unordered_map<std::string, Mannequin> g_mannequins;  // by face model path
		std::unordered_map<RE::FormID, std::pair<std::string, std::string>> g_mannequinOf;  // part -> model, why
		std::atomic<bool> g_clearRequested = false;

		// ---- the pipeline, built once ----

		constexpr const char kShader[] = R"(
cbuffer Scene : register(b0)
{
    row_major float4x4 ViewProj;
    float3   LightDir;   float  UseTexture;
    float3   Eye;        float  Cutoff;
    float    KeyStrength; float Ambient; float2 Pad;
    float3   Tint;       float  Pad2;
    float3   Glow;       float  Pad3;
};

Texture2D    Diffuse : register(t0);
SamplerState Linear  : register(s0);

struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; float3 n : NORMAL; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float3 n : TEXCOORD1; float3 w : TEXCOORD2; };

VSOut VS(VSIn i)
{
    VSOut o;
    o.pos = mul(float4(i.pos, 1.0), ViewProj);
    o.uv = i.uv;
    o.n = normalize(i.n);
    o.w = i.pos;
    return o;
}

float4 PS(VSOut i, bool front : SV_IsFrontFace) : SV_Target
{
    float4 s = UseTexture > 0.5 ? Diffuse.Sample(Linear, i.uv) : float4(0.62, 0.60, 0.56, 1.0);
    if (s.a <= Cutoff) { discard; }
    // Two-sided: hair is thin cards drawn with no culling, and a back face lit by its own normal comes out black
    float3 n = normalize(i.n) * (front ? 1.0 : -1.0);
    float3 l = normalize(LightDir);
    float3 v = normalize(Eye - i.w);
    float  key = saturate(dot(n, l) * 0.65 + 0.35);
    float  fill = saturate(dot(n, normalize(float3(-l.x * 0.75, l.y * 0.25, l.z * 0.55 + 0.25))));
    float  top = saturate(n.z * 0.5 + 0.5);
    float3 h = normalize(l + v);
    float  spec = pow(saturate(dot(n, h)), 42.0) * 0.10;
    float  rim = pow(1.0 - saturate(dot(n, v)), 2.2) * 0.12;
    float  lighting = Ambient + key * KeyStrength + spec + fill * 0.18 + top * 0.08 + rim;
    return float4(saturate(s.rgb * Tint * lighting + Glow), s.a);
}
)";

		struct SceneConstants
		{
			float viewProj[16];
			float lightDir[3];
			float useTexture;
			float eye[3];
			float cutoff;
			float keyStrength;
			float ambient;
			float pad[2];
			float tint[3];
			float pad2;
			float glow[3];
			float pad3;
		};

		ComPtr<ID3D11VertexShader> g_vs;
		ComPtr<ID3D11PixelShader> g_ps;
		ComPtr<ID3D11InputLayout> g_layout;
		ComPtr<ID3D11Buffer> g_constants;
		ComPtr<ID3D11RasterizerState> g_rasterizer;
		ComPtr<ID3D11BlendState> g_opaque;
		ComPtr<ID3D11BlendState> g_blend;
		ComPtr<ID3D11DepthStencilState> g_depthWrite;
		ComPtr<ID3D11DepthStencilState> g_depthTest;
		ComPtr<ID3D11SamplerState> g_sampler;
		ComPtr<ID3D11Texture2D> g_depth;
		ComPtr<ID3D11DepthStencilView> g_dsv;
		bool g_bPipelineFailed = false;

		bool EnsurePipeline(ID3D11Device* a_device)
		{
			if (g_vs) {
				return true;
			}
			if (g_bPipelineFailed) {
				return false;
			}

			ComPtr<ID3DBlob> vs, ps, errors;
			if (FAILED(::D3DCompile(kShader, sizeof(kShader) - 1, "MeshThumbs", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vs, &errors))) {
				g_bPipelineFailed = true;
				logger::error("meshthumbs: vertex shader: {}", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
				return false;
			}
			errors.Reset();
			if (FAILED(::D3DCompile(kShader, sizeof(kShader) - 1, "MeshThumbs", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &ps, &errors))) {
				g_bPipelineFailed = true;
				logger::error("meshthumbs: pixel shader: {}", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
				return false;
			}

			bool ok = SUCCEEDED(a_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs)) &&
			          SUCCEEDED(a_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_ps));

			const D3D11_INPUT_ELEMENT_DESC layout[]{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};
			ok = ok && SUCCEEDED(a_device->CreateInputLayout(layout, 3, vs->GetBufferPointer(), vs->GetBufferSize(), &g_layout));

			D3D11_BUFFER_DESC constants{};
			constants.ByteWidth = sizeof(SceneConstants);
			constants.Usage = D3D11_USAGE_DYNAMIC;
			constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			ok = ok && SUCCEEDED(a_device->CreateBuffer(&constants, nullptr, &g_constants));

			D3D11_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_NONE;
			rasterizer.DepthClipEnable = TRUE;
			ok = ok && SUCCEEDED(a_device->CreateRasterizerState(&rasterizer, &g_rasterizer));

			D3D11_BLEND_DESC opaque{};
			opaque.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			ok = ok && SUCCEEDED(a_device->CreateBlendState(&opaque, &g_opaque));

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].BlendEnable = TRUE;
			blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			ok = ok && SUCCEEDED(a_device->CreateBlendState(&blend, &g_blend));

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = TRUE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			ok = ok && SUCCEEDED(a_device->CreateDepthStencilState(&depth, &g_depthWrite));
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			ok = ok && SUCCEEDED(a_device->CreateDepthStencilState(&depth, &g_depthTest));

			D3D11_SAMPLER_DESC sampler{};
			sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampler.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
			sampler.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
			sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
			sampler.MaxLOD = D3D11_FLOAT32_MAX;
			ok = ok && SUCCEEDED(a_device->CreateSamplerState(&sampler, &g_sampler));

			D3D11_TEXTURE2D_DESC depthTexture{};
			depthTexture.Width = kSize;
			depthTexture.Height = kSize;
			depthTexture.MipLevels = 1;
			depthTexture.ArraySize = 1;
			depthTexture.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			depthTexture.SampleDesc.Count = 1;
			depthTexture.Usage = D3D11_USAGE_DEFAULT;
			depthTexture.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			ok = ok && SUCCEEDED(a_device->CreateTexture2D(&depthTexture, nullptr, &g_depth)) && SUCCEEDED(a_device->CreateDepthStencilView(g_depth.Get(), nullptr, &g_dsv));

			if (!ok) {
				g_bPipelineFailed = true;
				g_vs.Reset();
				logger::error("meshthumbs: could not build the pipeline");
			}
			return ok;
		}

		// ---- reading a model's geometry off the CPU copies ----

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

		// Positions sit first in the vertex, as halves unless the next attribute starts past 8 bytes; body morph's rule
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

		bool ReadPiece(RE::BSGeometry* a_geometry, RE::BSGraphics::VertexDesc a_desc, const std::uint8_t* a_raw, std::uint32_t a_vertexCount, const std::uint16_t* a_indices, std::uint32_t a_indexCount, Piece& a_out, std::string& a_error)
		{
			using A = RE::BSGraphics::Vertex::Attribute;
			using F = RE::BSGraphics::Vertex::Flags;
			if (!a_raw || !a_indices || a_vertexCount == 0 || a_indexCount == 0) {
				a_error = "no CPU copy of the geometry";
				return false;
			}
			// A body or head lit by a model-space normal map ships no vertex normals at all; they are computed below
			const auto stride = a_desc.GetSize();
			const bool bHalves = PositionsAreHalves(a_desc);
			const auto uvOffset = a_desc.GetAttributeOffset(A::VA_TEXCOORD0);
			const auto normalOffset = a_desc.GetAttributeOffset(A::VA_NORMAL);
			const bool bHasUV = (a_desc.HasFlag(F::VF_UV) || uvOffset != 0) && uvOffset + 4 <= stride;
			const bool bHasNormal = (a_desc.HasFlag(F::VF_NORMAL) || normalOffset != 0) && normalOffset + 4 <= stride;

			a_out.vertices.resize(a_vertexCount);
			for (std::uint32_t i = 0; i < a_vertexCount; ++i) {
				const auto* v = a_raw + static_cast<std::size_t>(i) * stride;
				auto& out = a_out.vertices[i];
				if (bHalves) {
					const auto* h = reinterpret_cast<const std::uint16_t*>(v);
					out.px = HalfToFloat(h[0]);
					out.py = HalfToFloat(h[1]);
					out.pz = HalfToFloat(h[2]);
				} else {
					const auto* f = reinterpret_cast<const float*>(v);
					out.px = f[0];
					out.py = f[1];
					out.pz = f[2];
				}
				if (bHasUV) {
					const auto* h = reinterpret_cast<const std::uint16_t*>(v + uvOffset);
					out.u = HalfToFloat(h[0]);
					out.v = HalfToFloat(h[1]);
				}
				if (bHasNormal) {
					const auto* b = v + normalOffset;
					out.nx = b[0] / 255.f * 2.f - 1.f;
					out.ny = b[1] / 255.f * 2.f - 1.f;
					out.nz = b[2] / 255.f * 2.f - 1.f;
				} else {
					out.nz = 1.f;
				}
			}

			// Every index clamped into the vertex range: third-party bytes
			a_out.indices.resize(a_indexCount);
			for (std::uint32_t i = 0; i < a_indexCount; ++i) {
				a_out.indices[i] = static_cast<std::uint16_t>((std::min)(static_cast<std::uint32_t>(a_indices[i]), a_vertexCount - 1));
			}
			a_out.bNeedsNormals = !bHasNormal;

			auto& runtime = a_geometry->GetGeometryRuntimeData();
			if (const auto& alpha = runtime.alphaProperty) {
				a_out.bAlphaTest = alpha->GetAlphaTesting();
				a_out.bAlphaBlend = alpha->GetAlphaBlending();
				a_out.cutoff = a_out.bAlphaTest ? alpha->alphaThreshold / 255.f : 0.f;
				// Source alpha over its inverse is the one blend the card has; anything else with a test draws opaque
				// through the test, and with none is FX the card cannot draw
				using Fn = RE::NiAlphaProperty::AlphaFunction;
				if (a_out.bAlphaBlend && (alpha->GetSrcBlendMode() != Fn::kSrcAlpha || alpha->GetDestBlendMode() != Fn::kInvSrcAlpha)) {
					if (a_out.bAlphaTest) {
						a_out.bAlphaBlend = false;
					} else {
						a_out.bFxBlend = true;
					}
				}
			}
			if (const auto& shader = runtime.shaderProperty) {
				if (auto* lighting = netimmerse_cast<RE::BSLightingShaderProperty*>(shader.get())) {
					if (auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(lighting->material)) {
						a_out.diffuse = material->diffuseTexture;
						a_out.materialAlpha = material->materialAlpha;
						const auto feature = material->GetFeature();
						a_out.bEyeShader = feature == RE::BSShaderMaterial::Feature::kEye;
						a_out.bSkinShader = feature == RE::BSShaderMaterial::Feature::kFaceGen || feature == RE::BSShaderMaterial::Feature::kFaceGenRGBTint;
					}
				}
			}
			return true;
		}

		// Smooth normals from the triangles, for a mesh whose buffer carries none
		void ComputeNormals(Piece& a_piece)
		{
			for (auto& vertex : a_piece.vertices) {
				vertex.nx = vertex.ny = vertex.nz = 0.f;
			}
			for (std::size_t i = 0; i + 2 < a_piece.indices.size(); i += 3) {
				auto& a = a_piece.vertices[a_piece.indices[i]];
				auto& b = a_piece.vertices[a_piece.indices[i + 1]];
				auto& c = a_piece.vertices[a_piece.indices[i + 2]];
				const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
				const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
				const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
				for (auto* v : { &a, &b, &c }) {
					v->nx += nx;
					v->ny += ny;
					v->nz += nz;
				}
			}
			for (auto& vertex : a_piece.vertices) {
				const auto length = std::sqrt(vertex.nx * vertex.nx + vertex.ny * vertex.ny + vertex.nz * vertex.nz);
				if (length > 1.0e-6f) {
					vertex.nx /= length;
					vertex.ny /= length;
					vertex.nz /= length;
				} else {
					vertex.nz = 1.f;
				}
			}
			a_piece.bNeedsNormals = false;
		}

		void Grow(std::array<float, 3>& a_min, std::array<float, 3>& a_max, bool& a_bHave, const Piece& a_piece)
		{
			for (const auto& vertex : a_piece.vertices) {
				if (!a_bHave) {
					a_min = { vertex.px, vertex.py, vertex.pz };
					a_max = a_min;
					a_bHave = true;
				}
				a_min[0] = (std::min)(a_min[0], vertex.px);
				a_min[1] = (std::min)(a_min[1], vertex.py);
				a_min[2] = (std::min)(a_min[2], vertex.pz);
				a_max[0] = (std::max)(a_max[0], vertex.px);
				a_max[1] = (std::max)(a_max[1], vertex.py);
				a_max[2] = (std::max)(a_max[2], vertex.pz);
			}
		}

		// Where the model's own skeleton puts the head bone, in the root's space; a hair authored against another
		// skeleton puts it elsewhere, and the difference is how far it has to move to sit on the mannequin
		bool HeadNodeOf(RE::NiNode* a_root, RE::NiPoint3& a_out)
		{
			RE::NiAVObject* found = nullptr;
			RE::BSVisit::TraverseScenegraphObjects(a_root, [&](RE::NiAVObject* a_object) {
				if (a_object && a_object->name == "NPC Head [Head]") {
					found = a_object;
					return RE::BSVisit::BSVisitControl::kStop;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			if (!found) {
				return false;
			}
			std::vector<const RE::NiAVObject*> chain;
			for (const auto* at = found; at && at != a_root; at = at->parent) {
				chain.push_back(at);
			}
			RE::NiTransform world;
			for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
				world = world * (*it)->local;
			}
			a_out = world.translate;
			return true;
		}

		void Shift(Entry& a_entry, const RE::NiPoint3& a_by)
		{
			for (auto& piece : a_entry.pieces) {
				for (auto& vertex : piece.vertices) {
					vertex.px += a_by.x;
					vertex.py += a_by.y;
					vertex.pz += a_by.z;
				}
			}
			for (auto* box : { &a_entry.boundsMin, &a_entry.boundsMax }) {
				(*box)[0] += a_by.x;
				(*box)[1] += a_by.y;
				(*box)[2] += a_by.z;
			}
		}

		// Walks the model: skinned geometry from its partitions' copies, anything else from the geometry's own
		void Extract(RE::NiNode* a_root, Entry& a_entry)
		{
			std::string firstError;
			bool& bHave = a_entry.bHavePieces;
			RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
				auto& runtime = a_geometry->GetGeometryRuntimeData();
				auto* triShape = a_geometry->AsTriShape();
				if (!triShape) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				if (!a_geometry->name.empty() && a_geometry->name.c_str()[0] == '[') {
					return RE::BSVisit::BSVisitControl::kContinue;  // an overlay or an effect plane
				}
				// Hidden in the file is hidden in the game: an SMP hair's collision body ships hidden
				for (RE::NiAVObject* at = a_geometry; at; at = at->parent) {
					if (at->GetAppCulled()) {
						return RE::BSVisit::BSVisitControl::kContinue;
					}
				}

				Piece piece;
				std::string error;
				bool bRead = false;
				if (runtime.skinInstance && runtime.skinInstance->skinPartition && runtime.skinInstance->skinPartition->numPartitions > 0) {
					// One buffer per partition, each the whole mesh's vertices and its own triangles: folded into one piece
					auto* skin = runtime.skinInstance->skinPartition.get();
					for (std::uint32_t p = 0; p < skin->numPartitions; ++p) {
						auto& partition = skin->partitions[p];
						auto* buffer = partition.buffData;
						if (!buffer || !buffer->rawVertexData) {
							error = "skinned, and no CPU copy";
							continue;
						}
						Piece one;
						std::string pieceError;
						std::vector<std::uint16_t> mapped;
						const std::uint16_t* indices = buffer->rawIndexData;
						const std::uint32_t indexCount = partition.triangles * 3u;
						if (!indices && partition.triList) {
							for (std::uint32_t i = 0; i < indexCount; ++i) {
								const auto local = partition.triList[i];
								mapped.push_back(partition.vertexMap && local < partition.vertices ? partition.vertexMap[local] : local);
							}
							indices = mapped.data();
						}
						if (ReadPiece(a_geometry, buffer->vertexDesc, buffer->rawVertexData, skin->vertexCount, indices, indexCount, one, pieceError)) {
							if (piece.vertices.empty()) {
								piece = std::move(one);
							} else {
								piece.indices.insert(piece.indices.end(), one.indices.begin(), one.indices.end());
							}
							bRead = true;
						} else if (error.empty()) {
							error = pieceError;
						}
					}
				} else if (auto* data = runtime.rendererData) {
					bRead = ReadPiece(a_geometry, data->vertexDesc, data->rawVertexData, triShape->GetTrishapeRuntimeData().vertexCount, data->rawIndexData, triShape->GetTrishapeRuntimeData().triangleCount * 3u, piece, error);
				} else {
					error = "no renderer data";
				}
				if (!bRead) {
					if (firstError.empty()) {
						firstError = std::format("{}: {}", a_geometry->name.c_str(), error);
					}
					return RE::BSVisit::BSVisitControl::kContinue;
				}

				// Invisible in the game is invisible on the card: collision proxies ship at alpha 0 or with no diffuse
				if (piece.materialAlpha <= 0.f || !piece.diffuse || piece.bFxBlend || piece.vertices.empty()) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				// A face part is a dynamic shape: its positions are the engine's own block, not the static copy's
				if (auto* dynamic = a_geometry->AsDynamicTriShape()) {
					auto& block = dynamic->GetDynamicTrishapeRuntimeData();
					const auto count = static_cast<std::uint32_t>(piece.vertices.size());
					const auto stride = count ? block.dataSize / count : 0u;
					if (block.dynamicData && stride >= sizeof(float) * 3) {
						const auto* bytes = static_cast<const std::uint8_t*>(block.dynamicData);
						for (std::uint32_t v = 0; v < count; ++v) {
							const auto* position = reinterpret_cast<const float*>(bytes + v * stride);
							piece.vertices[v].px = position[0];
							piece.vertices[v].py = position[1];
							piece.vertices[v].pz = position[2];
						}
					}
				}
				if (piece.bNeedsNormals) {
					ComputeNormals(piece);
				}
				Grow(a_entry.boundsMin, a_entry.boundsMax, bHave, piece);
				a_entry.pieces.push_back(std::move(piece));
				return RE::BSVisit::BSVisitControl::kContinue;
			});

			if (a_entry.pieces.empty()) {
				a_entry.state = State::kFailed;
				a_entry.error = firstError.empty() ? "no geometry in the model" : firstError;
			} else {
				a_entry.state = State::kExtracted;
			}
		}

		// A model whose vertices sit at the origin while its own head node stands at the skeleton's is moved onto that
		// node: vanilla heads are authored in the bone's space, UBE's in the skeleton's. Measured: a mesh already
		// there moves by nothing
		void OntoOwnHeadNode(RE::NiNode* a_root, Entry& a_entry)
		{
			RE::NiPoint3 head;
			if (a_entry.pieces.empty() || !HeadNodeOf(a_root, head)) {
				return;
			}
			const RE::NiPoint3 centre{ (a_entry.boundsMin[0] + a_entry.boundsMax[0]) * 0.5f, (a_entry.boundsMin[1] + a_entry.boundsMax[1]) * 0.5f, (a_entry.boundsMin[2] + a_entry.boundsMax[2]) * 0.5f };
			if (centre.Length() > 40.f || head.Length() < 60.f) {
				return;
			}
			Shift(a_entry, head);
		}

		// A texture loaded on its own by path, the way the engine loads a set's diffuse
		RE::NiPointer<RE::NiSourceTexture> LoadTexture(const char* a_path)
		{
			RE::NiPointer<RE::NiSourceTexture> loaded;
			if (!a_path || !*a_path) {
				return loaded;
			}
			std::string full = a_path;
			if (Scan::Files::Normalize(full).rfind("textures/", 0) != 0) {
				full = "textures\\" + full;
			}
			auto set = RE::NiPointer<RE::BSTextureSet>{ RE::BSShaderTextureSet::Create() };
			set->SetTexturePath(RE::BSTextureSet::Texture::kDiffuse, full.c_str());
			set->SetTexture(RE::BSTextureSet::Texture::kDiffuse, loaded);
			return loaded;
		}

		[[nodiscard]] std::string PartKey(RE::FormID a_part)
		{
			return std::format("part:{:08X}", a_part);
		}

		[[nodiscard]] std::string OverlayKey(const std::string& a_texture, Scan::SkinPart a_part, std::string_view a_uv)
		{
			return std::format("overlay:{}|{}|{}", Scan::Files::Normalize(a_texture), Scan::SkinPartName(a_part), a_uv);
		}

		// ---- which head a part goes on ----

		std::string MeshPath(const char* a_model)
		{
			if (!a_model || !*a_model) {
				return {};
			}
			auto path = Scan::Files::Normalize(a_model);
			return path.starts_with("meshes/") ? path : "meshes/" + path;
		}

		// The first of the part's valid races with a face model for its sex, from the scan; the Nord's when it names none
		std::pair<std::string, std::string> DecideMannequin(RE::FormID a_part)
		{
			const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue();
			if (!catalogue) {
				return { {}, "the scan has not finished" };
			}
			const auto part = std::ranges::find(catalogue->headParts, a_part, &Scan::HeadPart::formID);
			if (part == catalogue->headParts.end()) {
				return { {}, "not a scanned head part" };
			}
			const bool bFemale = part->bFemale || !part->bMale;
			const auto headOf = [&](std::string_view a_race) -> std::string {
				for (const auto& model : catalogue->skinModels) {
					if (model.part == Scan::SkinPart::kHead && (bFemale ? model.bFemale : model.bMale) && std::ranges::find(model.races, a_race) != model.races.end()) {
						return model.model;
					}
				}
				return {};
			};
			if (part->races.empty()) {
				if (auto model = headOf("NordRace"sv); !model.empty()) {
					return { model, {} };
				}
			}
			for (const auto& race : part->races) {
				if (auto model = headOf(race); !model.empty()) {
					return { model, {} };
				}
			}
			for (const auto& model : catalogue->skinModels) {
				if (model.part == Scan::SkinPart::kHead && (bFemale ? model.bFemale : model.bMale)) {
					return { model.model, {} };
				}
			}
			return { {}, std::format("no scanned {} face for any of its {} race(s)", bFemale ? "female" : "male", part->races.size()) };
		}

		// Loads a mannequin on the first ask and on every ask after a failure. Main thread, g_mutex held
		Mannequin& LoadMannequin(const std::string& a_model)
		{
			auto& mannequin = g_mannequins[a_model];
			if (mannequin.pieces && !mannequin.pieces->empty()) {
				return mannequin;
			}
			++mannequin.attempts;
			// The model database takes paths under the meshes folder with backslashes, as a record spells them; the scan's are Data-relative
			std::string demanded = a_model.starts_with("meshes/") ? a_model.substr(7) : a_model;
			std::ranges::replace(demanded, '/', '\\');
			RE::BSModelDB::DBTraits::ArgsType args;
			if (const auto code = RE::BSModelDB::Demand(demanded.c_str(), mannequin.model, args); code != RE::BSResource::ErrorCode::kNone || !mannequin.model) {
				mannequin.error = std::format("could not load '{}' ({})", a_model, static_cast<int>(code));
				logger::info("meshthumbs: mannequin {}", mannequin.error);
				return mannequin;
			}
			Entry read;
			Extract(mannequin.model.get(), read);
			OntoOwnHeadNode(mannequin.model.get(), read);
			if (read.pieces.empty()) {
				mannequin.error = std::format("'{}' gave no geometry: {}", a_model, read.error);
				logger::info("meshthumbs: mannequin {}", mannequin.error);
				return mannequin;
			}
			auto pieces = std::make_shared<std::vector<Piece>>();
			for (auto& piece : read.pieces) {
				piece.diffuse.reset();  // untextured: grey
				piece.bAlphaBlend = false;
				pieces->push_back(std::move(piece));
			}
			mannequin.pieces = std::move(pieces);
			mannequin.boundsMin = read.boundsMin;
			mannequin.boundsMax = read.boundsMax;
			mannequin.bHaveHead = HeadNodeOf(mannequin.model.get(), mannequin.head);
			mannequin.error.clear();
			logger::info("meshthumbs: mannequin '{}': {} piece(s), head bone {}", a_model, mannequin.pieces->size(), mannequin.bHaveHead ? "found" : "not in the model");
			return mannequin;
		}

		// The load, on the main thread
		void Load(RE::FormID a_formID)
		{
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				return;
			}
			tasks->AddTask([a_formID]() {
				auto* part = RE::TESForm::LookupByID<RE::BGSHeadPart>(a_formID);
				const auto* path = part ? part->model.c_str() : nullptr;

				Entry loaded;
				loaded.bEyes = part && part->type.get() == RE::BGSHeadPart::HeadPartType::kEyes;
				std::string why;
				const auto mannequinPath = MannequinOf(a_formID, &why);
				if (mannequinPath.empty()) {
					logger::info("meshthumbs: {:08X} drawn without a head: {}", a_formID, why);
				}
				if (!path || !*path) {
					loaded.state = State::kFailed;
					loaded.error = "the head part has no model";
				} else {
					RE::BSModelDB::DBTraits::ArgsType args;
					if (const auto code = RE::BSModelDB::Demand(path, loaded.model, args); code != RE::BSResource::ErrorCode::kNone || !loaded.model) {
						loaded.state = State::kFailed;
						loaded.error = std::format("could not load '{}' ({})", path, static_cast<int>(code));
					} else {
						Extract(loaded.model.get(), loaded);
						OntoOwnHeadNode(loaded.model.get(), loaded);

						// A hair can be several parts, a hairline, a scalp, a braid, each its own model and all the hairstyle
						std::vector<std::string> readPaths{ Scan::Files::Normalize(path) };
						for (auto* extra : part->extraParts) {
							const auto* extraPath = extra ? extra->model.c_str() : nullptr;
							if (!extraPath || !*extraPath || std::ranges::find(readPaths, Scan::Files::Normalize(extraPath)) != readPaths.end()) {
								continue;
							}
							readPaths.emplace_back(Scan::Files::Normalize(extraPath));
							Entry more;
							RE::BSModelDB::DBTraits::ArgsType extraArgs;
							if (RE::BSModelDB::Demand(extraPath, more.model, extraArgs) != RE::BSResource::ErrorCode::kNone || !more.model) {
								continue;
							}
							Extract(more.model.get(), more);
							OntoOwnHeadNode(more.model.get(), more);
							for (auto& piece : more.pieces) {
								Grow(loaded.boundsMin, loaded.boundsMax, loaded.bHavePieces, piece);
								loaded.pieces.push_back(std::move(piece));
							}
							loaded.extraModels.push_back(std::move(more.model));
						}

						// A part's record carries its texture set: an eye's iris onto the eye-shader piece, a scar's or any facegen
						// part's onto its skin-shaded pieces, whose file texture is a placeholder the game never draws; onto everything
						// when no piece says which
						if (part->textureSet) {
							loaded.partDiffuse = LoadTexture(part->textureSet->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse));
							const bool bAnyTarget = std::ranges::any_of(loaded.pieces, [&](const Piece& a_piece) { return loaded.bEyes ? a_piece.bEyeShader : a_piece.bSkinShader; });
							for (auto& piece : loaded.pieces) {
								if (loaded.partDiffuse && (!bAnyTarget || (loaded.bEyes ? piece.bEyeShader : piece.bSkinShader))) {
									piece.diffuse = loaded.partDiffuse;
								}
							}
						}

						// Onto the mannequin's head by the difference between the two head bones; a well authored part moves by nothing
						if (!mannequinPath.empty()) {
							RE::NiPoint3 mannequinHead;
							bool bHaveMannequinHead = false;
							{
								std::lock_guard<std::mutex> lock(g_mutex);
								auto& mannequin = LoadMannequin(mannequinPath);
								loaded.mannequin = mannequin.pieces;
								mannequinHead = mannequin.head;
								bHaveMannequinHead = mannequin.bHaveHead;
							}
							RE::NiPoint3 partHead;
							if (bHaveMannequinHead && HeadNodeOf(loaded.model.get(), partHead)) {
								if (const auto delta = mannequinHead - partHead; delta.Length() > 0.01f) {
									Shift(loaded, delta);
								}
							}
						}
					}
				}

				std::lock_guard<std::mutex> lock(g_mutex);
				auto it = g_entries.find(PartKey(a_formID));
				if (it == g_entries.end()) {
					return;  // cleared while loading
				}
				auto& entry = it->second;
				entry.state = loaded.state;
				entry.model = std::move(loaded.model);
				entry.extraModels = std::move(loaded.extraModels);
				entry.pieces = std::move(loaded.pieces);
				entry.mannequin = std::move(loaded.mannequin);
				entry.partDiffuse = std::move(loaded.partDiffuse);
				entry.bEyes = loaded.bEyes;
				entry.boundsMin = loaded.boundsMin;
				entry.boundsMax = loaded.boundsMax;
				entry.error = loaded.error;
				if (loaded.state == State::kFailed) {
					logger::info("meshthumbs: {:08X} failed: {}", a_formID, loaded.error);
				}
			});
		}

		// An overlay scene: the layout's naked model drawn grey, then again with the texture blended over it, bright
		// red and lit from within, since a card is for seeing where the paint lands, not what colour it is. Main thread
		void LoadOverlay(const std::string& a_key, const std::string& a_texture, Scan::SkinPart a_part, std::string_view a_uv)
		{
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) {
				return;
			}
			tasks->AddTask([a_key, a_texture, a_part, uv = std::string(a_uv)]() {
				Entry loaded;
				std::string why;
				const auto modelPath = BodyTypes::ModelFor(a_part, uv, &why);
				if (modelPath.empty()) {
					loaded.state = State::kFailed;
					loaded.error = why;
				} else {
					{
						std::lock_guard<std::mutex> lock(g_mutex);
						auto& mannequin = LoadMannequin(modelPath);
						if (!mannequin.pieces || mannequin.pieces->empty()) {
							loaded.state = State::kFailed;
							loaded.error = std::format("no {} figure: {}", Scan::SkinPartName(a_part), mannequin.error);
						} else {
							loaded.mannequin = mannequin.pieces;
							loaded.boundsMin = mannequin.boundsMin;
							loaded.boundsMax = mannequin.boundsMax;
							loaded.bHavePieces = true;
						}
					}
					if (loaded.state != State::kFailed) {
						loaded.partDiffuse = LoadTexture(a_texture.c_str());
						if (!loaded.partDiffuse) {
							loaded.state = State::kFailed;
							loaded.error = std::format("could not load '{}'", a_texture);
						} else {
							for (const auto& under : *loaded.mannequin) {
								Piece over = under;
								over.diffuse = loaded.partDiffuse;
								over.bAlphaTest = false;
								over.bAlphaBlend = true;
								over.cutoff = 0.f;
								over.tint[0] = 1.f;
								over.tint[1] = 0.15f;
								over.tint[2] = 0.15f;
								over.glow[0] = 0.45f;
								loaded.pieces.push_back(std::move(over));
							}
							loaded.state = State::kExtracted;
						}
					}
				}

				std::lock_guard<std::mutex> lock(g_mutex);
				auto it = g_entries.find(a_key);
				if (it == g_entries.end()) {
					return;
				}
				auto& entry = it->second;
				entry.state = loaded.state;
				entry.pieces = std::move(loaded.pieces);
				entry.mannequin = std::move(loaded.mannequin);
				entry.partDiffuse = std::move(loaded.partDiffuse);
				entry.boundsMin = loaded.boundsMin;
				entry.boundsMax = loaded.boundsMax;
				entry.error = loaded.error;
				if (loaded.state == State::kFailed) {
					logger::info("meshthumbs: overlay '{}' on {} {} failed: {}", a_texture, uv, Scan::SkinPartName(a_part), loaded.error);
				}
			});
		}

		// ---- the draw, on the render thread ----

		// Everything the draw binds, taken before and put back after: the game's own frame is mid-flight around this
		struct SavedState
		{
			ComPtr<ID3D11RenderTargetView> rtv;
			ComPtr<ID3D11DepthStencilView> dsv;
			D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
			UINT viewportCount = 0;
			ComPtr<ID3D11RasterizerState> rasterizer;
			ComPtr<ID3D11BlendState> blend;
			float blendFactor[4]{};
			UINT sampleMask = 0;
			ComPtr<ID3D11DepthStencilState> depth;
			UINT stencilRef = 0;
			ComPtr<ID3D11InputLayout> layout;
			ComPtr<ID3D11Buffer> vertexBuffer;
			UINT vertexStride = 0;
			UINT vertexOffset = 0;
			ComPtr<ID3D11Buffer> indexBuffer;
			DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
			UINT indexOffset = 0;
			D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
			ComPtr<ID3D11VertexShader> vs;
			ComPtr<ID3D11PixelShader> ps;
			ComPtr<ID3D11GeometryShader> gs;
			ComPtr<ID3D11HullShader> hs;
			ComPtr<ID3D11DomainShader> ds;
			ComPtr<ID3D11Buffer> vsConstants;
			ComPtr<ID3D11Buffer> psConstants;
			ComPtr<ID3D11SamplerState> sampler;
			ComPtr<ID3D11ShaderResourceView> resource;

			void Take(ID3D11DeviceContext* a_ctx)
			{
				a_ctx->OMGetRenderTargets(1, &rtv, &dsv);
				viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
				a_ctx->RSGetViewports(&viewportCount, viewports);
				a_ctx->RSGetState(&rasterizer);
				a_ctx->OMGetBlendState(&blend, blendFactor, &sampleMask);
				a_ctx->OMGetDepthStencilState(&depth, &stencilRef);
				a_ctx->IAGetInputLayout(&layout);
				a_ctx->IAGetVertexBuffers(0, 1, &vertexBuffer, &vertexStride, &vertexOffset);
				a_ctx->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);
				a_ctx->IAGetPrimitiveTopology(&topology);
				a_ctx->VSGetShader(&vs, nullptr, nullptr);
				a_ctx->PSGetShader(&ps, nullptr, nullptr);
				a_ctx->GSGetShader(&gs, nullptr, nullptr);
				a_ctx->HSGetShader(&hs, nullptr, nullptr);
				a_ctx->DSGetShader(&ds, nullptr, nullptr);
				a_ctx->VSGetConstantBuffers(0, 1, &vsConstants);
				a_ctx->PSGetConstantBuffers(0, 1, &psConstants);
				a_ctx->PSGetSamplers(0, 1, &sampler);
				a_ctx->PSGetShaderResources(0, 1, &resource);
			}

			void Restore(ID3D11DeviceContext* a_ctx)
			{
				ID3D11RenderTargetView* rtvs[]{ rtv.Get() };
				a_ctx->OMSetRenderTargets(1, rtvs, dsv.Get());
				a_ctx->RSSetViewports(viewportCount, viewports);
				a_ctx->RSSetState(rasterizer.Get());
				a_ctx->OMSetBlendState(blend.Get(), blendFactor, sampleMask);
				a_ctx->OMSetDepthStencilState(depth.Get(), stencilRef);
				a_ctx->IASetInputLayout(layout.Get());
				ID3D11Buffer* vbs[]{ vertexBuffer.Get() };
				a_ctx->IASetVertexBuffers(0, 1, vbs, &vertexStride, &vertexOffset);
				a_ctx->IASetIndexBuffer(indexBuffer.Get(), indexFormat, indexOffset);
				a_ctx->IASetPrimitiveTopology(topology);
				a_ctx->VSSetShader(vs.Get(), nullptr, 0);
				a_ctx->PSSetShader(ps.Get(), nullptr, 0);
				a_ctx->GSSetShader(gs.Get(), nullptr, 0);
				a_ctx->HSSetShader(hs.Get(), nullptr, 0);
				a_ctx->DSSetShader(ds.Get(), nullptr, 0);
				ID3D11Buffer* vcb[]{ vsConstants.Get() };
				a_ctx->VSSetConstantBuffers(0, 1, vcb);
				ID3D11Buffer* pcb[]{ psConstants.Get() };
				a_ctx->PSSetConstantBuffers(0, 1, pcb);
				ID3D11SamplerState* samplers[]{ sampler.Get() };
				a_ctx->PSSetSamplers(0, 1, samplers);
				ID3D11ShaderResourceView* resources[]{ resource.Get() };
				a_ctx->PSSetShaderResources(0, 1, resources);
			}
		};

		struct Framing
		{
			std::array<float, 3> lo{};
			std::array<float, 3> hi{};
			float cx = 0.f;
			float cy = 0.f;
			float cz = 0.f;
			float half = 1.f;
		};

		// A hairstyle is framed with the head it sits on; an eye on its own left eyeball, seen from the front
		Framing FrameOf(const Entry& a_entry)
		{
			Framing frame;
			frame.lo = a_entry.boundsMin;
			frame.hi = a_entry.boundsMax;
			auto& lo = frame.lo;
			auto& hi = frame.hi;
			if (!a_entry.bEyes && a_entry.mannequin) {
				for (const auto& piece : *a_entry.mannequin) {
					for (const auto& vertex : piece.vertices) {
						lo[0] = (std::min)(lo[0], vertex.px);
						lo[1] = (std::min)(lo[1], vertex.py);
						lo[2] = (std::min)(lo[2], vertex.pz);
						hi[0] = (std::max)(hi[0], vertex.px);
						hi[1] = (std::max)(hi[1], vertex.py);
						hi[2] = (std::max)(hi[2], vertex.pz);
					}
				}
			}
			if (a_entry.bEyes) {
				const bool bAnyEye = std::ranges::any_of(a_entry.pieces, &Piece::bEyeShader);
				const float middle = (a_entry.boundsMin[0] + a_entry.boundsMax[0]) * 0.5f;
				std::array<float, 3> eyeLo{};
				std::array<float, 3> eyeHi{};
				bool bAny = false;
				for (const auto& piece : a_entry.pieces) {
					if (bAnyEye && !piece.bEyeShader) {
						continue;
					}
					for (const auto& vertex : piece.vertices) {
						if (vertex.px <= middle) {
							continue;
						}
						if (!bAny) {
							eyeLo = { vertex.px, vertex.py, vertex.pz };
							eyeHi = eyeLo;
							bAny = true;
							continue;
						}
						eyeLo[0] = (std::min)(eyeLo[0], vertex.px);
						eyeLo[1] = (std::min)(eyeLo[1], vertex.py);
						eyeLo[2] = (std::min)(eyeLo[2], vertex.pz);
						eyeHi[0] = (std::max)(eyeHi[0], vertex.px);
						eyeHi[1] = (std::max)(eyeHi[1], vertex.py);
						eyeHi[2] = (std::max)(eyeHi[2], vertex.pz);
					}
				}
				if (bAny) {
					lo = eyeLo;
					hi = eyeHi;
				} else {
					lo[0] = middle;
				}
			}
			frame.cx = (lo[0] + hi[0]) * 0.5f;
			frame.cy = (lo[1] + hi[1]) * 0.5f;
			frame.cz = (lo[2] + hi[2]) * 0.5f;
			const float ex = hi[0] - lo[0];
			const float ez = hi[2] - lo[2];
			frame.half = (std::max)({ ex, ez, 1.f }) * (a_entry.bEyes ? 0.5f : 0.6f);
			return frame;
		}

		// An orthographic camera in front of the box, looking along -y with z up: the actor faces +y
		void BuildViewProj(const Framing& a_frame, SceneConstants& a_out)
		{
			const float depthHalf = (std::max)(a_frame.hi[1] - a_frame.lo[1], 1.f) * 0.6f + 40.f;
			const float sx = 1.f / a_frame.half;
			const float sy = 1.f / a_frame.half;
			const float sz = 1.f / (2.f * depthHalf);
			float m[16]{};
			m[0] = sx;
			m[12] = -a_frame.cx * sx;
			m[9] = sy;
			m[13] = -a_frame.cz * sy;
			m[6] = -sz;
			m[14] = (a_frame.cy + depthHalf) * sz;
			m[15] = 1.f;
			std::copy(std::begin(m), std::end(m), a_out.viewProj);

			const float light[3]{ 0.45f, 0.55f, 0.70f };
			std::copy(std::begin(light), std::end(light), a_out.lightDir);
			a_out.keyStrength = 0.56f;
			a_out.ambient = 0.28f;
			a_out.eye[0] = a_frame.cx;
			a_out.eye[1] = a_frame.cy + depthHalf * 4.f;
			a_out.eye[2] = a_frame.cz;
		}

		bool Render(ID3D11Device* a_device, ID3D11DeviceContext* a_ctx, Entry& a_entry, std::string& a_error)
		{
			D3D11_TEXTURE2D_DESC colour{};
			colour.Width = kSize;
			colour.Height = kSize;
			colour.MipLevels = 1;
			colour.ArraySize = 1;
			colour.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			colour.SampleDesc.Count = 1;
			colour.Usage = D3D11_USAGE_DEFAULT;
			colour.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			ComPtr<ID3D11RenderTargetView> rtv;
			if (FAILED(a_device->CreateTexture2D(&colour, nullptr, &a_entry.colour)) || FAILED(a_device->CreateRenderTargetView(a_entry.colour.Get(), nullptr, &rtv)) || FAILED(a_device->CreateShaderResourceView(a_entry.colour.Get(), nullptr, &a_entry.view))) {
				a_error = "could not create the target";
				return false;
			}

			// The buffers, built for this draw and dropped after it
			struct Drawn
			{
				ComPtr<ID3D11Buffer> vertices;
				ComPtr<ID3D11Buffer> indices;
				UINT count = 0;
				const Piece* piece = nullptr;
			};
			std::vector<Drawn> drawn;
			std::vector<const Piece*> all;
			const auto frame = FrameOf(a_entry);
			if (a_entry.mannequin) {
				for (const auto& piece : *a_entry.mannequin) {
					all.push_back(&piece);
				}
			}
			for (const auto& piece : a_entry.pieces) {
				all.push_back(&piece);
			}
			for (const auto* piecePointer : all) {
				const auto& piece = *piecePointer;
				Drawn one;
				D3D11_BUFFER_DESC vb{};
				vb.ByteWidth = static_cast<UINT>(piece.vertices.size() * sizeof(Vertex));
				vb.Usage = D3D11_USAGE_IMMUTABLE;
				vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				D3D11_SUBRESOURCE_DATA vdata{ piece.vertices.data(), 0, 0 };
				D3D11_BUFFER_DESC ib{};
				ib.ByteWidth = static_cast<UINT>(piece.indices.size() * sizeof(std::uint16_t));
				ib.Usage = D3D11_USAGE_IMMUTABLE;
				ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
				D3D11_SUBRESOURCE_DATA idata{ piece.indices.data(), 0, 0 };
				if (FAILED(a_device->CreateBuffer(&vb, &vdata, &one.vertices)) || FAILED(a_device->CreateBuffer(&ib, &idata, &one.indices))) {
					a_error = "could not create the buffers";
					return false;
				}
				one.count = static_cast<UINT>(piece.indices.size());
				one.piece = &piece;
				drawn.push_back(std::move(one));
			}

			SavedState saved;
			saved.Take(a_ctx);

			ID3D11RenderTargetView* rtvs[]{ rtv.Get() };
			a_ctx->OMSetRenderTargets(1, rtvs, g_dsv.Get());
			const float clear[4]{ 0.08f, 0.085f, 0.10f, 1.f };
			a_ctx->ClearRenderTargetView(rtv.Get(), clear);
			a_ctx->ClearDepthStencilView(g_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);

			D3D11_VIEWPORT viewport{ 0.f, 0.f, static_cast<float>(kSize), static_cast<float>(kSize), 0.f, 1.f };
			a_ctx->RSSetViewports(1, &viewport);
			a_ctx->RSSetState(g_rasterizer.Get());
			a_ctx->IASetInputLayout(g_layout.Get());
			a_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			a_ctx->VSSetShader(g_vs.Get(), nullptr, 0);
			a_ctx->PSSetShader(g_ps.Get(), nullptr, 0);
			a_ctx->GSSetShader(nullptr, nullptr, 0);
			a_ctx->HSSetShader(nullptr, nullptr, 0);
			a_ctx->DSSetShader(nullptr, nullptr, 0);
			ID3D11SamplerState* samplers[]{ g_sampler.Get() };
			a_ctx->PSSetSamplers(0, 1, samplers);
			ID3D11Buffer* constants[]{ g_constants.Get() };
			a_ctx->VSSetConstantBuffers(0, 1, constants);
			a_ctx->PSSetConstantBuffers(0, 1, constants);

			SceneConstants scene{};
			BuildViewProj(frame, scene);

			// Opaque and alpha-tested pieces first with depth writes, blended ones after with depth tests only
			for (const auto pass : { 0, 1 }) {
				for (const auto& one : drawn) {
					const auto& piece = *one.piece;
					const bool bBlended = piece.bAlphaBlend && !piece.bAlphaTest;
					if ((pass == 0) == bBlended) {
						continue;
					}
					auto* texture = piece.diffuse ? piece.diffuse->rendererTexture : nullptr;
					auto* view = texture ? texture->resourceView : nullptr;

					scene.useTexture = view ? 1.f : 0.f;
					scene.cutoff = piece.bAlphaTest ? piece.cutoff : (bBlended ? 0.02f : -1.f);
					std::copy(std::begin(piece.tint), std::end(piece.tint), scene.tint);
					std::copy(std::begin(piece.glow), std::end(piece.glow), scene.glow);
					D3D11_MAPPED_SUBRESOURCE mapped{};
					if (SUCCEEDED(a_ctx->Map(g_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
						std::memcpy(mapped.pData, &scene, sizeof(scene));
						a_ctx->Unmap(g_constants.Get(), 0);
					}

					ID3D11ShaderResourceView* views[]{ reinterpret_cast<ID3D11ShaderResourceView*>(view) };
					a_ctx->PSSetShaderResources(0, 1, views);
					a_ctx->OMSetBlendState(bBlended ? g_blend.Get() : g_opaque.Get(), nullptr, 0xFFFFFFFF);
					a_ctx->OMSetDepthStencilState(bBlended ? g_depthTest.Get() : g_depthWrite.Get(), 0);

					const UINT stride = sizeof(Vertex);
					const UINT offset = 0;
					ID3D11Buffer* vbs[]{ one.vertices.Get() };
					a_ctx->IASetVertexBuffers(0, 1, vbs, &stride, &offset);
					a_ctx->IASetIndexBuffer(one.indices.Get(), DXGI_FORMAT_R16_UINT, 0);
					a_ctx->DrawIndexed(one.count, 0, 0);
				}
			}

			ID3D11ShaderResourceView* none[]{ nullptr };
			a_ctx->PSSetShaderResources(0, 1, none);
			saved.Restore(a_ctx);
			return true;
		}

		// Whether every texture a scene needs has arrived
		bool TexturesReady(const Entry& a_entry)
		{
			return std::ranges::all_of(a_entry.pieces, [](const Piece& a_piece) {
				return !a_piece.diffuse || (a_piece.diffuse->rendererTexture && a_piece.diffuse->rendererTexture->resourceView);
			});
		}
	}

	std::string MannequinOf(RE::FormID a_part, std::string* a_why)
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = g_mannequinOf.find(a_part);
		if (it == g_mannequinOf.end()) {
			auto decided = DecideMannequin(a_part);
			if (!Scan::Scanner::GetSingleton().GetCatalogue()) {
				// Not remembered: asked before the scan, the answer is only "not yet"
				if (a_why) {
					*a_why = decided.second;
				}
				return decided.first;
			}
			it = g_mannequinOf.emplace(a_part, std::move(decided)).first;
		}
		if (a_why) {
			*a_why = it->second.second;
		}
		return it->second.first;
	}

	namespace
	{
		// The picture under a key, or 0 while it is on its way; the load is queued by whoever asks first
		ImTextureID CardFor(const std::string& a_key, const std::function<void()>& a_load)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			const auto it = g_entries.find(a_key);
			if (it == g_entries.end()) {
				g_entries[a_key] = Entry{};
				a_load();
				return 0;
			}
			return it->second.state == State::kRendered && it->second.view ? reinterpret_cast<ImTextureID>(it->second.view.Get()) : 0;
		}

		bool FailedFor(const std::string& a_key, std::string* a_why)
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			const auto it = g_entries.find(a_key);
			const bool bFailed = it != g_entries.end() && it->second.state == State::kFailed;
			if (bFailed && a_why) {
				*a_why = it->second.error;
			}
			return bFailed;
		}

		void DrawCardOf(ImTextureID a_id, bool a_bFailed, const std::string& a_why, float a_size, bool a_bTooltip);
	}

	ImTextureID Card(RE::FormID a_part)
	{
		if (a_part == 0 || !Scan::Scanner::GetSingleton().GetCatalogue()) {
			return 0;
		}
		return CardFor(PartKey(a_part), [&] { Load(a_part); });
	}

	bool Failed(RE::FormID a_part, std::string* a_why)
	{
		return FailedFor(PartKey(a_part), a_why);
	}

	void DrawCard(RE::FormID a_part, float a_size, bool a_bTooltip)
	{
		const auto id = Card(a_part);
		std::string why;
		const bool bFailed = id == 0 && Failed(a_part, &why);
		DrawCardOf(id, bFailed, why, a_size, a_bTooltip);
	}

	void DrawOverlayCard(const std::string& a_texture, Scan::SkinPart a_part, std::string_view a_uv, float a_size, bool a_bTooltip)
	{
		ImTextureID id = 0;
		bool bFailed = false;
		std::string why;
		if (!a_texture.empty() && Scan::Scanner::GetSingleton().GetCatalogue()) {
			const auto key = OverlayKey(a_texture, a_part, a_uv);
			id = CardFor(key, [&] { LoadOverlay(key, a_texture, a_part, a_uv); });
			bFailed = id == 0 && FailedFor(key, &why);
		}
		DrawCardOf(id, bFailed, why, a_size, a_bTooltip);
	}

	namespace
	{
	void DrawCardOf(ImTextureID id, bool bFailed, const std::string& why, float a_size, bool a_bTooltip)
	{
		const auto origin = ImGui::GetCursorScreenPos();
		auto* draw = ImGui::GetWindowDrawList();
		const ImVec2 end{ origin.x + a_size, origin.y + a_size };
		draw->AddRectFilled(origin, end, IM_COL32(24, 24, 28, 255));

		if (id != 0) {
			ImGui::Image(id, ImVec2{ a_size, a_size });
			if (a_bTooltip && ImGui::IsItemHovered() && ImGui::BeginTooltip()) {
				ImGui::Image(id, ImVec2{ a_size * 3.f, a_size * 3.f });
				ImGui::EndTooltip();
			}
		} else {
			ImGui::Dummy(ImVec2{ a_size, a_size });
			const auto* text = bFailed ? "no picture" : "...";
			const auto width = ImGui::CalcTextSize(text).x;
			draw->AddText(ImVec2{ origin.x + (a_size - width) * 0.5f, origin.y + a_size * 0.5f - 7.f }, ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
			if (bFailed && !why.empty() && ImGui::IsItemHovered()) {
				UICommon::AddTooltip(why.data());
			}
		}
		// The border last: the text colour round a picture, so it greys with the name when the card is greyed; the
		// disabled colour round a placeholder, with nothing else changing
		draw->AddRect(origin, end, ImGui::GetColorU32(id != 0 ? ImGuiCol_Text : ImGuiCol_TextDisabled));
	}
	}

	bool DrawGrid(std::optional<RE::BGSHeadPart::HeadPartType> a_type, std::string_view a_filter, RE::FormID a_current, RE::FormID& a_picked, float a_size, RE::Actor* a_reference)
	{
		const auto catalogue = Scan::Scanner::GetSingleton().GetCatalogue();
		if (!catalogue) {
			UICommon::TextUnformattedDisabled("Waiting for the scan.");
			return false;
		}

		// What the reference actor is, for greying what it cannot wear
		auto* referenceBase = a_reference ? a_reference->GetActorBase() : nullptr;
		auto* referenceRace = a_reference ? a_reference->GetRace() : nullptr;
		const std::string referenceRaceID = referenceRace && referenceRace->GetFormEditorID() ? referenceRace->GetFormEditorID() : "";
		const bool bReferenceFemale = referenceBase && referenceBase->GetSex() == RE::SEX::kFemale;
		const auto wearable = [&](const Scan::HeadPart& a_part) {
			if (!referenceBase || !referenceRace) {
				return true;
			}
			if (bReferenceFemale ? !a_part.bFemale : !a_part.bMale) {
				return false;
			}
			return a_part.races.empty() || std::ranges::find(a_part.races, referenceRaceID) != a_part.races.end();
		};

		// What passes the filters, by plugin then by name, so each plugin's parts sit together
		const auto& slots = Apply::HeadPart::Slots();
		const auto labelOf = [](const Scan::HeadPart& a_part) -> const std::string& { return a_part.editorID.empty() ? a_part.name : a_part.editorID; };
		std::vector<const Scan::HeadPart*> parts;
		for (const auto& part : catalogue->headParts) {
			if (part.bExtraPart || (a_type ? part.type != *a_type : std::ranges::none_of(slots, [&](const Apply::HeadPart::Slot& a_slot) { return a_slot.type == part.type; }))) {
				continue;
			}
			if (!a_filter.empty() && !Utils::ContainsStringIgnoreCase(labelOf(part), a_filter) && !Utils::ContainsStringIgnoreCase(part.name, a_filter) && !Utils::ContainsStringIgnoreCase(part.plugin, a_filter)) {
				continue;
			}
			parts.push_back(&part);
		}
		if (parts.empty()) {
			UICommon::TextUnformattedDisabled("Nothing matches.");
			return false;
		}
		std::ranges::stable_sort(parts, [&](const Scan::HeadPart* a, const Scan::HeadPart* b) {
			return a->plugin != b->plugin ? a->plugin < b->plugin : labelOf(*a) < labelOf(*b);
		});

		const auto& style = ImGui::GetStyle();
		const float cell = a_size + style.ItemSpacing.x;
		const int columns = (std::max)(1, static_cast<int>((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / cell));
		const float lineHeight = ImGui::GetTextLineHeight();
		bool bPicked = false;

		std::string_view plugin;
		int inRow = 0;
		for (const auto* partPointer : parts) {
			const auto& part = *partPointer;
			// A new plugin: its name on a line of its own, and the grid starts over
			if (part.plugin != plugin) {
				plugin = part.plugin;
				inRow = 0;
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, UICommon::PICKER_SECTION_COLOR);
				ImGui::SeparatorText(plugin.data());
				ImGui::PopStyleColor();
			}
			if (inRow != 0) {
				ImGui::SameLine();
			}
			inRow = (inRow + 1) % columns;

			const bool bWearable = wearable(part);
			ImGui::PushID(static_cast<int>(part.formID));
			ImGui::BeginGroup();
			const auto top = ImGui::GetCursorScreenPos();
			if (ImGui::Selectable("##cell", part.formID == a_current, 0, ImVec2(a_size, a_size + lineHeight + style.ItemSpacing.y))) {
				a_picked = part.formID;
				bPicked = true;
			}
			const bool bHovered = ImGui::IsItemHovered();
			ImGui::SetCursorScreenPos(top);
			// Greyed, not gone: a part the selected actor cannot wear is still one an author may be writing for
			if (!bWearable) {
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, style.Alpha * style.DisabledAlpha);
			}
			DrawCard(part.formID, a_size, false);
			UICommon::TextUnformattedEllipsis(labelOf(part).data(), nullptr, a_size);
			if (!bWearable) {
				ImGui::PopStyleVar();
			}
			ImGui::EndGroup();
			if (bHovered && ImGui::BeginTooltip()) {
				if (const auto id = Card(part.formID)) {
					ImGui::Image(id, ImVec2{ a_size * 3.f, a_size * 3.f });
				}
				ImGui::TextUnformatted(labelOf(part).data());
				UICommon::TextUnformattedDisabled(std::format("{} [{:08X}]", part.plugin, part.formID).data());
				if (!bWearable) {
					UICommon::TextUnformattedDisabled("not for the selected actor's race or sex");
				}
				ImGui::EndTooltip();
			}
			ImGui::PopID();
		}
		return bPicked;
	}

	bool PickerButton(std::optional<RE::BGSHeadPart::HeadPartType> a_type, RE::FormID a_current, RE::FormID& a_picked, RE::Actor* a_reference)
	{
		bool bPicked = false;
		const auto& style = ImGui::GetStyle();
		const float card = ImGui::GetTextLineHeight() * 5.f;

		if (ImGui::Button("Pick", ImVec2(UICommon::PickerButtonWidth(), 0.f))) {
			ImGui::OpenPopup("##PartPicker");
		}
		if (ImGui::BeginPopup("##PartPicker")) {
			static std::string search;
			if (ImGui::IsWindowAppearing()) {
				search.clear();
			}
			// Four cards across, five down, and the scrollbar
			const float width = card * 4.f + style.ItemSpacing.x * 3.f + style.ScrollbarSize + style.WindowPadding.x;
			const float height = (card + ImGui::GetTextLineHeight() + style.ItemSpacing.y * 2.f) * 5.f;
			ImGui::SetNextItemWidth(width);
			ImGui::InputTextWithHint("##search", "Search...", &search);
			if (ImGui::BeginChild("##grid", ImVec2(width, height))) {
				if (DrawGrid(a_type, search, a_current, a_picked, card, a_reference)) {
					bPicked = true;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndChild();
			ImGui::EndPopup();
		}
		return bPicked;
	}

	void Advance(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
	{
		if (g_clearRequested.exchange(false)) {
			// The frame that asked has been rendered; the models go back to the main thread, where the game frees renderer data
			std::unordered_map<std::string, Entry> held;
			std::unordered_map<std::string, Mannequin> mannequins;
			{
				std::lock_guard<std::mutex> lock(g_mutex);
				held.swap(g_entries);
				mannequins.swap(g_mannequins);
				g_mannequinOf.clear();
			}
			if (auto* tasks = SKSE::GetTaskInterface()) {
				auto keep = std::make_shared<std::unordered_map<std::string, Entry>>(std::move(held));
				auto keepMannequins = std::make_shared<std::unordered_map<std::string, Mannequin>>(std::move(mannequins));
				tasks->AddTask([keep, keepMannequins]() {
					keep->clear();
					keepMannequins->clear();
				});
			}
			return;
		}

		if (!a_device || !a_context) {
			return;
		}

		// One extracted entry per frame: the first whose textures have arrived, else the one that has waited longest
		std::string key;
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			std::string waitedOut;
			for (auto& [id, entry] : g_entries) {
				if (entry.state != State::kExtracted) {
					continue;
				}
				if (TexturesReady(entry)) {
					key = id;
					break;
				}
				if (entry.waited++ >= kTextureWaitFrames && waitedOut.empty()) {
					waitedOut = id;
				}
			}
			if (key.empty()) {
				key = waitedOut;
			}
		}
		if (key.empty()) {
			return;
		}
		if (!EnsurePipeline(a_device)) {
			std::lock_guard<std::mutex> lock(g_mutex);
			if (auto it = g_entries.find(key); it != g_entries.end()) {
				it->second.state = State::kFailed;
				it->second.error = "no pipeline";
			}
			return;
		}

		std::lock_guard<std::mutex> lock(g_mutex);
		auto it = g_entries.find(key);
		if (it == g_entries.end()) {
			return;
		}
		std::string error;
		if (Render(a_device, a_context, it->second, error)) {
			it->second.state = State::kRendered;
			it->second.pieces.clear();  // the CPU copies are not needed again
			it->second.mannequin.reset();
		} else {
			it->second.state = State::kFailed;
			it->second.error = error;
		}
	}

	void Clear()
	{
		g_clearRequested.store(true);
	}
}
