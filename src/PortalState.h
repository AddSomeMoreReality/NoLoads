#pragma once

// Internal to the portal (Portal*.cpp, T02: one Portal.cpp of 3 300 lines cut by subject, the code moved
// as it was). The state the files share, and the functions one file calls in another:
//   Portal.cpp        - Start / Stop / Tick, the quad, the door's rectangle on screen and the camera through it;
//   PortalRender.cpp  - the pass: render targets, lent engine slots, culling, submit, drain, faults;
//   PortalImage.cpp   - the interior's look (ambient, image space), TAA, texture dumps;
//   PortalLights.cpp  - copies of the interior's lights, the player's lights, lights for actors and effects;
//   PortalActors.cpp  - the interior's actors: shown, animated, seen, the world camera and distances;
//   PortalEffects.cpp - particles, controllers, particle budget, master particle systems, watched effects;
//   PortalAudio.cpp   - sounds and subtitles through the door;
//   PortalStatus.cpp  - the status JSON for the trace.
// Everything here is in Portal::detail; Portal's own functions see it through the using-directive below.

#include "Portal.h"

#include "Hud.h"
#include "Settings.h"
#include "Trace.h"

#include <d3d11.h>

namespace Portal::detail
{
	using RenderTarget = RE::RENDER_TARGET;

	constexpr std::size_t   kMaxAnimatedActors = 32;            // interior actors whose animation updates are made full
	constexpr std::uint32_t kUpdateControllers = 1u << 0;       // NiUpdateData::flags: NiNode::UpdateDownwardPass (AE#70301) runs the controllers (NG: kDirty)
	constexpr std::size_t   kAmbientOffset = 0xC8;  // BSShaderManager::State: directional ambient matrix and ambient specular ...
	constexpr std::size_t   kAmbientBytes = 0x44;   // ... the bytes AE#105643 writes (0x142033128..0x14203316C)

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
		ID3D11Texture2D*          depthCopy{ nullptr };  // what soft particles read: our depth, copied at the pass's first effect
		ID3D11ShaderResourceView* depthCopySrv{ nullptr };
		std::uint32_t             width{ 0 };
		std::uint32_t             height{ 0 };
	};

	// How the pass advances the interior's controllers (fire and smoke particles, candle flames, animated
	// textures): the game does it through the scene graph, which a cell without the player is not part
	// of. 0.8.12: no particles before the player's first visit, and the smoke stood still after it.
	// [Portal] iControllerUpdate.
	enum class ControllerMode : std::uint32_t
	{
		kOff = 0,
		kSelective = 1,  // NiAVObject::UpdateSelectedDownwardPass (vfunc 0x2D): only the nodes flagged kSelectiveUpdate
		kWholeTree = 2,  // NiAVObject::Update with kUpdateControllers: every node, its transform and controllers
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

	inline Target                                      g_target;
	inline RE::NiPointer<RE::NiCamera>                 g_camera;
	inline RE::NiPointer<RE::BSShaderAccumulator>      g_accumulator;
	inline RE::BSEffectShaderMaterial*                 g_quadMaterial{ nullptr };
	inline Portal::Door                                g_door;
	inline std::atomic<bool>                           g_active{ false };  // read by the watchdog
	inline std::string                                 g_fault;           // the pass broke a rule of the game's renderer: no more passes
	inline std::uint32_t                               g_tracedLeftovers{ 0 };   // geometry groups already reported
	inline std::uint32_t                               g_leftoverGroups{ 0 };    // ... and since the last status
	inline bool                                        g_dumpRequested{ false };

	// The interior's image space as the game blended it while the player stood inside (0.8.25, F2).
	struct InteriorLook
	{
		RE::FormID         cell{ 0 };
		RE::ImageSpaceData data{};
	};
	inline std::optional<InteriorLook> g_interiorLook;
	inline bool                        g_interiorImageSpace{ false };  // F2
	inline std::optional<RE::ImageSpaceData> g_gameImageSpace;          // what the manager held before we wrote the interior's
	inline std::uint64_t               g_imageSpaceSwaps{ 0 };
	inline std::vector<AddedLight>                     g_lights;
	inline std::vector<RE::NiPointer<RE::NiAVObject>>  g_actorRoots;  // the interior's actors' 3D, hidden by the game while the cell is unattached
	inline std::vector<RE::NiPointer<RE::Actor>>       g_interiorActors;  // the same actors, in the same order
	inline std::int64_t                                g_nextActorRefreshUs{ 0 };
	inline std::vector<RE::NiPointer<RE::BSGeometry>>  g_sampledShapes;
	inline bool                                        g_rebuildPasses{ false };  // the interior's render passes are built again on the next pass
	inline thread_local bool                           t_inPass{ false };         // SubmitAccumulator of our pass runs on this thread
	inline thread_local bool                           t_culling{ false };        // our culling runs on this thread; the game's own culls on its jobs at the same time
	inline bool                                        g_effectsLit{ false };     // effects get our lights (0.8.10-0.8.11: no fire at all)
	inline bool                                        g_rebuildEffects{ false };  // the interior's effect passes are built again on the next pass
	inline std::int64_t                                g_lastAnimateUs{ 0 };
	inline ControllerMode                              g_controllerMode{ ControllerMode::kSelective };

	// Read by AdjustAnimationUpdate on whatever thread updates the actors: the interior's actors, in the
	// order of g_interiorActors (compared, never dereferenced), and the updates the game made of them.
	inline std::atomic<bool>                                             g_forceAnimation{ false };
	inline std::atomic<bool>                                             g_fullProcessing{ false };
	inline std::array<std::atomic<const RE::Actor*>, kMaxAnimatedActors> g_animatedActors{};
	inline std::array<std::atomic<std::uint32_t>, kMaxAnimatedActors>    g_gameAnimationUpdates{};
	inline std::atomic<std::uint64_t>                                    g_forcedAnimationUpdates{ 0 };
	inline std::atomic<std::uint64_t>                                    g_fullProcessingGranted{ 0 };
	inline std::atomic<bool>                                             g_gameAnimation{ false };
	inline std::atomic<std::uint64_t>                                    g_gameAnimationAdded{ 0 };
	inline std::array<std::atomic<std::uint32_t>, kMaxAnimatedActors>    g_movementUpdates{};
	inline thread_local bool                                             t_ownAnimation{ false };  // AnimateActors is updating the actor

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
	inline std::atomic<const Space*>                                                           g_space{ nullptr };
	inline std::array<std::atomic<std::uint64_t>, std::to_underlying(Portal::Carried::kTotal)> g_pointsCarried{};  // IntoInterior answers yes, per Carried
	inline std::atomic<std::uint64_t>                                                          g_emissionsWithoutBudget{ 0 };

	// The interior's actors as the game's own logic sees them (0.8.24).
	inline std::atomic<bool>          g_actorsSeen{ false };
	inline std::atomic<RE::NiCamera*> g_virtualCamera{ nullptr };  // the portal's camera while [Portal] bVirtualCamera; never freed in a session
	inline std::atomic<bool>          g_soundThroughDoor{ false };
	inline std::atomic<bool>          g_distanceThroughDoor{ false };
	inline std::atomic<bool>          g_subtitlesThroughDoor{ false };
	inline std::uint64_t              g_subtitlesCarried{ 0 };  // main thread
	inline std::atomic<std::uint64_t> g_distancesCarried{ 0 };
	inline std::atomic<std::uint64_t> g_worldCameraCarried{ 0 };
	inline std::atomic<std::uint64_t> g_footFeedback{ 0 };
	inline std::atomic<std::uint64_t> g_notVisibleCleared{ 0 };
	inline std::atomic<std::uint64_t> g_soundsCarried{ 0 };
	inline RE::NiPoint3               g_lastSound;  // written on the audio thread, read for the status: a torn value does no harm

	// status since the last report
	inline std::uint64_t g_rendered{ 0 };
	inline std::uint64_t g_passFrame{ 0 };          // Trace::Frame() of the latest pass
	inline std::uint32_t g_passesInFrame{ 0 };
	inline std::uint32_t g_maxPassesPerFrame{ 0 };  // 0.8.18: the picture doubled after a visit inside; one pass a frame expected
	inline std::uint64_t g_skipped{ 0 };
	inline std::string   g_skipWhy;
	inline double        g_cpuMsSum{ 0.0 };
	inline double        g_cpuMsMax{ 0.0 };
	inline float         g_eyeDistance{ 0.0f };
	inline RE::NiFrustum g_frustum{};
	inline VisiblePart   g_visible{};
	inline std::uint32_t g_viewportWidth{ 0 };
	inline std::uint32_t g_viewportHeight{ 0 };
	inline std::size_t   g_actorsShown{ 0 };
	inline bool          g_textureSwapped{ false };
	inline std::uint64_t g_effectSetups{ 0 };      // BSEffectShader techniques set up during our passes
	inline std::uint64_t g_softEffectSetups{ 0 };  // ... of them with soft particles
	inline std::uint64_t g_depthCopies{ 0 };
	inline std::uint64_t g_effectLightsHeld{ 0 };  // effect shapes registered with our lights asleep
	inline std::uint64_t g_animated{ 0 };          // actor animation updates made by the portal
	inline std::uint64_t g_animatedByGame{ 0 };    // passes for which the game had updated the actor itself
	inline std::uint64_t g_poseChecks{ 0 };
	inline std::uint64_t g_poseChanged{ 0 };       // ... at which a bone of the actor stood elsewhere than at the last pass
	inline std::uint64_t g_positionsSynced{ 0 };
	inline std::uint64_t g_controllerUpdates{ 0 };
	inline double        g_controllerMsSum{ 0.0 };
	inline std::uint64_t g_actorLightsLent{ 0 };     // our lights put into the light lists of the actors' shapes for a pass
	inline std::uint64_t g_shapeRecollects{ 0 };     // times the lights' shapes were collected again: none had any

	// The particle packer in this pass: the cameras it could face, and what RendererShadowState held for it.
	inline PackCamera    g_packPortal;
	inline PackCamera    g_packMain;
	inline PackCamera    g_packView;
	inline std::uint64_t g_packCalls{ 0 };
	inline std::uint64_t g_packHeldPortal{ 0 };
	inline std::uint64_t g_packHeldMain{ 0 };
	inline std::uint64_t g_packHeldOther{ 0 };
	inline float         g_packOriginDrift{ 0.0f };  // units between the held origin and the portal camera

	template <class T>
	T* AsRex(void* a_object)
	{
		return static_cast<T*>(a_object);
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

	// An attached interior's actors are lit by ShadowSceneNode as moving objects (AE#106349 queues them),
	// from the lights awake for the world camera: our copies sleep then (0.8.17: NPCs almost black
	// through the portal; in 0.8.15, unattached, our copies collected their shapes like any other). For
	// the pass, our lights that reach an actor's lit shape go into its light list; out again after it.
	struct LentLight
	{
		RE::BSShaderPropertyLightData* data;
		RE::BSLight*                   light;
	};

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

	inline std::vector<WatchedEffect>                        g_watchedEffects;
	inline std::unordered_map<const void*, std::size_t>       g_watchedShapes;   // shape -> index
	inline std::unordered_multimap<const void*, std::size_t>  g_watchedParents;  // parent -> indices

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

	// The actors' 3D, made visible for the culling only: AppCulled nodes near the root shown, fade raised.
	struct ShownActors
	{
		std::vector<RE::NiAVObject*>                    unculled;
		std::vector<std::pair<RE::NiAVObject*, float>>  fadeAmounts;
		std::vector<std::pair<RE::BSFadeNode*, float>>  currentFades;
	};

	// Portal.cpp
	const RE::BSEffectShaderMaterial* QuadMaterialNow();
	void SwapQuadTexture();
	float QuadPreviousOffset();
	bool SeeDoor(const RE::NiCamera* a_main, VisiblePart& a_part);
	bool AimCamera(RE::NiCamera* a_main, const VisiblePart& a_part, std::string& a_why);
	void MapQuad(const VisiblePart& a_part, std::uint32_t a_width, std::uint32_t a_height);

	// PortalRender.cpp
	ID3D11Device* Device();
	ID3D11DeviceContext* Context();
	RE::ShadowSceneNode* WorldScene();
	D3D11_TEXTURE2D_DESC MainTargetDesc();
	bool CreateTarget(std::string& a_why);

	// PortalImage.cpp
	void UseInteriorLook(SavedLook& a_saved);
	void RestoreLook(const SavedLook& a_saved);
	void WriteDump(ID3D11Texture2D* a_texture, std::uint32_t a_width, std::uint32_t a_height, std::string_view a_name, std::uint32_t a_number);
	void LookTick();

	// PortalLights.cpp
	std::string LightJson(const AddedLight& a_added);
	void AddInteriorLights(std::size_t& a_alreadyInScene);
	void CollectLitShapes(RE::ShadowSceneNode* a_scene, RE::BSLight* a_light);
	std::vector<WokenLight> WakeLights();
	void SleepLights(const std::vector<WokenLight>& a_woken);
	void ForceNewRenderPasses();
	void MirrorPlayerLights();
	std::string NearPlayerLightsJson();
	std::string OriginalLightsJson();
	std::string LightSourceRefsJson();
	std::vector<LentLight> LendActorLights();
	void ReturnActorLights(const std::vector<LentLight>& a_lent);
	void RemoveLights();
	void SampleShapes();
	std::string SampledShapesJson();

	// PortalActors.cpp
	void RefreshActors(std::int64_t a_now);
	void ClearNotVisible(RE::NiAVObject* a_root);
	void ForgetActors();
	void AnimateActors();
	std::string PreprocessedJson();
	std::string ActorsJson();
	void ShowActors(ShownActors& a_shown);
	void HideActors(const ShownActors& a_shown);

	// PortalEffects.cpp
	RE::BSParticleSystemManager* ParticleManager();
	float& EmissionScale();
	void ForceNewEffectPasses();
	Trace::Args& ShapeState(Trace::Args& a_args, RE::BSGeometry* a_shape);
	std::string EffectsJson();
	void WatchEffects();
	std::string WatchedEffectsJson();
	std::string ControllersJson();
	void UpdateControllers();
	void AimParticles(const RE::NiCamera* a_main);
}

namespace Portal
{
	using namespace detail;
}
