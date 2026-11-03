#include "PortalState.h"

namespace Portal::detail
{
	constexpr std::int32_t  kRebuildPasses = 0x7FFFFFFF;        // BSShaderProperty::lastRenderPassState: GetRenderPasses builds them again
	constexpr std::size_t   kTracedEffects = 8;
	constexpr std::size_t   kWatchedEffects = 64;           // effect shapes whose way through our culling the trace follows

	constexpr std::array<const char*, 3> kControllerModeNames{ "off", "selective update", "whole tree" };

	// What the CPU quads of NiParticles face in the pass ([Portal] iParticleFacing).
	enum class ParticleFacing : std::uint32_t
	{
		kGame = 0,    // whatever RendererShadowState holds when the packer runs
		kPortal = 1,  // the portal camera: across the door plane
		kView = 2,    // the player's view carried through the door: what they would see standing inside
		kTotal
	};

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
}

namespace Portal
{
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
}
