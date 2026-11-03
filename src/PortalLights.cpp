#include "PortalState.h"

namespace Portal::detail
{
	using LightParams = RE::ShadowSceneNode::LIGHT_CREATE_PARAMS;

	constexpr float         kMirrorRadius = 2000.0f;  // units: the player's dynamic lights this close are carried through the door
	constexpr std::size_t   kSampledShapes = 4;  // interior shapes whose light lists go to the trace
	constexpr float         kRegatherDistance = 16.0f;  // units a carried light moves before its lit shapes are collected again
	constexpr std::uint32_t kLightInFrustum = 0;        // BSLight::frustrumCull after AE#108278 found it in the camera's frustum (0xFF: outside)
	constexpr std::uint32_t kLightOutOfFrustum = 0xFF;          // BSLight::frustrumCull: AE#107784 skips such lights

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

	std::string SampledShapesJson()
	{
		std::string json;
		for (const auto& shape : g_sampledShapes) {
			Trace::Args args;
			json += (json.empty() ? "" : ",") + ShapeState(args.Str("name", shape->name.c_str()), shape.get()).Json();
		}
		return "[" + json + "]";
	}
}

namespace Portal
{
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
}
