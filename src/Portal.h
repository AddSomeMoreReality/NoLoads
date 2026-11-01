#pragma once

// Experiment (requested 24.10.2026): show the kept interior through its exterior door, drawn every
// frame by a second camera into a texture, instead of hanging its 3D into the exterior scene
// (0.8.5: the tavern's interior is larger than its building and stuck out of the walls).
// - A quad (meshes\CellLoadResearch\portalquad.nif, unlit BSEffectShaderProperty) takes the door's
//   place in the door's cell node; its texture's shader resource view is swapped for the portal's
//   render target, so the game draws it like any reference (depth, fog, tone mapping).
// - Before Main::RenderWorld (AE#107142) the interior's own 3D, in its own coordinates and attached
//   nowhere, is culled (BSCullingProcess::CullingContext) and drawn (Renderer::SubmitAccumulator)
//   from the player's eye carried through the door: the camera looks across the door plane, its
//   off-axis frustum passes through the door rectangle and its near plane lies on it. The quad's UVs
//   span that frustum, so the picture is right from any viewpoint.
// The game's render state is saved before the pass and put back after it; the render target and
// depth buffer are ours, lent to two engine slots only while the pass runs. Only the part of the door
// on screen is drawn, at its size on screen; nothing when the door is off screen.
// Lights (0.8.8): copies of the interior's own lights and of the player's dynamic lights near the door
// (carried through it) are added to ShadowSceneNode[0], restricted to the interior's 3D. The
// interior's actors, hidden by the game while their cell has no player, are shown for the pass.
// 0.8.16: InteriorSim may attach the interior like a grid cell; its 3D then hangs under a node of its
// own, in no scene (0.8.16 hid it under TES::objRoot, and the lights collected no shape).
namespace Portal
{
	struct Door
	{
		RE::TESObjectCELL*            cell{ nullptr };  // the interior (a permanent form)
		RE::NiPointer<RE::NiAVObject> scene;            // the interior's 3D, in its own coordinates
		RE::NiTransform               toExterior;  // interior coordinates -> exterior
		RE::NiPoint3                  center;      // the door rectangle, exterior coordinates
		RE::NiPoint3                  right;       // unit: the right of a player facing the door from the street
		RE::NiPoint3                  up;          // unit
		RE::NiPoint3                  inward;      // unit: from the street into the building, across the door plane
		float                         width{ 0.0f };
		float                         height{ 0.0f };
		RE::NiNode*                   parent{ nullptr };  // where the quad hangs: the exterior door's node
		bool                          attached{ false };  // the interior is attached like a grid cell (InteriorSim), its 3D under a node of no scene
	};

	// AE only: the Main::RenderWorld detour. Called from Hooks::InstallDetours inside its transaction.
	[[nodiscard]] long AttachRenderWorldDetour();

	// [Portal] bPassAtSceneListFence: the pass moves from right before Main::RenderWorld to the point
	// right after RenderPlayerView finishes its scene list jobs (AE#36559 + 0x17E, AE#69353). No job
	// culls or registers render passes there, and the world's accumulation has not begun.
	void SetPassAtSceneListFence(bool a_atFence);
	void OnSceneListFence();

	// Main thread. Prepare checks everything a start needs and creates the shared resources, so the
	// caller can find out before it attaches the cell. Start does it again itself.
	[[nodiscard]] bool Prepare(std::string& a_why);
	[[nodiscard]] bool Start(const Door& a_door, std::string& a_why);
	void               Stop();
	[[nodiscard]] RE::NiAVObject* Quad();

	// Any thread.
	[[nodiscard]] bool Active();
	[[nodiscard]] std::string_view PassPhaseName();  // the step of the pass running now

	// Main thread: why passes stopped for this session, or empty. Set when the accumulator is not left
	// empty; see PassInventory in Portal.cpp.
	[[nodiscard]] const std::string& Fault();

	// Main thread: a line for the HUD and a JSON object for the trace; counters restart after it.
	[[nodiscard]] std::string StatusText();
	[[nodiscard]] std::string StatusJson();

	// Main thread (hotkey): the next pass writes the portal texture to <log directory>\portal-<n>.dds.
	void RequestDump();

	// Main thread (hotkey, 0.8.20): the game's temporal antialiasing on or off, in memory only. 0.8.18-0.8.19:
	// the portal's picture doubled while its texture was clean; is it TAA mixing frames?
	void ToggleTemporalAA();

	// Character::ModifyAnimationUpdateData hook (vfunc 0x79, AE#38054), after the original, on whatever
	// thread updates the actor: an actor of the portal's interior gets a full update, as if seen.
	void AdjustAnimationUpdate(const RE::Actor* a_actor, RE::BSAnimationUpdateData& a_data);

	// Detour of AE#37277, which the high-process update (AE#41369, on job threads) asks whether an actor
	// gets only the reduced update (AE#39114) instead of the full one (AE#37347: AI, movement, animation):
	// yes for an actor further from the camera than its fade distance (AE#105490). True: the actor is one
	// of the portal's interior actors and gets the full update ([Portal] bFullActorProcessing).
	[[nodiscard]] bool GrantFullProcessing(const RE::Actor* a_actor);

	// Detour of AE#41466, after the original: it picks the high-process actors whose animation the game
	// updates this frame (AE#41370 gives each a job, AE#41453), only those of attached cells. The
	// portal's interior actors are added to a_list (AE#41496 pushes) ([Portal] bGameAnimatesActors).
	void AddAnimatedActors(const RE::BSTArray<RE::ActorHandle>& a_handles, void* a_list);

	// Detour of AE#37350 (the actor's movement update, a job of AE#41306 per high actor), any thread:
	// counts the calls for the portal's interior actors.
	void NoteMovementUpdate(const RE::Actor* a_actor);

	// What a point carried through the door is for: counted apart in the status.
	enum class Carried
	{
		kControllerLod,  // AE#78274: the character controller's LOD distance (0.8.17)
		kFade,           // AE#105486 from AE#19384: the fade and camera distance of an animated reference (0.8.19)
		kTotal
	};

	// Any thread: the portal as a "transformation zone" (0.8.17, user request of 27.10.2026). When
	// a_object is the 3D of a reference in the portal's interior, a_point (exterior coordinates: the
	// player's camera) becomes the same point carried through the door into the interior's coordinates,
	// so a distance the game measures from it is the distance through the door. False otherwise.
	bool IntoInterior(const RE::NiAVObject* a_object, RE::NiPoint3& a_point, Carried a_what);

	// Any thread: the controller's flags in hex and the names of the bits that bear on movement (0.8.19:
	// the portal's NPCs could not climb stairs or a step, the controller rose and the actor stayed).
	[[nodiscard]] std::string ControllerFlagsText(const RE::bhkCharacterController& a_controller);

	// Any thread: a_object hangs under the 3D of the portal's interior.
	[[nodiscard]] bool InInterior(const RE::NiAVObject* a_object);

	// Detour of AE#74651 (NiPSysEmitter vfunc 0x2A, all emitters), any thread, before the original: the
	// game scales every emission by its particle budget (AE#388630). Outside in Whiterun it stood at 0.46,
	// and the hearth behind the door burned with half its particles. An emitter in the portal's interior
	// gets the difference added ([Portal] bIgnoreParticleBudget).
	void BeforeEmission(RE::NiPSysModifier* a_emitter, std::uint16_t a_count);

	// Main thread: the master particle systems (candle flames) and how many of their emitters hang under
	// a_scene. Written by the portal's status and by the probe while the player is inside.
	[[nodiscard]] std::string MasterParticlesJson(const RE::NiAVObject* a_scene);

	// Main thread: the shapes of the burning models under a_scene (the hearth, fireplaces, a few candles),
	// with their particles' size, place and alpha. Written by the portal's status and by the probe while
	// the player is inside, to compare the same flames (0.8.20: through the portal they look small).
	[[nodiscard]] std::string FireSampleJson(RE::NiAVObject* a_scene);

	// NiAVObject::OnVisible hooks, any thread: counts our culling's visits of the watched effect shapes.
	void NoteVisible(const RE::NiAVObject* a_object);

	// BSEffectShaderProperty::GetRenderPasses hook, around the original: while our culling registers an
	// effect shape, our lights are asleep unless effects get them ([Portal] bLightEffects). True: put
	// them back after the call.
	[[nodiscard]] bool BeforeEffectPasses(const RE::BSGeometry* a_geometry);
	void               AfterEffectPasses(bool a_held, const RE::BSGeometry* a_geometry, bool a_gotPasses);

	// BSEffectShader::SetupTechnique hook, before the original: counts the effects our pass draws and,
	// in depth mode 2, copies the depth buffer at the first of them. Returns the technique to set up:
	// without the soft particle bit while soft particles are off.
	[[nodiscard]] std::uint32_t BeforeEffectTechnique(std::uint32_t a_technique);
}
