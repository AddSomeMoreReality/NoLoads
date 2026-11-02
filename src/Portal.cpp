#include "Portal.h"

#include "Hud.h"
#include "Settings.h"
#include "Trace.h"

#include <d3d11.h>
#include <detours.h>

namespace
{
	using RenderTarget = RE::RENDER_TARGET;
	using LightParams = RE::ShadowSceneNode::LIGHT_CREATE_PARAMS;

	constexpr float         kMinEyeDistance = 10.0f;  // units from the door plane: closer, the frustum degenerates
	constexpr float         kNearBehindDoor = 2.0f;   // the near plane, units in front of the door plane
	constexpr std::uint32_t kAccumulatorType = 0x63;  // what the game passes to every BSShaderAccumulator constructor
	constexpr std::uint32_t kDepthTestWrite = 3;      // RendererShadowState::depthStencilDepthMode, as BSCubeMapCamera sets it
	constexpr std::uint32_t kAllDirty = 0xFFFFFFFF;   // RendererShadowState::stateUpdateFlags: apply everything again
	constexpr std::uint32_t kAllModified = 0xFFFF;    // ... and every texture, sampler and UAV slot
	constexpr std::uint32_t kMinViewport = 32;        // pixels: the smallest portal picture drawn
	constexpr float         kMirrorRadius = 2000.0f;  // units: the player's dynamic lights this close are carried through the door
	constexpr std::int64_t  kActorRefreshUs = 1'000'000;
	constexpr std::size_t   kSampledShapes = 4;  // interior shapes whose light lists go to the trace
	constexpr float         kRegatherDistance = 16.0f;  // units a carried light moves before its lit shapes are collected again
	constexpr std::uint32_t kLightInFrustum = 0;        // BSLight::frustrumCull after AE#108278 found it in the camera's frustum (0xFF: outside)
	constexpr int           kActorRevealDepth = 3;      // AppCulled nodes this deep under an actor's root are shown for the pass
	constexpr std::size_t   kTracedActors = 8;
	constexpr std::size_t   kTracedAncestors = 6;       // parents of an actor's root listed in the trace
	constexpr std::size_t   kMaxActorAncestors = 8;     // parents walked up from an actor's root to the interior's root
	constexpr std::uint32_t kEffectTechniqueBase = 0x4000002C;  // BSEffectShader::SetupTechnique (AE#107524) tests bits of technique - this
	constexpr std::uint32_t kSoftEffectBit = 1u << 18;          // ... soft particles: it binds the main depth buffer's shader view
	constexpr std::uint32_t kLightOutOfFrustum = 0xFF;          // BSLight::frustrumCull: AE#107784 skips such lights
	constexpr std::int32_t  kRebuildPasses = 0x7FFFFFFF;        // BSShaderProperty::lastRenderPassState: GetRenderPasses builds them again
	constexpr std::size_t   kTracedEffects = 8;
	constexpr std::size_t   kWatchedEffects = 64;           // effect shapes whose way through our culling the trace follows
	constexpr float         kMaxAnimationStep = 0.1f;           // seconds: the longest step given to the actors' animation
	constexpr const char*   kPoseBone = "NPC Spine [Spn0]";     // a bone whose local transform tells whether the pose moved
	constexpr std::size_t   kMaxAnimatedActors = 32;            // interior actors whose animation updates are made full
	constexpr float         kHavokToSkyrim = 69.99125f;         // Havok metres -> game units
	constexpr std::uint32_t kUpdateControllers = 1u << 0;       // NiUpdateData::flags: NiNode::UpdateDownwardPass (AE#70301) runs the controllers (NG: kDirty)
	constexpr const char*   kQuadModel = "CellLoadResearch\\portalquad.nif";
	constexpr std::size_t   kAmbientOffset = 0xC8;  // BSShaderManager::State: directional ambient matrix and ambient specular ...
	constexpr std::size_t   kAmbientBytes = 0x44;   // ... the bytes AE#105643 writes (0x142033128..0x14203316C)
	constexpr std::int32_t  kRefractionNormalsGroup = 5;  // BSBatchRenderer::geometryGroups: only the frame renderer draws it
	constexpr std::uint32_t kGeometryGroups = 16;         // BSBatchRenderer::geometryGroups entries
	constexpr std::uint32_t kAlphaGroupBit = 1u << kGeometryGroups;
	constexpr std::uint32_t kMaxInspectedTechniques = 4096;

	// Where the pass is: a frozen pass names its step to the watchdog.
	enum class PassPhase : std::uint8_t
	{
		kIdle,
		kLendSlots,
		kAnimateActors,
		kUpdateControllers,
		kCullInterior,
		kCullMasterParticles,
		kSetTargets,
		kSubmit,
		kDrain,
		kRestore,
		kTotal
	};

	constexpr std::array<std::string_view, std::to_underlying(PassPhase::kTotal)> kPassPhaseNames{
		"idle", "lend slots", "animate actors", "update controllers", "cull interior", "cull master particles", "set targets", "submit", "drain", "restore"
	};

	// Our render target and depth buffer (D3D11 objects of the game's device, never released).
	struct Target
	{
		ID3D11Texture2D*          color{ nullptr };
		ID3D11RenderTargetView*   rtv{ nullptr };
		ID3D11ShaderResourceView* srv{ nullptr };
		ID3D11Texture2D*          depth{ nullptr };
		ID3D11DepthStencilView*   dsv{ nullptr };
		ID3D11DepthStencilView*   dsvReadOnly{ nullptr };
		ID3D11ShaderResourceView* depthSrv{ nullptr };
		ID3D11Texture2D*          depthCopy{ nullptr };  // DepthMode::kCopiedView: what soft particles read
		ID3D11ShaderResourceView* depthCopySrv{ nullptr };
		std::uint32_t             width{ 0 };
		std::uint32_t             height{ 0 };
	};

	// How the pass lends its depth buffer to the engine ([Portal] iDepthMode, cycled by a hotkey):
	// effects test depth against the kMAIN slot after their first technique and read its shader view.
	enum class DepthMode : std::uint32_t
	{
		kOwnSlot = 0,     // 0.8.9: our slot, and only the shader view of kMAIN (fire tested against the street's depth)
		kWholeMain = 1,   // 0.8.10: kMAIN as a whole (no fire at all)
		kCopiedView = 2,  // kMAIN as a whole, its shader view a copy of our depth taken at the pass's first effect
		kTotal
	};

	constexpr std::array<const char*, 3> kDepthModeNames{ "own slot + main view", "whole main slot", "whole main slot, copied view" };

	// How the pass advances the interior's controllers (fire and smoke particles, candle flames, animated
	// textures): the game does it through the scene graph, which a cell without the player is not part
	// of. 0.8.12: no particles before the player's first visit, and the smoke stood still after it.
	// [Portal] iControllerUpdate, cycled by a hotkey.
	enum class ControllerMode : std::uint32_t
	{
		kOff = 0,
		kSelective = 1,  // NiAVObject::UpdateSelectedDownwardPass (vfunc 0x2D): only the nodes flagged kSelectiveUpdate
		kWholeTree = 2,  // NiAVObject::Update with kUpdateControllers: every node, its transform and controllers
		kTotal
	};

	constexpr std::array<const char*, 3> kControllerModeNames{ "off", "selective update", "whole tree" };

	// What the CPU quads of NiParticles face in the pass ([Portal] iParticleFacing).
	enum class ParticleFacing : std::uint32_t
	{
		kGame = 0,    // whatever RendererShadowState holds when the packer runs
		kPortal = 1,  // the portal camera: across the door plane
		kView = 2,    // the player's view carried through the door: what they would see standing inside
		kTotal
	};

	// The camera the particle packer reads from RendererShadowState.
	struct PackCamera
	{
		RE::NiPoint3 origin;
		RE::NiPoint3 up;
		RE::NiPoint3 right;
		RE::NiPoint3 forward;
	};

	// The part of the door rectangle the main camera sees: door UVs (0..1, v down) and screen pixels.
	struct VisiblePart
	{
		float u0{ 0.0f }, u1{ 1.0f }, v0{ 0.0f }, v1{ 1.0f };
		float pixelsWide{ 0.0f }, pixelsHigh{ 0.0f };
	};

	// A light we added to ShadowSceneNode[0], restricted to the interior's 3D: a copy of one of the
	// interior's own lights, or of a dynamic light near the player (torch, Candlelight) carried through the door.
	struct AddedLight
	{
		RE::NiPointer<RE::NiLight> source;
		RE::NiPointer<RE::NiLight> copy;
		RE::BSLight*               light{ nullptr };
		bool                       mirror{ false };
		bool                       seen{ false };
		RE::NiPoint3               gatheredAt{};  // the copy's position when its lit shapes were collected
	};

	Target                                      g_target;
	std::string                                 g_targetFailure;  // creation failed once: do not retry
	RE::NiPointer<RE::NiCamera>                 g_camera;
	RE::NiPointer<RE::BSShaderAccumulator>      g_accumulator;
	RE::NiPointer<RE::NiAVObject>               g_quad;
	RE::BSEffectShaderMaterial*                 g_quadMaterial{ nullptr };
	std::vector<ID3D11ShaderResourceView*>      g_replacedViews;  // the placeholder texture's views we swapped out (kept)
	Portal::Door                                g_door;
	std::atomic<bool>                           g_active{ false };  // read by the watchdog
	std::atomic<PassPhase>                      g_passPhase{ PassPhase::kIdle };
	std::atomic<bool>                           g_passAtFence{ false };  // [Portal] bPassAtSceneListFence, once its hook is in
	std::string                                 g_fault;           // the pass broke a rule of the game's renderer: no more passes
	std::uint32_t                               g_tracedLeftovers{ 0 };   // geometry groups already reported
	std::uint32_t                               g_leftoverGroups{ 0 };    // ... and since the last status
	bool                                        g_dumpRequested{ false };
	bool                                        g_mainDumpRequested{ false };
	std::uint32_t                               g_mainDumps{ 0 };

	// The interior's image space as the game blended it while the player stood inside (0.8.25, F2).
	struct InteriorLook
	{
		RE::FormID         cell{ 0 };
		RE::ImageSpaceData data{};
	};
	std::optional<InteriorLook> g_interiorLook;
	bool                        g_interiorImageSpace{ false };  // F2
	std::optional<RE::ImageSpaceData> g_gameImageSpace;          // what the manager held before we wrote the interior's
	std::uint64_t               g_imageSpaceSwaps{ 0 };
	std::uint64_t               g_imageSpaceHeld{ 0 };  // swaps still in place after RenderPlayerView
	std::uint32_t                               g_dumps{ 0 };
	std::vector<AddedLight>                     g_lights;
	std::vector<RE::NiPointer<RE::NiAVObject>>  g_actorRoots;  // the interior's actors' 3D, hidden by the game while the cell is unattached
	std::vector<RE::NiPointer<RE::Actor>>       g_interiorActors;  // the same actors, in the same order
	std::int64_t                                g_nextActorRefreshUs{ 0 };
	std::vector<RE::NiPointer<RE::BSGeometry>>  g_sampledShapes;
	bool                                        g_rebuildPasses{ false };  // the interior's render passes are built again on the next pass
	DepthMode                                   g_depthMode{ DepthMode::kCopiedView };
	bool                                        g_depthModeRead{ false };  // taken from the INI once
	thread_local bool                           t_inPass{ false };         // SubmitAccumulator of our pass runs on this thread
	bool                                        g_depthCopied{ false };    // this pass's copy of the depth buffer is taken
	bool                                        g_foreground{ true };      // the game window was in the foreground at the last frame
	thread_local bool                           t_culling{ false };        // our culling runs on this thread; the game's own culls on its jobs at the same time
	bool                                        g_effectsLit{ false };     // effects get our lights (0.8.10-0.8.11: no fire at all)
	bool                                        g_effectsLitRead{ false };
	bool                                        g_rebuildEffects{ false };  // the interior's effect passes are built again on the next pass
	std::int64_t                                g_lastAnimateUs{ 0 };
	ControllerMode                              g_controllerMode{ ControllerMode::kSelective };
	bool                                        g_controllerModeRead{ false };
	std::vector<float>                          g_poseSignatures;    // per interior actor: the pose bone at the last pass
	std::vector<std::uint32_t>                  g_seenGameUpdates;   // per interior actor: g_gameAnimationUpdates at the last pass

	// Read by AdjustAnimationUpdate on whatever thread updates the actors: the interior's actors, in the
	// order of g_interiorActors (compared, never dereferenced), and the updates the game made of them.
	std::atomic<bool>                                             g_forceAnimation{ false };
	std::atomic<bool>                                             g_fullProcessing{ false };
	std::array<std::atomic<const RE::Actor*>, kMaxAnimatedActors> g_animatedActors{};
	std::array<std::atomic<std::uint32_t>, kMaxAnimatedActors>    g_gameAnimationUpdates{};
	std::atomic<std::uint64_t>                                    g_forcedAnimationUpdates{ 0 };
	std::atomic<std::uint64_t>                                    g_fullProcessingGranted{ 0 };
	std::atomic<bool>                                             g_gameAnimation{ false };
	std::atomic<std::uint64_t>                                    g_gameAnimationAdded{ 0 };
	std::array<std::atomic<std::uint32_t>, kMaxAnimatedActors>    g_movementUpdates{};
	thread_local bool                                             t_ownAnimation{ false };  // AnimateActors is updating the actor
	bool                                                          g_softEffects{ true };    // soft particles in the pass ([Portal] bSoftEffects, hotkey)
	bool                                                          g_softEffectsRead{ false };

	// The portal's space for IntoInterior, read by the game's actor updates on job threads: published
	// by Start, withdrawn by Stop, never freed (one per start).
	struct Space
	{
		const RE::TESObjectCELL* cell{ nullptr };
		const RE::NiAVObject*    scene{ nullptr };  // the interior's 3D
		RE::NiTransform          toInterior;
		RE::NiTransform          toExterior;
		RE::NiPoint3             center;            // the interior's bound
		float                    radius{ 0.0f };
	};
	std::atomic<const Space*>                                                           g_space{ nullptr };
	std::array<std::atomic<std::uint64_t>, std::to_underlying(Portal::Carried::kTotal)> g_pointsCarried{};  // IntoInterior answers yes, per Carried
	std::atomic<std::uint64_t>                                                          g_emissionsWithoutBudget{ 0 };

	// The interior's actors as the game's own logic sees them (0.8.24).
	std::atomic<bool>          g_actorsSeen{ false };
	std::atomic<RE::NiCamera*> g_virtualCamera{ nullptr };  // the portal's camera while [Portal] bVirtualCamera; never freed in a session
	std::atomic<bool>          g_soundThroughDoor{ false };
	std::atomic<bool>          g_distanceThroughDoor{ false };
	std::atomic<bool>          g_subtitlesThroughDoor{ false };
	std::uint64_t              g_subtitlesCarried{ 0 };  // main thread
	std::atomic<std::uint64_t> g_distancesCarried{ 0 };
	thread_local std::uint32_t t_actorScope{ 0 };
	std::atomic<std::uint64_t> g_worldCameraCarried{ 0 };
	std::atomic<std::uint64_t> g_footFeedback{ 0 };
	std::atomic<std::uint64_t> g_notVisibleCleared{ 0 };
	std::atomic<std::uint64_t> g_soundsCarried{ 0 };
	RE::NiPoint3               g_lastSound;  // written on the audio thread, read for the status: a torn value does no harm

	// per interior actor, since the last status: the lowest and highest the actor stood (0.8.16: NPCs sank into the floor)
	std::vector<float> g_actorZMin;
	std::vector<float> g_actorZMax;

	// status since the last report
	std::uint64_t g_rendered{ 0 };
	std::uint64_t g_passFrame{ 0 };          // Trace::Frame() of the latest pass
	std::uint32_t g_passesInFrame{ 0 };
	std::uint32_t g_maxPassesPerFrame{ 0 };  // 0.8.18: the picture doubled after a visit inside; one pass a frame expected
	std::uint64_t g_skipped{ 0 };
	std::string   g_skipWhy;
	double        g_cpuMsSum{ 0.0 };
	double        g_cpuMsMax{ 0.0 };
	float         g_eyeDistance{ 0.0f };
	RE::NiFrustum g_frustum{};
	VisiblePart   g_visible{};
	std::uint32_t g_viewportWidth{ 0 };
	std::uint32_t g_viewportHeight{ 0 };
	std::size_t   g_actorsShown{ 0 };
	bool          g_textureSwapped{ false };
	std::uint64_t g_effectSetups{ 0 };      // BSEffectShader techniques set up during our passes
	std::uint64_t g_softEffectSetups{ 0 };  // ... of them with soft particles
	std::uint64_t g_depthCopies{ 0 };
	std::uint64_t g_effectLightsHeld{ 0 };  // effect shapes registered with our lights asleep
	std::uint64_t g_animated{ 0 };          // actor animation updates made by the portal
	std::uint64_t g_animatedByGame{ 0 };    // passes for which the game had updated the actor itself
	std::uint64_t g_poseChecks{ 0 };
	std::uint64_t g_poseChanged{ 0 };       // ... at which a bone of the actor stood elsewhere than at the last pass
	std::uint64_t g_positionsSynced{ 0 };
	std::uint64_t g_controllerUpdates{ 0 };
	double        g_controllerMsSum{ 0.0 };
	std::uint64_t g_softEffectsDropped{ 0 };  // soft effect techniques drawn without their soft part
	std::uint64_t g_actorLightsLent{ 0 };     // our lights put into the light lists of the actors' shapes for a pass
	std::uint64_t g_shapeRecollects{ 0 };     // times the lights' shapes were collected again: none had any

	// The particle packer in this pass: the cameras it could face, and what RendererShadowState held for it.
	PackCamera    g_packPortal;
	PackCamera    g_packMain;
	PackCamera    g_packView;
	std::uint64_t g_packCalls{ 0 };
	std::uint64_t g_packHeldPortal{ 0 };
	std::uint64_t g_packHeldMain{ 0 };
	std::uint64_t g_packHeldOther{ 0 };
	float         g_packOriginDrift{ 0.0f };  // units between the held origin and the portal camera

	template <class T>
	T* AsRex(void* a_object)
	{
		return static_cast<T*>(a_object);
	}

	ID3D11Device* Device()
	{
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		return renderer ? reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder) : nullptr;
	}

	// NG's RenderTargetManager::GetSingleton() dereferences AE#411451, but that address is the manager
	// itself (the game passes it with lea): 0.8.6 read its first target's size (2560 x 1600) as a pointer.
	RE::BSGraphics::RenderTargetManager* RenderTargets()
	{
		static REL::Relocation<RE::BSGraphics::RenderTargetManager*> manager{ RELOCATION_ID(524970, 411451) };
		return manager.get();
	}

	ID3D11DeviceContext* Context()
	{
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		return renderer ? reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context) : nullptr;
	}

	RE::ShadowSceneNode* WorldScene()
	{
		return RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0];
	}

	// The game's own main render target: our default size, and its format for the trace.
	D3D11_TEXTURE2D_DESC MainTargetDesc()
	{
		D3D11_TEXTURE2D_DESC desc{};
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		if (auto* texture = renderer ? reinterpret_cast<ID3D11Texture2D*>(renderer->GetRuntimeData().renderTargets[RenderTarget::kMAIN].texture) : nullptr) {
			texture->GetDesc(&desc);
		}
		return desc;
	}

	bool CreateTarget(std::string& a_why)
	{
		if (!g_targetFailure.empty()) {  // first: a failure can leave the color texture behind
			a_why = g_targetFailure;
			return false;
		}
		if (g_target.color) {
			return true;
		}
		auto* device = Device();
		if (!device) {
			a_why = "no D3D11 device";
			return false;
		}
		const auto& settings = Settings::Get();
		const auto  fail = [&](std::string_view a_what, HRESULT a_result) {
            g_targetFailure = std::format("{} failed: 0x{:08X}", a_what, static_cast<std::uint32_t>(a_result));
            a_why = g_targetFailure;
            return false;
		};

		// HDR, like the game's main target: the quad shows it before tone mapping. Screen-sized by
		// default: each frame only the part the door covers on screen is drawn (0.8.7 used 1024 x 1024
		// and the picture went soft with the door filling the screen).
		const auto main = MainTargetDesc();
		D3D11_TEXTURE2D_DESC color{};
		color.Width = (std::max)(settings.portalWidth ? settings.portalWidth : main.Width, 64u);
		color.Height = (std::max)(settings.portalHeight ? settings.portalHeight : main.Height, 64u);
		color.MipLevels = 1;
		color.ArraySize = 1;
		color.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		color.SampleDesc.Count = 1;
		color.Usage = D3D11_USAGE_DEFAULT;
		color.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		if (const auto hr = device->CreateTexture2D(&color, nullptr, &g_target.color); FAILED(hr)) {
			return fail("CreateTexture2D(color)", hr);
		}
		if (const auto hr = device->CreateRenderTargetView(g_target.color, nullptr, &g_target.rtv); FAILED(hr)) {
			return fail("CreateRenderTargetView", hr);
		}
		if (const auto hr = device->CreateShaderResourceView(g_target.color, nullptr, &g_target.srv); FAILED(hr)) {
			return fail("CreateShaderResourceView(color)", hr);
		}

		D3D11_TEXTURE2D_DESC depth = color;
		depth.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depth.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		if (const auto hr = device->CreateTexture2D(&depth, nullptr, &g_target.depth); FAILED(hr)) {
			return fail("CreateTexture2D(depth)", hr);
		}
		D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
		dsv.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (const auto hr = device->CreateDepthStencilView(g_target.depth, &dsv, &g_target.dsv); FAILED(hr)) {
			return fail("CreateDepthStencilView", hr);
		}
		dsv.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
		if (const auto hr = device->CreateDepthStencilView(g_target.depth, &dsv, &g_target.dsvReadOnly); FAILED(hr)) {
			return fail("CreateDepthStencilView(read only)", hr);
		}
		D3D11_SHADER_RESOURCE_VIEW_DESC depthSrv{};
		depthSrv.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		depthSrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		depthSrv.Texture2D.MipLevels = 1;
		if (const auto hr = device->CreateShaderResourceView(g_target.depth, &depthSrv, &g_target.depthSrv); FAILED(hr)) {
			return fail("CreateShaderResourceView(depth)", hr);
		}
		D3D11_TEXTURE2D_DESC copy = depth;
		copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (const auto hr = device->CreateTexture2D(&copy, nullptr, &g_target.depthCopy); FAILED(hr)) {
			return fail("CreateTexture2D(depth copy)", hr);
		}
		if (const auto hr = device->CreateShaderResourceView(g_target.depthCopy, &depthSrv, &g_target.depthCopySrv); FAILED(hr)) {
			return fail("CreateShaderResourceView(depth copy)", hr);
		}
		g_target.width = color.Width;
		g_target.height = color.Height;
		Trace::Write("portal.init", Trace::Args{}
										.Int("width", g_target.width)
										.Int("height", g_target.height)
										.Int("colorSlot", settings.portalColorSlot)
										.Int("depthSlot", settings.portalDepthSlot)
										.Raw("gameMainTarget", Trace::Args{}.Int("width", main.Width).Int("height", main.Height).Int("dxgiFormat", main.Format).Json()),
			nullptr, true);
		return true;
	}

	// The quad model, loaded once and cloned: its own NiSourceTexture (a path no other model uses).
	bool LoadQuad(std::string& a_why)
	{
		if (g_quad) {
			return true;
		}
		RE::NiPointer<RE::NiNode>         model;
		RE::BSModelDB::DBTraits::ArgsType args{};
		const auto                        error = RE::BSModelDB::Demand(kQuadModel, model, args);
		if (!model) {
			a_why = std::format("cannot load meshes\\{} (error {})", kQuadModel, std::to_underlying(error));
			return false;
		}
		auto* clone = netimmerse_cast<RE::NiAVObject*>(model->Clone());
		if (!clone) {
			a_why = "cannot clone the quad model";
			return false;
		}
		g_quad.reset(clone);
		RE::BSVisit::TraverseScenegraphGeometries(clone, [&](RE::BSGeometry* a_geometry) {
			auto* property = netimmerse_cast<RE::BSEffectShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get());
			g_quadMaterial = property ? static_cast<RE::BSEffectShaderMaterial*>(property->material) : nullptr;
			return g_quadMaterial ? RE::BSVisit::BSVisitControl::kStop : RE::BSVisit::BSVisitControl::kContinue;
		});
		if (!g_quadMaterial) {
			g_quad.reset();
			a_why = "the quad model has no effect shader material";
			return false;
		}
		return true;
	}

	// The material the quad's shape has now: MapQuad writes the UVs into g_quadMaterial.
	const RE::BSEffectShaderMaterial* QuadMaterialNow()
	{
		const RE::BSEffectShaderMaterial* material = nullptr;
		if (g_quad) {
			RE::BSVisit::TraverseScenegraphGeometries(g_quad.get(), [&](RE::BSGeometry* a_geometry) {
				auto* property = netimmerse_cast<RE::BSEffectShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get());
				material = property ? static_cast<const RE::BSEffectShaderMaterial*>(property->material) : nullptr;
				return material ? RE::BSVisit::BSVisitControl::kStop : RE::BSVisit::BSVisitControl::kContinue;
			});
		}
		return material;
	}

	// Every pass: the texture may be (re)loaded after the model, so check the view each time.
	void SwapQuadTexture()
	{
		auto* texture = g_quadMaterial && g_quadMaterial->sourceTexture ? g_quadMaterial->sourceTexture->rendererTexture : nullptr;
		auto* ours = AsRex<REX::W32::ID3D11ShaderResourceView>(g_target.srv);
		if (!texture || texture->resourceView == ours) {
			g_textureSwapped = texture != nullptr;
			return;
		}
		if (texture->resourceView) {
			g_replacedViews.push_back(reinterpret_cast<ID3D11ShaderResourceView*>(texture->resourceView));
		}
		g_target.srv->AddRef();  // the game releases the view with the texture
		texture->resourceView = ours;
		g_textureSwapped = true;
	}

	void PlaceQuad()
	{
		auto*       parent = g_door.parent;
		const float scale = (std::max)(g_door.width, g_door.height);
		// The quad lies in its local XZ plane, x to the right, z up, -0.5..0.5: the columns carry the
		// door's aspect (NiTransform has one scale), the scale its larger side.
		RE::NiTransform world;
		const RE::NiPoint3 columns[3]{ g_door.right * (g_door.width / scale), g_door.inward, g_door.up * (g_door.height / scale) };
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 3; ++column) {
				world.rotate.entry[row][column] = (&columns[column].x)[row];
			}
		}
		world.translate = g_door.center;
		world.scale = scale;
		g_quad->local = parent->world.Invert() * world;
		g_quad->SetAppCulled(false);
		parent->AttachChild(g_quad.get());  // at the end, as the interior's container in 0.8.5
		RE::NiUpdateData update{};
		g_quad->Update(update);
		// BSEffectShader::SetupGeometry builds the motion vectors from previousWorld (+0xB0). Update moves
		// world there, and nothing updates a static quad again: placed twice, it kept the first place for
		// good. After a visit the door was still moving at the first start (1.5 units off), TAA took the
		// quad for moving by that much every frame and mixed in history from the wrong pixels (0.8.18-0.8.20).
		RE::BSVisit::TraverseScenegraphObjects(g_quad.get(), [](RE::NiAVObject* a_object) {
			a_object->previousWorld = a_object->world;
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}

	// How far the quad's shape stands from its previous transform: 0 keeps TAA out of the picture.
	float QuadPreviousOffset()
	{
		float offset = -1.0f;
		if (g_quad) {
			RE::BSVisit::TraverseScenegraphGeometries(g_quad.get(), [&](RE::BSGeometry* a_geometry) {
				offset = a_geometry->world.translate.GetDistance(a_geometry->previousWorld.translate);
				return RE::BSVisit::BSVisitControl::kStop;
			});
		}
		return offset;
	}

	// The game's manager of master particle systems (mov rcx, [AE#411270] in AE#19206): candle flames and
	// other addon nodes. Child 5 of the world's root, no part of any cell.
	RE::BSParticleSystemManager* ParticleManager()
	{
		static REL::Relocation<RE::BSParticleSystemManager**> manager{ REL::ID(411270) };
		return *manager;
	}

	// The particle budget, AE#71099 once a frame: emission * (iMaxDesired:Particles / live particles)^2
	// when there are more of them than that.
	float& EmissionScale()
	{
		static REL::Relocation<float*> scale{ REL::ID(388630) };
		return *scale;
	}

	RE::NiPoint3 DoorPoint(float a_u, float a_v)
	{
		return g_door.center + g_door.right * ((a_u - 0.5f) * g_door.width) + g_door.up * ((0.5f - a_v) * g_door.height);
	}

	// Sutherland-Hodgman against one plane: keeps the points with a_distance >= 0.
	template <class Distance>
	void ClipPolygon(std::vector<RE::NiPoint3>& a_polygon, Distance a_distance)
	{
		std::vector<RE::NiPoint3> kept;
		for (std::size_t i = 0; i < a_polygon.size(); ++i) {
			const auto& a = a_polygon[i];
			const auto& b = a_polygon[(i + 1) % a_polygon.size()];
			const float da = a_distance(a);
			const float db = a_distance(b);
			if (da >= 0.0f) {
				kept.push_back(a);
			}
			if ((da >= 0.0f) != (db >= 0.0f)) {
				kept.push_back(a + (b - a) * (da / (da - db)));
			}
		}
		a_polygon = std::move(kept);
	}

	// The door rectangle clipped by the main camera's frustum: what part of it is on screen, and how
	// many pixels that part covers. False when none of it is.
	bool SeeDoor(const RE::NiCamera* a_main, VisiblePart& a_part)
	{
		const auto&        world = a_main->world;
		const auto&        frustum = a_main->GetRuntimeData2().viewFrustum;
		const auto         eye = world.translate;
		const RE::NiPoint3 direction{ world.rotate.entry[0][0], world.rotate.entry[1][0], world.rotate.entry[2][0] };
		const RE::NiPoint3 up{ world.rotate.entry[0][1], world.rotate.entry[1][1], world.rotate.entry[2][1] };
		const RE::NiPoint3 right{ world.rotate.entry[0][2], world.rotate.entry[1][2], world.rotate.entry[2][2] };
		const auto         x = [&](const RE::NiPoint3& a_point) { return (a_point - eye).Dot(right); };
		const auto         y = [&](const RE::NiPoint3& a_point) { return (a_point - eye).Dot(up); };
		const auto         z = [&](const RE::NiPoint3& a_point) { return (a_point - eye).Dot(direction); };

		std::vector<RE::NiPoint3> polygon{ DoorPoint(0.0f, 0.0f), DoorPoint(1.0f, 0.0f), DoorPoint(1.0f, 1.0f), DoorPoint(0.0f, 1.0f) };
		ClipPolygon(polygon, [&](const RE::NiPoint3& p) { return z(p) - frustum.fNear; });
		ClipPolygon(polygon, [&](const RE::NiPoint3& p) { return x(p) - frustum.fLeft * z(p); });
		ClipPolygon(polygon, [&](const RE::NiPoint3& p) { return frustum.fRight * z(p) - x(p); });
		ClipPolygon(polygon, [&](const RE::NiPoint3& p) { return y(p) - frustum.fBottom * z(p); });
		ClipPolygon(polygon, [&](const RE::NiPoint3& p) { return frustum.fTop * z(p) - y(p); });
		if (polygon.size() < 3) {
			return false;
		}
		const auto  screen = MainTargetDesc();
		float       u0 = 1.0f, u1 = 0.0f, v0 = 1.0f, v1 = 0.0f;
		float       sx0 = FLT_MAX, sx1 = -FLT_MAX, sy0 = FLT_MAX, sy1 = -FLT_MAX;
		for (const auto& point : polygon) {
			const auto offset = point - g_door.center;
			const float u = std::clamp(offset.Dot(g_door.right) / g_door.width + 0.5f, 0.0f, 1.0f);
			const float v = std::clamp(0.5f - offset.Dot(g_door.up) / g_door.height, 0.0f, 1.0f);
			u0 = (std::min)(u0, u);
			u1 = (std::max)(u1, u);
			v0 = (std::min)(v0, v);
			v1 = (std::max)(v1, v);
			const float depth = (std::max)(z(point), frustum.fNear);
			const float sx = (x(point) / depth - frustum.fLeft) / (frustum.fRight - frustum.fLeft) * static_cast<float>(screen.Width);
			const float sy = (frustum.fTop - y(point) / depth) / (frustum.fTop - frustum.fBottom) * static_cast<float>(screen.Height);
			sx0 = (std::min)(sx0, sx);
			sx1 = (std::max)(sx1, sx);
			sy0 = (std::min)(sy0, sy);
			sy1 = (std::max)(sy1, sy);
		}
		if (u1 - u0 < 1e-4f || v1 - v0 < 1e-4f) {
			return false;
		}
		a_part = { .u0 = u0, .u1 = u1, .v0 = v0, .v1 = v1, .pixelsWide = sx1 - sx0, .pixelsHigh = sy1 - sy0 };
		return true;
	}

	// The portal camera in the interior's coordinates: at the eye carried through the door, looking
	// across the door plane, its frustum through the visible part of the door rectangle.
	bool AimCamera(RE::NiCamera* a_main, const VisiblePart& a_part, std::string& a_why)
	{
		const auto eyeOutside = a_main->world.translate;
		const auto distance = (g_door.center - eyeOutside).Dot(g_door.inward);
		g_eyeDistance = distance;
		if (distance < kMinEyeDistance) {
			a_why = "eye at or behind the door plane";
			return false;
		}
		const auto toInterior = g_door.toExterior.Invert();
		const auto eye = toInterior * eyeOutside;
		const auto direction = toInterior.rotate * g_door.inward;
		const auto up = toInterior.rotate * g_door.up;
		const auto right = toInterior.rotate * g_door.right;

		const auto& mainView = a_main->GetRuntimeData2();
		// NiFrustum: left/right/top/bottom at distance 1 for perspective cameras (0.8.6: the world
		// camera's are +-0.755 x +-0.472); if they ever look like near-plane extents, use those units.
		const bool  unitDistance = std::abs(mainView.viewFrustum.fRight) < 4.0f;
		const float nearPlane = (std::max)(distance - kNearBehindDoor, 1.0f);
		float       left = FLT_MAX, rightEdge = -FLT_MAX, bottom = FLT_MAX, top = -FLT_MAX;
		for (const float u : { a_part.u0, a_part.u1 }) {
			for (const float v : { a_part.v0, a_part.v1 }) {
				const auto corner = toInterior * DoorPoint(u, v);
				const auto offset = corner - eye;
				const auto depth = offset.Dot(direction);
				const auto x = offset.Dot(right) / depth * (unitDistance ? 1.0f : nearPlane);
				const auto y = offset.Dot(up) / depth * (unitDistance ? 1.0f : nearPlane);
				left = (std::min)(left, x);
				rightEdge = (std::max)(rightEdge, x);
				bottom = (std::min)(bottom, y);
				top = (std::max)(top, y);
			}
		}

		auto& camera = *g_camera;
		// Gamebryo cameras look along local +X, with +Y up and +Z right.
		const RE::NiPoint3 columns[3]{ direction, up, right };
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 3; ++column) {
				camera.local.rotate.entry[row][column] = (&columns[column].x)[row];
			}
		}
		camera.local.translate = eye;
		camera.local.scale = 1.0f;
		auto& view = camera.GetRuntimeData2();
		view.viewFrustum.fLeft = left;
		view.viewFrustum.fRight = rightEdge;
		view.viewFrustum.fTop = top;
		view.viewFrustum.fBottom = bottom;
		view.viewFrustum.fNear = nearPlane;
		view.viewFrustum.fFar = mainView.viewFrustum.fFar;
		view.viewFrustum.bOrtho = false;
		view.port = mainView.port;
		view.lodAdjust = mainView.lodAdjust;
		RE::NiUpdateData update{};
		camera.Update(update);
		g_frustum = view.viewFrustum;
		return true;
	}

	// The picture fills the top left a_width x a_height of the target, the visible part of the door.
	void MapQuad(const VisiblePart& a_part, std::uint32_t a_width, std::uint32_t a_height)
	{
		if (!g_quadMaterial) {
			return;
		}
		// BSEffectShader: texcoord = uv * scale + offset. Both buffers: the game alternates them
		// (BSShaderManager::State::textureTransformCurrentBuffer); 0.8.8 set only [0] and every other
		// frame the quad showed the whole target, stale parts of earlier passes included.
		const float scaleU = static_cast<float>(a_width) / static_cast<float>(g_target.width) / (a_part.u1 - a_part.u0);
		const float scaleV = static_cast<float>(a_height) / static_cast<float>(g_target.height) / (a_part.v1 - a_part.v0);
		for (int buffer = 0; buffer < 2; ++buffer) {
			g_quadMaterial->texCoordScale[buffer] = { scaleU, scaleV };
			g_quadMaterial->texCoordOffset[buffer] = { -a_part.u0 * scaleU, -a_part.v0 * scaleV };
		}
	}

	// ------------------------------------------------------------------
	// Lights
	// ------------------------------------------------------------------
	bool IsLight(const RE::NiAVObject* a_object)
	{
		const auto*            rtti = a_object->GetRTTI();
		const std::string_view name = rtti && rtti->name ? rtti->name : "";
		return name == "NiPointLight" || name == "NiSpotLight";
	}

	// Restricted to the interior's 3D, so it lights nothing outside; never a shadow light (no shadow
	// map is drawn for the portal).
	RE::BSLight* AddRestricted(RE::NiLight* a_light, bool a_dynamic)
	{
		auto* scene = WorldScene();
		if (!scene) {
			return nullptr;
		}
		LightParams params{};
		params.dynamic = a_dynamic;
		params.shadowLight = false;
		params.portalStrict = false;
		params.affectLand = true;
		params.affectWater = true;
		params.neverFades = true;
		params.fov = 0.0f;
		params.falloff = 1.0f;
		params.nearDistance = 5.0f;
		params.depthBias = 0.0f;
		params.sceneGraphIndex = 0;
		params.restrictedNode = g_door.scene.get();
		params.lensFlareData = nullptr;
		return scene->AddLight(a_light, params);
	}

	std::string LightJson(const AddedLight& a_added)
	{
		const auto& light = *a_added.copy;
		const auto& data = light.GetLightRuntimeData();
		Trace::Args args;
		args.Str("name", a_added.source ? a_added.source->name.c_str() : "")
			.Bool("mirror", a_added.mirror)
			.Pos("pos", light.world.translate)
			.Pos("radius", data.radius)
			.Raw("diffuse", std::format("[{:.2f},{:.2f},{:.2f}]", data.diffuse.red, data.diffuse.green, data.diffuse.blue))
			.Num("fade", data.fade)
			.Bool("added", a_added.light != nullptr);
		if (a_added.light) {
			args.Int("litShapes", a_added.light->geomList.size())
				.Int("frustumCull", a_added.light->frustrumCull)
				.Num("lodDimmer", a_added.light->lodDimmer)
				.Bool("pointLight", a_added.light->pointLight)
				.Bool("hasObjectNode", a_added.light->objectNode != nullptr);
		}
		return args.Json();
	}

	// Copies of the interior's own point and spot lights. The originals stay untouched: the game adds
	// them again when the player enters, and removing by the same NiLight could take its light away.
	// 0.8.8: all 18 of the tavern's lights were in ShadowSceneNode[0] already (its 3D was loaded), but
	// their lit shapes come from the scene the game culls, which the unattached interior is not in.
	void AddInteriorLights(std::size_t& a_alreadyInScene)
	{
		if (!Settings::Get().portalInteriorLights) {
			return;
		}
		auto* scene = WorldScene();
		std::vector<RE::NiPointer<RE::NiLight>> found;
		RE::BSVisit::TraverseScenegraphObjects(g_door.scene.get(), [&](RE::NiAVObject* a_object) {
			if (IsLight(a_object)) {
				found.emplace_back(static_cast<RE::NiLight*>(a_object));
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		for (auto& light : found) {
			if (scene && scene->GetLight(light.get())) {
				++a_alreadyInScene;
			}
			auto* copy = netimmerse_cast<RE::NiLight*>(light->Clone());
			if (!copy) {
				continue;
			}
			copy->local = light->world;  // the copy has no parent: its world is the original's
			RE::NiUpdateData update{};
			copy->Update(update);
			AddedLight added{ .source = light, .copy = RE::NiPointer<RE::NiLight>(copy) };
			added.light = AddRestricted(copy, false);
			g_lights.push_back(std::move(added));
		}
	}

	// ShadowSceneNode: collect the shapes a light reaches again (AE#106342; the game calls it for the
	// moving lights in the world camera's frustum).
	void CollectLitShapes(RE::ShadowSceneNode* a_scene, RE::BSLight* a_light)
	{
		using Collect = void (*)(RE::ShadowSceneNode*, RE::BSLight*);
		static REL::Relocation<Collect> collect{ REL::ID(106342) };
		collect(a_scene, a_light);
	}

	// For the pass, our lights count as in the camera's frustum and undimmed: ShadowSceneNode tests
	// them against the world camera (AE#108278), far from the interior's coordinates. The lights of a
	// shape are picked when the culling registers it (BSLightingShaderProperty::GetRenderPasses,
	// AE#106516 -> AE#107784), and lights with frustrumCull 0xFF are skipped there: 0.8.9 woke them for
	// SubmitAccumulator only, after the passes were built without them. Put back after the pass.
	struct WokenLight
	{
		RE::BSLight*  light;
		std::uint32_t frustumCull;
		float         lodDimmer;
	};

	std::vector<WokenLight> WakeLights()
	{
		std::vector<WokenLight> woken;
		for (auto& added : g_lights) {
			if (auto* light = added.light) {
				woken.push_back({ light, light->frustrumCull, light->lodDimmer });
				light->frustrumCull = kLightInFrustum;
				light->lodDimmer = 1.0f;
				light->worldTranslate = added.copy->world.translate;
			}
		}
		return woken;
	}

	void SleepLights(const std::vector<WokenLight>& a_woken)
	{
		for (const auto& woken : a_woken) {
			woken.light->frustrumCull = woken.frustumCull;
			woken.light->lodDimmer = woken.lodDimmer;
		}
	}

	// GetRenderPasses (AE#106516) keeps a shape's passes, their lights included, until its light list
	// changes. Passes built while our lights were asleep have none of them: mark every list changed.
	// Lit shapes only: what lightData points to in other properties is not known.
	void ForceNewRenderPasses()
	{
		RE::BSVisit::TraverseScenegraphGeometries(g_door.scene.get(), [](RE::BSGeometry* a_geometry) {
			auto* property = netimmerse_cast<RE::BSLightingShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get());
			if (auto* data = property ? property->lightData : nullptr) {
				data->lightListChanged = true;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}

	// The player's dynamic lights near the door, carried into the interior's coordinates every pass.
	void MirrorPlayerLights()
	{
		if (!Settings::Get().portalMirrorPlayerLights) {
			return;
		}
		auto* scene = WorldScene();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!scene || !player) {
			return;
		}
		const auto center = player->GetPosition();
		std::vector<RE::NiPointer<RE::NiLight>> sources;
		for (const auto& light : scene->GetRuntimeData().activeLights) {
			auto* source = light ? light->light.get() : nullptr;
			if (!source || !light->dynamic || light->objectNode || source->world.translate.GetDistance(center) > kMirrorRadius) {
				continue;  // static, restricted (ours) or far
			}
			if (std::ranges::any_of(g_lights, [&](const AddedLight& a_added) { return a_added.copy.get() == source; })) {
				continue;
			}
			sources.emplace_back(source);
		}
		for (auto& added : g_lights) {
			added.seen = !added.mirror;
		}
		const auto toInterior = g_door.toExterior.Invert();
		for (auto& source : sources) {
			auto it = std::ranges::find_if(g_lights, [&](const AddedLight& a_added) { return a_added.mirror && a_added.source == source; });
			if (it == g_lights.end()) {
				auto* copy = netimmerse_cast<RE::NiLight*>(source->Clone());
				if (!copy) {
					continue;
				}
				g_lights.push_back(AddedLight{ .source = source, .copy = RE::NiPointer<RE::NiLight>(copy), .mirror = true });
				it = std::prev(g_lights.end());
				it->light = AddRestricted(copy, true);
			}
			it->seen = true;
			auto& copy = *it->copy;
			copy.GetLightRuntimeData() = source->GetLightRuntimeData();
			copy.local.rotate = toInterior.rotate * source->world.rotate;
			copy.local.translate = toInterior * source->world.translate;
			copy.local.scale = 1.0f;
			RE::NiUpdateData update{};
			copy.Update(update);
			if (it->light) {
				// ShadowSceneNode updates these only for lights in the world camera's frustum (AE#106335)
				it->light->worldTranslate = copy.world.translate;
				if (copy.world.translate.GetDistance(it->gatheredAt) > kRegatherDistance) {
					it->gatheredAt = copy.world.translate;
					CollectLitShapes(scene, it->light);
				}
			}
		}
		for (auto it = g_lights.begin(); it != g_lights.end();) {
			if (it->mirror && !it->seen) {
				scene->RemoveLight(it->copy.get());
				it = g_lights.erase(it);
			} else {
				++it;
			}
		}
	}

	// The world's lights near the player, for the trace: is the torch a dynamic light?
	std::string NearPlayerLightsJson()
	{
		auto* scene = WorldScene();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!scene || !player) {
			return "[]";
		}
		const auto  center = player->GetPosition();
		std::string json;
		for (const auto& light : scene->GetRuntimeData().activeLights) {
			auto* source = light ? light->light.get() : nullptr;
			if (!source || source->world.translate.GetDistance(center) > kMirrorRadius) {
				continue;
			}
			json += (json.empty() ? "" : ",") + Trace::Args{}
													  .Str("name", source->name.c_str())
													  .Num("distance", source->world.translate.GetDistance(center))
													  .Bool("dynamic", light->dynamic)
													  .Bool("pointLight", light->pointLight)
													  .Bool("restricted", light->objectNode != nullptr)
													  .Num("radius", source->GetLightRuntimeData().radius.x)
													  .Json();
		}
		return "[" + json + "]";
	}

	// The interior's own lights in ShadowSceneNode[0]: once the cell is attached (0.8.16), do they collect
	// the interior's shapes themselves? Our copies then light them a second time.
	std::string OriginalLightsJson()
	{
		auto*        scene = WorldScene();
		std::int64_t inScene = 0;
		std::int64_t lighting = 0;
		std::int64_t litShapes = 0;
		for (const auto& added : g_lights) {
			auto* light = !added.mirror && added.source && scene ? scene->GetLight(added.source.get()) : nullptr;
			if (light) {
				const auto shapes = static_cast<std::int64_t>(light->geomList.size());
				++inScene;
				lighting += shapes > 0 ? 1 : 0;
				litShapes += shapes;
			}
		}
		return Trace::Args{}.Int("inScene", inScene).Int("lighting", lighting).Int("litShapes", litShapes).Json();
	}

	bool HiddenBelow(const RE::NiAVObject* a_object, const RE::NiAVObject* a_root)
	{
		for (const auto* node = a_object; node; node = node == a_root ? nullptr : node->parent) {
			if (node->GetAppCulled()) {
				return true;
			}
		}
		return false;
	}

	// One shape of a light source's model: how it would glow (own emittance, the emittance of a light
	// reference, a glow map), its alpha blending, and whether a pass ever drew it.
	std::string ShapeSampleJson(RE::BSGeometry* a_geometry, const RE::NiAVObject* a_root)
	{
		using Flag = RE::BSShaderProperty::EShaderPropertyFlag;
		auto*       property = a_geometry->GetGeometryRuntimeData().shaderProperty.get();
		const auto* alpha = a_geometry->GetGeometryRuntimeData().alphaProperty.get();
		const auto* rtti = property ? property->GetRTTI() : nullptr;
		Trace::Args args;
		args.Str("shape", a_geometry->name.c_str())
			.Str("shader", rtti && rtti->name ? rtti->name : "-")
			.Bool("hidden", HiddenBelow(a_geometry, a_root))
			.Bool("alphaProperty", alpha != nullptr)
			.Num("radius", a_geometry->worldBound.radius);
		if (property) {
			args.Bool("ownEmit", property->flags.all(Flag::kOwnEmit))
				.Bool("externalEmittance", property->flags.all(Flag::kExternalEmittance))
				.Bool("glowMap", property->flags.all(Flag::kGlowMap))
				.Num("alpha", property->alpha)
				.Int("lastAccumulated", property->lastAccumulatedFrameCount);
		}
		if (auto* lit = netimmerse_cast<RE::BSLightingShaderProperty*>(property)) {
			const auto* emit = lit->emissiveColor;
			args.Raw("emit", emit ? std::format("[{:.2f},{:.2f},{:.2f}]", emit->red, emit->green, emit->blue) : "null")
				.Num("emitMult", lit->emissiveMult);
		}
		return args.Json();
	}

	// The interior's light references and the references whose model looks like a light source
	// (candles, lanterns, braziers): what their 3D holds. 0.8.12-0.8.15: no candle flame through the
	// portal, and no candle among the interior's effect shapes. Grouped by model.
	std::string LightSourceRefsJson()
	{
		struct Model
		{
			std::int64_t refs{ 0 };
			std::int64_t lights{ 0 };  // the base is a LIGH
			std::int64_t with3D{ 0 };
			std::int64_t effectShapes{ 0 };
			std::int64_t litShapes{ 0 };
			std::int64_t otherShapes{ 0 };
			std::int64_t hiddenShapes{ 0 };
			std::string  samples;  // the shapes of one reference (candles: where is the flame?)
			std::string  addonNodes;  // its BSValueNodes: where the game hangs an addon (candle flames: BSMasterParticleSystem)
		};
		static constexpr std::array<std::string_view, 6> keywords{ "candle", "lantern", "chandelier", "brazier", "torch", "fire" };
		std::map<std::string, Model> models;
		g_door.cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			auto*       base = a_ref ? a_ref->GetBaseObject() : nullptr;
			const auto* model = base ? base->As<RE::TESModel>() : nullptr;
			std::string path = model && model->GetModel() ? model->GetModel() : "";
			std::string lower = path;
			std::ranges::transform(lower, lower.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
			const bool light = base && base->Is(RE::FormType::Light);
			if (!light && std::ranges::none_of(keywords, [&](std::string_view a_word) { return lower.find(a_word) != std::string::npos; })) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			auto& entry = models[path.empty() ? "(no model)" : path];
			++entry.refs;
			entry.lights += light ? 1 : 0;
			if (auto* root = a_ref->Get3D()) {
				const bool sample = entry.with3D == 0;
				++entry.with3D;
				if (sample) {
					RE::BSVisit::TraverseScenegraphObjects(root, [&](RE::NiAVObject* a_object) {
						const auto* rtti = a_object->GetRTTI();
						if (!rtti || !rtti->name || std::string_view{ rtti->name } != "BSValueNode") {
							return RE::BSVisit::BSVisitControl::kContinue;
						}
						auto*       node = static_cast<RE::BSValueNode*>(a_object);
						entry.addonNodes += (entry.addonNodes.empty() ? "" : ",") + Trace::Args{}
																						.Str("name", node->name.c_str())
																						.Int("value", node->value)
																						.Bool("masterParticleSystem", node->associatedObject != nullptr)
																						.Int("children", static_cast<std::int64_t>(node->GetChildren().size()))
																						.Bool("hidden", HiddenBelow(node, root))
																						.Json();
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				}
				RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geometry) {
					auto* property = a_geometry->GetGeometryRuntimeData().shaderProperty.get();
					if (sample) {
						entry.samples += (entry.samples.empty() ? "" : ",") + ShapeSampleJson(a_geometry, root);
					}
					if (netimmerse_cast<RE::BSEffectShaderProperty*>(property)) {
						++entry.effectShapes;
					} else if (netimmerse_cast<RE::BSLightingShaderProperty*>(property)) {
						++entry.litShapes;
					} else {
						++entry.otherShapes;
					}
					entry.hiddenShapes += HiddenBelow(a_geometry, root) ? 1 : 0;
					return RE::BSVisit::BSVisitControl::kContinue;
				});
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		std::string json;
		for (const auto& [path, entry] : models) {
			json += (json.empty() ? "" : ",") + Trace::Args{}
													  .Str("model", path)
													  .Int("refs", entry.refs)
													  .Int("lights", entry.lights)
													  .Int("with3D", entry.with3D)
													  .Int("effectShapes", entry.effectShapes)
													  .Int("litShapes", entry.litShapes)
													  .Int("otherShapes", entry.otherShapes)
													  .Int("hiddenShapes", entry.hiddenShapes)
													  .Raw("shapes", "[" + entry.samples + "]")
													  .Raw("addonNodes", "[" + entry.addonNodes + "]")
													  .Json();
		}
		return "[" + json + "]";
	}

	// An attached interior's actors are lit by ShadowSceneNode as moving objects (AE#106349 queues them),
	// from the lights awake for the world camera: our copies sleep then (0.8.17: NPCs almost black
	// through the portal; in 0.8.15, unattached, our copies collected their shapes like any other). For
	// the pass, our lights that reach an actor's lit shape go into its light list; out again after it.
	struct LentLight
	{
		RE::BSShaderPropertyLightData* data;
		RE::BSLight*                   light;
	};

	std::vector<LentLight> LendActorLights()
	{
		std::vector<LentLight> lent;
		if (!g_door.attached) {
			return lent;
		}
		for (auto& root : g_actorRoots) {
			if (!root) {
				continue;
			}
			RE::BSVisit::TraverseScenegraphGeometries(root.get(), [&](RE::BSGeometry* a_geometry) {
				auto* property = netimmerse_cast<RE::BSLightingShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get());
				auto* data = property ? property->lightData : nullptr;
				if (!data) {
					return RE::BSVisit::BSVisitControl::kContinue;
				}
				const auto& bound = a_geometry->worldBound;
				for (auto& added : g_lights) {
					auto* light = added.light;
					if (!light || added.copy->world.translate.GetDistance(bound.center) > added.copy->GetLightRuntimeData().radius.x + bound.radius) {
						continue;
					}
					if (std::ranges::find(data->lights, light) != data->lights.end()) {
						continue;
					}
					data->lights.push_back(light);
					data->lightListChanged = true;
					lent.push_back({ data, light });
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
		g_actorLightsLent += lent.size();
		return lent;
	}

	void ReturnActorLights(const std::vector<LentLight>& a_lent)
	{
		for (const auto& [data, light] : a_lent) {
			if (const auto it = std::ranges::find(data->lights, light); it != data->lights.end()) {
				data->lights.erase(it);
				data->lightListChanged = true;
			}
		}
	}

	void RemoveLights()
	{
		if (auto* scene = WorldScene()) {
			for (auto& added : g_lights) {
				if (added.light) {
					scene->RemoveLight(added.copy.get());
				}
			}
		}
		g_lights.clear();
	}

	// A few lit shapes of the interior near its door: do their light lists fill up?
	void SampleShapes()
	{
		g_sampledShapes.clear();
		std::vector<std::pair<float, RE::BSGeometry*>> shapes;
		const auto door = g_door.toExterior.Invert() * g_door.center;
		RE::BSVisit::TraverseScenegraphGeometries(g_door.scene.get(), [&](RE::BSGeometry* a_geometry) {
			if (netimmerse_cast<RE::BSLightingShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get())) {
				shapes.emplace_back(a_geometry->worldBound.center.GetDistance(door), a_geometry);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		std::ranges::sort(shapes, {}, &std::pair<float, RE::BSGeometry*>::first);
		for (std::size_t i = 0; i < shapes.size() && i < kSampledShapes; ++i) {
			g_sampledShapes.emplace_back(shapes[i].second);
		}
	}

	// Effect shapes keep their passes too (BSEffectShaderProperty::GetRenderPasses, AE#106876, builds them
	// again when lastRenderPassState is 0x7FFFFFFF): after the effects' lights change.
	void ForceNewEffectPasses()
	{
		RE::BSVisit::TraverseScenegraphGeometries(g_door.scene.get(), [](RE::BSGeometry* a_geometry) {
			if (auto* property = netimmerse_cast<RE::BSEffectShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get())) {
				property->lastRenderPassState = kRebuildPasses;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}

	// What the last culling made of a shape: its alpha (material alpha times the fade, AE#106516), its
	// render passes and the lights of the first one, the frame it was last accumulated in.
	Trace::Args& ShapeState(Trace::Args& a_args, RE::BSGeometry* a_shape)
	{
		auto* property = a_shape->GetGeometryRuntimeData().shaderProperty.get();
		if (!property) {
			return a_args.Str("shape", a_shape->name.c_str()).Bool("shaderProperty", false);
		}
		std::int64_t passes = 0;
		for (auto* pass = property->renderPassList.head; pass && passes < 64; pass = pass->next) {
			++passes;
		}
		const auto* first = property->renderPassList.head;
		return a_args.Str("shape", a_shape->name.c_str())
			.Int("lights", property->lightData ? static_cast<std::int64_t>(property->lightData->lights.size()) : -1)
			.Num("alpha", property->alpha)
			.Int("passes", passes)
			.Int("passLights", first ? first->numLights : -1)
			.Int("passShadowLights", first ? first->numShadowLights : -1)
			.Int("lastAccumulated", property->lastAccumulatedFrameCount);
	}

	// The interior's effect shapes (fire, embers, smoke, candle flames) nearest to its strongest light,
	// the hearth's: 0.8.10-0.8.11 drew about 4 effect techniques per pass and showed no fire at all.
	std::string EffectsJson()
	{
		RE::NiPoint3 hearth{};
		float        strongest = -1.0f;
		for (const auto& added : g_lights) {
			if (!added.mirror && added.copy && added.copy->GetLightRuntimeData().radius.x > strongest) {
				strongest = added.copy->GetLightRuntimeData().radius.x;
				hearth = added.copy->world.translate;
			}
		}
		std::vector<std::pair<float, RE::BSGeometry*>> effects;
		RE::BSVisit::TraverseScenegraphGeometries(g_door.scene.get(), [&](RE::BSGeometry* a_geometry) {
			if (netimmerse_cast<RE::BSEffectShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get())) {
				effects.emplace_back(a_geometry->worldBound.center.GetDistance(hearth), a_geometry);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		std::ranges::sort(effects, {}, &std::pair<float, RE::BSGeometry*>::first);
		std::string json;
		for (std::size_t i = 0; i < effects.size() && i < kTracedEffects; ++i) {
			auto* shape = effects[i].second;
			auto* property = static_cast<RE::BSEffectShaderProperty*>(shape->GetGeometryRuntimeData().shaderProperty.get());
			auto* material = static_cast<RE::BSEffectShaderMaterial*>(property->material);
			const auto* rtti = shape->GetRTTI();
			const auto* first = property->renderPassList.head;
			Trace::Args args;
			ShapeState(args, shape)
				.Str("type", rtti && rtti->name ? rtti->name : "?")
				.Num("fromHearth", effects[i].first)
				.Num("materialAlpha", property->QMaterialAlpha())
				.Num("fade", property->fadeNode ? property->fadeNode->GetRuntimeData().currentFade : -1.0f)
				.Bool("lit", property->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kEffectLighting))
				.Int("technique", first ? static_cast<std::int64_t>(first->passEnum) : -1)
				.Num("baseAlpha", material ? material->baseColor.alpha : -1.0f)
				.Num("baseColorScale", material ? material->baseColorScale : -1.0f)
				.Bool("culled", shape->GetAppCulled())
				.Str("parentFlags", std::format("0x{:08X}", shape->parent ? shape->parent->GetFlags().underlying() : 0));
			if (auto* particles = netimmerse_cast<RE::NiParticleSystem*>(shape)) {
				const auto& data = particles->GetParticlesRuntimeData().particleData;
				args.Int("particles", data ? data->GetActiveVertexCount() : -1).Num("lastUpdate", particles->GetParticleSystemRuntimeData().lastUpdate);
			}
			json += (json.empty() ? "" : ",") + args.Json();
		}
		return Trace::Args{}.Int("count", static_cast<std::int64_t>(effects.size())).Pos("hearth", hearth).Raw("nearest", "[" + json + "]").Json();
	}

	// The interior's effect shapes that no hidden node hides, watched through our culling: how often it
	// reached each (OnVisible) and its parent, asked for its render passes and got none. 0.8.13-0.8.14:
	// the glow cards of the hearth and of the fireplaces never got a frame number of the accumulator (the
	// fire looked smaller than inside), and no candle on a fixture burned.
	struct WatchedEffect
	{
		RE::NiPointer<RE::BSGeometry> shape;
		const RE::NiAVObject*         parent{ nullptr };
		std::uint32_t                 visits{ 0 };
		std::uint32_t                 parentVisits{ 0 };
		std::uint32_t                 passCalls{ 0 };
		std::uint32_t                 noPasses{ 0 };
	};

	std::vector<WatchedEffect>                        g_watchedEffects;
	std::unordered_map<const void*, std::size_t>       g_watchedShapes;   // shape -> index
	std::unordered_multimap<const void*, std::size_t>  g_watchedParents;  // parent -> indices

	void WatchEffects()
	{
		g_watchedEffects.clear();
		g_watchedShapes.clear();
		g_watchedParents.clear();
		RE::BSVisit::TraverseScenegraphGeometries(g_door.scene.get(), [&](RE::BSGeometry* a_geometry) {
			if (g_watchedEffects.size() >= kWatchedEffects ||
				!netimmerse_cast<RE::BSEffectShaderProperty*>(a_geometry->GetGeometryRuntimeData().shaderProperty.get())) {
				return RE::BSVisit::BSVisitControl::kContinue;
			}
			for (const RE::NiAVObject* object = a_geometry; object && object != g_door.scene.get(); object = object->parent) {
				if (object->GetAppCulled()) {
					return RE::BSVisit::BSVisitControl::kContinue;  // editor markers, blood on weapons: hidden on purpose
				}
			}
			g_watchedShapes.emplace(a_geometry, g_watchedEffects.size());
			g_watchedParents.emplace(a_geometry->parent, g_watchedEffects.size());
			g_watchedEffects.push_back({ RE::NiPointer<RE::BSGeometry>(a_geometry), a_geometry->parent });
			return RE::BSVisit::BSVisitControl::kContinue;
		});
	}

	std::string WatchedEffectsJson()
	{
		std::string json;
		for (const auto& watched : g_watchedEffects) {
			auto*                 shape = watched.shape.get();
			auto*                 property = static_cast<RE::BSEffectShaderProperty*>(shape->GetGeometryRuntimeData().shaderProperty.get());
			const RE::NiAVObject* owner = nullptr;  // the reference's root: its model's name
			std::string           chain;
			for (RE::NiAVObject* parent = shape->parent; parent && parent != g_door.scene.get(); parent = parent->parent) {
				if (!owner && parent->AsFadeNode()) {
					owner = parent;
				}
				if (chain.size() < 400) {
					const auto* rtti = parent->GetRTTI();
					chain += std::format("{}{}({}) 0x{:08X} r{:.0f}", chain.empty() ? "" : " < ", parent->name.c_str(), rtti && rtti->name ? rtti->name : "?",
						parent->GetFlags().underlying(), parent->worldBound.radius);
				}
			}
			const auto* rtti = shape->GetRTTI();
			Trace::Args args;
			args.Str("shape", shape->name.c_str())
				.Str("type", rtti && rtti->name ? rtti->name : "?")
				.Str("model", owner ? owner->name.c_str() : "")
				.Str("flags", std::format("0x{:08X}", shape->GetFlags().underlying()))
				.Num("radius", shape->worldBound.radius)
				.Int("visits", watched.visits)
				.Int("parentVisits", watched.parentVisits)
				.Int("passCalls", watched.passCalls)
				.Int("noPasses", watched.noPasses)
				.Int("lastAccumulated", property ? property->lastAccumulatedFrameCount : 0)
				.Num("materialAlpha", property ? property->QMaterialAlpha() : -1.0f)
				.Num("fade", property && property->fadeNode ? property->fadeNode->GetRuntimeData().currentFade : -1.0f)
				// BSFadeNode + 0x144: its distance from the camera of the latest fade (AE#105506), read by
				// BSPSysLODModifier (AE#108480) for the emission and size of the particles under it
				.Num("fadeDistance", property && property->fadeNode ? std::bit_cast<float>(property->fadeNode->GetRuntimeData().unk144) : -1.0f)
				.Str("parents", chain);
			if (auto* particles = netimmerse_cast<RE::NiParticleSystem*>(shape)) {
				const auto& data = particles->GetParticlesRuntimeData().particleData;
				const auto& system = particles->GetParticleSystemRuntimeData();
				args.Int("particles", data ? data->GetActiveVertexCount() : -1)
					.Num("emitScale", system.unk180)  // written by BSPSysLODModifier: 1 near, its end emit scale far
					.Num("sizeScale", system.unk188);
			}
			json += (json.empty() ? "" : ",") + args.Json();
		}
		for (auto& watched : g_watchedEffects) {
			watched.visits = watched.parentVisits = watched.passCalls = watched.noPasses = 0;
		}
		return "[" + json + "]";
	}

	std::string SampledShapesJson()
	{
		std::string json;
		for (const auto& shape : g_sampledShapes) {
			Trace::Args args;
			json += (json.empty() ? "" : ",") + ShapeState(args.Str("name", shape->name.c_str()), shape.get()).Json();
		}
		return "[" + json + "]";
	}

	// The interior's look for the pass: its directional ambient as Sky::Update sets it while the player
	// is inside (AE#18966 reads it from the cell and its lighting template, AE#105643 writes it into
	// BSShaderManager::State, as MistMenu does around its 3D), and the sun light's color replaced by the
	// interior's directional color. 0.8.7 lit the tavern with the street's daylight ambient.
	struct SavedLook
	{
		std::array<std::byte, kAmbientBytes> ambient{};
		RE::NiColor                          sun{};
		bool                                 sunChanged{ false };
	};

	void UseInteriorLook(SavedLook& a_saved)
	{
		using GetAmbient = void (*)(RE::TESObjectCELL*, float*, float*, float*);
		using SetAmbient = void (*)(const float*, const float*, float);
		static REL::Relocation<GetAmbient> getAmbient{ REL::ID(18966) };
		static REL::Relocation<SetAmbient> setAmbient{ REL::ID(105643) };

		auto* ambient = reinterpret_cast<std::byte*>(&RE::BSShaderManager::State::GetSingleton()) + kAmbientOffset;
		std::memcpy(a_saved.ambient.data(), ambient, kAmbientBytes);
		alignas(16) float colors[24]{};  // six directional colors (0x48 bytes in MistMenu), with room to spare
		alignas(16) float specular[8]{};
		float             fresnel = 0.0f;
		getAmbient(g_door.cell, colors, specular, &fresnel);
		setAmbient(colors, specular, fresnel);

		auto*       scene = WorldScene();
		auto*       sun = scene ? scene->GetRuntimeData().sunLight : nullptr;
		const auto* lighting = g_door.cell->GetLighting();
		if (sun && sun->light && lighting) {
			auto& data = sun->light->GetLightRuntimeData();
			a_saved.sun = data.diffuse;
			a_saved.sunChanged = true;
			data.diffuse = { lighting->directional.red / 255.0f, lighting->directional.green / 255.0f, lighting->directional.blue / 255.0f };
		}
	}

	void RestoreLook(const SavedLook& a_saved)
	{
		auto* ambient = reinterpret_cast<std::byte*>(&RE::BSShaderManager::State::GetSingleton()) + kAmbientOffset;
		std::memcpy(ambient, a_saved.ambient.data(), kAmbientBytes);
		auto* scene = WorldScene();
		auto* sun = scene ? scene->GetRuntimeData().sunLight : nullptr;
		if (a_saved.sunChanged && sun && sun->light) {
			sun->light->GetLightRuntimeData().diffuse = a_saved.sun;
		}
	}

	// The portal's camera stands in the player's eye even while no pass aims it (the door off screen):
	// the actors' updates measure their distances from it.
	void CarryCamera()
	{
		const auto* space = g_space.load(std::memory_order_acquire);
		auto*       main = RE::Main::WorldRootCamera();
		if (space && main && g_camera) {
			g_camera->world.translate = space->toInterior * main->world.translate;
		}
	}

	// ------------------------------------------------------------------
	// Actors
	// ------------------------------------------------------------------
	void RefreshActors(std::int64_t a_now)
	{
		if (a_now < g_nextActorRefreshUs || !g_door.cell) {
			return;
		}
		g_nextActorRefreshUs = a_now + kActorRefreshUs;
		std::vector<RE::NiPointer<RE::NiAVObject>> roots;
		std::vector<RE::NiPointer<RE::Actor>>      actors;
		g_door.cell->ForEachReference([&](RE::TESObjectREFR* a_ref) {
			auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
			// 0.8.18: an NPC who walked out into the street stayed in the cell's list (shown, synced and
			// lit by the portal every frame while she stood at the door).
			if (auto* root = actor && !actor->IsDisabled() && actor->GetParentCell() == g_door.cell ? actor->Get3D() : nullptr) {
				roots.emplace_back(root);
				actors.emplace_back(actor);
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});
		g_actorRoots = std::move(roots);
		if (actors == g_interiorActors) {
			return;
		}
		// A new list: published for AdjustAnimationUpdate, the per-actor state of the last pass forgotten.
		g_interiorActors = std::move(actors);
		for (std::size_t i = 0; i < kMaxAnimatedActors; ++i) {
			g_animatedActors[i].store(i < g_interiorActors.size() ? g_interiorActors[i].get() : nullptr, std::memory_order_relaxed);
		}
		g_poseSignatures.assign(g_interiorActors.size(), 0.0f);
		g_seenGameUpdates.assign(g_interiorActors.size(), 0);
		g_actorZMin.assign(g_interiorActors.size(), FLT_MAX);
		g_actorZMax.assign(g_interiorActors.size(), -FLT_MAX);
		for (std::size_t i = 0; i < g_seenGameUpdates.size() && i < kMaxAnimatedActors; ++i) {
			g_seenGameUpdates[i] = g_gameAnimationUpdates[i].load(std::memory_order_relaxed);
		}
	}

	// Whether a_actor is one of the portal's interior actors (any thread; compared, never dereferenced).
	std::size_t PortalActorIndex(const RE::Actor* a_actor)
	{
		for (std::size_t i = 0; i < kMaxAnimatedActors; ++i) {
			const auto* actor = g_animatedActors[i].load(std::memory_order_relaxed);
			if (!actor || actor == a_actor) {
				return actor ? i : kMaxAnimatedActors;
			}
		}
		return kMaxAnimatedActors;
	}

	// The mark the game leaves on actors outside the main camera's frustum: no foot IK feedback, so their
	// height stops following the character controller, kFarAway on the controller (AE#37992), and an
	// animation update that moves no bone (AE#38054).
	void ClearNotVisible(RE::NiAVObject* a_root)
	{
		if (a_root && a_root->GetFlags().all(RE::NiAVObject::Flag::kNotVisible)) {
			a_root->GetFlags().reset(RE::NiAVObject::Flag::kNotVisible);
			g_notVisibleCleared.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void ForgetActors()
	{
		g_forceAnimation.store(false, std::memory_order_relaxed);
		g_actorsSeen.store(false, std::memory_order_relaxed);
		g_fullProcessing.store(false, std::memory_order_relaxed);
		g_gameAnimation.store(false, std::memory_order_relaxed);
		for (auto& actor : g_animatedActors) {
			actor.store(nullptr, std::memory_order_relaxed);
		}
		g_actorRoots.clear();
		g_interiorActors.clear();
		g_poseSignatures.clear();
		g_seenGameUpdates.clear();
		g_actorZMin.clear();
		g_actorZMax.clear();
	}

	// A sum over one bone's local transform: does the pose move?
	float PoseSignature(RE::NiAVObject* a_root)
	{
		static const RE::BSFixedString bone{ kPoseBone };
		const auto*                    node = a_root->GetObjectByName(bone);
		if (!node) {
			return 0.0f;
		}
		float sum = node->local.translate.x + node->local.translate.y + node->local.translate.z;
		for (const auto& row : node->local.rotate.entry) {
			sum += row[0] * 1.0f + row[1] * 2.0f + row[2] * 3.0f;
		}
		return sum;
	}

	// The game updates the animation of high-process actors with BOOL_BITS::kProcessMe itself (AE#41377,
	// Actor::UpdateAnimation, vfunc 0x7D), but before that Actor::ModifyAnimationUpdateData (AE#38054)
	// marks an actor whose 3D is kNotVisible as unseen, and BShkbAnimationGraph::Update (AE#63587) then
	// moves no bone of it (AE#63575): the interior's actors kept their bind pose (A-pose), or the pose
	// of the player's last visit. AdjustAnimationUpdate makes their updates full ones. Here, before each
	// pass: their 3D is put where the actors are, the animation of an actor the game skipped since the
	// last pass is advanced, and the world transforms are computed again (without pushing them into Havok).
	void AnimateActors()
	{
		const auto now = Trace::NowUs();
		const auto delta = g_lastAnimateUs ? std::clamp(static_cast<float>(now - g_lastAnimateUs) / 1'000'000.0f, 0.0f, kMaxAnimationStep) : 0.0f;
		g_lastAnimateUs = now;
		for (std::size_t i = 0; i < g_interiorActors.size() && i < g_actorZMin.size(); ++i) {
			const auto z = g_interiorActors[i] ? g_interiorActors[i]->GetPosition().z : 0.0f;
			g_actorZMin[i] = (std::min)(g_actorZMin[i], z);
			g_actorZMax[i] = (std::max)(g_actorZMax[i], z);
		}
		const auto& settings = Settings::Get();
		if (!settings.portalAnimateActors || delta <= 0.0f) {
			return;
		}
		for (std::size_t i = 0; i < g_interiorActors.size(); ++i) {
			auto& actor = g_interiorActors[i];
			auto* root = actor ? actor->Get3D() : nullptr;
			if (!root || !root->parent) {
				continue;
			}
			if (settings.portalSyncActorPositions && root->world.translate.GetDistance(actor->GetPosition()) > 1.0f) {
				actor->Update3DPosition(false);
				++g_positionsSynced;
			}
			const auto gameUpdates = i < kMaxAnimatedActors ? g_gameAnimationUpdates[i].load(std::memory_order_relaxed) : 0;
			if (gameUpdates != g_seenGameUpdates[i]) {
				g_seenGameUpdates[i] = gameUpdates;
				++g_animatedByGame;
			} else if (!g_door.attached) {  // an attached cell's actors stand when the game stands: a pause, a fade
				t_ownAnimation = true;
				actor->UpdateAnimation(delta);
				t_ownAnimation = false;
				++g_animated;
			}
			RE::NiUpdateData update{};
			update.flags.set(RE::NiUpdateData::Flag::kDisableCollision);
			root->Update(update);
			const auto pose = PoseSignature(root);
			g_poseChanged += pose != g_poseSignatures[i] ? 1 : 0;
			g_poseSignatures[i] = pose;
			++g_poseChecks;
		}
	}

	// What hides an actor of the interior from the portal: 0.8.8 found no AppCulled roots, 0.8.9 no
	// culled nodes near them (2 deep inside each) and fades raised for the culling. So the trace shows
	// the nodes above the root, up to the interior's root, and what the culling made of its first shape.
	std::string AncestorsJson(const RE::NiAVObject* a_root)
	{
		std::string json;
		std::size_t count = 0;
		for (const auto* parent = a_root->parent; parent && count < kTracedAncestors; parent = parent->parent, ++count) {
			const auto* rtti = parent->GetRTTI();
			json += (json.empty() ? "" : ",") + Trace::Args{}
													  .Str("name", parent->name.c_str())
													  .Str("type", rtti && rtti->name ? rtti->name : "?")
													  .Bool("culled", parent->GetAppCulled())
													  .Bool("interiorRoot", parent == g_door.scene.get())
													  .Num("boundRadius", parent->worldBound.radius)
													  .Json();
			if (parent == g_door.scene.get()) {
				break;
			}
		}
		return "[" + json + "]";
	}

	// The first shape under a_object that no hidden node hides (0.8.10 took the hidden weapon blood).
	RE::BSGeometry* FirstShownShape(RE::NiAVObject* a_object)
	{
		if (!a_object || a_object->GetAppCulled()) {
			return nullptr;
		}
		if (auto* geometry = a_object->AsGeometry()) {
			return geometry;
		}
		if (auto* node = a_object->AsNode()) {
			for (auto& child : node->GetChildren()) {
				if (auto* shape = FirstShownShape(child.get())) {
					return shape;
				}
			}
		}
		return nullptr;
	}

	// Nodes of the interior that the game's culling marks in a pass of its own (kPreProcessedNode), and
	// those of them it found not visible (kNotVisible): BSCullingProcess::Process (AE#76553) skips them
	// unless the context ignores that pass. 0.8.10: the culling reached the actors' node every pass but
	// none of the actors under it.
	std::string PreprocessedJson()
	{
		std::size_t preprocessed = 0;
		std::size_t notVisible = 0;
		RE::BSVisit::TraverseScenegraphObjects(g_door.scene.get(), [&](RE::NiAVObject* a_object) {
			const auto flags = a_object->GetFlags();
			if (flags.all(RE::NiAVObject::Flag::kPreProcessedNode)) {
				++preprocessed;
				notVisible += flags.all(RE::NiAVObject::Flag::kNotVisible) ? 1 : 0;
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return Trace::Args{}.Int("preprocessed", static_cast<std::int64_t>(preprocessed)).Int("notVisible", static_cast<std::int64_t>(notVisible)).Json();
	}

	// The time the controllers are given (NiUpdateData::time is absolute: controllers and particle systems
	// keep the last one and take the difference): the loaded cells' own, which TES advances by each
	// frame's time (AE#13291) and gives their references' updates (AE#18890). 0.8.13-0.8.15 took the run
	// time, 18 s ahead of it at 115 s (0.8.15): particles of a cell the player enters jump back in time.
	float ControllerTime()
	{
		static REL::Relocation<float*> cellTime{ REL::ID(400333) };
		return *cellTime;
	}

	struct ParticleStats
	{
		std::int64_t systems{ 0 };
		std::int64_t particles{ 0 };
		float        lastUpdate{ -1.0f };  // the latest NiParticleSystem::lastUpdate: the time base its updates used
	};

	ParticleStats CountParticles(RE::NiAVObject* a_root)
	{
		ParticleStats stats;
		if (!a_root) {
			return stats;
		}
		RE::BSVisit::TraverseScenegraphGeometries(a_root, [&](RE::BSGeometry* a_geometry) {
			if (auto* particles = netimmerse_cast<RE::NiParticleSystem*>(a_geometry)) {
				const auto& data = particles->GetParticlesRuntimeData().particleData;
				++stats.systems;
				stats.particles += data ? data->GetActiveVertexCount() : 0;
				stats.lastUpdate = (std::max)(stats.lastUpdate, particles->GetParticleSystemRuntimeData().lastUpdate);
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		return stats;
	}

	// What the controller update can reach in the interior (nodes flagged for the selective update, objects
	// with controllers, particle systems and their particles), and the time bases: ours, and the one the
	// game's own update gave the particle systems of the player's cell.
	std::string ControllersJson()
	{
		std::int64_t selective = 0;
		std::int64_t selectiveControllers = 0;
		std::int64_t withControllers = 0;
		RE::BSVisit::TraverseScenegraphObjects(g_door.scene.get(), [&](RE::NiAVObject* a_object) {
			const auto flags = a_object->GetFlags();
			selective += flags.all(RE::NiAVObject::Flag::kSelectiveUpdate) ? 1 : 0;
			selectiveControllers += flags.all(RE::NiAVObject::Flag::kSelectiveUpdateController) ? 1 : 0;
			withControllers += a_object->GetControllers() ? 1 : 0;
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		const auto interior = CountParticles(g_door.scene.get());
		auto*      player = RE::PlayerCharacter::GetSingleton();
		auto*      cell = player ? player->GetParentCell() : nullptr;
		const auto exterior = CountParticles(cell ? cell->GetRuntimeData().loadedData ? cell->GetRuntimeData().loadedData->cell3D.get() : nullptr : nullptr);
		return Trace::Args{}
			.Int("mode", std::to_underlying(g_controllerMode))
			.Str("modeName", kControllerModeNames[std::to_underlying(g_controllerMode)])
			.Int("selectiveNodes", selective)
			.Int("selectiveControllerNodes", selectiveControllers)
			.Int("withControllers", withControllers)
			.Int("particleSystems", interior.systems)
			.Int("particles", interior.particles)
			.Num("particlesLastUpdate", interior.lastUpdate)
			.Num("ourTime", ControllerTime())
			.Num("runTime", static_cast<float>(RE::GetDurationOfApplicationRunTime()) / 1000.0f)
			.Int("playerCellParticleSystems", exterior.systems)
			.Num("playerCellParticlesLastUpdate", exterior.lastUpdate)
			.Json();
	}

	// One step of the interior's controllers, before the culling (the particles it registers are the new ones).
	void UpdateControllers()
	{
		if (g_controllerMode == ControllerMode::kOff) {
			return;
		}
		const auto     t0 = std::chrono::steady_clock::now();
		RE::NiUpdateData data{};
		data.time = ControllerTime();
		data.flags.set(RE::NiUpdateData::Flag::kDisableCollision);  // the interior's Havok world is stepped on the physics thread
		if (g_controllerMode == ControllerMode::kSelective) {
			g_door.scene->UpdateSelectedDownwardPass(data, 0);
		} else {
			data.flags.set(static_cast<RE::NiUpdateData::Flag>(kUpdateControllers));
			g_door.scene->Update(data);
		}
		g_controllerMsSum += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		++g_controllerUpdates;
	}

	std::string ActorsJson()
	{
		std::string json;
		for (std::size_t i = 0; i < g_actorRoots.size() && i < kTracedActors; ++i) {
			auto&       root = g_actorRoots[i];
			RE::BSGeometry* shape = FirstShownShape(root.get());
			auto*           actor = i < g_interiorActors.size() ? g_interiorActors[i].get() : nullptr;
			const auto      bits = actor ? actor->GetActorRuntimeData().boolBits.underlying() : 0u;
			Trace::Args firstShape;
			if (shape) {
				ShapeState(firstShape, shape);
			}
			const auto* rtti = root->GetRTTI();
			std::size_t culled = 0;
			std::size_t nodes = 0;
			RE::BSVisit::TraverseScenegraphObjects(root.get(), [&](RE::NiAVObject* a_object) {
				++nodes;
				culled += a_object->GetAppCulled() ? 1 : 0;
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			auto*        fade = root->AsFadeNode();
			auto*        controller = actor ? actor->GetCharController() : nullptr;
			RE::NiPoint3 velocity{};
			if (actor) {
				actor->GetLinearVelocity(velocity);
			}
			RE::NiPoint3 controllerPos{};
			if (controller) {
				RE::hkVector4 position;
				controller->GetPosition(position, false);
				alignas(16) float xyzw[4]{};
				_mm_store_ps(xyzw, position.quad);
				controllerPos = { xyzw[0] * kHavokToSkyrim, xyzw[1] * kHavokToSkyrim, xyzw[2] * kHavokToSkyrim };
			}
			auto*      process = actor ? actor->GetActorRuntimeData().currentProcess : nullptr;
			const auto movement = i < kMaxAnimatedActors ? g_movementUpdates[i].exchange(0, std::memory_order_relaxed) : 0u;
			const auto zMin = i < g_actorZMin.size() ? g_actorZMin[i] : FLT_MAX;
			const auto zMax = i < g_actorZMax.size() ? g_actorZMax[i] : -FLT_MAX;
			if (i < g_actorZMin.size()) {
				g_actorZMin[i] = FLT_MAX;
				g_actorZMax[i] = -FLT_MAX;
			}
			const auto* interiorWorld = g_door.cell ? g_door.cell->GetbhkWorld() : nullptr;
			alignas(16) float supportNormal[4]{};
			if (controller) {
				_mm_store_ps(supportNormal, controller->supportNorm.quad);
			}
			json += (json.empty() ? "" : ",") + Trace::Args{}
													  .Str("name", root->name.c_str())
													  .Str("actor", actor ? actor->GetName() : "")
													  .Str("type", rtti && rtti->name ? rtti->name : "?")
													  .Bool("culled", root->GetAppCulled())
													  .Str("flags", std::format("0x{:08X}", root->GetFlags().underlying()))
													  .Str("boolBits", std::format("0x{:08X}", bits))
													  .Bool("processMe", (bits & std::to_underlying(RE::Actor::BOOL_BITS::kProcessMe)) != 0)
													  .Pos("actorPos", actor ? actor->GetPosition() : RE::NiPoint3{})
													  .Num("rootOffset", actor ? root->world.translate.GetDistance(actor->GetPosition()) : -1.0f)
													  .Num("fadeAmount", root->fadeAmount)
													  .Num("currentFade", fade ? fade->GetRuntimeData().currentFade : -1.0f)
													  .Int("nodes", static_cast<std::int64_t>(nodes))
													  .Int("culledNodes", static_cast<std::int64_t>(culled))
													  .Pos("boundCenter", root->worldBound.center)
													  .Num("boundRadius", root->worldBound.radius)
													  .Bool("hasParent", root->parent != nullptr)
													  .Int("movementUpdates", movement)
													  .Bool("charController", controller != nullptr)
													  .Pos("controllerPos", controllerPos)
													  .Pos("velocity", velocity)
													  .Str("package", process && process->currentPackage.package ? std::format("{:08X}", process->currentPackage.package->GetFormID()) : "")
													  .Int("processLevel", actor ? static_cast<std::int64_t>(std::to_underlying(actor->GetProcessLevel())) : -1)
													  .Num("lodDistance", controller ? controller->lodDistance : -2.0f)
													  .Int("controllerState", controller ? static_cast<std::int64_t>(controller->context.currentState) : -1)
													  .Num("fallTime", controller ? controller->fallTime : -1.0f)
													  .Bool("controllerInInteriorWorld", controller && interiorWorld && controller->GetHavokWorld() == interiorWorld)
													  .Str("controllerFlags", controller ? Portal::ControllerFlagsText(*controller) : "")
													  .Int("supportedState", controller ? static_cast<std::int64_t>(std::to_underlying(controller->surfaceInfo.supportedState.get())) : -1)
													  .Num("supportNormalZ", supportNormal[2])
													  .Num("zMin", zMin == FLT_MAX ? 0.0f : zMin)
													  .Num("zMax", zMax == -FLT_MAX ? 0.0f : zMax)
													  .Raw("ancestors", AncestorsJson(root.get()))
													  .Raw("firstShape", shape ? firstShape.Json() : "null")
													  .Json();
		}
		return "[" + json + "]";
	}

	// The actors' 3D, made visible for the culling only: AppCulled nodes near the root shown, fade raised.
	struct ShownActors
	{
		std::vector<RE::NiAVObject*>                    unculled;
		std::vector<std::pair<RE::NiAVObject*, float>>  fadeAmounts;
		std::vector<std::pair<RE::BSFadeNode*, float>>  currentFades;
	};

	void RevealNodes(RE::NiAVObject* a_object, int a_depth, ShownActors& a_shown)
	{
		if (a_object->GetAppCulled()) {
			a_object->SetAppCulled(false);
			a_shown.unculled.push_back(a_object);
		}
		if (auto* node = a_depth > 0 ? a_object->AsNode() : nullptr) {
			for (auto& child : node->GetChildren()) {
				if (child) {
					RevealNodes(child.get(), a_depth - 1, a_shown);
				}
			}
		}
	}

	void ShowActors(ShownActors& a_shown)
	{
		for (auto& root : g_actorRoots) {
			// The nodes above the root, up to the interior's: a hidden one hides every actor under it,
			// and their bounds follow the actors that walked since the last pass. Only when the root
			// hangs in the interior's 3D: never the exterior scene's nodes.
			std::vector<RE::NiNode*> ancestors;
			for (auto* parent = root->parent; parent && ancestors.size() < kMaxActorAncestors; parent = parent->parent) {
				ancestors.push_back(parent);
				if (parent == g_door.scene.get()) {
					break;
				}
			}
			if (!ancestors.empty() && ancestors.back() == g_door.scene.get()) {
				for (auto* parent : ancestors) {
					if (parent->GetAppCulled()) {
						parent->SetAppCulled(false);
						a_shown.unculled.push_back(parent);
					}
					if (parent != g_door.scene.get()) {
						parent->UpdateWorldBound();  // the actors' node has kFixedBound: a no-op there (0.8.10)
					}
				}
			}
			RevealNodes(root.get(), kActorRevealDepth, a_shown);
			if (root->fadeAmount < 1.0f) {
				a_shown.fadeAmounts.emplace_back(root.get(), root->fadeAmount);
				root->fadeAmount = 1.0f;
			}
			RE::BSVisit::TraverseScenegraphObjects(root.get(), [&](RE::NiAVObject* a_object) {
				if (auto* fade = a_object->AsFadeNode(); fade && fade->GetRuntimeData().currentFade < 1.0f) {
					a_shown.currentFades.emplace_back(fade, fade->GetRuntimeData().currentFade);
					fade->GetRuntimeData().currentFade = 1.0f;
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}
	}

	void HideActors(const ShownActors& a_shown)
	{
		for (auto* object : a_shown.unculled) {
			object->SetAppCulled(true);
		}
		for (const auto& [object, value] : a_shown.fadeAmounts) {
			object->fadeAmount = value;
		}
		for (const auto& [fade, value] : a_shown.currentFades) {
			fade->GetRuntimeData().currentFade = value;
		}
	}

	// ------------------------------------------------------------------
	// The pass
	// ------------------------------------------------------------------
	// The engine slots the pass draws into, lent to our textures for its duration. The main depth slot
	// too: BSEffectShader's RestoreTechnique (AE#107522) and other passes set the depth target back to
	// kMAIN (AE#77457 returns 0), and soft particles read its shader view. 0.8.7 drew the hearth's fire
	// through the interior's walls; 0.8.9, which lent only that view, drew it only with the eye a meter
	// or more from the door: tested against the street's depth, where the door stands at the eye's
	// distance, while the portal camera's near plane is 2 units in front of the door.
	struct LentSlots
	{
		RE::BSGraphics::RenderTargetData             color{};
		RE::BSGraphics::RenderTargetProperties       colorProperties{};
		RE::BSGraphics::DepthStencilData             depth{};
		RE::BSGraphics::DepthStencilTargetProperties depthProperties{};
		RE::BSGraphics::DepthStencilData             mainDepth{};
		RE::BSGraphics::DepthStencilTargetProperties mainDepthProperties{};
	};

	void LendSlots(LentSlots& a_saved, std::uint32_t a_width, std::uint32_t a_height)
	{
		const auto& settings = Settings::Get();
		auto*       renderer = RE::BSGraphics::Renderer::GetSingleton();
		auto*       manager = RenderTargets();

		auto& color = renderer->GetRuntimeData().renderTargets[settings.portalColorSlot];
		a_saved.color = color;
		color.texture = AsRex<REX::W32::ID3D11Texture2D>(g_target.color);
		color.textureCopy = nullptr;
		color.RTV = AsRex<REX::W32::ID3D11RenderTargetView>(g_target.rtv);
		color.SRV = AsRex<REX::W32::ID3D11ShaderResourceView>(g_target.srv);
		color.SRVCopy = nullptr;
		color.UAV = nullptr;
		auto& colorProperties = manager->renderTargetData[settings.portalColorSlot];
		a_saved.colorProperties = colorProperties;
		colorProperties.width = a_width;  // the viewport: the part of the target this pass fills
		colorProperties.height = a_height;

		auto&      depths = renderer->GetDepthStencilData().depthStencils;
		const auto lendDepth = [&](std::uint32_t a_slot, RE::BSGraphics::DepthStencilData& a_data, RE::BSGraphics::DepthStencilTargetProperties& a_properties) {
			auto& depth = depths[a_slot];
			a_data = depth;
			depth.texture = AsRex<REX::W32::ID3D11Texture2D>(g_target.depth);
			for (auto& view : depth.views) {
				view = AsRex<REX::W32::ID3D11DepthStencilView>(g_target.dsv);
			}
			for (auto& view : depth.readOnlyViews) {
				view = AsRex<REX::W32::ID3D11DepthStencilView>(g_target.dsvReadOnly);
			}
			depth.depthSRV = AsRex<REX::W32::ID3D11ShaderResourceView>(g_target.depthSrv);
			depth.stencilSRV = nullptr;
			auto& properties = manager->GetDepthStencilTargetData()[a_slot];
			a_properties = properties;
			properties.width = a_width;
			properties.height = a_height;
		};
		lendDepth(settings.portalDepthSlot, a_saved.depth, a_saved.depthProperties);
		auto& mainDepth = depths[RE::RENDER_TARGET_DEPTHSTENCIL::kMAIN];
		if (g_depthMode == DepthMode::kOwnSlot) {
			a_saved.mainDepth = mainDepth;
			a_saved.mainDepthProperties = manager->GetDepthStencilTargetData()[RE::RENDER_TARGET_DEPTHSTENCIL::kMAIN];
			mainDepth.depthSRV = AsRex<REX::W32::ID3D11ShaderResourceView>(g_target.depthSrv);
			return;
		}
		lendDepth(RE::RENDER_TARGET_DEPTHSTENCIL::kMAIN, a_saved.mainDepth, a_saved.mainDepthProperties);
		if (g_depthMode == DepthMode::kCopiedView) {
			// a shader view of the buffer the effects test against is a hazard if it is ever bound writable
			mainDepth.depthSRV = AsRex<REX::W32::ID3D11ShaderResourceView>(g_target.depthCopySrv);
			depths[settings.portalDepthSlot].depthSRV = mainDepth.depthSRV;
		}
	}

	void ReturnSlots(const LentSlots& a_saved)
	{
		const auto& settings = Settings::Get();
		auto*       renderer = RE::BSGraphics::Renderer::GetSingleton();
		auto*       manager = RenderTargets();
		auto&       depths = renderer->GetDepthStencilData().depthStencils;
		renderer->GetRuntimeData().renderTargets[settings.portalColorSlot] = a_saved.color;
		manager->renderTargetData[settings.portalColorSlot] = a_saved.colorProperties;
		depths[settings.portalDepthSlot] = a_saved.depth;
		manager->GetDepthStencilTargetData()[settings.portalDepthSlot] = a_saved.depthProperties;
		depths[RE::RENDER_TARGET_DEPTHSTENCIL::kMAIN] = a_saved.mainDepth;
		manager->GetDepthStencilTargetData()[RE::RENDER_TARGET_DEPTHSTENCIL::kMAIN] = a_saved.mainDepthProperties;
	}

	void WriteDump(ID3D11Texture2D* a_texture, std::uint32_t a_width, std::uint32_t a_height, std::string_view a_name, std::uint32_t a_number);

	void Mark(PassPhase a_phase)
	{
		g_passPhase.store(a_phase, std::memory_order_relaxed);
	}

	void NoteFault(std::string a_why)
	{
		if (g_fault.empty()) {
			g_fault = std::move(a_why);
			Trace::Write("portal.fault", Trace::Args{}.Str("why", g_fault), nullptr, true);
			logs::error("portal: {}", g_fault);
		}
	}

	// What an accumulator holds. Render passes come from a pool shared with the game's accumulators, so
	// a pass left behind in ours can be reused there and the lists join up: the renderer then walks in
	// circles (0.8.21 froze this way). The game keeps its own off-screen pass clean the same way (AE#52727).
	struct PassInventory
	{
		std::uint32_t techniqueGroups{ 0 };  // technique buckets with passes
		std::uint32_t geometryGroups{ 0 };   // a bit per geometry group, kAlphaGroupBit for the alpha group

		[[nodiscard]] bool Empty() const { return techniqueGroups == 0 && geometryGroups == 0; }
	};

	// SEH only, no objects with destructors. NG declares renderPass as an array of PassGroup*, but the
	// game's RegisterPass indexes inline records of that type.
	bool ReadPassInventory(const RE::BSBatchRenderer* a_batches, PassInventory& a_out)
	{
		__try {
			const auto* techniques = reinterpret_cast<const RE::BSBatchRenderer::PassGroup*>(a_batches->renderPass.data());
			const auto  count = (std::min)(a_batches->renderPass.size(), kMaxInspectedTechniques);
			for (std::uint32_t i = 0; i < count; ++i) {
				a_out.techniqueGroups += techniques[i].validPassBits != 0 ? 1 : 0;
			}
			for (std::uint32_t i = 0; i <= kGeometryGroups; ++i) {
				const auto* group = i < kGeometryGroups ? a_batches->geometryGroups[i] : a_batches->alphaGroup;
				if (group && (group->passList.head || group->count != 0)) {
					a_out.geometryGroups |= 1u << i;
				}
			}
			return true;
		} __except (1 /* EXCEPTION_EXECUTE_HANDLER */) {
			return false;
		}
	}

	// SubmitAccumulator (AE#106436) drains the technique buckets but never draws the refraction normals
	// group: only the frame renderer does, so the game clears it after its own off-screen pass (AE#52727).
	void DrainPasses(RE::BSShaderAccumulator* a_accumulator)
	{
		using ClearGroup = void (*)(RE::BSShaderAccumulator*, std::int32_t);
		static REL::Relocation<ClearGroup> clearGroup{ RELOCATION_ID(99955, 106600) };
		clearGroup(a_accumulator, kRefractionNormalsGroup);

		PassInventory left;
		if (!ReadPassInventory(a_accumulator->GetRuntimeData().batchRenderer, left)) {
			NoteFault("the accumulator's batches cannot be read");
			return;
		}
		g_leftoverGroups |= left.geometryGroups;
		if (left.Empty()) {
			return;
		}
		if ((left.geometryGroups & ~g_tracedLeftovers) != 0 || left.techniqueGroups != 0) {
			g_tracedLeftovers |= left.geometryGroups;
			Trace::Write("portal.leftovers", Trace::Args{}
												 .Int("techniqueGroups", left.techniqueGroups)
												 .Str("geometryGroups", std::format("0x{:05X}", left.geometryGroups)),
				nullptr, true);
		}
		for (std::uint32_t i = 0; i < kGeometryGroups; ++i) {
			if ((left.geometryGroups & (1u << i)) != 0) {
				clearGroup(a_accumulator, static_cast<std::int32_t>(i));
			}
		}
		if (left.techniqueGroups != 0 || (left.geometryGroups & kAlphaGroupBit) != 0) {
			NoteFault(std::format("passes stayed in the accumulator: {} technique groups, geometry groups 0x{:05X}", left.techniqueGroups, left.geometryGroups));
		}
	}

	RE::NiPoint3 Column(const RE::NiMatrix3& a_matrix, int a_column)
	{
		return { a_matrix.entry[0][a_column], a_matrix.entry[1][a_column], a_matrix.entry[2][a_column] };
	}

	PackCamera CameraOf(const RE::NiTransform& a_world)
	{
		// Gamebryo cameras look along local +X, with +Y up and +Z right.
		return { .origin = a_world.translate, .up = Column(a_world.rotate, 1), .right = Column(a_world.rotate, 2), .forward = Column(a_world.rotate, 0) };
	}

	// The cameras the particle packer could face in this pass. The portal camera always looks across the
	// door plane, so through a door seen at an angle only the player's own view faces them to the eye.
	void AimParticles(const RE::NiCamera* a_main)
	{
		const auto rotation = g_door.toExterior.Invert().rotate;
		g_packPortal = CameraOf(g_camera->world);
		g_packMain = CameraOf(a_main->world);
		g_packView = { .origin = g_packPortal.origin, .up = rotation * g_packMain.up, .right = rotation * g_packMain.right, .forward = rotation * g_packMain.forward };
	}

	RE::NiPoint3 ReadVector(const void* a_vector)
	{
		const auto* floats = static_cast<const float*>(a_vector);
		return { floats[0], floats[1], floats[2] };
	}

	void WriteVector(void* a_vector, const RE::NiPoint3& a_value)
	{
		auto* floats = static_cast<float*>(a_vector);
		floats[0] = a_value.x;
		floats[1] = a_value.y;
		floats[2] = a_value.z;
		floats[3] = 0.0f;
	}

	bool Faces(const RE::NiPoint3& a_up, const RE::NiPoint3& a_right, const PackCamera& a_camera)
	{
		constexpr float kSameDirection = 0.999f;
		return a_up.Dot(a_camera.up) > kSameDirection && a_right.Dot(a_camera.right) > kSameDirection;
	}

	// One pass, main thread: right before Main::RenderWorld, or at the scene list fence.
	void Render()
	{
		auto* main = RE::Main::WorldRootCamera();
		if (!main || !g_door.scene || !g_fault.empty()) {
			return;
		}
		const auto t0 = std::chrono::steady_clock::now();
		MirrorPlayerLights();  // before the visibility test: the copies follow the player even when the door is off screen
		VisiblePart part;
		std::string why;
		if (!SeeDoor(main, part)) {
			++g_skipped;
			g_skipWhy = "door not on screen";
			return;
		}
		if (!AimCamera(main, part, why)) {
			++g_skipped;
			g_skipWhy = std::move(why);
			return;
		}
		SwapQuadTexture();
		const auto& settings = Settings::Get();
		const auto  width = std::clamp(static_cast<std::uint32_t>(std::ceil(part.pixelsWide)), kMinViewport, g_target.width);
		const auto  height = std::clamp(static_cast<std::uint32_t>(std::ceil(part.pixelsHigh)), kMinViewport, g_target.height);
		MapQuad(part, width, height);
		g_visible = part;
		g_viewportWidth = width;
		g_viewportHeight = height;

		auto* accumulator = g_accumulator.get();
		PassInventory before;
		if (ReadPassInventory(accumulator->GetRuntimeData().batchRenderer, before) && !before.Empty()) {
			NoteFault(std::format("the accumulator is not empty before a pass: {} technique groups, geometry groups 0x{:05X}", before.techniqueGroups, before.geometryGroups));
			return;
		}

		Mark(PassPhase::kLendSlots);
		auto*      shadow = RE::BSGraphics::RendererShadowState::GetSingleton();
		auto&      state = shadow->GetRuntimeData();
		const auto savedState = state;
		auto*      savedAccumulator = RE::BSShaderAccumulator::GetCurrentAccumulator();
		LentSlots  lent;
		LendSlots(lent, width, height);
		const auto woken = WakeLights();  // before the culling: it picks each shape's lights
		if (g_rebuildPasses) {
			g_rebuildPasses = false;
			ForceNewRenderPasses();
		}
		if (g_rebuildEffects) {
			g_rebuildEffects = false;
			ForceNewEffectPasses();
		}
		Mark(PassPhase::kAnimateActors);
		AnimateActors();
		Mark(PassPhase::kUpdateControllers);
		UpdateControllers();

		// The game hides the actors of a cell without the player (0.8.5 showed them only after
		// clearing every AppCulled flag and raising every fade of the interior): shown for this pass.
		ShownActors shown;
		ShowActors(shown);
		g_actorsShown = shown.unculled.size() + shown.fadeAmounts.size() + shown.currentFades.size();
		const auto lentLights = LendActorLights();

		Mark(PassPhase::kCullInterior);
		accumulator->StartAccumulating(g_camera.get());
		auto& accumulatorData = accumulator->GetRuntimeData();
		accumulatorData.activeShadowSceneNode = WorldScene();
		accumulatorData.renderMode = RE::BSShaderAccumulator::RENDER_MODE::kNormal;
		accumulatorData.eyePosition = g_camera->world.translate;

		{
			RE::BSCullingProcess::CullingContext context;
			context.accumulator.reset(accumulator);
			context.camera.reset(g_camera.get());
			context.frustum = &g_camera->GetRuntimeData2().viewFrustum;
			context.scene = g_door.scene;
			context.cullMode = RE::BSCullingProcess::BSCPCullingType::kIgnoreMultiBounds;  // the interior's rooms and portals are not active
			context.ignorePreprocess = true;  // the game's own visibility marks (kNotVisible) are stale in a cell without the player
			// NiBillboardNode::OnVisible (AE#71063) turns to the culling's camera only with this (process
			// + 0x11C): without it the flames and glows faced the last camera that saw them, edge-on through
			// the portal (0.8.13-0.8.17: a "small" fire).
			context.cameraRelatedUpdates = true;
			t_culling = true;
			RE::BSCullingProcess::Process(context);
			// Candle flames: the addon node (BSValueNode) in the candle's model is only the emitter, the
			// particles live in a master system under the particle manager, in world coordinates. The
			// interior's flames were never in the tree culled above (0.8.8-0.8.20: no candle burned).
			// Right before RenderWorld this froze the game (0.8.21): those shapes are the street's too, and
			// the game's jobs are registering the same render passes at that moment. It takes the fence.
			if (auto* manager = settings.portalMasterParticles && g_passAtFence.load(std::memory_order_relaxed) ? ParticleManager() : nullptr) {
				Mark(PassPhase::kCullMasterParticles);
				context.scene.reset(manager);
				RE::BSCullingProcess::Process(context);
			}
			t_culling = false;
		}
		Mark(PassPhase::kSetTargets);
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		auto* manager = RenderTargets();
		renderer->SetRenderTarget(0, static_cast<RenderTarget>(settings.portalColorSlot), RE::BSGraphics::SetRenderTargetMode::SRTM_CLEAR, true);
		for (std::uint32_t slot = 1; slot < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++slot) {
			renderer->SetRenderTarget(slot, RenderTarget::kNONE, RE::BSGraphics::SetRenderTargetMode::SRTM_NO_CLEAR, false);
		}
		manager->SetCurrentDepthStencilTarget(static_cast<RE::RENDER_TARGET_DEPTHSTENCIL>(settings.portalDepthSlot),
			RE::BSGraphics::SetRenderTargetMode::SRTM_CLEAR, 0);
		state.depthStencilDepthMode = static_cast<decltype(state.depthStencilDepthMode)>(kDepthTestWrite);
		state.stateUpdateFlags.set(RE::BSGraphics::ShaderFlags::DIRTY_DEPTH_MODE);
		SavedLook  look;
		const bool interiorLook = settings.portalInteriorAmbient && g_door.cell;
		if (interiorLook) {
			UseInteriorLook(look);
		}
		AimParticles(main);
		t_inPass = true;
		g_depthCopied = false;
		Mark(PassPhase::kSubmit);
		RE::BSGraphics::Renderer::SubmitAccumulator(g_camera.get(), accumulator, 0);
		t_inPass = false;
		Mark(PassPhase::kDrain);
		DrainPasses(accumulator);
		Mark(PassPhase::kRestore);
		ReturnActorLights(lentLights);
		SleepLights(woken);
		HideActors(shown);
		if (interiorLook) {
			RestoreLook(look);
		}

		if (g_dumpRequested) {
			g_dumpRequested = false;
			WriteDump(g_target.color, g_viewportWidth, g_viewportHeight, "portal", ++g_dumps);
		}

		if (g_interiorImageSpace && g_interiorLook && g_door.cell && g_interiorLook->cell == g_door.cell->GetFormID()) {
			if (auto* imageSpaces = RE::ImageSpaceManager::GetSingleton()) {
				auto& data = imageSpaces->GetImageSpaceData();
				if (!g_gameImageSpace) {
					g_gameImageSpace = data;
				}
				data = g_interiorLook->data;
				++g_imageSpaceSwaps;
			}
		}

		// Back to the game: nothing of ours stays bound, and its whole state is applied again.
		if (auto* context = Context()) {
			context->OMSetRenderTargets(0, nullptr, nullptr);
		}
		ReturnSlots(lent);
		state = savedState;
		state.stateUpdateFlags = static_cast<RE::BSGraphics::ShaderFlags>(kAllDirty);
		state.PSResourceModifiedBits = kAllModified;
		state.PSSamplerModifiedBits = kAllModified;
		state.CSResourceModifiedBits = kAllModified;
		state.CSSamplerModifiedBits = kAllModified;
		state.CSUAVModifiedBits = 0xFF;
		RE::BSShaderAccumulator::SetCurrentAccumulator(savedAccumulator);
		Mark(PassPhase::kIdle);

		const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		g_cpuMsSum += ms;
		g_cpuMsMax = (std::max)(g_cpuMsMax, ms);
		++g_rendered;
		const auto frame = Trace::Frame();
		g_passesInFrame = frame == g_passFrame ? g_passesInFrame + 1 : 1;
		g_passFrame = frame;
		g_maxPassesPerFrame = (std::max)(g_maxPassesPerFrame, g_passesInFrame);
	}

	std::uint32_t BytesPerPixel(DXGI_FORMAT a_format)
	{
		switch (a_format) {
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
			return 8;
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
			return 4;
		default:
			return 0;
		}
	}

	// DDS (DX10 header) of the top left a_width x a_height of a texture, read back through a staging copy:
	// the GPU waits for it.
	void WriteDump(ID3D11Texture2D* a_texture, std::uint32_t a_width, std::uint32_t a_height, std::string_view a_name, std::uint32_t a_number)
	{
		auto* device = Device();
		auto* context = Context();
		if (!device || !context || !a_texture) {
			return;
		}
		D3D11_TEXTURE2D_DESC desc{};
		a_texture->GetDesc(&desc);
		const auto pixelBytes = BytesPerPixel(desc.Format);
		if (pixelBytes == 0 || desc.SampleDesc.Count != 1) {
			Trace::Write("portal.dump", Trace::Args{}.Str("name", a_name).Int("format", desc.Format).Int("samples", desc.SampleDesc.Count).Str("skipped", "format"), nullptr, true);
			return;
		}
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.MiscFlags = 0;
		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		ID3D11Texture2D* staging = nullptr;
		if (FAILED(device->CreateTexture2D(&desc, nullptr, &staging))) {
			return;
		}
		context->CopySubresourceRegion(staging, 0, 0, 0, 0, a_texture, 0, nullptr);
		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
			staging->Release();
			return;
		}
		const std::uint32_t width = (std::min)(a_width, desc.Width);
		const std::uint32_t height = (std::min)(a_height, desc.Height);
		bool                fallback = false;
		const auto          path = Settings::Get().ResolveLogDirectory(fallback) / std::format("{}-{}.dds", a_name, a_number);
		if (std::ofstream file{ path, std::ios::binary }) {
			const std::uint32_t rowBytes = width * pixelBytes;
			std::uint32_t       header[32]{};
			header[0] = 124;
			header[1] = 0x100F;  // caps, height, width, pitch, pixel format
			header[2] = height;
			header[3] = width;
			header[4] = rowBytes;
			header[18] = 32;   // pixel format size
			header[19] = 0x4;  // fourCC
			header[20] = 'D' | ('X' << 8) | ('1' << 16) | ('0' << 24);
			header[26] = 0x1000;  // caps: texture
			const std::uint32_t dx10[5]{ static_cast<std::uint32_t>(desc.Format), 3 /* texture 2D */, 0, 1, 0 };
			file.write("DDS ", 4);
			file.write(reinterpret_cast<const char*>(header), 124);
			file.write(reinterpret_cast<const char*>(dx10), sizeof(dx10));
			for (std::uint32_t row = 0; row < height; ++row) {
				file.write(static_cast<const char*>(mapped.pData) + static_cast<std::size_t>(row) * mapped.RowPitch, rowBytes);
			}
		}
		context->Unmap(staging, 0);
		staging->Release();
		auto*      player = RE::PlayerCharacter::GetSingleton();
		const auto format = desc.Format;
		Trace::Write("portal.dump", Trace::Args{}
										.Str("name", a_name)
										.Str("path", path.string())
										.Int("width", width)
										.Int("height", height)
										.Int("format", format)
										.Cell("playerCell", player ? player->GetParentCell() : nullptr)
										.Bool("portal", g_active)
										.Raw("imageSpace", Portal::ImageSpaceJson()),
			nullptr, true);
	}

	void RunPass()
	{
		// 0.8.9-0.8.10: after Alt+Tab the fullscreen window came back and went away again. No pass
		// while the window is not in front; every change of that goes to the trace.
		const auto* mainLoop = RE::Main::GetSingleton();
		const auto  window = mainLoop ? reinterpret_cast<HWND>(mainLoop->wnd) : nullptr;
		const bool  foreground = window && ::GetForegroundWindow() == window && !::IsIconic(window);
		if (foreground != g_foreground) {
			g_foreground = foreground;
			Trace::Write("window.focus", Trace::Args{}.Bool("foreground", foreground).Bool("iconic", window && ::IsIconic(window)).Bool("portal", g_active),
				nullptr, true);
		}
		if (g_active && foreground) {
			Render();
		} else if (g_active) {
			++g_skipped;
			g_skipWhy = "game window not in the foreground";
		}
	}

	// Main::RenderWorld(bool) (AE#107142): draws the world. RenderPlayerView (AE#36559) submitted the
	// world's culling jobs before it and finishes them after it (AE#69353), so they are running here.
	struct RenderWorld
	{
		static void thunk(bool a_arg)
		{
			if (Trace::IsMainThread() && !g_passAtFence.load(std::memory_order_relaxed)) {
				RunPass();
			}
			func(a_arg);
		}

		static inline decltype(&thunk) func;
	};
}

namespace Portal
{
	long AttachRenderWorldDetour()
	{
		RenderWorld::func = reinterpret_cast<decltype(RenderWorld::func)>(REL::Relocation<std::uintptr_t>{ REL::ID(107142) }.address());
		return DetourAttach(reinterpret_cast<PVOID*>(&RenderWorld::func), reinterpret_cast<PVOID>(&RenderWorld::thunk));
	}

	void SetPassAtSceneListFence(bool a_atFence)
	{
		g_passAtFence.store(a_atFence, std::memory_order_relaxed);
	}

	void OnSceneListFence()
	{
		if (g_passAtFence.load(std::memory_order_relaxed) && Trace::IsMainThread()) {
			RunPass();
		}
	}

	std::string_view PassPhaseName()
	{
		return kPassPhaseNames[std::to_underlying(g_passPhase.load(std::memory_order_relaxed))];
	}

	const std::string& Fault()
	{
		return g_fault;
	}

	void ToggleTemporalAA()
	{
		// ImageSpaceManager (a pointer: the game loads AE#414660 with mov) + 0x1F0 -> TAA shaders, + 0x18
		// taaEnabled. The game reads it while it runs (AE#52727, AE#23157); bUseTAA:Display, SkyrimPrefs.ini
		// is not touched.
		auto* manager = RE::ImageSpaceManager::GetSingleton();
		auto* taa = manager ? manager->GetRuntimeData().BSImagespaceShaderISTemporalAA : nullptr;
		if (!taa) {
			Hud::Show("portal", "CLR: TAA switch not found");
			return;
		}
		taa->taaEnabled = !taa->taaEnabled;
		Trace::Write("portal.taa", Trace::Args{}.Bool("taaEnabled", taa->taaEnabled).Bool("portal", g_active), nullptr, true);
		Hud::Show("portal", std::format("CLR: temporal antialiasing (TAA) {}", taa->taaEnabled ? "on" : "off"));
	}

	void AdjustAnimationUpdate(const RE::Actor* a_actor, RE::BSAnimationUpdateData& a_data)
	{
		if (!g_forceAnimation.load(std::memory_order_relaxed)) {
			return;
		}
		if (const auto i = PortalActorIndex(a_actor); i < kMaxAnimatedActors) {
			a_data.unk2A = true;  // a full update: BShkbAnimationGraph::Update skips no frame and moves every bone
			a_data.unk2C = true;  // seen: AE#63575 gives the bones of an unseen actor no motion
			g_forcedAnimationUpdates.fetch_add(1, std::memory_order_relaxed);
			if (!t_ownAnimation) {
				g_gameAnimationUpdates[i].fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	void ToggleInteriorImageSpace()
	{
		g_interiorImageSpace = !g_interiorImageSpace;
		Trace::Write("portal.imageSpace", Trace::Args{}
											   .Bool("on", g_interiorImageSpace)
											   .Bool("portal", g_active)
											   .FormID("captured", g_interiorLook ? g_interiorLook->cell : 0)
											   .Raw("game", ImageSpaceJson()),
			nullptr, true);
		Hud::Show("portal", g_interiorImageSpace && !g_interiorLook ?
		                        "CLR: interior image space through the portal on, but not captured yet: step inside once" :
		                        std::format("CLR: interior image space through the portal {}", g_interiorImageSpace ? "on" : "off"));
	}

	std::string ImageSpaceDataJson(const RE::ImageSpaceData& a_data)
	{
		const auto& hdr = a_data.baseData.hdr;
		const auto& cinematic = a_data.baseData.cinematic;
		const auto& tint = a_data.baseData.tint;
		return Trace::Args{}
			.Raw("hdr", std::format("[{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f},{:.3f}]", hdr.eyeAdaptSpeed, hdr.bloomBlurRadius, hdr.bloomThreshold,
							hdr.bloomScale, hdr.receiveBloomThreshold, hdr.white, hdr.sunlightScale, hdr.skyScale, hdr.eyeAdaptStrength))
			.Raw("cinematic", std::format("[{:.3f},{:.3f},{:.3f}]", cinematic.saturation, cinematic.brightness, cinematic.contrast))
			.Raw("tint", std::format("[{:.3f},{:.3f},{:.3f},{:.3f}]", tint.amount, tint.color.red, tint.color.green, tint.color.blue))
			.Num("fade", a_data.modData.data[RE::ImageSpaceModData::kFadeAmount])
			.Num("baseAmount", a_data.baseAmount)
			.Num("modAmount", a_data.modAmount)
			.Json();
	}

	std::string ImageSpaceJson()
	{
		auto* manager = RE::ImageSpaceManager::GetSingleton();
		if (!manager) {
			return "null";
		}
		// hdr: eyeAdaptSpeed, bloomBlurRadius, bloomThreshold, bloomScale, receiveBloomThreshold, white,
		// sunlightScale, skyScale, eyeAdaptStrength; cinematic: saturation, brightness, contrast; tint: amount, rgb
		return Trace::Args{}
			.Raw("now", ImageSpaceDataJson(manager->GetImageSpaceData()))
			.Raw("interior", g_interiorLook ? ImageSpaceDataJson(g_interiorLook->data) : "null")
			.FormID("interiorCell", g_interiorLook ? g_interiorLook->cell : 0)
			.Bool("swapOn", g_interiorImageSpace)
			.Int("swaps", static_cast<std::int64_t>(g_imageSpaceSwaps))
			.Int("held", static_cast<std::int64_t>(g_imageSpaceHeld))
			.Json();
	}

	// Every frame, before the passes: the swap of the last frame goes back unless the game wrote its own
	// values since, and inside an interior its image space is kept for the swap.
	void LookTick()
	{
		auto* manager = RE::ImageSpaceManager::GetSingleton();
		if (!manager) {
			return;
		}
		auto& data = manager->GetImageSpaceData();
		if (g_gameImageSpace) {
			if (g_interiorLook && std::memcmp(&data, &g_interiorLook->data, sizeof(data)) == 0) {
				data = *g_gameImageSpace;
			}
			g_gameImageSpace.reset();
		}
		auto*      player = RE::PlayerCharacter::GetSingleton();
		auto*      cell = player ? player->GetParentCell() : nullptr;
		auto*      ui = RE::UI::GetSingleton();
		const bool transition = !ui || ui->IsMenuOpen(RE::FaderMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
		if (cell && cell->IsInteriorCell() && !g_active && !transition) {
			g_interiorLook = InteriorLook{ cell->GetFormID(), data };
		}
	}

	void AddAnimatedActors(const RE::BSTArray<RE::ActorHandle>& a_handles, void* a_list)
	{
		using Push = void (*)(void*, RE::NiPointer<RE::Actor>*);
		static REL::Relocation<Push> push{ REL::ID(41496) };
		if (!g_gameAnimation.load(std::memory_order_relaxed) || !a_list) {
			return;
		}
		for (const auto& handle : a_handles) {
			auto actor = handle.get();
			if (!actor || PortalActorIndex(actor.get()) >= kMaxAnimatedActors) {
				continue;
			}
			// AE#41466's own tests, but for the cell: already in the list when it is attached.
			const auto bits = actor->GetActorRuntimeData().boolBits;
			auto*      cell = actor->GetParentCell();
			auto*      root = actor->Get3D();
			if (!cell || cell->IsAttached() || !root || !root->parent || !actor->GetActorRuntimeData().currentProcess ||
				!bits.all(RE::Actor::BOOL_BITS::kProcessMe) || bits.all(RE::Actor::BOOL_BITS::kInTempChangeList) || actor->IsDisabled() || actor->IsDeleted()) {
				continue;
			}
			push(a_list, &actor);
			g_gameAnimationAdded.fetch_add(1, std::memory_order_relaxed);
		}
	}

	std::string ControllerFlagsText(const RE::bhkCharacterController& a_controller)
	{
		using Flag = RE::CHARACTER_FLAGS;
		static constexpr std::pair<Flag, std::string_view> kNames[]{
			{ Flag::kTryStep, "tryStep" },
			{ Flag::kSupport, "support" },
			{ Flag::kCheckSupport, "checkSupport" },
			{ Flag::kNoSim, "noSim" },
			{ Flag::kFarAway, "farAway" },
			{ Flag::kQuickSimulate, "quickSimulate" },
			{ Flag::kOnStairs, "onStairs" },
			{ Flag::kStuckQuad, "stuck" },
			{ Flag::kNoGravityOnGround, "noGravityOnGround" },
			{ Flag::kNoCharacterCollisions, "noCharacterCollisions" },
			{ Flag::kPossiblePathObstacle, "possiblePathObstacle" },
		};
		auto text = std::format("0x{:08X}", a_controller.flags.underlying());
		for (const auto& [flag, name] : kNames) {
			if (a_controller.flags.all(flag)) {
				text += ' ';
				text += name;
			}
		}
		return text;
	}

	bool InInterior(const RE::NiAVObject* a_object)
	{
		const auto* space = g_space.load(std::memory_order_acquire);
		if (!space) {
			return false;
		}
		for (const auto* object = a_object; object; object = object->parent) {
			if (object == space->scene) {
				return true;
			}
		}
		return false;
	}

	void BeforeEmission(RE::NiPSysModifier* a_emitter, std::uint16_t a_count)
	{
		if (!Settings::Get().portalIgnoreParticleBudget || !InInterior(a_emitter->target)) {
			return;
		}
		const float scale = EmissionScale();
		if (scale < 1.0f) {
			// AE#74651 emits count * scale + this remainder of its last call, the first field after
			// NiPSysModifier. The difference added here makes that the whole count. Not the scale itself:
			// emitters of other cells read that global at the same moment.
			auto& remainder = *reinterpret_cast<float*>(reinterpret_cast<std::uintptr_t>(a_emitter) + sizeof(RE::NiPSysModifier));
			remainder += static_cast<float>(a_count) * (1.0f - scale);
			g_emissionsWithoutBudget.fetch_add(1, std::memory_order_relaxed);
		}
	}

	std::string MasterParticlesJson(const RE::NiAVObject* a_scene)
	{
		auto* manager = ParticleManager();
		if (!manager) {
			return "null";
		}
		std::string chain;
		for (const RE::NiAVObject* object = manager->parent; object; object = object->parent) {
			const auto* rtti = object->GetRTTI();
			chain += std::format("{}{}({})", chain.empty() ? "" : " < ", object->name.c_str(), rtti && rtti->name ? rtti->name : "?");
		}
		std::string systems;
		for (const auto& child : manager->GetChildren()) {
			auto* master = child ? netimmerse_cast<RE::BSMasterParticleSystem*>(child.get()) : nullptr;
			if (!master) {
				continue;
			}
			const auto&  data = master->GetMasterParticleSystemRuntimeData();
			std::int64_t emittersInScene = 0;
			RE::NiPoint3 firstEmitter;
			for (const auto& emitter : data.emitterObjs) {
				for (const RE::NiAVObject* object = emitter.get(); object; object = object->parent) {
					if (object == a_scene) {
						firstEmitter = emittersInScene++ ? firstEmitter : emitter->world.translate;
						break;
					}
				}
			}
			std::int64_t particles = 0;
			std::string  name;
			std::string  texture;
			RE::NiPoint3 firstParticle;
			for (auto* system : data.particles) {
				auto* particleData = system ? system->GetParticlesRuntimeData().particleData.get() : nullptr;
				if (!particleData) {
					continue;
				}
				const auto active = particleData->GetActiveVertexCount();
				if (name.empty()) {
					name = system->name.c_str();
					auto* shader = system->GetGeometryRuntimeData().shaderProperty.get();
					auto* effect = netimmerse_cast<RE::BSEffectShaderProperty*>(shader);
					const auto* material = effect ? static_cast<const RE::BSEffectShaderMaterial*>(effect->material) : nullptr;
					texture = material ? material->sourceTexturePath.c_str() : "";
				}
				if (!particles && active && particleData->GetParticlesRuntimeData().positions) {
					firstParticle = particleData->GetParticlesRuntimeData().positions[0];
				}
				particles += active;
			}
			systems += (systems.empty() ? "" : ",") + Trace::Args{}
														  .Str("system", name)
														  .Str("texture", texture)
														  .Int("index", data.index)
														  .Int("emitters", static_cast<std::int64_t>(data.emitterObjs.size()))
														  .Int("activeEmitters", data.unk160)
														  .Int("maxEmitters", data.maxEmitterObj)
														  .Int("maxParticlesPerFrame", data.maxParticlesPerFrame)
														  .Int("emittersInScene", emittersInScene)
														  .Pos("firstEmitterInScene", firstEmitter)
														  .Int("particles", particles)
														  .Pos("firstParticle", firstParticle)
														  .Bool("culled", master->GetAppCulled())
														  .Json();
		}
		return Trace::Args{}.Str("parents", chain).Bool("culled", manager->GetAppCulled()).Raw("systems", "[" + systems + "]").Json();
	}

	std::string FireSampleJson(RE::NiAVObject* a_scene)
	{
		constexpr std::size_t kFireOwners = 3;    // the hearth and fireplaces
		constexpr std::size_t kCandleOwners = 2;  // candles
		constexpr std::size_t kShapesPerOwner = 12;
		constexpr std::size_t kParticles = 3;
		if (!a_scene) {
			return "null";
		}
		std::vector<RE::NiAVObject*> owners;
		std::size_t                  fires = 0;
		std::size_t                  candles = 0;
		RE::BSVisit::TraverseScenegraphObjects(a_scene, [&](RE::NiAVObject* a_object) {
			if (a_object->AsFadeNode()) {
				std::string name = a_object->name.c_str();
				std::ranges::transform(name, name.begin(), [](unsigned char a_c) { return static_cast<char>(std::tolower(a_c)); });
				if ((name.contains("campfire") || name.contains("fireplace")) && fires < kFireOwners) {
					owners.push_back(a_object);
					++fires;
				} else if (name.contains("candle") && candles < kCandleOwners) {
					owners.push_back(a_object);
					++candles;
				}
			}
			return RE::BSVisit::BSVisitControl::kContinue;
		});
		std::string json;
		for (auto* owner : owners) {
			std::string shapes;
			std::size_t count = 0;
			RE::BSVisit::TraverseScenegraphGeometries(owner, [&](RE::BSGeometry* a_geometry) {
				if (count++ >= kShapesPerOwner) {
					return RE::BSVisit::BSVisitControl::kStop;
				}
				bool hidden = false;
				for (const RE::NiAVObject* object = a_geometry; object && object != owner->parent; object = object->parent) {
					hidden = hidden || object->GetAppCulled();
				}
				const auto* rtti = a_geometry->GetRTTI();
				auto*       shader = a_geometry->GetGeometryRuntimeData().shaderProperty.get();
				Trace::Args args;
				args.Str("shape", a_geometry->name.c_str())
					.Str("type", rtti && rtti->name ? rtti->name : "?")
					.Bool("hidden", hidden)
					.Pos("offset", a_geometry->worldBound.center - owner->world.translate)
					.Num("radius", a_geometry->worldBound.radius);
				if (shader) {
					args.Str("shaderFlags", std::format("0x{:016X}", shader->flags.underlying()))
						.Num("alpha", shader->alpha)
						.Int("lastAccumulated", shader->lastAccumulatedFrameCount)
						.Num("fade", shader->fadeNode ? shader->fadeNode->GetRuntimeData().currentFade : -1.0f);
				}
				if (auto* effect = netimmerse_cast<RE::BSEffectShaderProperty*>(shader)) {
					if (const auto* material = static_cast<const RE::BSEffectShaderMaterial*>(effect->material)) {
						const auto& color = material->baseColor;
						args.Raw("baseColor", std::format("[{:.2f},{:.2f},{:.2f},{:.2f}]", color.red, color.green, color.blue, color.alpha))
							.Num("baseColorScale", material->baseColorScale)
							.Num("softFalloffDepth", material->softFalloffDepth)
							.Str("texture", material->sourceTexturePath.c_str())
							.Str("greyscale", material->greyscaleTexturePath.c_str());
					}
				}
				if (auto* particles = netimmerse_cast<RE::NiParticleSystem*>(a_geometry)) {
					const auto& system = particles->GetParticleSystemRuntimeData();
					auto*       data = particles->GetParticlesRuntimeData().particleData.get();
					const auto  active = data ? data->GetActiveVertexCount() : 0;
					args.Int("particles", active).Bool("worldSpace", system.isWorldspace).Num("emitScale", system.unk180).Num("sizeScale", system.unk188);
					std::string each;
					const auto* arrays = data ? &data->GetParticlesRuntimeData() : nullptr;
					const auto* info = data ? data->GetPSysRuntimeData().particleInfo : nullptr;
					for (std::uint16_t i = 0; arrays && i < active && i < kParticles; ++i) {
						Trace::Args particle;
						if (arrays->positions) {
							const auto world = system.isWorldspace ? arrays->positions[i] : particles->world * arrays->positions[i];
							particle.Pos("offset", world - owner->world.translate);
						}
						particle.Num("radius", arrays->radii ? arrays->radii[i] : -1.0f)
							.Num("size", arrays->sizes ? arrays->sizes[i] : -1.0f)
							.Num("alpha", arrays->color ? arrays->color[i].alpha : -1.0f);
						if (info) {
							particle.Num("age", info[i].age).Num("lifespan", info[i].lifespan);
						}
						each += (each.empty() ? "" : ",") + particle.Json();
					}
					args.Raw("first", "[" + each + "]");
				}
				shapes += (shapes.empty() ? "" : ",") + args.Json();
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			json += (json.empty() ? "" : ",") + Trace::Args{}
													.Str("model", owner->name.c_str())
													.Bool("hidden", owner->GetAppCulled())
													.Pos("at", owner->world.translate)
													.Raw("shapes", "[" + shapes + "]")
													.Json();
		}
		return "[" + json + "]";
	}

	bool IntoInterior(const RE::NiAVObject* a_object, RE::NiPoint3& a_point, Carried a_what)
	{
		const auto* space = g_space.load(std::memory_order_acquire);
		const auto* ref = space && a_object ? a_object->GetUserData() : nullptr;
		if (!ref || ref->GetParentCell() != space->cell) {
			return false;
		}
		a_point = space->toInterior * a_point;
		g_pointsCarried[std::to_underlying(a_what)].fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	void NoteMovementUpdate(const RE::Actor* a_actor)
	{
		if (const auto i = PortalActorIndex(a_actor); i < kMaxAnimatedActors) {
			g_movementUpdates[i].fetch_add(1, std::memory_order_relaxed);
		}
	}

	void NoteFootFeedback(const RE::Actor* a_actor)
	{
		if (PortalActorIndex(a_actor) < kMaxAnimatedActors) {
			g_footFeedback.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void AfterActorCulled(RE::Actor* a_actor)
	{
		if (g_actorsSeen.load(std::memory_order_relaxed) && PortalActorIndex(a_actor) < kMaxAnimatedActors) {
			ClearNotVisible(a_actor->Get3D());
		}
	}

	ActorScope::ActorScope(const RE::Actor* a_actor)
	{
		if (g_virtualCamera.load(std::memory_order_relaxed) && PortalActorIndex(a_actor) < kMaxAnimatedActors) {
			++t_actorScope;
			_entered = true;
		}
	}

	ActorScope::~ActorScope()
	{
		if (_entered) {
			--t_actorScope;
		}
	}

	RE::NiCamera* ScopedCamera()
	{
		if (t_actorScope == 0) {
			return nullptr;
		}
		auto* camera = g_virtualCamera.load(std::memory_order_relaxed);
		if (camera) {
			g_worldCameraCarried.fetch_add(1, std::memory_order_relaxed);
		}
		return camera;
	}

	bool DistanceThroughDoor(const RE::TESObjectREFR* a_first, const RE::TESObjectREFR* a_second, float& a_squared)
	{
		const auto* space = g_space.load(std::memory_order_acquire);
		if (!space || !g_distanceThroughDoor.load(std::memory_order_relaxed)) {
			return false;
		}
		const auto* player = RE::PlayerCharacter::GetSingleton();
		const auto* other = a_first == player ? a_second : a_second == player ? a_first : nullptr;
		if (!other || other->GetParentCell() != space->cell) {
			return false;
		}
		const auto carried = space->toInterior * player->GetPosition();
		a_squared = carried.GetSquaredDistance(other->GetPosition());
		g_distancesCarried.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	bool SpeakerThroughDoor(const RE::TESObjectREFR* a_speaker, RE::NiPoint3& a_position)
	{
		const auto* space = g_space.load(std::memory_order_acquire);
		if (!space || !a_speaker || !g_subtitlesThroughDoor.load(std::memory_order_relaxed) || a_speaker->GetParentCell() != space->cell) {
			return false;
		}
		a_position = space->toExterior * a_position;
		++g_subtitlesCarried;
		return true;
	}

	bool SoundThroughDoor(RE::NiPoint3& a_position)
	{
		const auto* space = g_space.load(std::memory_order_acquire);
		if (!space || !g_soundThroughDoor.load(std::memory_order_relaxed) || a_position.GetDistance(space->center) > space->radius) {
			return false;
		}
		if (a_position.x == 0.0f && a_position.y == 0.0f && a_position.z == 0.0f) {
			return false;  // a sound with no place of its own
		}
		a_position = space->toExterior * a_position;
		g_lastSound = a_position;
		g_soundsCarried.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	void NoteVisible(const RE::NiAVObject* a_object)
	{
		if (!t_culling || g_watchedEffects.empty()) {
			return;
		}
		if (const auto it = g_watchedShapes.find(a_object); it != g_watchedShapes.end()) {
			++g_watchedEffects[it->second].visits;
		}
		const auto [first, last] = g_watchedParents.equal_range(a_object);
		for (auto it = first; it != last; ++it) {
			++g_watchedEffects[it->second].parentVisits;
		}
	}

	bool GrantFullProcessing(const RE::Actor* a_actor)
	{
		if (!g_fullProcessing.load(std::memory_order_relaxed) || PortalActorIndex(a_actor) >= kMaxAnimatedActors) {
			return false;
		}
		g_fullProcessingGranted.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	bool BeforeEffectPasses(const RE::BSGeometry* a_geometry)
	{
		if (!t_culling) {
			return false;
		}
		if (const auto it = g_watchedShapes.find(a_geometry); it != g_watchedShapes.end()) {
			++g_watchedEffects[it->second].passCalls;
		}
		if (g_effectsLit) {
			return false;
		}
		for (auto& added : g_lights) {
			if (added.light) {
				added.light->frustrumCull = kLightOutOfFrustum;
			}
		}
		++g_effectLightsHeld;
		return true;
	}

	void AfterEffectPasses(bool a_held, const RE::BSGeometry* a_geometry, bool a_gotPasses)
	{
		if (t_culling && !a_gotPasses) {
			if (const auto it = g_watchedShapes.find(a_geometry); it != g_watchedShapes.end()) {
				++g_watchedEffects[it->second].noPasses;
			}
		}
		if (!a_held) {
			return;
		}
		for (auto& added : g_lights) {
			if (added.light) {
				added.light->frustrumCull = kLightInFrustum;
			}
		}
	}

	void BeforeParticlePack(PackedCamera& a_saved)
	{
		if (!t_inPass) {
			return;
		}
		auto&       state = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData();
		auto*       origin = reinterpret_cast<RE::NiPoint3*>(&state.posAdjust);  // the fields are private arrays of one eye
		auto*       view = reinterpret_cast<RE::BSGraphics::ViewData*>(&state.cameraData);
		const auto  heldUp = ReadVector(&view->viewUp);
		const auto  heldRight = ReadVector(&view->viewRight);
		++g_packCalls;
		g_packHeldPortal += Faces(heldUp, heldRight, g_packPortal) ? 1 : 0;
		g_packHeldMain += Faces(heldUp, heldRight, g_packMain) ? 1 : 0;
		g_packHeldOther += !Faces(heldUp, heldRight, g_packPortal) && !Faces(heldUp, heldRight, g_packMain) ? 1 : 0;
		g_packOriginDrift = (std::max)(g_packOriginDrift, origin->GetDistance(g_packPortal.origin));

		const auto facing = static_cast<ParticleFacing>(Settings::Get().portalParticleFacing);
		if (facing != ParticleFacing::kPortal && facing != ParticleFacing::kView) {
			return;
		}
		a_saved = { .origin = *origin, .up = heldUp, .right = heldRight, .forward = ReadVector(&view->viewForward), .replaced = true };
		const auto& camera = facing == ParticleFacing::kView ? g_packView : g_packPortal;
		*origin = camera.origin;
		WriteVector(&view->viewUp, camera.up);
		WriteVector(&view->viewRight, camera.right);
		WriteVector(&view->viewForward, camera.forward);
	}

	void AfterParticlePack(const PackedCamera& a_saved)
	{
		if (!a_saved.replaced) {
			return;
		}
		auto& state = RE::BSGraphics::RendererShadowState::GetSingleton()->GetRuntimeData();
		auto* view = reinterpret_cast<RE::BSGraphics::ViewData*>(&state.cameraData);
		*reinterpret_cast<RE::NiPoint3*>(&state.posAdjust) = a_saved.origin;
		WriteVector(&view->viewUp, a_saved.up);
		WriteVector(&view->viewRight, a_saved.right);
		WriteVector(&view->viewForward, a_saved.forward);
	}

	std::uint32_t BeforeEffectTechnique(std::uint32_t a_technique)
	{
		if (!t_inPass) {
			return a_technique;
		}
		++g_effectSetups;
		if (((a_technique - kEffectTechniqueBase) & kSoftEffectBit) != 0) {
			++g_softEffectSetups;
			if (!g_softEffects) {
				++g_softEffectsDropped;
				return a_technique - kSoftEffectBit;  // the same effect without the soft part (its depth fade)
			}
		}
		if (g_depthMode == DepthMode::kCopiedView && !g_depthCopied) {
			g_depthCopied = true;  // the opaque shapes are drawn: effects come after them
			if (auto* context = Context()) {
				context->CopyResource(g_target.depthCopy, g_target.depth);
				++g_depthCopies;
			}
		}
		return a_technique;
	}

	bool Prepare(std::string& a_why)
	{
		const auto& settings = Settings::Get();
		if (!settings.portalEnabled) {
			a_why = "[Portal] bEnabled=0";
			return false;
		}
		if (REL::Module::get().version() != SKSE::RUNTIME_SSE_1_6_1170) {
			a_why = "the portal is verified on 1.6.1170 only (docs/PORTABILITY.md)";
			return false;
		}
		if (settings.portalColorSlot == 0 || settings.portalColorSlot >= RenderTarget::kTOTAL ||
			settings.portalDepthSlot == 0 || settings.portalDepthSlot >= RE::RENDER_TARGET_DEPTHSTENCIL::kTOTAL) {
			a_why = "[Portal] iColorSlot / iDepthSlot out of range (0 is the game's main target)";
			return false;
		}
		if (!CreateTarget(a_why) || !LoadQuad(a_why)) {
			return false;
		}
		if (!g_depthModeRead) {
			g_depthModeRead = true;
			g_depthMode = static_cast<DepthMode>((std::min)(settings.portalDepthMode, std::to_underlying(DepthMode::kTotal) - 1));
		}
		if (!g_effectsLitRead) {
			g_effectsLitRead = true;
			g_effectsLit = settings.portalLightEffects;
		}
		if (!g_softEffectsRead) {
			g_softEffectsRead = true;
			g_softEffects = settings.portalSoftEffects;
		}
		if (!g_controllerModeRead) {
			g_controllerModeRead = true;
			g_controllerMode = static_cast<ControllerMode>((std::min)(settings.portalControllerMode, std::to_underlying(ControllerMode::kTotal) - 1));
		}
		if (!g_camera) {
			auto* main = RE::Main::WorldRootCamera();
			auto* clone = main ? netimmerse_cast<RE::NiCamera*>(main->Clone()) : nullptr;
			if (!clone) {
				a_why = "cannot clone the world camera";
				return false;
			}
			g_camera.reset(clone);
		}
		if (!g_accumulator) {
			g_accumulator.reset(RE::BSShaderAccumulator::Create(kAccumulatorType));
			if (!g_accumulator) {
				a_why = "cannot create a shader accumulator";
				return false;
			}
		}
		return true;
	}

	bool Start(const Door& a_door, std::string& a_why)
	{
		Stop();
		if (!a_door.scene || !a_door.parent || a_door.width <= 0.0f || a_door.height <= 0.0f) {
			a_why = "no interior 3D, door node or door size";
			return false;
		}
		if (!Prepare(a_why)) {
			return false;
		}
		const auto& settings = Settings::Get();
		g_door = a_door;

		// The unattached cell's own nodes keep stale bounds (radius 1 at the origin, 0.8.1 tree): the
		// culling tests nodes by them, so recompute them from the references' bounds.
		auto*       root = g_door.scene->AsNode();
		const float radiusBefore = g_door.scene->worldBound.radius;
		if (root) {
			for (auto& child : root->GetChildren()) {
				if (child) {
					child->UpdateWorldBound();
				}
			}
		}
		g_door.scene->UpdateWorldBound();

		PlaceQuad();
		SwapQuadTexture();
		std::size_t alreadyInScene = 0;
		AddInteriorLights(alreadyInScene);
		SampleShapes();
		g_rebuildPasses = true;
		g_rebuildEffects = true;
		g_lastAnimateUs = 0;
		g_nextActorRefreshUs = 0;
		g_forceAnimation.store(settings.portalAnimateActors, std::memory_order_relaxed);
		g_fullProcessing.store(settings.portalFullActorProcessing, std::memory_order_relaxed);
		g_gameAnimation.store(settings.portalGameAnimatesActors, std::memory_order_relaxed);
		WatchEffects();
		g_actorsSeen.store(settings.portalActorsSeen, std::memory_order_relaxed);
		g_soundThroughDoor.store(settings.portalSoundThroughDoor, std::memory_order_relaxed);
		g_distanceThroughDoor.store(settings.portalDistanceThroughDoor, std::memory_order_relaxed);
		g_subtitlesThroughDoor.store(settings.portalSubtitlesThroughDoor, std::memory_order_relaxed);
		g_space.store(new Space{ .cell = g_door.cell,
						  .scene = g_door.scene.get(),
						  .toInterior = g_door.toExterior.Invert(),
						  .toExterior = g_door.toExterior,
						  .center = g_door.scene->worldBound.center,
						  .radius = g_door.scene->worldBound.radius },
			std::memory_order_release);
		CarryCamera();
		g_virtualCamera.store(settings.portalVirtualCamera ? g_camera.get() : nullptr, std::memory_order_relaxed);
		g_active = true;

		std::string lights;
		for (const auto& added : g_lights) {
			lights += (lights.empty() ? "" : ",") + LightJson(added);
		}
		Trace::Write("portal.start", Trace::Args{}
										 .Num("sceneRadiusBefore", radiusBefore)
										 .Num("sceneRadiusAfter", g_door.scene->worldBound.radius)
										 .Pos("sceneCenterAfter", g_door.scene->worldBound.center)
										 .Int("interiorLightsAlreadyInScene", static_cast<std::int64_t>(alreadyInScene))
										 .Bool("cellAttached", g_door.attached)
										 .Raw("originalLights", OriginalLightsJson())
										 .Raw("lightSourceRefs", LightSourceRefsJson())
										 .Raw("lights", "[" + lights + "]")
										 .Raw("sampledShapes", SampledShapesJson())
										 .Raw("controllers", ControllersJson()),
			nullptr, true);
		return true;
	}

	void Stop()
	{
		g_active = false;
		g_virtualCamera.store(nullptr, std::memory_order_relaxed);
		g_soundThroughDoor.store(false, std::memory_order_relaxed);
		g_distanceThroughDoor.store(false, std::memory_order_relaxed);
		g_subtitlesThroughDoor.store(false, std::memory_order_relaxed);
		g_space.store(nullptr, std::memory_order_release);
		RemoveLights();
		ForgetActors();
		g_sampledShapes.clear();
		g_watchedEffects.clear();
		g_watchedShapes.clear();
		g_watchedParents.clear();
		if (g_quad && g_quad->parent) {
			g_quad->parent->DetachChild(g_quad.get());
		}
		g_door = {};
	}

	void Tick()
	{
		LookTick();
		if (!g_active) {
			return;
		}
		CarryCamera();
		if (g_actorsSeen.load(std::memory_order_relaxed)) {
			for (const auto& root : g_actorRoots) {
				ClearNotVisible(root.get());
			}
		}
		const auto now = Trace::NowUs();
		if (now < g_nextActorRefreshUs) {
			return;
		}
		RefreshActors(now);

		// Lights added before their cell's references arrived hold no shapes, and the game collects a
		// static light's shapes only once (0.8.22: a dark portal after a quick load).
		auto* scene = WorldScene();
		const bool lit = std::ranges::any_of(g_lights, [](const AddedLight& a_added) { return a_added.mirror || !a_added.light || !a_added.light->geomList.empty(); });
		if (scene && !lit) {
			for (auto& added : g_lights) {
				if (added.light) {
					CollectLitShapes(scene, added.light);
				}
			}
			++g_shapeRecollects;
			g_rebuildPasses = true;
		}
	}

	bool Active()
	{
		return g_active;
	}

	RE::NiAVObject* Quad()
	{
		return g_quad.get();
	}

	std::string StatusJson()
	{
		std::string lights;
		for (const auto& added : g_lights) {
			lights += (lights.empty() ? "" : ",") + LightJson(added);
		}
		const auto json = Trace::Args{}
							  .Bool("active", g_active)
							  .Int("passes", static_cast<std::int64_t>(g_rendered))
							  .Int("skipped", static_cast<std::int64_t>(g_skipped))
							  .Str("skipWhy", g_skipWhy)
							  .Num("cpuMsAverage", g_rendered ? g_cpuMsSum / static_cast<double>(g_rendered) : 0.0)
							  .Num("cpuMsMax", g_cpuMsMax)
							  .Num("eyeDistance", g_eyeDistance)
							  .Bool("textureSwapped", g_textureSwapped)
							  .Int("viewportWidth", g_viewportWidth)
							  .Int("viewportHeight", g_viewportHeight)
							  .Raw("visibleUV", std::format("[{:.3f},{:.3f},{:.3f},{:.3f}]", g_visible.u0, g_visible.u1, g_visible.v0, g_visible.v1))
							  .Int("actors", static_cast<std::int64_t>(g_actorRoots.size()))
							  .Int("actorsShown", static_cast<std::int64_t>(g_actorsShown))
							  .Raw("actorStates", ActorsJson())
							  .Raw("interiorPreprocessed", g_door.scene ? PreprocessedJson() : "null")
							  .Int("depthMode", std::to_underlying(g_depthMode))
							  .Str("depthModeName", kDepthModeNames[std::to_underlying(g_depthMode)])
							  .Int("effectSetups", static_cast<std::int64_t>(g_effectSetups))
							  .Int("softEffectSetups", static_cast<std::int64_t>(g_softEffectSetups))
							  .Int("depthCopies", static_cast<std::int64_t>(g_depthCopies))
							  .Bool("effectsLit", g_effectsLit)
							  .Int("effectLightsHeld", static_cast<std::int64_t>(g_effectLightsHeld))
							  .Raw("effects", g_door.scene ? EffectsJson() : "null")
							  .Int("animated", static_cast<std::int64_t>(g_animated))
							  .Int("animatedByGame", static_cast<std::int64_t>(g_animatedByGame))
							  .Int("forcedAnimationUpdates", static_cast<std::int64_t>(g_forcedAnimationUpdates.exchange(0, std::memory_order_relaxed)))
							  .Int("poseChecks", static_cast<std::int64_t>(g_poseChecks))
							  .Int("poseChanged", static_cast<std::int64_t>(g_poseChanged))
							  .Int("positionsSynced", static_cast<std::int64_t>(g_positionsSynced))
							  .Int("controllerUpdates", static_cast<std::int64_t>(g_controllerUpdates))
							  .Num("controllerMsAverage", g_controllerUpdates ? g_controllerMsSum / static_cast<double>(g_controllerUpdates) : 0.0)
							  .Raw("controllers", g_door.scene ? ControllersJson() : "null")
							  .Int("fullProcessingGranted", static_cast<std::int64_t>(g_fullProcessingGranted.exchange(0, std::memory_order_relaxed)))
							  .Bool("softEffects", g_softEffects)
							  .Int("softEffectsDropped", static_cast<std::int64_t>(g_softEffectsDropped))
							  .Int("gameAnimationAdded", static_cast<std::int64_t>(g_gameAnimationAdded.exchange(0, std::memory_order_relaxed)))
							  .Int("pointsCarried", static_cast<std::int64_t>(g_pointsCarried[std::to_underlying(Carried::kControllerLod)].exchange(0, std::memory_order_relaxed)))
							  .Int("fadesCarried", static_cast<std::int64_t>(g_pointsCarried[std::to_underlying(Carried::kFade)].exchange(0, std::memory_order_relaxed)))
							  .Int("maxPassesPerFrame", static_cast<std::int64_t>(g_maxPassesPerFrame))
							  .Bool("passAtFence", g_passAtFence.load(std::memory_order_relaxed))
							  .Int("particleFacing", Settings::Get().portalParticleFacing)
							  .Int("packCalls", static_cast<std::int64_t>(g_packCalls))
							  .Int("packHeldPortal", static_cast<std::int64_t>(g_packHeldPortal))
							  .Int("packHeldMain", static_cast<std::int64_t>(g_packHeldMain))
							  .Int("packHeldOther", static_cast<std::int64_t>(g_packHeldOther))
							  .Num("packOriginDrift", g_packOriginDrift)
							  .Int("shapeRecollects", static_cast<std::int64_t>(g_shapeRecollects))
							  .Bool("actorsSeen", g_actorsSeen.load(std::memory_order_relaxed))
							  .Int("notVisibleCleared", static_cast<std::int64_t>(g_notVisibleCleared.exchange(0, std::memory_order_relaxed)))
							  .Int("footFeedback", static_cast<std::int64_t>(g_footFeedback.exchange(0, std::memory_order_relaxed)))
							  .Int("worldCameraCarried", static_cast<std::int64_t>(g_worldCameraCarried.exchange(0, std::memory_order_relaxed)))
							  .Int("distancesCarried", static_cast<std::int64_t>(g_distancesCarried.exchange(0, std::memory_order_relaxed)))
							  .Int("soundsCarried", static_cast<std::int64_t>(g_soundsCarried.exchange(0, std::memory_order_relaxed)))
							  .Pos("lastSound", g_lastSound)
							  .Int("subtitlesCarried", static_cast<std::int64_t>(std::exchange(g_subtitlesCarried, 0)))
							  .Raw("imageSpace", ImageSpaceJson())
							  .Str("leftoverGroups", std::format("0x{:05X}", g_leftoverGroups))
							  .Str("fault", g_fault)
							  .Bool("quadMaterialOurs", QuadMaterialNow() == g_quadMaterial)
							  .Num("quadPreviousOffset", QuadPreviousOffset())
							  .Num("emissionScale", EmissionScale())
							  .Int("emissionsWithoutBudget", static_cast<std::int64_t>(g_emissionsWithoutBudget.exchange(0, std::memory_order_relaxed)))
							  .Raw("masters", g_door.scene ? MasterParticlesJson(g_door.scene.get()) : "null")
							  .Int("actorLightsLent", static_cast<std::int64_t>(g_actorLightsLent))
							  .Raw("watchedEffects", g_door.scene ? WatchedEffectsJson() : "null")
							  .Raw("fires", g_door.scene ? FireSampleJson(g_door.scene.get()) : "null")
							  .Bool("cellAttached", g_door.attached)
							  .Raw("originalLights", OriginalLightsJson())
							  .Raw("lights", "[" + lights + "]")
							  .Raw("sampledShapes", SampledShapesJson())
							  .Raw("nearPlayerLights", NearPlayerLightsJson())
							  .Raw("frustum", std::format("[{:.4f},{:.4f},{:.4f},{:.4f},{:.1f},{:.0f}]", g_frustum.fLeft, g_frustum.fRight, g_frustum.fTop,
												  g_frustum.fBottom, g_frustum.fNear, g_frustum.fFar))
							  .Json();
		g_rendered = 0;
		g_maxPassesPerFrame = 0;
		g_packCalls = 0;
		g_packHeldPortal = 0;
		g_packHeldMain = 0;
		g_packHeldOther = 0;
		g_packOriginDrift = 0.0f;
		g_leftoverGroups = 0;
		g_skipped = 0;
		g_cpuMsSum = 0.0;
		g_cpuMsMax = 0.0;
		g_effectSetups = 0;
		g_softEffectSetups = 0;
		g_depthCopies = 0;
		g_effectLightsHeld = 0;
		g_animated = 0;
		g_animatedByGame = 0;
		g_poseChecks = 0;
		g_poseChanged = 0;
		g_positionsSynced = 0;
		g_controllerUpdates = 0;
		g_controllerMsSum = 0.0;
		g_softEffectsDropped = 0;
		g_actorLightsLent = 0;
		return json;
	}

	void RequestDump()
	{
		g_dumpRequested = g_active;
		g_mainDumpRequested = true;
	}

	void AfterPlayerView()
	{
		if (g_gameImageSpace && g_interiorLook) {
			auto* imageSpaces = RE::ImageSpaceManager::GetSingleton();
			if (imageSpaces && std::memcmp(&imageSpaces->GetImageSpaceData(), &g_interiorLook->data, sizeof(RE::ImageSpaceData)) == 0) {
				++g_imageSpaceHeld;  // the game did not write its own values between our pass and the image space
			}
		}
		if (!g_mainDumpRequested) {
			return;
		}
		g_mainDumpRequested = false;
		auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
		auto* texture = renderer ? reinterpret_cast<ID3D11Texture2D*>(renderer->GetRuntimeData().renderTargets[RenderTarget::kMAIN].texture) : nullptr;
		if (texture) {
			D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(&desc);
			WriteDump(texture, desc.Width, desc.Height, "main", ++g_mainDumps);
		}
	}
}
