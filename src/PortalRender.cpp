#include "PortalState.h"

namespace Portal::detail
{
	constexpr std::uint32_t kDepthTestWrite = 3;      // RendererShadowState::depthStencilDepthMode, as BSCubeMapCamera sets it
	constexpr std::uint32_t kAllDirty = 0xFFFFFFFF;   // RendererShadowState::stateUpdateFlags: apply everything again
	constexpr std::uint32_t kAllModified = 0xFFFF;    // ... and every texture, sampler and UAV slot
	constexpr std::uint32_t kMinViewport = 32;        // pixels: the smallest portal picture drawn
	constexpr std::uint32_t kEffectTechniqueBase = 0x4000002C;  // BSEffectShader::SetupTechnique (AE#107524) tests bits of technique - this
	constexpr std::uint32_t kSoftEffectBit = 1u << 18;          // ... soft particles: it binds the main depth buffer's shader view
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
		kComposite,
		kDrain,
		kRestore,
		kGrade,
		kTotal
	};

	constexpr std::array<std::string_view, std::to_underlying(PassPhase::kTotal)> kPassPhaseNames{
		"idle", "lend slots", "animate actors", "update controllers", "cull interior", "cull master particles", "set targets", "submit", "composite", "drain", "restore", "grade"
	};

	std::string                                 g_targetFailure;  // creation failed once: do not retry
	std::atomic<PassPhase>                      g_passPhase{ PassPhase::kIdle };
	std::uint32_t                               g_dumps{ 0 };
	bool                                        g_depthCopied{ false };    // this pass's copy of the depth buffer is taken
	bool                                        g_foreground{ true };      // the game window was in the foreground at the last frame

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
		std::string gradeWhy;
		if (!CreateGradeTargets(gradeWhy)) {
			logs::warn("portal: no grading ({})", gradeWhy);  // the portal works without it, as at stage 0
		}
		Trace::Write("portal.init", Trace::Args{}
										.Int("width", g_target.width)
										.Int("height", g_target.height)
										.Int("colorSlot", settings.portalColorSlot)
										.Int("depthSlot", settings.portalDepthSlot)
										.Raw("gameMainTarget", Trace::Args{}.Int("width", main.Width).Int("height", main.Height).Int("dxgiFormat", main.Format).Json()),
			nullptr, true);
		return true;
	}

	// ------------------------------------------------------------------
	// The pass
	// ------------------------------------------------------------------
	// The engine slots the pass draws into, lent to our textures for its duration. The main depth slot
	// too: BSEffectShader's RestoreTechnique (AE#107522) and other passes set the depth target back to
	// kMAIN (AE#77457 returns 0), and soft particles read its shader view. 0.8.7 drew the hearth's fire
	// through the interior's walls; 0.8.9, which lent only that view, drew it only with the eye a meter
	// or more from the door: tested against the street's depth, where the door stands at the eye's
	// distance, while the portal camera's near plane is 2 units in front of the door. 0.8.10 lent the
	// whole slot, and there was no fire at all: its shader view must be a copy of our depth, taken at the
	// pass's first effect (BeforeEffectTechnique). [Portal] iDepthMode chose among the three until T02.
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
		lendDepth(RE::RENDER_TARGET_DEPTHSTENCIL::kMAIN, a_saved.mainDepth, a_saved.mainDepthProperties);
		// a shader view of the buffer the effects test against is a hazard if it is ever bound writable
		mainDepth.depthSRV = AsRex<REX::W32::ID3D11ShaderResourceView>(g_target.depthCopySrv);
		depths[settings.portalDepthSlot].depthSRV = mainDepth.depthSRV;
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

	// SEH only, no objects with destructors: the shapes of a geometry group's pass list (BSRenderPass::passGroupNext).
	std::uint32_t ReadGroupShapes(const RE::BSBatchRenderer* a_batches, std::uint32_t a_group, const char** a_names, std::uint32_t a_max)
	{
		__try {
			const auto*   group = a_batches->geometryGroups[a_group];
			std::uint32_t count = 0;
			for (const auto* pass = group ? group->passList.head : nullptr; pass && count < kMaxInspectedTechniques; pass = pass->passGroupNext) {
				if (count < a_max) {
					a_names[count] = pass->geometry ? pass->geometry->name.c_str() : nullptr;
				}
				++count;
			}
			return count;
		} __except (1 /* EXCEPTION_EXECUTE_HANDLER */) {
			return 0;
		}
	}

	// T13: what our culling put into the refraction normals group (heat haze over the hearth, run 0.8.26: never drawn).
	void NoteRefractionGroup(RE::BSShaderAccumulator* a_accumulator)
	{
		constexpr std::uint32_t kNames = 8;
		constexpr std::size_t   kKept = 16;
		const char*             names[kNames]{};
		const auto              count = ReadGroupShapes(a_accumulator->GetRuntimeData().batchRenderer, kRefractionNormalsGroup, names, kNames);
		g_refractionPasses += count;
		for (std::uint32_t i = 0; i < (std::min)(count, kNames); ++i) {
			const std::string name = names[i] ? names[i] : "(no geometry)";
			if (g_refractionShapes.size() < kKept && std::ranges::find(g_refractionShapes, name) == g_refractionShapes.end()) {
				g_refractionShapes.push_back(name);
			}
		}
	}

	// SubmitAccumulator (AE#106436) drains the technique buckets but never draws the refraction normals
	// group: only the frame renderer does, so the game clears it after its own off-screen pass (AE#52727).
	void DrainPasses(RE::BSShaderAccumulator* a_accumulator)
	{
		using ClearGroup = void (*)(RE::BSShaderAccumulator*, std::int32_t);
		static REL::Relocation<ClearGroup> clearGroup{ RELOCATION_ID(99955, 106600) };
		NoteRefractionGroup(a_accumulator);
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

	// One pass, main thread, at the scene list fence.
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
			// the game's jobs are registering the same render passes at that moment. The pass runs at the fence.
			if (auto* manager = settings.portalMasterParticles ? ParticleManager() : nullptr) {
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
		g_composited = false;
		Mark(PassPhase::kSubmit);
		RE::BSGraphics::Renderer::SubmitAccumulator(g_camera.get(), accumulator, 0);
		t_inPass = false;
		// T12, layer 1: a pass without effects composites here; otherwise it did at the first effect.
		if (g_gradeStages >= 1 && !g_composited) {
			if (!g_depthCopied) {
				g_depthCopied = true;
				if (auto* context = Context()) {
					context->CopyResource(g_target.depthCopy, g_target.depth);
					++g_depthCopies;
				}
			}
			Mark(PassPhase::kComposite);
			Composite(width, height);
		}
		Mark(PassPhase::kDrain);
		DrainPasses(accumulator);
		Mark(PassPhase::kRestore);
		ReturnActorLights(lentLights);
		SleepLights(woken);
		HideActors(shown);
		if (interiorLook) {
			RestoreLook(look);
		}
		// T12, layer 2: after RestoreLook, so the street's fog is what the quad gets.
		if (g_gradeStages >= 2) {
			Mark(PassPhase::kGrade);
			Grade(width, height);
		}

		if (g_dumpRequested) {
			g_dumpRequested = false;
			++g_dumps;
			WriteDump(g_target.color, g_viewportWidth, g_viewportHeight, "portal", g_dumps);
			if (g_gradeStages >= 2 && g_target.gradedSrv && QuadView() == g_target.gradedSrv) {
				WriteDump(g_target.graded, g_viewportWidth, g_viewportHeight, "graded", g_dumps);
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
}

namespace Portal
{
	void OnSceneListFence()
	{
		if (Trace::IsMainThread()) {
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

	std::uint32_t BeforeEffectTechnique(std::uint32_t a_technique)
	{
		if (!t_inPass) {
			return a_technique;
		}
		++g_effectSetups;
		if (((a_technique - kEffectTechniqueBase) & kSoftEffectBit) != 0) {
			++g_softEffectSetups;
		}
		if (!g_depthCopied) {
			g_depthCopied = true;  // the opaque shapes are drawn: effects come after them
			if (auto* context = Context()) {
				context->CopyResource(g_target.depthCopy, g_target.depth);
				++g_depthCopies;
			}
			if (g_gradeStages >= 1) {
				Composite(g_viewportWidth, g_viewportHeight);  // T12: where the game composites its frame
			}
		}
		return a_technique;
	}
}
