#include "PortalState.h"

namespace Portal
{
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
							  .Int("gameAnimationAdded", static_cast<std::int64_t>(g_gameAnimationAdded.exchange(0, std::memory_order_relaxed)))
							  .Int("pointsCarried", static_cast<std::int64_t>(g_pointsCarried[std::to_underlying(Carried::kControllerLod)].exchange(0, std::memory_order_relaxed)))
							  .Int("fadesCarried", static_cast<std::int64_t>(g_pointsCarried[std::to_underlying(Carried::kFade)].exchange(0, std::memory_order_relaxed)))
							  .Int("maxPassesPerFrame", static_cast<std::int64_t>(g_maxPassesPerFrame))
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
		g_actorLightsLent = 0;
		return json;
	}
}
