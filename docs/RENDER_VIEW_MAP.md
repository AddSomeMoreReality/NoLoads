# Frame rendering map and view states (T44)

Base — `SkyrimSE.exe` 1.6.1170, Ghidra (`docs/GHIDRA.md`). Conclusions, `AE#id+offset`, and short instruction quotes only. Offsets are from `asm`.

Labels:
- **code** — verified against machine code;
- **game** — confirmed by a run;
- **hyp.** — hypothesis.

All functions below run on the main thread, except `BSJobs` jobs (marked "jobs").

## 1. Frame chain

`Main::Update` (`AE#36564`) `+0xA97` → the renderer frame `AE#36555` (**code**):

| Where | What | Which view it takes |
|---|---|---|
| `AE#36555+0x2D/+0x39/+0x47` | `AE#77515` (State, frame start), `Renderer::Lock`, `Begin` | — |
| `AE#36555+0xB4` | Decals: `AE#36562`, own camera and `BGSDecalManager` accumulator | own camera |
| `AE#36555+0x1CD` | `AE#82085` (UI): writes `AE#411475 = AE#77457()` | — |
| `AE#36555+0x4C8` | `AE#36558` → world (in a menu `+0x284` with parameter `0xE`, frozen background) | world camera |
| `AE#36555+0x371` | `AE#82084` (UI, menu): writes `AE#411475` again | — |
| `AE#36555+0x601/+0x60D` | `Renderer::End`, `Unlock` | — |

`AE#36558` (wrapper) does the following:
- `+0x66`: `AE#415299` = world camera;
- `+0x9A`: clear color;
- `+0xA6…+0xBE`: `SSN[0].cameraPos` (`+0x2F4`) = camera position;
- `+0xD1…`: the eye of the main accumulator `AE#415009 + 0x16C`;
- `+0x11D`: the call to `RenderPlayerView` (`AE#36559`, `Main::Draw` in the base).

The world camera is `[AE#403513] + 0x128` (`AE#36609`: `mov rax,[AE#403513]`, `mov rax,[rax+0x128]`). This is the same object as `DrawWorld::singleton` (`AE#415027`) `.worldSceneGraph->camera`.

`RenderPlayerView` `AE#36559` (**code**, from `asm`):

| Offset | Step | Thread |
|---|---|---|
| `+0xA7/+0xC1` | Puts the camera (`+0x18`) and the frustum (`SetFrustum(camera+0x150)`) on the culling processes of jobs `AE#415022[AE#415016]` | M |
| `+0xD3` | `AE#36561`, view preparation:<br>• reset of the camera `port` (`+0x37`);<br>• FOV (`AE#107127`);<br>• `cachedPlayerBound`;<br>• `HighActorCuller` by camera (`AE#40336`, `+0x11B`) — sets `kNotVisible` on actors;<br>• sky and weather nodes — to the camera position (`+0x147…+0x1D1`);<br>• filling `DrawWorld`;<br>• `AE#36661` (actors around) | M |
| `+0x14F…+0x16A` | Jobs `DrawWorld_BuildSceneLists` (`AE#36643`) in `gJobList_SceneListAccumCulling` (`AE#414944`): room and portal visibility (`BSPortalGraphEntry::ClearVisibility`, `SSN::BuildSharedCompoundFrustum`), shared culling lists | jobs |
| `+0x172` | `AE#36560`, water. **The water reflection cubemap is drawn here, in parallel with the jobs:**<br>`SSN[0].disableLightUpdate = 1` (`+0xD4`) → `AE#32174` → `TESWaterReflections::UpdateActor` (`AE#32160`) → `RenderCubemap` (`AE#107562` → `AE#107564`), 2 faces per frame | M |
| **`+0x17E`** | `JobList::Finish` (`AE#69353`) — **our pass goes here** (`write_call`) | M |
| `+0x194` | `AE#106337` → `AE#106335` for 4 `SSN`: "in frustum" light flags by process `AE#415022[0]` | M |
| `+0x1C4` | `NiCamera::CalculateAndDrawShadowCasterLights` (`AE#107132`): shadow maps, cascades by the world camera | M |
| `+0x1F2` | First-person culling (`AE#36563`, process `AE#414946`) | M |
| `+0x26A…+0x296` | First person: `AE#107123` (camera `AE#415007`, changes `SSN[0].cameraPos`/`lightingOffset`), `AE#77506` | M |
| `+0x2B4/+0x2C6/+0x2D2/+0x2E7` | world camera cache `AE#77506` (flags 0 and 1), history shift `AE#77505`, `SetCameraData(world, 1)` | M |
| **`+0x2EC`** | `AE#107133`: **registration** — jobs `BSAccumProcess::DoSceneListAccumRegisterJob` (`AE#106927` → `AE#76558` → `AE#106567`) into the main accumulator `AE#415009` and the prepass accumulator `AE#415008`; list `gJobList_SceneListAccumRegister` | jobs |
| `+0x31D/+0x335` | Volumetric light (`AE#107023`) | M |
| `+0x395` | Depth prepass `AE#107139`: `StartAccumulating(world, AE#415008, 0x21)` | M |
| `+0x39C` | `AE#107140` → `AE#107141`: **shadow mask in screen coordinates.** `SetRenderTarget(0, 0x12 = kSHADOW_MASK)` (`AE#107141+0x4E`) | M |
| `+0x4D2…+0x514` | Jobs `Clear Lists` (`AE#36655`): remove references from past lists `AE#415017`/`AE#415020` | jobs |
| `+0x520…+0x645` | Sky colors into ImageSpace shaders | M |
| `+0x6CE…+0x74D` | World MRT:<br>• RT0 = `kMAIN` (1);<br>• RT1 = `kMOTION_VECTOR` (7);<br>• RT2 = `AE#107177()` (`kNORMAL_TAAMASK_SSRMASK` or its pair) | M |
| `+0x800…+0x828` | `AE#411319–411321` ← `posAdjust` (`AE#388909–388911`) | M |
| `+0x841` | `Main::RenderWorld` (`AE#107142`), details — after the table | M |
| `+0x84D` | `Finish(Clear Lists)` | M |
| `+0x88C/+0x89D` | `AE#106600`: clearing groups 6 and 5 of the main accumulator (or `AE#107145`) | M |
| `+0x8CE…+0x9C2` | First person: culling `AE#107128`, drawing `AE#107129` (`+0x946`): `SubmitAccumulator(AE#415007, AE#415011, …\|0x43)`, depth 7 | M |
| `+0xA54` | `AE#107148`, world ImageSpace: `AE#414580` = world camera, `AE#105674` — the `ImageSpaceManager` effect chain. When `AE#414573 && gEnableEOFImageSpace` | M |
| `+0xA73/+0xA78` | Screen splashes (`AE#107147`), end of frame (`AE#107143`) | M |

What `Main::RenderWorld` (`AE#107142`) does:
- `+0x55`: `AE#411475 = 7`;
- `+0x74`: `AE#415299`;
- `+0x11F`: `Renderer::StartAccumulating(world, AE#415009)`;
- `+0x1F2/+0x2BA`: SSAO, SSR and composite (`AE#107245`);
- `+0x2DF`: `FinishAccumulatingPostResolveDepth`;
- `+0x410`: screen splashes (`AE#415013`);
- sun occlusion.

Job events `SetJobTaskEvent` (`AE#69514`):
- 7 — at `+0x2A2` (third-person view), `+0x9CC`, `+0xA1E`;
- 6 — at `+0x8A7`.

Which tasks they wake was not determined (section 9).

## 2. The pass window: what is ready at `+0x17E` and what is not

- **Ready:**
  - world camera position and FOV, `port`;
  - `kNotVisible` of actors by the main frustum;
  - room and portal visibility (portal graph, by the main camera);
  - the faces of this frame's water cubemap.
- **Not done yet:**
  - "in frustum" light flags — still last frame's (`+0x194`);
  - shadow maps and mask — last frame's (`+0x1C4`, `+0x39C`);
  - registration of world passes (`+0x2EC`);
  - this frame's TAA history and world camera cache (`+0x2B4…+0x2E7`).
- **Why the window is safe (code):** jobs `+0x14F…+0x17E` only build lists. `BSRenderPass` passes for the world are registered later, at `+0x2EC`, and drawn in `RenderWorld`.
  - A pass that registered, drew, and drained its passes before `+0x2EC` does not intersect the game's lists.
  - The game itself does the same: the water cubemap is drawn at `+0x172`.
- **Correction to FINDINGS (run 0.8.21):** `0x1406449C4`/`0x140644CFD` = `+0x514`/`+0x84D` — these are the `Clear Lists` jobs (`AE#36655`), not culling.
  - **Hyp.:** the 0.8.21 hang was caused not by a parallel thread but by re-registering shared particles between `+0x2EC` (the game registered them) and `RenderWorld` (it had not drawn them yet).
  - Then `GetRenderPasses` rebuilt the pass list that the main accumulator already held.

## 3. View state: where it is written, where it is read, the class

Classes: **view** — its own per camera; **frame** — one per frame, computed for the main view; **world** — one per `ShadowSceneNode` or world. "Pass" — what `Portal::Render` does now (`src/PortalRender.cpp`).

| State | Writes | Reads | Class | Pass currently |
|---|---|---|---|---|
| World camera `[AE#403513]+0x128` | `PlayerCamera`; `port` — `AE#36561+0x37`, `AE#107148` saves and restores | `AE#36609` (15 places), `RenderPlayerView`, `RenderWorld` via `DrawWorld+0x28` | view (main) | does not touch; own camera `g_camera` (**code**) |
| `AE#415299` (camera copy) | `AE#36558+0x66`, `RenderWorld+0x74` | `AE#107753+0x7C` (projecting a point onto the screen) | view | does not touch |
| `AE#414580` (camera of the current submission) | `SubmitAccumulator+0x49`, `StartAccumulating+0x49`, `AE#107140+0x75`, `AE#107148+0x5D`, `SSN::OnVisible` (`AE#106322`) | near/far of soft effects `AE#107524+0x20C`; water `AE#107362`; `BSUtilityShader`; SSAO `AE#107245`; DoF; volumetric light (27 places) | view | sets `SubmitAccumulator`, does **not restore** |
| Culling processes of jobs `AE#415022[]` | `RenderPlayerView+0xA7/+0xC1` | jobs `+0x14F`, `+0x2EC`; light `+0x194` | view (main) | does not touch |
| `BSCullingProcess::Process` processes `AE#414842`, `AE#414845` | static, configured on each call (`AE#106921`, selected by `ctx+0x5C`) | all `Process` calls | **shared**, no reentry | one call at a time (**code**) |
| Camera data cache `State+0xA8` (records `0x290`, key — camera and jitter flag) | `AE#77506` (add or update), `AE#77505` (history shift, once per frame `+0x2D2`) | `SetCameraData` (`AE#77503`): on a miss builds into a temporary `AE#389043` | view | the clone is not in the cache: matrices are built from scratch, there are no "past" ones |
| TAA jitter | `BuildCameraStateData` (`AE#77520+0x1CE`): only when `taaEnabled` and `flags & 1` | `SetCameraData(cam, flags)` from `SubmitAccumulator` (R8 = flags) | view | flags = 0, no jitter (**code**) |
| Camera in `RendererShadowState` (`AE#388819`): vectors `AE#388915–388920`, `posAdjust` `AE#388909–388911` | `AE#77521` from `SetCameraData` | particle packing `AE#77463` (`+0xB6`, `+0x218`); LOD range in `BSLightingShader::SetupTechnique` | view (current) | copies and restores the whole state; replaces the particle vectors in `BeforeParticlePack` |
| Accumulator eye `+0x16C` | `AE#36558+0xD1` (main), `AE#107564+0x181` (cubemap), `AE#52727+0x162` (menu) | accumulator sorting and passes | view (per accumulator) | sets its own `eyePosition` |
| `SSN[0].cameraPos` `+0x2F4`, `lightingOffset` `+0x2E8` | `AE#36558+0xA6`; first person `AE#107123+0x1B5`, `AE#107124`, `AE#107139` | few places (`AE#39429`, `AE#91863`, …) | view (main) | does not touch |
| Light "in frustum" `BSLight+0x5C` | `AE#106335` via `AE#106337` (`+0x194`) | limit `AE#107784+0x1C3` | view | last frame; `WakeLights` |
| Shape light list `AE#107784` | sun `SSN+0x200`, sun with shadow `+0x210` or clouds `+0x208`, then shadow `SSN`, then `lightData->lights` with `+0x5C != 0xFF` | `GetRenderPasses` | world (`SSN`) + view (flag) | `LendActorLights`, `SSN[0]` street light |
| Shadow maps | `AE#107132` (`+0x1C4`) by the world camera | — | view (main) | last frame |
| **Shadow mask `kSHADOW_MASK` (0x12)** | `AE#107141+0x59`, main view, screen coordinates | `BSLightingShader::SetupTechnique` `AE#107297+0x9C0` (`renderTargets[0x12].SRV`, bit 14 of the technique and sun shadow or shadow sources); scale = 1/`kMAIN` size (`+0xA49`) | view (main) | **does not replace** (section 7) |
| Fog | `SSN[State.sceneGraph]->fogProperty` (`AE#105637`: `State+0xC0`, `SSN+0x220`) | light `AE#107320+0x17`, effect `AE#107524+0xA1`, SSAO composite `AE#107245+0xD26`, grass, water, distant trees | world, the view is chosen by the `sceneGraph` index | `SSN[0]` fog values are patched in place (T12) |
| Ambient/background light of `State` (directional ambient) | frame and cell (T42) | shaders | world | `UseInteriorLook` |
| Soft effects depth `AE#411475` (`depthStencils` index) | `RenderWorld+0x55` (= 7, `kPOST_ZPREPASS_COPY`), UI `AE#82084+0xA0`, `AE#82085+0xB4` (= `AE#77457()`) | `AE#107524+0x295`, `AE#107522+0x16`, SSAO | view | at `+0x17E` it is 0 there from last frame's UI; that is why borrowing `kMAIN` works (**hyp.**, fragile) |
| TAA, ImageSpace, adaptation | `AE#107148` → `AE#105674`; `taaEnabled` (`ISM+0x1F0 → +0x18`) | — | view (main) | the quad goes through the street's ImageSpace (T12) |
| Particle and billboard orientation | billboards — when culling with `cameraRelatedUpdates` (`AE#71063`); `NiParticles` — `RendererShadowState` vectors | `AE#77463` | view | `cameraRelatedUpdates`, `g_packView` |
| LOD and fading | `AE#19384+0xE4` → `AE#105486` (camera `AE#36609`), controller `AE#37348+0x1F3` | `BSPSysLODModifier` `AE#108480` (`+0x144`) | frame (main camera) | hooks carry the camera across the door |
| `loadedRange` − `posAdjust` (land LOD) | `State.loadedRange`, `posAdjust` | `AE#107297` (LOD techniques) | world + view | not relevant for an interior |
| Room and portal visibility | `AE#36643` (jobs) | world culling | view (main) | `kIgnoreMultiBounds` |
| `kNotVisible` of actors | `HighActorCuller` `AE#40336` (`AE#36561+0x11B`) | culling (`ignorePreprocess`) | view (main) | `ShowActors`, `ignorePreprocess` |
| Sky and weather at the camera position | `AE#36561+0x147…+0x1D1` | — | view (main) | not needed for an interior; needed for "exterior — exterior" |
| `cameraInWaterState` | `AE#36560+0x6D` | shaders | view | — |
| "Camera moved > 1 %" `AE#415422` | `RenderWorld+0x529…+0x5BE` | not analyzed | view (main) | — |

## 4. The game's own second views

All game views draw via `Renderer::SubmitAccumulator` (`AE#106436`: `SetCameraData(cam, flags)`, with bit 10 `UpdateViewPort`, `AE#414580 = cam`, `SetCurrentAccumulator`, vfuncs `0x128/0x150/0x158`). 12 callers. **Not a single view restores `AE#414580`:** the next view or the world overwrites it itself. No view is nested in another.

| View | Function, place | Camera, accumulator | Light, fog | Shadows, TAA, ImageSpace | What it saves and replaces |
|---|---|---|---|---|---|
| Water reflection (cubemap) | `AE#36560+0xE9` → `AE#32160+0x444` → `AE#107564`; at `RenderPlayerView+0x172`, while the list jobs run | `BSCubeMapCamera` at the reflection point, far = far of the world camera (`AE#32160+0x1E3`); own accumulator, `activeShadowSceneNode = SSN[0]` (`AE#107564+0x121`), eye = camera; `cullMode` 3, depth 6, `Submit(…, 8)` | `SSN[0]`; `disableLightUpdate = 1` for the duration (`AE#36560+0xD4`, restore `+0xEE`); clear with sky color × `invFrameBufferRange` | no shadows; no jitter (flags 8); no ImageSpace | 2 faces per frame in turn (6 in menu); the scene is land and object LOD, trees, sky |
| Decals | `AE#36555+0xB4` → `AE#36562+0x3BE` | camera and accumulator of `BGSDecalManager`, `SSN[0]` (`+0x2EB`) | `SSN[0]` | `Submit(…, 0)`, then `SetRenderMode(0)` | — |
| Local map | `AE#16335+0x342/+0x433` | own camera, accumulator `+0x302C8`, `SSN[0]` (`+0x3C`); target `0x11` = `kLOCAL_MAP_SWAP` (`+0x32C`) | `disableLightUpdate = 1` (`+0x35`); fading globals `AE#414746`, `AE#390865` | `Submit(…, 0)` | culling by individual children of `SSN[0]` (9, 8); **our portal color takes the same slot 17** |
| 3D in menus | `AE#52727` (`Inventory3DManager::Render`, menu `PostDisplay`) | menu camera or world camera; accumulators `+0x10`/`+0x18`, eye = camera (`+0x162…`) | **own `SSN[1]` and `State.sceneGraph = 1`** (`+0x213`, restore 0 at `+0x69E`): fog and light come from `SSN[1]`; light `AE#106335(SSN[1])` (`+0x203`); `AE#414582/414583` substituted and restored (`+0x1CC/+0x1EB/+0x8FA`) | TAA: jitter by mode (`+0x236`), the camera is **registered** in the cache `AE#77506` + `AE#77505` + `SetCameraData`; `Submit(…, \|0x200)`; clearing group 5 (`+0x695`); own ImageSpace `AE#105674` (`+0x7A4`) without TAA | — |
| First person | `AE#107129+0x101/+0x2E7` | `AE#415007`, `AE#415011` | `SSN[0].cameraPos`/`lightingOffset` are translated into the first-person system and back (`AE#107123`/`AE#107124`) | `Submit(…, \|0x43)`, depth 7 | — |
| Depth prepass, shadow mask, point shadows (`AE#107604`), precipitation (`AE#26184`), splashes (`RenderWorld+0x410`) | — | own accumulators | — | — | — |

Conclusion: the game has three techniques for separating state.
1. Own accumulator and own camera for each view.
2. Own `ShadowSceneNode` and `State.sceneGraph` index for another world: this selects fog and light.
3. The view's time is chosen so that its passes do not intersect the world's registration: `+0x172`, before `+0x2EC` or after drawing.

Second views do not recompute shadows and TAA: they have no shadows, jitter is off.

## 5. The "camera as a parameter" pass

**Substitute for the duration of the pass and restore** (**code**):
- `RendererShadowState` in full, the current accumulator — already done.
- `AE#414580` — save and restore on nesting or if the pass runs after `+0x39C`. At `+0x17E` it may be left unrestored.
- Soft effects depth — explicitly set `AE#411475` to an own slot and restore, instead of borrowing `kMAIN`.
- Fog, ambient light, sun: either patch `SSN[0]` in place (now), or **own `SSN` and `State.sceneGraph`**, as in the 3D menu. Whether the `SSN` indices 2 and 3 are free was not determined (T42).
- Light flags `+0x5C` (`WakeLights`), `kNotVisible`, `AppCulled`, NPC fading (`ShowActors`).
- The shadow mask: own neutral texture in slot `0x12` or a pass without sun shadow (section 7).

**Keep per "portal + view" pair** (T29, T27):
- color, depth and depth-copy textures, the texture after color correction;
- camera clone (`NiCamera`), rectangle, quad UV, visible polygon;
- `g_eyeDistance` for fog from the door plane, particle packing vectors;
- adaptation support (T12);
- TAA history, if needed: register the clone in the cache `AE#77506`, as the 3D menu does.

**Shared per frame:**
- the main view's shadow maps and mask;
- `SSN` light update (`+0x194`);
- fading and LOD by the main camera;
- **interior animation and controllers.** Currently `AnimateActors` and `UpdateControllers` run in every pass (`PortalRender.cpp`, `Render`). With N views the world time will step N times — this must be moved to "once per frame per world".

**Reentry and recursion:**
- `BSCullingProcess::Process` uses the static processes `AE#414842`/`AE#414845` (`AE#106921`), so a nested `Process` is forbidden (**code**).
- `GetRenderPasses` holds one list per property, the pass pool is shared. So a figure must not be registered in two accumulators at once (T14, **code** + **game** 0.8.21).
- Order: each view completes fully (culling → submission → `DrainPasses`) before the next one starts culling. Deep views go first (reverse-order traversal), all within the window `+0x17E…+0x2EC`, on the main thread.
- An accumulator can be shared between views if it is empty between them. Otherwise — one per level.
- Each level needs its own copy of the saved state (a stack), not a single `savedState`.

**Depth limit.** Order of options:
1. The last ready texture of this pair from the previous frame (1 frame delay, as Realistic Reflections does — "nested" mirrors).
2. The same portal's texture from the parent view.
3. Fill with the interior fog color.

**Recursion-free scheme within a frame** (**hyp.**, recommended): each view in frame N takes the other views' textures from frame N−1. Then:
- the order between portals and mirrors does not matter;
- recursion depth accumulates one level per frame;
- a texture pool per pair is needed, and the quad SRV is chosen by the current view (section 6).

## 6. Mirrors (Realistic Reflections - Mirrors)

- **Boundary.** Both mods place a `write_call` at `RenderPlayerView+0x17E` (`docs/FEATURES.md`, 5.15).
  - Our `WriteCall` requires the native `E8` at `AE#69353` (`src/HooksCommon.h`, check `called != target`).
  - If the mirrors mod installs first, our pass **will not be installed at all** (**code**, ours).
  - We need to accept the other's trampoline, as the mirrors mod does.
- **Order.** Each trampoline calls the "original" first (ours does). So the passes run in the order of installation: the one installed earlier draws earlier. Installation order depends on DLL load order and the moment of installation (**hyp.**).
- **Particle packing.** The mirrors mod hooks the call `AE#107637 → AE#77463` and requires it to be native. We place a detour on the function itself (`src/HooksPortal.cpp`, `PackParticles`) — compatible.
- **Portal in a mirror.** The mirror draws the portal's quad with its own camera. The quad must receive the "portal + reflected camera" view's texture.
  - The quad's SRV is chosen at draw time, by the current camera: `AE#414580`, which `SubmitAccumulator+0x49` writes, or the camera in `RendererShadowState`.
  - A hook on the quad setup is needed: the property or `SetupGeometry` of its shader.
- **Mirror behind a portal.** The interior pass draws the interior's mirror. Its image in the mirrors mod is for the main camera.
  - The correct reflection requires the mirrors mod to render the capture for our camera. There is no public API — an open question.
  - Fallback — capturing the mirror from the main view (incorrect perspective) or the N−1 scheme with texture exchange via SKSE messages.
- **Target slots.** Our color — slot 17 (`kLOCAL_MAP_SWAP`), depth — 9 (`kBOOK_TEXT`). Which slots the mirrors mod borrows was not checked: a possible collision.

## 7. T17 from code: candidates

**Haze and brightness in the distance** (β grows with distance, nearly gray):
1. **The street's shadow mask in the interior** (**code** for the binding, **hyp.** for the effect).
   - Interior figures are lit by the sun of `SSN[0]` of the street. With shadow (`SSN+0x210`, `AE#107784+0x41`) the technique gets the shadow bits. Then `AE#107297+0x9B6…+0xA65` binds the main view's `kSHADOW_MASK` of the previous frame and reads it in the pass's pixels with the `kMAIN` scale.
   - The interior directional light is multiplied by the street's shadows in the upper-left corner of the screen.
   - The effect is multiplicative: the best candidate for "spots" and the brightness difference, weaker for the gray addition.
   - Check: F11 by day and night (at night the mask is empty) and with a neutral mask.
2. **Volumetric light** (`AE#107023` at `+0x335`, composited in `AE#107148+0x189`).
   - Additive, computed from the frame's depth. The quad's depth is the door plane, so the street's scattering in front of the door grows with distance.
   - This is physically correct for the street's air, but may give excess (**hyp.**).
   - Check: `gVolumetricLightingEnabled` (`AE#414913`) = 0 in memory.
3. **SSR on the quad.** The quad is drawn into the world MRT: RT2 = normals mask, TAA and SSR (`+0x6B4…+0x74D`). The SSR composite (`AE#107245` at `RenderWorld+0x1F2/+0x2BA`) adds reflections, stronger in rain (**hyp.**). Check — as in the T17 brief.
4. Quad fog: `SSN[sceneGraph].fogProperty` in the effect shader and in the composite (`AE#107245+0xD26`) — already in the T12 model.

**Smoke disappears:**
1. Culling the particle system by its `worldBound` against the portal camera's frustum. Static processes `AE#106921`, `kIgnoreMultiBounds`. If the bound sits at the emitter, all the smoke drops out (**hyp.**, T17 brief).
2. `BSPSysLODModifier` (`AE#108480`) takes the fading node's distance `+0x144` from the main camera. The camera carry exists, but only in `FadeNodeDistance` (**code**, ours).
3. Updating the system only "when visible" (**not verified**, batch 2).

## 8. Possible conflicts with Community Shaders and ENB (hyp.)

- `RenderPlayerView`: `+0x17E` (our boundary), `+0x395` / `+0x39C` / `+0x841` (prepass, shadow mask, world).
- `AE#36558+0x11D` (our `PlayerView`).
- Shaders: `BSLightingShader::SetupTechnique` (shadow mask, fog), `BSEffectShader` vf 2 (our hook), `ImageSpaceEffectHDR` vf 1 (ours), `AE#77463` (our detour).
- The light list `AE#107784` (Light Limit Fix), target slots (Deferred adds targets).

## 9. Open questions

- Which tasks do `SetJobTaskEvent` 6 and 7 wake, and what runs in parallel with `RenderWorld` (batch 1).
- Are `SSN[2]` and `SSN[3]` free, and who sets them (T42).
- The meaning of the `AE#77505` shift (which fields are "current", which are "past").
- Why the water cubemap at `+0x172` is safe with shared LOD objects: registration into its own accumulator before `+0x2EC` — check whether `AE#36643` registers passes.
- API for texture exchange with the mirrors mod.
