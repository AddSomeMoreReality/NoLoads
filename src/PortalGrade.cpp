#include "PortalState.h"

#include <d3dcompiler.h>

// T12: the portal's own grading (docs/tasks/results/T12.md). The pass draws the interior's lit HDR picture, but
// the game does more to a frame than draw it, and the quad then goes through the street's frame:
// - layer 1, Composite: what ISSAOComposite does to the game's frame after its opaque shapes, before its
//   effects (vanilla bytecode: fog lerp, x FogNearColor.w = BSShaderManager::State::invFrameBufferRange, then
//   mad_sat). Our pass has no composite: the interior lacked its haze and stood brighter than inside;
// - layer 2, Grade: the interior's look (ISHDRTonemapBlendCinematic with the interior's image space: Reinhard
//   with a white point, saturation, tint, brightness, contrast around the average luminance) carried under the
//   street's, which grades the quad afterwards: x' = T_street^-1(T_interior(x)). Before the street's image
//   space the quad also goes through the street's fog and composite (0.8.25 snapshots: 0.62 x + a blue haze):
//   that is undone too. Bloom is neither applied nor undone.
// Eye adaptation: the HDR blend reads a 1x1 target (t2): x the slowly, y the quickly adapted average luminance
// of the frame, exposure y / x (1 at rest), contrast pivots on x. Two such targets alternate by AE#415054.
namespace Portal::detail
{
	struct GradeConstants
	{
		float fogNear[4];       // composite: the interior's fog near colour; w: inverse frame buffer range
		float fogFar[4];        // composite: far colour; w: 1 = fog on
		float fogParam[4];      // composite: near/(far-near), 1/(far-near), power, clamp
		float depthRange[4];    // composite: the pass camera's near and far plane
		float srcParam[4];      // grade: the interior's p, average luminance, exposure
		float srcCine[4];       // saturation, brightness, contrast, tint amount
		float srcTint[4];       // tint colour
		float dstParam[4];      // grade: the street's p, average luminance without the adaptation texture, 1 = texture bound
		float dstCine[4];
		float dstTint[4];
		float quadFogNear[4];   // grade: the street's fog near colour; w: inverse frame buffer range
		float quadFogFar[4];    // far colour; w: what the quad goes through: 0 nothing, 1 the effect's fog, 2 and the composite
		float quadFogParam[4];
		float doorOrigin[4];    // grade: door centre - eye, exterior coordinates
		float doorRight[4];     // door right * width
		float doorUp[4];        // door up * height
		float uvRect[4];        // u0, v0, u1 - u0, v1 - v0 of the door the viewport shows
		float viewport[4];      // width, height
	};
	static_assert(sizeof(GradeConstants) == 18 * 16);

	// Compiled at run time by d3dcompiler_47.dll (Windows 10 and later).
	constexpr std::string_view kShaderSource = R"(
static const float3 kLuminance = float3(0.2125, 0.7154, 0.0721);

cbuffer Constants : register(b0)
{
	float4 fogNear;
	float4 fogFar;
	float4 fogParam;
	float4 depthRange;
	float4 srcParam;
	float4 srcCine;
	float4 srcTint;
	float4 dstParam;
	float4 dstCine;
	float4 dstTint;
	float4 quadFogNear;
	float4 quadFogFar;
	float4 quadFogParam;
	float4 doorOrigin;
	float4 doorRight;
	float4 doorUp;
	float4 uvRect;
	float4 viewport;
};

Texture2D<float4> colorTex : register(t0);
Texture2D<float>  depthTex : register(t1);
Texture2D<float4> adaptTex : register(t2);
SamplerState      linearClamp : register(s0);

float4 VSMain(uint id : SV_VertexID) : SV_Position
{
	float2 uv = float2((id << 1) & 2, id & 2);
	return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

float FogFactor(float distance, float4 param)
{
	return min(param.w, pow(saturate(distance * param.y - param.x), max(param.z, 0.001)));
}

float4 PSComposite(float4 pos : SV_Position) : SV_Target
{
	int3   p = int3(pos.xy, 0);
	float4 c = colorTex.Load(p);
	float  d = depthTex.Load(p);
	if (d >= 0.999999) {
		return c;
	}
	float3 rgb = c.rgb;
	if (fogFar.w > 0.5) {
		float z = depthRange.x * depthRange.y / max(depthRange.y - d * (depthRange.y - depthRange.x), 0.001);
		float f = FogFactor(z, fogParam);
		rgb = lerp(rgb, lerp(fogNear.rgb, fogFar.rgb, f), f);
	}
	return float4(saturate(fogNear.w * rgb), c.a);
}

float3 Look(float3 c, float p, float average, float exposure, float4 cine, float3 tint)
{
	float  luminance = max(dot(c, kLuminance), 0.00001);
	float  le = luminance * exposure;
	float  t = le * (le * p + 1) / (le + 1);
	float3 o = c * (t / luminance);
	float  lb = dot(o, kLuminance);
	float3 s = lb + cine.x * (o - lb);
	s = lerp(s, lb * tint, cine.w);
	s = average + cine.z * (cine.y * s - average);
	return saturate(s);
}

float3 LookInverse(float3 y, float p, float average, float exposure, float4 cine, float3 tint)
{
	float3 s = ((y - average) / max(cine.z, 0.001) + average) / max(cine.y, 0.001);
	float  amount = min(cine.w, 0.98);
	float  lb = dot(s, kLuminance) / max(1 - amount + amount * dot(tint, kLuminance), 0.001);
	float3 u = (s - amount * lb * tint) / (1 - amount);
	float3 o = max(lb + (u - lb) / max(cine.x, 0.02), 0);
	float  t = max(dot(o, kLuminance), 0.000001);
	float  q = max(p, 0.0001);
	float  le = ((t - 1) + sqrt((1 - t) * (1 - t) + 4 * q * t)) / (2 * q);
	return o * (le / max(exposure, 0.0001) / t);
}

float4 PSGrade(float4 pos : SV_Position) : SV_Target
{
	float4 c = colorTex.Load(int3(pos.xy, 0));
	float  average = dstParam.y;
	float  exposure = 1;
	if (dstParam.z > 0.5) {
		float2 a = adaptTex.SampleLevel(linearClamp, float2(0.5, 0.5), 0).xy;
		if (a.x > 0.00001 && a.y > 0.00001) {
			average = min(a.x, 16);
			exposure = clamp(a.y / a.x, 0.25, 4);  // a wrong target must not blow the picture up
		}
	}
	float3 y = Look(max(c.rgb, 0), srcParam.x, srcParam.y, srcParam.z, srcCine, srcTint.rgb);
	float3 x = LookInverse(y, dstParam.x, average, exposure, dstCine, dstTint.rgb);
	if (quadFogFar.w > 0.5) {
		float2 uv = uvRect.xy + pos.xy / viewport.xy * uvRect.zw;
		float3 v = doorOrigin.xyz + doorRight.xyz * (uv.x - 0.5) + doorUp.xyz * (0.5 - uv.y);
		float  f = FogFactor(length(v), quadFogParam);
		float3 fogColor = lerp(quadFogNear.rgb, quadFogFar.rgb, f);
		float  a = quadFogNear.w * (1 - f);
		float3 b = quadFogNear.w * f * fogColor;
		if (quadFogFar.w > 1.5) {
			b = b * (1 + a);
			a = a * a;
		}
		x = max((x - b) / max(a, 0.05), 0);
	}
	return float4(x, 1);
}
)";

	struct GradeResources
	{
		ID3D11VertexShader*      vs{ nullptr };
		ID3D11PixelShader*       composite{ nullptr };
		ID3D11PixelShader*       grade{ nullptr };
		ID3D11Buffer*            constants{ nullptr };
		ID3D11SamplerState*      linear{ nullptr };
		ID3D11BlendState*        blend{ nullptr };
		ID3D11DepthStencilState* depth{ nullptr };
		ID3D11RasterizerState*   raster{ nullptr };
	};

	// The image space's parameters as ImageSpaceEffectHDR::UpdateParams (AE#107185) gives them to the blend.
	struct ToneLook
	{
		float            p{ 1.0f };  // 1 / (fReinhardWhiteScale * white)^2
		float            saturation{ 1.0f };
		float            brightness{ 1.0f };
		float            contrast{ 1.0f };
		float            tintAmount{ 0.0f };
		RE::NiColor      tint{};
		std::string_view source{ "neutral" };
	};

	struct DisplaySettings
	{
		RE::Setting* whiteScale{ nullptr };       // fReinhardWhiteScale:Display (1.1)
		RE::Setting* filmic{ nullptr };           // bUseFilmicCurve:Display (0)
		RE::Setting* saturationBoost{ nullptr };  // fGlobalSaturationBoost:Display (0)
		RE::Setting* brightnessBoost{ nullptr };  // fGlobalBrightnessBoost:Display (0)
		RE::Setting* contrastBoost{ nullptr };    // fGlobalContrastBoost:Display (0)
	};

	struct InteriorAdapt
	{
		RE::FormID cell{ 0 };
		float      slow{ 0.0f };  // x: the contrast pivot
		float      fast{ 0.0f };
	};

	GradeResources               g_grade;
	DisplaySettings              g_display;
	std::string                  g_gradeFailure;  // resources or shaders failed once: no grading this session
	std::string                  g_gradeSkip;     // why the last grade did not draw
	std::string                  g_gradeTraceWhy;
	bool                         g_gradeValid{ false };  // the graded texture holds the last pass
	bool                         g_proxyD3D11{ false };  // ENB's d3d11.dll: its own tone mapping, layer 2 off
	bool                         g_communityShaders{ false };
	std::uint64_t                g_composites{ 0 };
	std::uint64_t                g_grades{ 0 };
	GradeConstants               g_lastConstants{};
	RE::TESImageSpace*           g_interiorImageSpace{ nullptr };  // the door cell's image space (ExtraCellImageSpace)
	ToneLook                     g_lastSource;
	ToneLook                     g_lastView;
	std::string_view             g_lastFogSource{ "none" };
	bool                         g_ldrDumpRequested{ false };
	bool                         g_adaptDumpRequested{ false };
	std::uint32_t                g_ldrDumps{ 0 };
	bool                         g_adaptSearched{ false };
	std::vector<std::uint32_t>   g_adaptCandidates;  // render targets of at most 2 x 2 pixels
	std::int32_t                 g_adaptBase{ -1 };   // the first of the two that alternate (the other is base + 2)
	std::optional<InteriorAdapt> g_interiorAdapt;
	ID3D11Texture2D*             g_adaptStaging{ nullptr };
	DXGI_FORMAT                  g_adaptStagingFormat{ DXGI_FORMAT_UNKNOWN };
	std::uint64_t                g_adaptPendingFrame{ 0 };  // 0: no copy waits
	RE::FormID                   g_adaptPendingCell{ 0 };

	constexpr float kDefaultAverage = 0.25f;  // the contrast pivot when no adaptation is known

	template <class T>
	void ReleaseCom(T*& a_object)
	{
		if (a_object) {
			a_object->Release();
			a_object = nullptr;
		}
	}

	// Everything Composite and Grade change in the device context, put back exactly: Composite runs inside the
	// game's SubmitAccumulator, whose state cache must stay true.
	struct ContextState
	{
		static constexpr UINT kInstances = 256;
		static constexpr UINT kViews = 3;

		ID3D11RenderTargetView*   rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
		ID3D11DepthStencilView*   dsv{ nullptr };
		UINT                      viewportCount{ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE };
		D3D11_VIEWPORT            viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
		UINT                      scissorCount{ D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE };
		D3D11_RECT                scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
		ID3D11RasterizerState*    raster{ nullptr };
		ID3D11BlendState*         blend{ nullptr };
		FLOAT                     blendFactor[4]{};
		UINT                      sampleMask{ 0 };
		ID3D11DepthStencilState*  depth{ nullptr };
		UINT                      stencilRef{ 0 };
		ID3D11InputLayout*        layout{ nullptr };
		D3D11_PRIMITIVE_TOPOLOGY  topology{ D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED };
		ID3D11Buffer*             vertexBuffer{ nullptr };
		UINT                      stride{ 0 };
		UINT                      offset{ 0 };
		ID3D11Buffer*             indexBuffer{ nullptr };
		DXGI_FORMAT               indexFormat{ DXGI_FORMAT_UNKNOWN };
		UINT                      indexOffset{ 0 };
		ID3D11VertexShader*       vs{ nullptr };
		ID3D11ClassInstance*      vsInstances[kInstances]{};
		UINT                      vsInstanceCount{ kInstances };
		ID3D11PixelShader*        ps{ nullptr };
		ID3D11ClassInstance*      psInstances[kInstances]{};
		UINT                      psInstanceCount{ kInstances };
		ID3D11GeometryShader*     gs{ nullptr };
		ID3D11ClassInstance*      gsInstances[kInstances]{};
		UINT                      gsInstanceCount{ kInstances };
		ID3D11HullShader*         hs{ nullptr };
		ID3D11ClassInstance*      hsInstances[kInstances]{};
		UINT                      hsInstanceCount{ kInstances };
		ID3D11DomainShader*       ds{ nullptr };
		ID3D11ClassInstance*      dsInstances[kInstances]{};
		UINT                      dsInstanceCount{ kInstances };
		ID3D11Buffer*             vsConstants{ nullptr };
		ID3D11Buffer*             psConstants{ nullptr };
		ID3D11ShaderResourceView* psViews[kViews]{};
		ID3D11SamplerState*       psSampler{ nullptr };

		void Save(ID3D11DeviceContext* a_context)
		{
			// in: the room in the arrays; out: how many were bound (the last Save left those)
			viewportCount = scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
			vsInstanceCount = psInstanceCount = gsInstanceCount = hsInstanceCount = dsInstanceCount = kInstances;
			a_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
			a_context->RSGetViewports(&viewportCount, viewports);
			a_context->RSGetScissorRects(&scissorCount, scissors);
			a_context->RSGetState(&raster);
			a_context->OMGetBlendState(&blend, blendFactor, &sampleMask);
			a_context->OMGetDepthStencilState(&depth, &stencilRef);
			a_context->IAGetInputLayout(&layout);
			a_context->IAGetPrimitiveTopology(&topology);
			a_context->IAGetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
			a_context->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);
			a_context->VSGetShader(&vs, vsInstances, &vsInstanceCount);
			a_context->PSGetShader(&ps, psInstances, &psInstanceCount);
			a_context->GSGetShader(&gs, gsInstances, &gsInstanceCount);
			a_context->HSGetShader(&hs, hsInstances, &hsInstanceCount);
			a_context->DSGetShader(&ds, dsInstances, &dsInstanceCount);
			a_context->VSGetConstantBuffers(0, 1, &vsConstants);
			a_context->PSGetConstantBuffers(0, 1, &psConstants);
			a_context->PSGetShaderResources(0, kViews, psViews);
			a_context->PSGetSamplers(0, 1, &psSampler);
		}

		void Restore(ID3D11DeviceContext* a_context)
		{
			a_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv);
			a_context->RSSetViewports(viewportCount, viewports);
			a_context->RSSetScissorRects(scissorCount, scissors);
			a_context->RSSetState(raster);
			a_context->OMSetBlendState(blend, blendFactor, sampleMask);
			a_context->OMSetDepthStencilState(depth, stencilRef);
			a_context->IASetInputLayout(layout);
			a_context->IASetPrimitiveTopology(topology);
			a_context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
			a_context->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);
			a_context->VSSetShader(vs, vsInstances, vsInstanceCount);
			a_context->PSSetShader(ps, psInstances, psInstanceCount);
			a_context->GSSetShader(gs, gsInstances, gsInstanceCount);
			a_context->HSSetShader(hs, hsInstances, hsInstanceCount);
			a_context->DSSetShader(ds, dsInstances, dsInstanceCount);
			a_context->VSSetConstantBuffers(0, 1, &vsConstants);
			a_context->PSSetConstantBuffers(0, 1, &psConstants);
			a_context->PSSetShaderResources(0, kViews, psViews);
			a_context->PSSetSamplers(0, 1, &psSampler);

			for (auto*& view : rtvs) {
				ReleaseCom(view);
			}
			ReleaseCom(dsv);
			ReleaseCom(raster);
			ReleaseCom(blend);
			ReleaseCom(depth);
			ReleaseCom(layout);
			ReleaseCom(vertexBuffer);
			ReleaseCom(indexBuffer);
			ReleaseCom(vs);
			ReleaseCom(ps);
			ReleaseCom(gs);
			ReleaseCom(hs);
			ReleaseCom(ds);
			for (auto* instances : { vsInstances, psInstances, gsInstances, hsInstances, dsInstances }) {
				for (UINT i = 0; i < kInstances; ++i) {
					ReleaseCom(instances[i]);
				}
			}
			ReleaseCom(vsConstants);
			ReleaseCom(psConstants);
			for (auto*& view : psViews) {
				ReleaseCom(view);
			}
			ReleaseCom(psSampler);
		}
	};

	ContextState g_savedContext;  // main thread only; too large for the stack of a hook

	void Copy4(float (&a_to)[4], float a_x, float a_y, float a_z, float a_w)
	{
		a_to[0] = a_x;
		a_to[1] = a_y;
		a_to[2] = a_z;
		a_to[3] = a_w;
	}

	void FogParam(float (&a_to)[4], const FogValues& a_fog)
	{
		const float range = a_fog.farDistance - a_fog.nearDistance;
		if (std::abs(range) < 0.001f) {
			Copy4(a_to, 5000000.0f, 0.1f, 1.0f, 0.0f);  // what AE#107524 sets for a fog with near = far = 0: none
			return;
		}
		Copy4(a_to, a_fog.nearDistance / range, 1.0f / range, a_fog.power, a_fog.clamp);
	}

	RE::Setting* FindDisplaySetting(std::string_view a_name)
	{
		if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton()) {
			if (auto* setting = prefs->GetSetting(a_name)) {
				return setting;
			}
		}
		auto* ini = RE::INISettingCollection::GetSingleton();
		return ini ? ini->GetSetting(a_name) : nullptr;
	}

	float FloatSetting(const RE::Setting* a_setting, float a_default)
	{
		return a_setting && a_setting->GetType() == RE::Setting::Type::kFloat ? a_setting->GetFloat() : a_default;
	}

	ToneLook LookOf(const RE::ImageSpaceBaseData& a_data, std::string_view a_source)
	{
		const float whiteScale = FloatSetting(g_display.whiteScale, 1.1f) * a_data.hdr.white;
		return { .p = whiteScale > 0.0001f ? 1.0f / (whiteScale * whiteScale) : 1.0f,
			.saturation = a_data.cinematic.saturation + FloatSetting(g_display.saturationBoost, 0.0f),
			.brightness = a_data.cinematic.brightness + FloatSetting(g_display.brightnessBoost, 0.0f),
			.contrast = a_data.cinematic.contrast + FloatSetting(g_display.contrastBoost, 0.0f),
			.tintAmount = a_data.tint.amount,
			.tint = a_data.tint.color,
			.source = a_source };
	}

	ToneLook InteriorLookNow()
	{
		if (g_interiorImageSpace) {
			return LookOf(g_interiorImageSpace->data, "cell image space");
		}
		if (g_interiorLook && g_door.cell && g_interiorLook->cell == g_door.cell->GetFormID()) {
			return LookOf(g_interiorLook->data.baseData, "captured inside");
		}
		RE::ImageSpaceBaseData neutral{};
		neutral.hdr.white = 1.0f / 1.1f;  // p = 1: the Reinhard curve is the identity
		neutral.cinematic = { .saturation = 1.0f, .brightness = 1.0f, .contrast = 1.0f };
		return LookOf(neutral, "neutral");
	}

	void Fill(float (&a_param)[4], float (&a_cine)[4], float (&a_tint)[4], const ToneLook& a_look, float a_average, float a_third)
	{
		Copy4(a_param, a_look.p, a_average, a_third, 0.0f);
		Copy4(a_cine, a_look.saturation, a_look.brightness, a_look.contrast, a_look.tintAmount);
		Copy4(a_tint, a_look.tint.red, a_look.tint.green, a_look.tint.blue, 0.0f);
	}

	bool Upload(ID3D11DeviceContext* a_context, const GradeConstants& a_constants)
	{
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(a_context->Map(g_grade.constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
			return false;
		}
		std::memcpy(mapped.pData, &a_constants, sizeof(a_constants));
		a_context->Unmap(g_grade.constants, 0);
		return true;
	}

	// One full screen triangle over the top left a_width x a_height of a_target.
	void Draw(ID3D11DeviceContext* a_context, ID3D11RenderTargetView* a_target, ID3D11PixelShader* a_shader, ID3D11ShaderResourceView* const (&a_views)[3],
		std::uint32_t a_width, std::uint32_t a_height)
	{
		g_savedContext.Save(a_context);
		a_context->OMSetRenderTargets(1, &a_target, nullptr);
		const D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(a_width), static_cast<float>(a_height), 0.0f, 1.0f };
		a_context->RSSetViewports(1, &viewport);
		a_context->RSSetState(g_grade.raster);
		const FLOAT factor[4]{};
		a_context->OMSetBlendState(g_grade.blend, factor, 0xFFFFFFFF);
		a_context->OMSetDepthStencilState(g_grade.depth, 0);
		a_context->IASetInputLayout(nullptr);
		a_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ID3D11Buffer* noBuffer = nullptr;
		const UINT    zero = 0;
		a_context->IASetVertexBuffers(0, 1, &noBuffer, &zero, &zero);
		a_context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
		a_context->VSSetShader(g_grade.vs, nullptr, 0);
		a_context->GSSetShader(nullptr, nullptr, 0);
		a_context->HSSetShader(nullptr, nullptr, 0);
		a_context->DSSetShader(nullptr, nullptr, 0);
		a_context->PSSetShader(a_shader, nullptr, 0);
		a_context->PSSetConstantBuffers(0, 1, &g_grade.constants);
		a_context->PSSetShaderResources(0, 3, a_views);
		a_context->PSSetSamplers(0, 1, &g_grade.linear);
		a_context->Draw(3, 0);
		g_savedContext.Restore(a_context);
	}

	ID3DBlob* Compile(pD3DCompile a_compile, const char* a_entry, const char* a_target, std::string& a_why)
	{
		ID3DBlob* code = nullptr;
		ID3DBlob* errors = nullptr;
		const auto hr = a_compile(kShaderSource.data(), kShaderSource.size(), "CellLoadResearch-grade", nullptr, nullptr, a_entry, a_target,
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
		if (FAILED(hr) || !code) {
			a_why = std::format("D3DCompile({}) failed: 0x{:08X} {}", a_entry, static_cast<std::uint32_t>(hr),
				errors ? std::string_view(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()).substr(0, 400) : "");
			ReleaseCom(errors);
			ReleaseCom(code);
			return nullptr;
		}
		ReleaseCom(errors);
		return code;
	}

	bool CreateShaders(ID3D11Device* a_device, std::string& a_why)
	{
		auto* library = ::LoadLibraryW(L"d3dcompiler_47.dll");
		auto  compile = library ? reinterpret_cast<pD3DCompile>(::GetProcAddress(library, "D3DCompile")) : nullptr;
		if (!compile) {
			a_why = "d3dcompiler_47.dll or its D3DCompile not found";
			return false;
		}
		auto* vs = Compile(compile, "VSMain", "vs_5_0", a_why);
		auto* composite = vs ? Compile(compile, "PSComposite", "ps_5_0", a_why) : nullptr;
		auto* grade = composite ? Compile(compile, "PSGrade", "ps_5_0", a_why) : nullptr;
		bool  ok = grade != nullptr;
		if (ok && FAILED(a_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_grade.vs))) {
			a_why = "CreateVertexShader failed";
			ok = false;
		}
		if (ok && FAILED(a_device->CreatePixelShader(composite->GetBufferPointer(), composite->GetBufferSize(), nullptr, &g_grade.composite))) {
			a_why = "CreatePixelShader(composite) failed";
			ok = false;
		}
		if (ok && FAILED(a_device->CreatePixelShader(grade->GetBufferPointer(), grade->GetBufferSize(), nullptr, &g_grade.grade))) {
			a_why = "CreatePixelShader(grade) failed";
			ok = false;
		}
		ReleaseCom(vs);
		ReleaseCom(composite);
		ReleaseCom(grade);
		return ok;
	}

	bool CreateStates(ID3D11Device* a_device, std::string& a_why)
	{
		D3D11_BUFFER_DESC buffer{};
		buffer.ByteWidth = sizeof(GradeConstants);
		buffer.Usage = D3D11_USAGE_DYNAMIC;
		buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		D3D11_SAMPLER_DESC sampler{};
		sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampler.MaxLOD = D3D11_FLOAT32_MAX;
		D3D11_BLEND_DESC blend{};
		blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		D3D11_DEPTH_STENCIL_DESC depth{};
		depth.DepthEnable = FALSE;
		depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
		D3D11_RASTERIZER_DESC raster{};
		raster.FillMode = D3D11_FILL_SOLID;
		raster.CullMode = D3D11_CULL_NONE;
		raster.DepthClipEnable = TRUE;
		if (FAILED(a_device->CreateBuffer(&buffer, nullptr, &g_grade.constants)) || FAILED(a_device->CreateSamplerState(&sampler, &g_grade.linear)) ||
			FAILED(a_device->CreateBlendState(&blend, &g_grade.blend)) || FAILED(a_device->CreateDepthStencilState(&depth, &g_grade.depth)) ||
			FAILED(a_device->CreateRasterizerState(&raster, &g_grade.raster))) {
			a_why = "creating the constant buffer or a state object failed";
			return false;
		}
		return true;
	}

	bool CreateTexture(ID3D11Device* a_device, ID3D11Texture2D*& a_texture, ID3D11ShaderResourceView*& a_srv, ID3D11RenderTargetView** a_rtv, std::string& a_why)
	{
		D3D11_TEXTURE2D_DESC desc{};
		g_target.color->GetDesc(&desc);
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (a_rtv ? D3D11_BIND_RENDER_TARGET : 0u);
		if (FAILED(a_device->CreateTexture2D(&desc, nullptr, &a_texture)) || FAILED(a_device->CreateShaderResourceView(a_texture, nullptr, &a_srv)) ||
			(a_rtv && FAILED(a_device->CreateRenderTargetView(a_texture, nullptr, a_rtv)))) {
			a_why = "creating a grading texture failed";
			return false;
		}
		return true;
	}

	// Render targets of at most 2 x 2 pixels: the HDR chain's last downsample (AE#107183 ends it at 1 or 2
	// pixels) writes the adaptation into target base + 2 * flag, flag AE#415054 flipped every frame.
	void FindAdaptTargets()
	{
		if (g_adaptSearched) {
			return;
		}
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (!renderer) {
			return;
		}
		auto&      targets = renderer->GetRuntimeData().renderTargets;
		const auto count = static_cast<std::uint32_t>(std::size(targets));
		for (std::uint32_t i = 0; i < count; ++i) {
			auto* texture = reinterpret_cast<ID3D11Texture2D*>(targets[i].texture);
			if (!texture || !targets[i].SRV) {
				continue;
			}
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			if (desc.Width <= 2 && desc.Height <= 2) {
				g_adaptCandidates.push_back(i);
			}
		}
		if (g_adaptCandidates.empty()) {
			return;  // the image space targets are not created yet: next time
		}
		g_adaptSearched = true;
		for (const auto index : g_adaptCandidates) {
			if (std::ranges::find(g_adaptCandidates, index + 2) != g_adaptCandidates.end()) {
				g_adaptBase = static_cast<std::int32_t>(index);
				break;
			}
		}
		std::string list;
		for (const auto index : g_adaptCandidates) {
			list += std::format("{}{}", list.empty() ? "" : ",", index);
		}
		Trace::Write("portal.adaptTargets", Trace::Args{}.Raw("candidates", "[" + list + "]").Int("base", g_adaptBase), nullptr, true);
	}

	std::uint32_t AdaptFlag()
	{
		static REL::Relocation<std::uint32_t*> flag{ REL::ID(415054) };
		return *flag & 1;
	}

	ID3D11Texture2D* AdaptTexture(ID3D11ShaderResourceView** a_srv = nullptr, std::int32_t* a_index = nullptr)
	{
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (!renderer || g_adaptBase < 0) {
			return nullptr;
		}
		const auto index = static_cast<std::uint32_t>(g_adaptBase) + 2 * AdaptFlag();
		auto&      target = renderer->GetRuntimeData().renderTargets[index];
		if (a_srv) {
			*a_srv = reinterpret_cast<ID3D11ShaderResourceView*>(target.SRV);
		}
		if (a_index) {
			*a_index = static_cast<std::int32_t>(index);
		}
		return reinterpret_cast<ID3D11Texture2D*>(target.texture);
	}

	float Half(std::uint16_t a_bits)
	{
		const std::uint32_t sign = (a_bits & 0x8000u) << 16;
		const std::uint32_t exponent = (a_bits >> 10) & 0x1Fu;
		const std::uint32_t mantissa = a_bits & 0x3FFu;
		float               value = 0.0f;
		if (exponent == 0) {
			value = std::ldexp(static_cast<float>(mantissa), -24);
		} else if (exponent == 31) {
			value = mantissa ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
		} else {
			value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
		}
		return sign ? -value : value;
	}

	// The first texel of a mapped texture, for the formats an adaptation target may have.
	std::array<float, 4> Texel(DXGI_FORMAT a_format, const void* a_data)
	{
		std::array<float, 4> out{};
		const auto*          halves = static_cast<const std::uint16_t*>(a_data);
		const auto*          floats = static_cast<const float*>(a_data);
		switch (a_format) {
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			out = { Half(halves[0]), Half(halves[1]), Half(halves[2]), Half(halves[3]) };
			break;
		case DXGI_FORMAT_R16G16_FLOAT:
			out = { Half(halves[0]), Half(halves[1]), 0.0f, 0.0f };
			break;
		case DXGI_FORMAT_R16_FLOAT:
			out = { Half(halves[0]), 0.0f, 0.0f, 0.0f };
			break;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			out = { floats[0], floats[1], floats[2], floats[3] };
			break;
		case DXGI_FORMAT_R32G32_FLOAT:
			out = { floats[0], floats[1], 0.0f, 0.0f };
			break;
		case DXGI_FORMAT_R32_FLOAT:
			out = { floats[0], 0.0f, 0.0f, 0.0f };
			break;
		default:
			out = { -1.0f, -1.0f, -1.0f, -1.0f };
			break;
		}
		return out;
	}

	ID3D11Texture2D* Staging(ID3D11Texture2D* a_source)
	{
		D3D11_TEXTURE2D_DESC desc{};
		a_source->GetDesc(&desc);
		desc.Width = 1;
		desc.Height = 1;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.MiscFlags = 0;
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* staging = nullptr;
		auto*            device = Device();
		return device && SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)) ? staging : nullptr;
	}

	void CopyTexel(ID3D11DeviceContext* a_context, ID3D11Texture2D* a_to, ID3D11Texture2D* a_from)
	{
		const D3D11_BOX box{ 0, 0, 0, 1, 1, 1 };
		a_context->CopySubresourceRegion(a_to, 0, 0, 0, 0, a_from, 0, &box);
	}

	// F11: every candidate's first texel, the GPU waited for.
	void TraceAdaptTargets()
	{
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		auto* context = Context();
		if (!renderer || !context) {
			return;
		}
		std::int32_t current = -1;
		AdaptTexture(nullptr, &current);
		std::string json;
		for (const auto index : g_adaptCandidates) {
			auto* texture = reinterpret_cast<ID3D11Texture2D*>(renderer->GetRuntimeData().renderTargets[index].texture);
			if (!texture) {
				continue;
			}
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			std::array<float, 4> value{ -1.0f, -1.0f, -1.0f, -1.0f };
			if (auto* staging = Staging(texture)) {
				CopyTexel(context, staging, texture);
				D3D11_MAPPED_SUBRESOURCE mapped{};
				if (SUCCEEDED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
					value = Texel(desc.Format, mapped.pData);
					context->Unmap(staging, 0);
				}
				staging->Release();
			}
			json += (json.empty() ? "" : ",") + Trace::Args{}
													.Int("index", index)
													.Int("width", desc.Width)
													.Int("height", desc.Height)
													.Int("format", desc.Format)
													.Raw("texel", std::format("[{:.4f},{:.4f},{:.4f},{:.4f}]", value[0], value[1], value[2], value[3]))
													.Json();
		}
		Trace::Write("portal.adapt", Trace::Args{}.Int("flag", AdaptFlag()).Int("base", g_adaptBase).Int("current", current).Raw("targets", "[" + json + "]"),
			nullptr, true);
	}

	// Inside an interior without the portal: its adaptation, read back a few frames after the copy (no stall).
	void CaptureInteriorAdapt()
	{
		constexpr std::uint64_t kEveryFrames = 30;
		constexpr std::uint64_t kReadAfterFrames = 3;
		auto*                   context = Context();
		auto*                   player = RE::PlayerCharacter::GetSingleton();
		auto*                   cell = player ? player->GetParentCell() : nullptr;
		const auto              frame = Trace::Frame();
		if (!context) {
			return;
		}
		if (g_adaptPendingFrame) {
			if (frame < g_adaptPendingFrame + kReadAfterFrames) {
				return;
			}
			D3D11_MAPPED_SUBRESOURCE mapped{};
			const auto               hr = context->Map(g_adaptStaging, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
			if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
				return;
			}
			if (SUCCEEDED(hr)) {
				const auto texel = Texel(g_adaptStagingFormat, mapped.pData);
				context->Unmap(g_adaptStaging, 0);
				if (texel[0] > 0.0f) {
					g_interiorAdapt = InteriorAdapt{ g_adaptPendingCell, texel[0], texel[1] };
				}
			}
			g_adaptPendingFrame = 0;
			return;
		}
		if (!cell || !cell->IsInteriorCell() || g_active || frame % kEveryFrames != 0) {
			return;
		}
		auto* texture = AdaptTexture();
		if (!texture) {
			return;
		}
		D3D11_TEXTURE2D_DESC desc{};
		texture->GetDesc(&desc);
		if (!g_adaptStaging || g_adaptStagingFormat != desc.Format) {
			ReleaseCom(g_adaptStaging);
			g_adaptStaging = Staging(texture);
			g_adaptStagingFormat = desc.Format;
		}
		if (g_adaptStaging) {
			CopyTexel(context, g_adaptStaging, texture);
			g_adaptPendingFrame = (std::max)(frame, std::uint64_t{ 1 });
			g_adaptPendingCell = cell->GetFormID();
		}
	}

	std::string LookJson(const ToneLook& a_look)
	{
		return Trace::Args{}
			.Str("source", a_look.source)
			.Num("p", a_look.p)
			.Num("saturation", a_look.saturation)
			.Num("brightness", a_look.brightness)
			.Num("contrast", a_look.contrast)
			.Num("tintAmount", a_look.tintAmount)
			.Raw("tint", std::format("[{:.3f},{:.3f},{:.3f}]", a_look.tint.red, a_look.tint.green, a_look.tint.blue))
			.Json();
	}

	std::string Vector4Json(const float (&a_values)[4])
	{
		return std::format("[{:.4f},{:.4f},{:.4f},{:.4f}]", a_values[0], a_values[1], a_values[2], a_values[3]);
	}

	void FlushGradeTrace()
	{
		if (g_gradeTraceWhy.empty()) {
			return;
		}
		const auto& c = g_lastConstants;
		Trace::Write("portal.grade", Trace::Args{}
										 .Str("event", g_gradeTraceWhy)
										 .Int("stages", g_gradeStages)
										 .Bool("valid", g_gradeValid)
										 .Str("skip", g_gradeSkip)
										 .Str("failure", g_gradeFailure)
										 .Raw("interiorLook", LookJson(g_lastSource))
										 .Raw("streetLook", LookJson(g_lastView))
										 .Str("interiorFogSource", g_lastFogSource)
										 .Raw("compositeFog", std::format("[{},{},{}]", Vector4Json(c.fogNear), Vector4Json(c.fogFar), Vector4Json(c.fogParam)))
										 .Raw("depthRange", Vector4Json(c.depthRange))
										 .Raw("srcParam", Vector4Json(c.srcParam))
										 .Raw("dstParam", Vector4Json(c.dstParam))
										 .Raw("quadFog", std::format("[{},{},{}]", Vector4Json(c.quadFogNear), Vector4Json(c.quadFogFar), Vector4Json(c.quadFogParam)))
										 .Raw("doorOrigin", Vector4Json(c.doorOrigin))
										 .Raw("uvRect", Vector4Json(c.uvRect))
										 .Raw("viewport", Vector4Json(c.viewport))
										 .Int("adaptBase", g_adaptBase)
										 .Int("adaptFlag", AdaptFlag())
										 .Raw("interiorAdapt", g_interiorAdapt ? std::format("[\"{:08X}\",{:.4f},{:.4f}]", g_interiorAdapt->cell, g_interiorAdapt->slow, g_interiorAdapt->fast) : "null")
										 .Bool("enb", g_proxyD3D11)
										 .Bool("communityShaders", g_communityShaders)
										 .Raw("fog", Portal::FogJson(g_door.cell)),
			nullptr, true);
		g_gradeTraceWhy.clear();
	}
}

namespace Portal::detail
{
	void ReadGradeStages()
	{
		static bool read = false;
		if (!read) {
			read = true;
			g_gradeStages = (std::min)(Settings::Get().portalGradeStages, 2u);
		}
	}

	bool CreateGradeTargets(std::string& a_why)
	{
		ReadGradeStages();
		if (!g_gradeFailure.empty()) {
			a_why = g_gradeFailure;
			return false;
		}
		if (g_target.graded) {
			return true;
		}
		auto* device = Device();
		if (!device || !g_target.color) {
			a_why = "no device or portal target";
			return false;
		}
		g_display = { .whiteScale = FindDisplaySetting("fReinhardWhiteScale:Display"),
			.filmic = FindDisplaySetting("bUseFilmicCurve:Display"),
			.saturationBoost = FindDisplaySetting("fGlobalSaturationBoost:Display"),
			.brightnessBoost = FindDisplaySetting("fGlobalBrightnessBoost:Display"),
			.contrastBoost = FindDisplaySetting("fGlobalContrastBoost:Display") };
		// ENB replaces the HDR blend with its own tone mapping (its d3d11.dll exports ENBGetSDKVersion);
		// Community Shaders keeps the vanilla curve in SDR.
		auto* d3d11 = ::GetModuleHandleW(L"d3d11.dll");
		g_proxyD3D11 = d3d11 && ::GetProcAddress(d3d11, "ENBGetSDKVersion");
		g_communityShaders = ::GetModuleHandleW(L"CommunityShaders.dll") != nullptr;
		std::string why;
		const bool  ok = CreateShaders(device, why) && CreateStates(device, why) &&
		                CreateTexture(device, g_target.scratch, g_target.scratchSrv, nullptr, why) &&
		                CreateTexture(device, g_target.graded, g_target.gradedSrv, &g_target.gradedRtv, why);
		Trace::Write("portal.grade", Trace::Args{}
										 .Str("event", "init")
										 .Bool("ok", ok)
										 .Str("why", why)
										 .Int("stages", g_gradeStages)
										 .Bool("whiteScaleFound", g_display.whiteScale != nullptr)
										 .Bool("filmicFound", g_display.filmic != nullptr)
										 .Bool("enb", g_proxyD3D11)
										 .Bool("communityShaders", g_communityShaders),
			nullptr, true);
		if (!ok) {
			g_gradeFailure = why;
			a_why = why;
			return false;
		}
		return true;
	}

	void GradeStart()
	{
		g_interiorImageSpace = nullptr;
		if (g_door.cell) {
			if (const auto* extra = g_door.cell->extraList.GetByType<RE::ExtraCellImageSpace>()) {
				g_interiorImageSpace = extra->imageSpace;
			}
		}
		g_gradeValid = false;
		RequestGradeTrace("start");
	}

	void Composite(std::uint32_t a_width, std::uint32_t a_height)
	{
		auto* context = Context();
		if (!context || !g_grade.composite || !g_target.scratch || g_composited) {
			return;
		}
		g_composited = true;
		GradeConstants constants{};
		const float    inverseRange = RE::BSShaderManager::State::GetSingleton().invFrameBufferRange;
		const auto     fog = InteriorFogOf(g_door.cell);
		g_lastFogSource = fog ? fog->source : "none";
		Copy4(constants.fogNear, fog ? fog->nearColor.red : 0.0f, fog ? fog->nearColor.green : 0.0f, fog ? fog->nearColor.blue : 0.0f, inverseRange);
		Copy4(constants.fogFar, fog ? fog->farColor.red : 0.0f, fog ? fog->farColor.green : 0.0f, fog ? fog->farColor.blue : 0.0f, fog ? 1.0f : 0.0f);
		FogParam(constants.fogParam, fog ? *fog : FogValues{});
		Copy4(constants.depthRange, g_frustum.fNear, g_frustum.fFar, 0.0f, 0.0f);
		Copy4(constants.viewport, static_cast<float>(a_width), static_cast<float>(a_height), 0.0f, 0.0f);
		if (!Upload(context, constants)) {
			return;
		}
		std::ranges::copy(constants.fogNear, g_lastConstants.fogNear);
		std::ranges::copy(constants.fogFar, g_lastConstants.fogFar);
		std::ranges::copy(constants.fogParam, g_lastConstants.fogParam);
		std::ranges::copy(constants.depthRange, g_lastConstants.depthRange);
		const D3D11_BOX box{ 0, 0, 0, a_width, a_height, 1 };
		context->CopySubresourceRegion(g_target.scratch, 0, 0, 0, 0, g_target.color, 0, &box);
		ID3D11ShaderResourceView* const views[3]{ g_target.scratchSrv, g_target.depthCopySrv, nullptr };
		Draw(context, g_target.rtv, g_grade.composite, views, a_width, a_height);
		++g_composites;
	}

	void Grade(std::uint32_t a_width, std::uint32_t a_height)
	{
		g_gradeValid = false;
		auto* context = Context();
		auto* main = RE::Main::WorldRootCamera();
		auto* manager = RE::ImageSpaceManager::GetSingleton();
		if (!context || !g_grade.grade || !g_target.graded || !main || !manager) {
			g_gradeSkip = g_gradeFailure.empty() ? "not ready" : g_gradeFailure;
			FlushGradeTrace();
			return;
		}
		if (g_proxyD3D11) {
			g_gradeSkip = "ENB: the street's tone mapping is not the game's";
			FlushGradeTrace();
			return;
		}
		if (g_display.filmic && g_display.filmic->GetType() == RE::Setting::Type::kBool && g_display.filmic->GetBool()) {
			g_gradeSkip = "bUseFilmicCurve: no inverse here";
			FlushGradeTrace();
			return;
		}
		FindAdaptTargets();
		const auto&    settings = Settings::Get();
		GradeConstants constants = g_lastConstants;
		const auto     source = InteriorLookNow();
		const auto     view = LookOf(manager->GetImageSpaceData().baseData, "street image space");
		const float    interiorAverage = g_interiorAdapt && g_door.cell && g_interiorAdapt->cell == g_door.cell->GetFormID() ? g_interiorAdapt->slow : kDefaultAverage;
		ID3D11ShaderResourceView* adapt = nullptr;
		AdaptTexture(&adapt);
		Fill(constants.srcParam, constants.srcCine, constants.srcTint, source, interiorAverage, 1.0f);
		Fill(constants.dstParam, constants.dstCine, constants.dstTint, view, kDefaultAverage, adapt ? 1.0f : 0.0f);

		const float inverseRange = RE::BSShaderManager::State::GetSingleton().invFrameBufferRange;
		const auto* fog = WorldFog();
		const auto  street = fog ? ReadFog(*fog) : FogValues{};
		const auto  model = fog ? (std::min)(settings.portalQuadFogModel, 2u) : 0u;
		Copy4(constants.quadFogNear, street.nearColor.red, street.nearColor.green, street.nearColor.blue, inverseRange);
		Copy4(constants.quadFogFar, street.farColor.red, street.farColor.green, street.farColor.blue, static_cast<float>(model));
		FogParam(constants.quadFogParam, street);
		const auto origin = g_door.center - main->world.translate;
		const auto right = g_door.right * g_door.width;
		const auto up = g_door.up * g_door.height;
		Copy4(constants.doorOrigin, origin.x, origin.y, origin.z, 0.0f);
		Copy4(constants.doorRight, right.x, right.y, right.z, 0.0f);
		Copy4(constants.doorUp, up.x, up.y, up.z, 0.0f);
		Copy4(constants.uvRect, g_visible.u0, g_visible.v0, g_visible.u1 - g_visible.u0, g_visible.v1 - g_visible.v0);
		Copy4(constants.viewport, static_cast<float>(a_width), static_cast<float>(a_height), 0.0f, 0.0f);
		g_lastConstants = constants;
		g_lastSource = source;
		g_lastView = view;
		if (!Upload(context, constants)) {
			g_gradeSkip = "constant buffer";
			FlushGradeTrace();
			return;
		}
		ID3D11ShaderResourceView* const views[3]{ g_target.srv, nullptr, adapt };
		Draw(context, g_target.gradedRtv, g_grade.grade, views, a_width, a_height);
		g_gradeValid = true;
		g_gradeSkip.clear();
		++g_grades;
		FlushGradeTrace();
	}

	ID3D11ShaderResourceView* QuadView()
	{
		return g_gradeStages >= 2 && g_gradeValid && g_target.gradedSrv ? g_target.gradedSrv : g_target.srv;
	}

	void RequestGradeTrace(std::string_view a_why)
	{
		g_gradeTraceWhy = a_why;
	}

	void RequestHdrDumps()
	{
		g_ldrDumpRequested = true;
		g_adaptDumpRequested = true;
	}

	std::string GradeJson()
	{
		const auto json = Trace::Args{}
							  .Int("stages", g_gradeStages)
							  .Bool("valid", g_gradeValid)
							  .Str("skip", g_gradeSkip)
							  .Str("failure", g_gradeFailure)
							  .Int("composites", static_cast<std::int64_t>(g_composites))
							  .Int("grades", static_cast<std::int64_t>(g_grades))
							  .Str("interiorLook", g_lastSource.source)
							  .Str("interiorFog", g_lastFogSource)
							  .Int("adaptBase", g_adaptBase)
							  .Int("adaptCandidates", static_cast<std::int64_t>(g_adaptCandidates.size()))
							  .Raw("interiorAdapt", g_interiorAdapt ? std::format("[\"{:08X}\",{:.4f},{:.4f}]", g_interiorAdapt->cell, g_interiorAdapt->slow, g_interiorAdapt->fast) : "null")
							  .Int("quadFogModel", Settings::Get().portalQuadFogModel)
							  .Json();
		g_composites = 0;
		g_grades = 0;
		return json;
	}
}

namespace Portal
{
	void AfterImageSpaceHdr()
	{
		if (!Trace::IsMainThread()) {
			return;
		}
		FindAdaptTargets();
		auto* context = Context();
		if (g_ldrDumpRequested && context) {
			g_ldrDumpRequested = false;
			ID3D11RenderTargetView* view = nullptr;
			context->OMGetRenderTargets(1, &view, nullptr);
			if (view) {
				ID3D11Resource* resource = nullptr;
				view->GetResource(&resource);
				ID3D11Texture2D* texture = nullptr;
				if (resource && SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture)))) {
					D3D11_TEXTURE2D_DESC desc{};
					texture->GetDesc(&desc);
					WriteDump(texture, desc.Width, desc.Height, "ldr", ++g_ldrDumps);
					texture->Release();
				}
				ReleaseCom(resource);
				view->Release();
			}
		}
		if (g_adaptDumpRequested) {
			g_adaptDumpRequested = false;
			TraceAdaptTargets();
		}
		CaptureInteriorAdapt();
	}
}
