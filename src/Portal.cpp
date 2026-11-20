#include "PortalState.h"

namespace Portal::detail
{
	constexpr float         kMinEyeDistance = 10.0f;  // units from the door plane: closer, the frustum degenerates
	constexpr float         kNearBehindDoor = 2.0f;   // the near plane, units in front of the door plane
	constexpr std::uint32_t kAccumulatorType = 0x63;  // what the game passes to every BSShaderAccumulator constructor
	constexpr const char*   kQuadModel = "CellLoadResearch\\portalquad.nif";

	RE::NiPointer<RE::NiAVObject>               g_quad;
	std::vector<ID3D11ShaderResourceView*>      g_replacedViews;  // the placeholder texture's views we swapped out (kept)

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

	// Every pass: the texture may be (re)loaded after the model, so check the view each time. The quad shows the
	// pass's target, or with grading stage 2 the graded copy (T12).
	void SwapQuadTexture()
	{
		auto* texture = g_quadMaterial && g_quadMaterial->sourceTexture ? g_quadMaterial->sourceTexture->rendererTexture : nullptr;
		auto* wanted = QuadView();
		auto* ours = AsRex<REX::W32::ID3D11ShaderResourceView>(wanted);
		if (!texture || !wanted || texture->resourceView == ours) {
			g_textureSwapped = texture != nullptr;
			return;
		}
		auto* current = reinterpret_cast<ID3D11ShaderResourceView*>(texture->resourceView);
		if (current == g_target.srv || (current && current == g_target.gradedSrv)) {
			current->Release();  // the reference we added when we put it there; the target keeps its own
		} else if (current) {
			g_replacedViews.push_back(current);
		}
		wanted->AddRef();  // the game releases the view with the texture
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
}

namespace Portal
{
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
		GradeStart();
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
}
