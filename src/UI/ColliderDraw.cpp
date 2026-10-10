#include "ColliderDraw.h"

#include "Settings.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <map>
#include <mutex>
#include <numbers>

namespace UI::ColliderDraw
{
	namespace
	{
		using Microsoft::WRL::ComPtr;
		using DirectX::SimpleMath::Matrix;
		using DirectX::SimpleMath::Vector4;

		std::mutex g_mutex;
		std::map<std::string, std::vector<Collider>, std::less<>> g_colliders;

		// The lines arrive projected; the scene's depth, read at each pixel, says what hides them
		constexpr const char kShader[] = R"(
cbuffer Frame : register(b0)
{
    float2 TargetSize; float2 DepthScale;
    float  ReversedDepth; float3 Pad;
};

Texture2D<float> SceneDepth : register(t0);

struct VSIn  { float4 pos : POSITION; float4 colour : COLOR; };
struct VSOut { float4 pos : SV_Position; float4 colour : COLOR; };

VSOut VS(VSIn i)
{
    VSOut o;
    o.pos = i.pos;
    o.colour = i.colour;
    return o;
}

float4 PS(VSOut i) : SV_Target
{
    uint w, h;
    SceneDepth.GetDimensions(w, h);
    float2 at = i.pos.xy / TargetSize * DepthScale * float2(w, h);
    float scene = SceneDepth.Load(int3(at, 0));
    bool hidden = ReversedDepth > 0.5 ? i.pos.z < scene - 0.00001 : i.pos.z > scene + 0.00001;
    return float4(i.colour.rgb, hidden ? 0.22 : 1.0);
}
)";

		struct FrameConstants
		{
			float targetSize[2];
			float depthScale[2];
			float reversedDepth;
			float pad[3];
		};

		struct Vertex
		{
			float x, y, z, w;
			float r, g, b, a;
		};

		ComPtr<ID3D11VertexShader> g_vs;
		ComPtr<ID3D11PixelShader> g_ps;
		ComPtr<ID3D11InputLayout> g_layout;
		ComPtr<ID3D11Buffer> g_constants;
		ComPtr<ID3D11Buffer> g_vertices;
		UINT g_vertexCapacity = 0;
		ComPtr<ID3D11RasterizerState> g_rasterizer;
		ComPtr<ID3D11BlendState> g_blend;
		ComPtr<ID3D11DepthStencilState> g_noDepth;
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
			if (FAILED(::D3DCompile(kShader, sizeof(kShader) - 1, "ColliderDraw", nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vs, &errors)) ||
				FAILED(::D3DCompile(kShader, sizeof(kShader) - 1, "ColliderDraw", nullptr, nullptr, "PS", "ps_5_0", 0, 0, &ps, &errors))) {
				g_bPipelineFailed = true;
				logger::error("colliders: shader: {}", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
				return false;
			}
			bool ok = SUCCEEDED(a_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs)) &&
			          SUCCEEDED(a_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &g_ps));
			const D3D11_INPUT_ELEMENT_DESC layout[]{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};
			ok = ok && SUCCEEDED(a_device->CreateInputLayout(layout, 2, vs->GetBufferPointer(), vs->GetBufferSize(), &g_layout));
			D3D11_BUFFER_DESC constants{};
			constants.ByteWidth = sizeof(FrameConstants);
			constants.Usage = D3D11_USAGE_DYNAMIC;
			constants.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			constants.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			ok = ok && SUCCEEDED(a_device->CreateBuffer(&constants, nullptr, &g_constants));
			D3D11_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_NONE;
			rasterizer.DepthClipEnable = FALSE;
			ok = ok && SUCCEEDED(a_device->CreateRasterizerState(&rasterizer, &g_rasterizer));
			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].BlendEnable = TRUE;
			blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO;
			blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
			blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			ok = ok && SUCCEEDED(a_device->CreateBlendState(&blend, &g_blend));
			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = FALSE;
			ok = ok && SUCCEEDED(a_device->CreateDepthStencilState(&depth, &g_noDepth));
			if (!ok) {
				g_bPipelineFailed = true;
				g_vs.Reset();
				logger::error("colliders: the pipeline could not be built");
			}
			return ok;
		}

		// The game's camera for this frame: its unjittered view-projection, the offset positions are taken from, and which
		// way round it multiplies and stores depth
		struct Camera
		{
			Matrix viewProj;
			RE::NiPoint3 origin;
			bool bTransposed = false;
			bool bReversedDepth = false;
		};

		std::optional<Camera> CameraOf()
		{
			auto* camera = RE::Main::WorldRootCamera();
			auto* state = RE::BSGraphics::State::GetSingleton();
			if (!camera || !state) {
				return std::nullopt;
			}
			// The cache's entries read by their SE/AE layout, which a build for every runtime does not declare
			constexpr std::size_t kEntrySize = 0x290;
			constexpr std::size_t kViewData = 0x10;
			constexpr std::size_t kPosAdjust = 0x260;
			constexpr std::size_t kUseJitter = 0x284;
			const auto& cache = state->GetRuntimeData().cameraDataCacheA;
			const auto* entries = reinterpret_cast<const std::byte*>(cache.data());
			const std::byte* entry = nullptr;
			for (std::uint32_t i = 0; entries && i < cache.size(); ++i) {
				const auto* at = entries + i * kEntrySize;
				if (*reinterpret_cast<RE::NiCamera* const*>(at) == camera && (!entry || !*reinterpret_cast<const bool*>(at + kUseJitter))) {
					entry = at;
				}
			}
			if (!entry) {
				return std::nullopt;
			}
			const auto& view = *reinterpret_cast<const RE::BSGraphics::ViewData*>(entry + kViewData);
			Camera result{ view.viewProjMatrixUnjittered, *reinterpret_cast<const RE::NiPoint3*>(entry + kPosAdjust) };
			const auto& forward = view.viewForward;
			const auto at = [&](float a_distance, const Matrix& a_matrix) {
				const RE::NiPoint3 p = camera->world.translate - result.origin;
				return Vector4::Transform(Vector4(p.x + forward.x * a_distance, p.y + forward.y * a_distance, p.z + forward.z * a_distance, 1.f), a_matrix);
			};
			auto ahead = at(100.f, result.viewProj);
			if (ahead.w <= 0.f || std::fabs(ahead.x / ahead.w) > 1.f || std::fabs(ahead.y / ahead.w) > 1.f) {
				result.viewProj = result.viewProj.Transpose();
				result.bTransposed = true;
				ahead = at(100.f, result.viewProj);
			}
			const auto beyond = at(2000.f, result.viewProj);
			result.bReversedDepth = ahead.w > 0.f && beyond.w > 0.f && ahead.z / ahead.w > beyond.z / beyond.w;
			static bool bLogged = false;
			if (!bLogged) {
				bLogged = true;
				logger::info("colliders: camera matrix {}, depth {}, a point ahead at depth {:.4f}", result.bTransposed ? "transposed" : "as stored", result.bReversedDepth ? "reversed" : "standard", ahead.w > 0.f ? ahead.z / ahead.w : -1.f);
			}
			return result;
		}

		// A collider's world ends and radii as CBPC places them each frame
		struct Placed
		{
			RE::NiPoint3 end1;
			RE::NiPoint3 end2;
			float radius1 = 0.f;
			float radius2 = 0.f;
			RE::NiMatrix3 rotation;
		};

		std::optional<Placed> Place(const Collider& a_collider)
		{
			const auto actor = a_collider.actor.get();
			auto* root = actor ? actor->Get3D(false) : nullptr;
			auto* node = root ? root->GetObjectByName(a_collider.node.c_str()) : nullptr;
			if (!node) {
				return std::nullopt;
			}
			const float base = actor->GetScale();
			const float scale = base * (1.f - (1.f - node->world.scale / (base > 0.f ? base : 1.f)) * a_collider.scaleWeight);
			Placed placed;
			placed.rotation = node->world.rotate;
			placed.end1 = node->world.translate + node->world.rotate * (a_collider.end1 * scale);
			placed.radius1 = a_collider.radius1 * scale;
			if (a_collider.bCapsule && !a_collider.endNode.empty()) {
				auto* endBone = root->GetObjectByName(a_collider.endNode.c_str());
				if (!endBone) {
					return std::nullopt;
				}
				placed.end2 = endBone->world.translate + endBone->world.rotate * (a_collider.end2 * scale);
				placed.radius2 = a_collider.radius2 * scale;
			} else if (a_collider.bCapsule) {
				placed.end2 = node->world.translate + node->world.rotate * (a_collider.end2 * scale);
				placed.radius2 = a_collider.radius2 * scale;
			} else {
				placed.end2 = placed.end1;
				placed.radius2 = placed.radius1;
			}
			return placed;
		}

		struct Lines
		{
			const Camera& camera;
			std::vector<Vertex>& out;
			float colour[4];

			void Add(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
			{
				const auto project = [&](const RE::NiPoint3& a_point) {
					const auto p = a_point - camera.origin;
					return Vector4::Transform(Vector4(p.x, p.y, p.z, 1.f), camera.viewProj);
				};
				const auto a = project(a_from);
				const auto b = project(a_to);
				// Behind the camera: left out rather than clipped
				if (a.w <= 1.f || b.w <= 1.f) {
					return;
				}
				out.push_back({ a.x / a.w, a.y / a.w, a.z / a.w, 1.f, colour[0], colour[1], colour[2], colour[3] });
				out.push_back({ b.x / b.w, b.y / b.w, b.z / b.w, 1.f, colour[0], colour[1], colour[2], colour[3] });
			}

			// A circle around a centre in the plane of two directions
			void Circle(const RE::NiPoint3& a_centre, const RE::NiPoint3& a_u, const RE::NiPoint3& a_v, float a_radius, float a_from = 0.f, float a_to = 2.f * std::numbers::pi_v<float>)
			{
				constexpr int kSegments = 24;
				RE::NiPoint3 previous = a_centre + (a_u * std::cos(a_from) + a_v * std::sin(a_from)) * a_radius;
				for (int i = 1; i <= kSegments; ++i) {
					const float t = a_from + (a_to - a_from) * static_cast<float>(i) / kSegments;
					const RE::NiPoint3 next = a_centre + (a_u * std::cos(t) + a_v * std::sin(t)) * a_radius;
					Add(previous, next);
					previous = next;
				}
			}
		};

		RE::NiPoint3 Column(const RE::NiMatrix3& a_matrix, int a_column)
		{
			return { a_matrix.entry[0][a_column], a_matrix.entry[1][a_column], a_matrix.entry[2][a_column] };
		}

		void Outline(Lines& a_lines, const Placed& a_placed, bool a_bCapsule)
		{
			if (!a_bCapsule) {
				const auto x = Column(a_placed.rotation, 0);
				const auto y = Column(a_placed.rotation, 1);
				const auto z = Column(a_placed.rotation, 2);
				a_lines.Circle(a_placed.end1, x, y, a_placed.radius1);
				a_lines.Circle(a_placed.end1, y, z, a_placed.radius1);
				a_lines.Circle(a_placed.end1, z, x, a_placed.radius1);
				return;
			}
			// A capsule: a ring at each end, half circles closing them, and four lines between
			auto axis = a_placed.end2 - a_placed.end1;
			const float length = axis.Length();
			axis = length > 0.0001f ? axis / length : Column(a_placed.rotation, 2);
			auto side = Column(a_placed.rotation, 0);
			if (std::fabs(side.Dot(axis)) > 0.9f) {
				side = Column(a_placed.rotation, 1);
			}
			const auto u = (side - axis * side.Dot(axis)) / (side - axis * side.Dot(axis)).Length();
			const auto v = axis.Cross(u);
			constexpr float kPi = std::numbers::pi_v<float>;
			a_lines.Circle(a_placed.end1, u, v, a_placed.radius1);
			a_lines.Circle(a_placed.end2, u, v, a_placed.radius2);
			a_lines.Circle(a_placed.end1, u, axis, a_placed.radius1, kPi, 2.f * kPi);
			a_lines.Circle(a_placed.end1, v, axis, a_placed.radius1, kPi, 2.f * kPi);
			a_lines.Circle(a_placed.end2, u, axis, a_placed.radius2, 0.f, kPi);
			a_lines.Circle(a_placed.end2, v, axis, a_placed.radius2, 0.f, kPi);
			for (const auto& d : { u, u * -1.f, v, v * -1.f }) {
				a_lines.Add(a_placed.end1 + d * a_placed.radius1, a_placed.end2 + d * a_placed.radius2);
			}
		}
	}

	void Set(std::string_view a_owner, std::vector<Collider> a_colliders)
	{
		std::scoped_lock guard(g_mutex);
		if (a_colliders.empty()) {
			if (const auto it = g_colliders.find(a_owner); it != g_colliders.end()) {
				g_colliders.erase(it);
			}
			return;
		}
		g_colliders.insert_or_assign(std::string(a_owner), std::move(a_colliders));
	}

	void Draw(ID3D11Device* a_device, ID3D11DeviceContext* a_context)
	{
		if (!Settings::bDrawCBPCColliders || !a_device || !a_context) {
			return;
		}
		std::vector<Collider> colliders;
		{
			std::scoped_lock guard(g_mutex);
			for (const auto& [owner, list] : g_colliders) {
				colliders.insert(colliders.end(), list.begin(), list.end());
			}
		}
		if (colliders.empty()) {
			return;
		}
		const auto camera = CameraOf();
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		auto* state = RE::BSGraphics::State::GetSingleton();
		if (!camera || !renderer || !state || !EnsurePipeline(a_device)) {
			return;
		}
		auto* depth = reinterpret_cast<ID3D11ShaderResourceView*>(renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kMAIN].depthSRV);
		if (!depth) {
			return;
		}

		std::vector<Vertex> vertices;
		for (const auto& collider : colliders) {
			const auto placed = Place(collider);
			if (!placed) {
				continue;
			}
			Lines lines{ *camera, vertices, {} };
			const float pushed[4]{ 1.f, 0.55f, 0.1f, 1.f };
			const float pushing[4]{ 0.2f, 0.85f, 1.f, 1.f };
			const float bone[4]{ 1.f, 1.f, 1.f, 1.f };
			std::copy_n(collider.bBone ? bone : collider.bAffected ? pushed : pushing, 4, lines.colour);
			Outline(lines, *placed, collider.bCapsule);
		}
		if (vertices.empty()) {
			return;
		}
		if (vertices.size() > g_vertexCapacity) {
			g_vertices.Reset();
			g_vertexCapacity = static_cast<UINT>(vertices.size() * 2);
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = g_vertexCapacity * sizeof(Vertex);
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(a_device->CreateBuffer(&desc, nullptr, &g_vertices))) {
				g_vertexCapacity = 0;
				return;
			}
		}

		// The pass draws over the frame the game just finished, its state put back after
		ComPtr<ID3D11RenderTargetView> rtv;
		ComPtr<ID3D11DepthStencilView> dsv;
		a_context->OMGetRenderTargets(1, &rtv, &dsv);
		if (!rtv) {
			return;
		}
		ComPtr<ID3D11Resource> target;
		rtv->GetResource(&target);
		ComPtr<ID3D11Texture2D> targetTexture;
		if (FAILED(target.As(&targetTexture))) {
			return;
		}
		D3D11_TEXTURE2D_DESC targetDesc{};
		targetTexture->GetDesc(&targetDesc);

		D3D11_VIEWPORT savedViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
		UINT savedViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
		a_context->RSGetViewports(&savedViewportCount, savedViewports);
		ComPtr<ID3D11RasterizerState> savedRasterizer;
		a_context->RSGetState(&savedRasterizer);
		ComPtr<ID3D11BlendState> savedBlend;
		float savedBlendFactor[4]{};
		UINT savedSampleMask = 0;
		a_context->OMGetBlendState(&savedBlend, savedBlendFactor, &savedSampleMask);
		ComPtr<ID3D11DepthStencilState> savedDepth;
		UINT savedStencilRef = 0;
		a_context->OMGetDepthStencilState(&savedDepth, &savedStencilRef);
		ComPtr<ID3D11InputLayout> savedLayout;
		a_context->IAGetInputLayout(&savedLayout);
		ComPtr<ID3D11Buffer> savedVertices;
		UINT savedStride = 0, savedOffset = 0;
		a_context->IAGetVertexBuffers(0, 1, &savedVertices, &savedStride, &savedOffset);
		D3D11_PRIMITIVE_TOPOLOGY savedTopology{};
		a_context->IAGetPrimitiveTopology(&savedTopology);
		ComPtr<ID3D11VertexShader> savedVs;
		ComPtr<ID3D11PixelShader> savedPs;
		ComPtr<ID3D11GeometryShader> savedGs;
		a_context->VSGetShader(&savedVs, nullptr, nullptr);
		a_context->PSGetShader(&savedPs, nullptr, nullptr);
		a_context->GSGetShader(&savedGs, nullptr, nullptr);
		ComPtr<ID3D11Buffer> savedPsConstants;
		a_context->PSGetConstantBuffers(0, 1, &savedPsConstants);
		ComPtr<ID3D11ShaderResourceView> savedResource;
		a_context->PSGetShaderResources(0, 1, &savedResource);

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (SUCCEEDED(a_context->Map(g_vertices.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
			std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(Vertex));
			a_context->Unmap(g_vertices.Get(), 0);
		}
		const auto& runtime = state->GetRuntimeData();
		FrameConstants frame{ { static_cast<float>(targetDesc.Width), static_cast<float>(targetDesc.Height) }, { runtime.dynamicResolutionWidthRatio, runtime.dynamicResolutionHeightRatio }, camera->bReversedDepth ? 1.f : 0.f, {} };
		if (SUCCEEDED(a_context->Map(g_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
			std::memcpy(mapped.pData, &frame, sizeof(frame));
			a_context->Unmap(g_constants.Get(), 0);
		}

		ID3D11RenderTargetView* rtvs[]{ rtv.Get() };
		a_context->OMSetRenderTargets(1, rtvs, nullptr);
		D3D11_VIEWPORT viewport{ 0.f, 0.f, static_cast<float>(targetDesc.Width), static_cast<float>(targetDesc.Height), 0.f, 1.f };
		a_context->RSSetViewports(1, &viewport);
		a_context->RSSetState(g_rasterizer.Get());
		a_context->OMSetBlendState(g_blend.Get(), nullptr, 0xFFFFFFFF);
		a_context->OMSetDepthStencilState(g_noDepth.Get(), 0);
		a_context->IASetInputLayout(g_layout.Get());
		const UINT stride = sizeof(Vertex), offset = 0;
		ID3D11Buffer* buffers[]{ g_vertices.Get() };
		a_context->IASetVertexBuffers(0, 1, buffers, &stride, &offset);
		a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
		a_context->VSSetShader(g_vs.Get(), nullptr, 0);
		a_context->PSSetShader(g_ps.Get(), nullptr, 0);
		a_context->GSSetShader(nullptr, nullptr, 0);
		ID3D11Buffer* constants[]{ g_constants.Get() };
		a_context->PSSetConstantBuffers(0, 1, constants);
		ID3D11ShaderResourceView* resources[]{ depth };
		a_context->PSSetShaderResources(0, 1, resources);
		a_context->Draw(static_cast<UINT>(vertices.size()), 0);

		ID3D11ShaderResourceView* noResource[]{ savedResource.Get() };
		a_context->PSSetShaderResources(0, 1, noResource);
		ID3D11Buffer* savedConstants[]{ savedPsConstants.Get() };
		a_context->PSSetConstantBuffers(0, 1, savedConstants);
		a_context->VSSetShader(savedVs.Get(), nullptr, 0);
		a_context->PSSetShader(savedPs.Get(), nullptr, 0);
		a_context->GSSetShader(savedGs.Get(), nullptr, 0);
		a_context->IASetPrimitiveTopology(savedTopology);
		ID3D11Buffer* savedBuffers[]{ savedVertices.Get() };
		a_context->IASetVertexBuffers(0, 1, savedBuffers, &savedStride, &savedOffset);
		a_context->IASetInputLayout(savedLayout.Get());
		a_context->OMSetDepthStencilState(savedDepth.Get(), savedStencilRef);
		a_context->OMSetBlendState(savedBlend.Get(), savedBlendFactor, savedSampleMask);
		a_context->RSSetState(savedRasterizer.Get());
		a_context->RSSetViewports(savedViewportCount, savedViewports);
		a_context->OMSetRenderTargets(1, rtvs, dsv.Get());
	}
}
