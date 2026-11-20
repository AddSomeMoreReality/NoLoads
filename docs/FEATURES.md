# Plugin features (formerly sections 5.4–5.13 and 5.15 of `CLAUDE.md`)

The text was moved unchanged, and the 5.x numbering was preserved. The final mod requirements (5.14) remain in `CLAUDE.md`. The task and trace (5.1–5.3) are in `docs/ORIGINAL_TASK.md`.

## Module table of contents

| Section | Module (`src/`) | Subject |
|---|---|---|
| 5.4 | `Settings` | INI, hotkeys |
| 5.5 | `Preload` | keeping a cell in memory |
| 5.6 | `Hud` | on-screen messages |
| 5.7 | `Probe`, `ActorJumps` | simulation probe, NPC jumps |
| 5.8 | `Profiler`, `tools/name_functions.py` | loading-screen profile |
| 5.9 | `Physics`, `Overrides` | physics warm-up, setting presets |
| 5.10 | `Detectors` | doors (windows removed in 0.8.26) |
| 5.11 | `InteriorSim` | interior as an adjacent cell |
| 5.12 | `Portal*` | portal: interior through a second camera (lighting, NPCs, particles, distances, sound). Files — `docs/ARCHITECTURE.md`, 3.3 |
| 5.13 | `Portal*` | "full portal" goal, steps 1–6 |
| 5.15 | — | ideas from Realistic Reflections - Mirrors |

### 5.4 INI (`Data\SKSE\Plugins\CellLoadResearch.ini`)

```ini
[General]
bTraceEnabled=1
bSnapshotGrid=1
bReadCellBuffers=0        ; H9: read interior/exteriorBuffer — disabled until the length is confirmed
bLogCellAttachPerRef=0    ; TESCellAttachDetachEvent is very noisy

[Hotkeys]
iSnapshot=87              ; F11 (DirectInput scan code)
```

`[AutoTest]` and `iStartAutoTest` (F10) were removed in 0.8.26 (T02): the automated test was removed from the requirements on 01.11.2026.

Capture keys through `BSTEventSink<RE::InputEvent*>` on `RE::BSInputDeviceManager`: `ButtonEvent::IsDown()` and `GetIDCode()`.

The actual INI is in `res/CellLoadResearch.ini`. Its comments are on separate lines: `GetPrivateProfileString` does not strip `; ...` after a value. Additional keys (added at the user's request on 23.10.2026):

```ini
[General]
bHotkeyMessages=1         ; 5.6: only responses to hotkeys on screen (0.8.25)
bProfileLoadingScreen=1   ; sample the main thread during the loading screen ("profile" record)
sLogDirectory=            ; build.ps1 substitutes <project>\logs

[Hotkeys]
iCycleOverrides=65        ; F7: cycle through [Overrides] presets. Not F9 — that is the game's quickload key
iDetectorsReport=64       ; F6: door detector summary (5.10; windows removed in 0.8.26)
iInteriorSimStage=66      ; F8: InteriorSim off / portal (5.11)
iToggleTemporalAA=62      ; F4: game TAA on / off in memory (5.12, since 0.8.20; previously portal soft particles)
iPortalGrading=60         ; F2: cycle custom portal color-grading stage 2 -> 0 -> 1 (0.8.28, T12; in 0.8.25 interior ImageSpace applied to the whole screen)
iAccessWatch=61           ; F3: capture accesses to player and camera positions (0.8.24, docs/DISTANCE_CHECKS.md)

[Preload]                 ; in addition to 5.5
bKeepInterior=1           ; call QueueInteriorCellLoad (AE#18540) for sInteriorCell every fRefreshSeconds
bKeepInteriorNearDoorOnly=1

[Detectors]               ; 5.10
bEnabled=0                ; disabled on 24.10.2026 at the user's request
fDoorsIntervalSeconds=0.5
iNearestCount=5

[InteriorSim]             ; 5.11
iStartStage=0             ; 0 off, 1 portal
fStatusSeconds=10
bForceWorldActive=1
bAttachInterior=1         ; the portal interior is attached like an adjacent cell (kAttached), with its 3D under its own node outside the scene

[Portal]                  ; 5.12: InteriorSim stage 1
bEnabled=1
iWidth=0                  ; portal texture size, 0 — same as the screen
iHeight=0
iColorSlot=17             ; engine slots occupied by the portal during its pass
iDepthSlot=9
bInteriorLights=1
bMirrorPlayerLights=1
bInteriorAmbient=1
bAnimateActors=1
bSyncActorPositions=1
bFullActorProcessing=1    ; interior NPCs receive full high processing (AI, movement)
bGameAnimatesActors=1     ; interior NPCs included in the game's animation jobs
bIgnoreParticleBudget=1   ; interior emitters ignore the game's particle budget
bMasterParticles=1        ; the portal pass also renders shared particle systems (candle flames)
bActorsSeen=1             ; interior NPCs without kNotVisible: height follows the controller (0.8.24)
bVirtualCamera=1          ; world camera for interior NPCs - portal camera (0.8.24)
bDistanceThroughDoor=1    ; GetDistance between the player and interior references through the door (0.8.24)
bSoundThroughDoor=1       ; interior sounds audible outdoors (0.8.24)
bSubtitlesThroughDoor=1   ; subtitles from speakers in the interior visible outdoors (0.8.25)
iGradeStages=2            ; T12: 0 as rendered by the pass, 1 + interior composite, 2 + interior view under outdoor grading (docs/tasks/results/T12.md)
iQuadFogModel=1           ; T12: what stage 2 removes from the quad: 0 nothing, 1 effect fog, 2 also outdoor composite (default 1 since 0.8.31: 2 is twice as bright at a distance)
bDumpLdr=1                ; 0.8.29: F11 writes the post-HDR frame (ldr-N.dds)
bDumpAdaptTargets=1       ; 0.8.29: F11 writes 1x1 adaptation targets (portal.adapt); 0.8.28 hung here
bDumpGraded=1             ; 0.8.29: at stage 2, F11 writes the image after color grading (graded-N.dds)

[Probe]                   ; 5.7
bActorJumps=1             ; actor.jump record: NPC jumps, cell changes, with SetPosition stack (0.8.25)

[AccessWatch]             ; hardware breakpoints on player and camera positions, F3 (0.8.24)
bEnabled=1
fSeconds=3
iMaxHits=20000000

[Watchdog]                ; watchdog thread: hang while the portal is active (7, 0.8.22)
bEnabled=1
fHangSeconds=6
bTerminate=1              ; terminate the game after writing CellLoadResearch.hang.txt

[Overrides]               ; 5.9: presets "name:Section=value | ...", substituted in memory (game settings — without a section)
sSet1=fWarmStartMaxTime:HAVOK=0 | fNormalDoorFadeSecs:General=0.05 | fAutoDoorFadeSecs:General=0.05 | fMinSecondsForLoadFadeIn:Interface=0 | fFadeToBlackFadeSeconds:Interface=0.05
sSet2=<sSet1> | fTeleportDoorActivateDelayTimer=0

[Preload]                 ; 5.5
bEnabled=1
sInteriorCell=WhiterunBanneredMare
fRefreshSeconds=2.0
bPreloadLinkedInteriors=1
```

### 5.5 Experiment: "keep a cell in memory" (`Preload.*`)

**User goal:** one interior and one exterior area are loaded in advance and kept in memory so that entering them is nearly instantaneous. This must work both for natural entry (through a door or by approaching an exterior cell) and for `coc`.

- **Exterior (removed in 0.8.26, T02).** `(2r+1)²` cells around `sExteriorCell` were queued through `ExteriorCellLoader::QueueCellLoad` if unloaded, no more than once every 10 s. Disabled at the user's request after the 0.4.0 crash (`sExteriorCell` is empty).
- **Interior.**
  - The game's own `bPreloadLinkedInteriors` mechanism is enabled in game memory (`Skyrim.ini` is not modified): the game preloads interiors behind nearby doors.
  - There is no ready-made function that loads an arbitrary interior without entering it. If the interior is unloaded, we need to find what does it and block that specific location.
- **Diagnostics.** Every state change of the retained cell is written to the trace as `preload.change`, with the location where it was noticed (`TES.SetCurrentCell`, `GridCellArray.DetachAll`, `LoadingMenu.close`, `tick` …).
- **Success criterion** — `kPre → ActorCell enter` time and `Loading Menu` duration for retained cells versus cold cells. Baseline values from FINDINGS: interior 383–642 ms, exterior 2.2–2.6 s.

### 5.6 On-screen messages

**Since 0.8.25, only responses to hotkeys appear on screen** (user request of 02.11.2026): which mode is enabled (F2, F4, F7, F8) and what was recorded (F3, F6, F11). Cell transitions, loading time, portal state, and retained-cell state are written only to the trace and log (`transition.visible`, `interior.sim status`, `preload.change`). To disable the responses as well, use `[General] bHotkeyMessages=0`.

Before 0.8.25, every `BGSActorCellEvent enter` displayed `CLR: <from> -> <to> | load N ms`; after fade-in, `CLR: door -> picture N ms = wait + fade-out + loading + fade-in` (H13); and every `fStatusSeconds`, the portal state was displayed.

Cell names come from `GetName()`, otherwise the editor ID, with `(x,y)` added for exteriors. Game strings are UTF-8, so Russian text is displayed as is.

**Message queue (`Hud.*`, since 0.8.1).** In 0.8.0, the game's queue became backed up for minutes.
- Messages wait in our own channelled queue; no more than one is sent to the game every 2 s.
- A new message on the same channel replaces the pending one.

### 5.7 Simulation probe (`Probe.*`, user request of 23.10.2026)

User question: does world simulation (NPC AI, physics, rendering) run in interiors while the player is in an exterior of the same world, and in an exterior while the player is in an adjacent interior? These are hypotheses H11 and H12.

A `probe` record is written every `fIntervalSeconds`, after every transition, and on F11.

**Cells** — with a category: `player`, `grid`, `gridWhileInside`, `doorInterior` (interiors behind doors in the loaded area), `bufferedInterior`. For each cell:
- `state`;
- rendering: `inScene` (the cell's 3D is attached to the scene) and `culled`;
- physics: `physicsTime` — the time of the cell world's `hkpSimulation`.

`hkpSimulation` is not described in NG. The Havok 2010 layout is used and checked through the `world` back-pointer at offset `0x10`.

**Actors** — all from `ProcessLists` (high / middleHigh / middleLow / low), up to `iMaxActors`. For each: cell, position, `3D`, `ai`, AI package, distance to the player.

The analyzer aggregates records by category: how many cells are in the scene, whether physics is running, processing levels, and the fraction of NPCs that moved more than 20 units.

**NPC jumps (`ActorJumps.*`, 0.8.25, `[Probe] bActorJumps`).** Every frame, NPCs in the high and middleHigh lists are compared with the previous frame. An `actor.jump` is written to the trace if:
- the step is longer than 150 units in one frame (`kind` = `jump`);
- the NPC changed cells (`cell`);
- the NPC returned to these lists far from its previous location (`returned`).

If the jump was made by `TESObjectREFR::SetPosition` (detour `AE#19790`), the record includes its stack.

### 5.8 Loading-screen profile (`Profiler.*`) and function names (`tools/name_functions.py`)

- **Profiler.** While the loading screen is open, approximately once every 1 ms the plugin unwinds the main thread's stack: it copies the stack and calls `RtlVirtualUnwind` on the copy. The `profile` report contains a call tree by transition phase, with self and inclusive time.
- **`tools/name_functions.py`.** Labels `AE#id` using data from `SkyrimSE.exe` itself:
  - RTTI class names whose virtual function tables contain the function;
  - strings referenced by its code;
  - direct callers.

  `--tree <phase>` mode prints a call tree from the trace with these labels.

### 5.9 Physics warm-up (`Physics.*`)

The `kPost → kFinish` tail is `TES` stepping physics worlds in response to `PositionPlayerEvent kPost` (FINDINGS, H4). Two detour hooks are installed for AE; the IDs were found with the tool, and the signatures checked against the machine code:
- `AE#61410` = `hkpWorld::stepDeltaTime(float) -> int`: counts steps by world for the probe and loading-screen report;
- `AE#13419` = `TES[+0x68]::ProcessEvent(PositionPlayerEvent)`: `hook.TES.ProcessEvent.PositionPlayer` record with event type, step count, and simulated time.

Result in 0.7.0: warm-up is exactly `fWarmStartMaxTime` (241 steps of 1/60 s); at 0, entering a preloaded interior takes 26 ms instead of 316.

**Setting presets (`Overrides.*`, since 0.7.1).** F7 cycles through: game values → `sSet1` → `sSet2` → … → game values. Values are written to `RE::Setting` objects in memory (INI, INIPref, GameSetting; numeric only); `Skyrim.ini` is not modified. The active preset number is written to `loading.summary` and `transition.visible`.
- Before 0.7.1 this was F9. **F9 is the game's quickload key:** pressing it caused a corrupted-save message. The free keys in the standard layout are F1–F4 and F6–F8.
- The outcome of the 0.7.x experiments for a separate mod is in `docs/FAST_DOOR_TRANSITIONS.md` (a document for handoff to another chat).

### 5.10 Door and window detectors (`Detectors.*`, user request of 23.10.2026)

**Disabled since 0.8.4** (`bEnabled=0`, user request: the "murky" windows are most likely part of large wall textures; window experiments postponed). **Window detection was removed in 0.8.26** (T02): keys `bWindows`, `fWindows*`, `sWindow*`, the `detector.windows` record, and the `window` flag in `detector.look`. The description below is for historical reference. Door detection remains.
- Windows are not detected, and there are no `detector.*` records or messages.
- Doors are detected silently: `NearestDoorTo` is needed by `Preload` and `InteriorSim`.
- F6 shows only what is under the crosshair.

- **Doors.** Every `fDoorsIntervalSeconds`, references with `ExtraTeleport` in loaded cells are enumerated: the player's interior or the attached exterior grid.
  - Fields for each instance: position, angles, `meshYawDeg` (mesh +Y in the world), destination cell and its state, arrival marker, linked door, `automatic`, `locked`.
  - A `detector.doors` record is written when the nearest `iNearestCount` changes.
- **Windows.**
  - Geometry of nearby references (up to `fWindowRadius`) whose diffuse texture path contains `sWindowKeywords` and does not contain `sWindowExclude`.
  - If a `BSGraphics::TriShape` has `rawVertexData`/`rawIndexData`, the geometry is split into connected pieces (one piece per window): center, size, normal.
  - Otherwise, the bounds of the entire geometry are used. References are cached, and the cache is reset when the interior or world changes.
  - `detector.windows` record.
- **Screen.** When the nearest door or window changes, a message is displayed (`door`/`window` channels).
- **F6:** summary plus `detector.look`. Direction is the camera's local +X, checked against the player's crosshair. This is needed to find window textures not included in `sWindowKeywords`.
  - Since 0.8.2: a Havok ray against collision (`bhkWorld::PickObject`, `COL_LAYER::kLOS`, `TESHavokUtilities::FindCollidableRef`); the hit reference's geometry is sorted by proximity to the hit point, smallest first among those whose bounds contain the point.
  - If the ray hits nothing, bounding spheres are used as in 0.8.1.
  - Since 0.8.4, the ray passes through references with hidden 3D (the tavern's hidden door retains collision) and records them in `lookedThroughHidden`.
- `rawVertexData` in static meshes stores positions as full `float` values even without the `VF_FULLPREC` flag. Precision is determined by the space before the next attribute: offsets in descriptor half-bytes 2–9, ≥ 12 bytes means `float` (0.8.2). The first 96 bytes are written to `detector.windows.rawSamples`.
- Whiterun windows: `wrwindows01.dds`; window lattices: `wrwoodlattice01.dds` (`sWindowKeywords=window,lattice` since 0.8.3).
- **Vertex format:** stride = `(desc & 0xF) * 4`, position always at offset 0. `GetAttributeOffset(VA_POSITION)` in NG is unsuitable for this (0.8.0).

### 5.11 Interior as an adjacent cell (`InteriorSim.*`, H14–H16)

The user wants the interior to be loaded while the player is in the adjacent exterior, as fully as adjacent exterior cells: NPC AI fully active, preferably with rendering. The engine functions found are described in FINDINGS (H14–H16).

- **Retention:** `Preload` calls `QueueInteriorCellLoad` (`AE#18540`, AE only) every `fRefreshSeconds` — loads the cell or moves it to the front of the buffer.
  - The `bPreloadLinkedInteriors` manager (`AE#40845`) unloads interiors behind doors that are not among the nearest on the very next frame (0.8.0).
  - Therefore, detours of `AE#13370` (remove from buffer and unload; only it calls this) and `AE#18547` (cancel loading; skipped only when the return address is inside `AE#40845`) leave the retained interior alone while the player is outside it and near its door.
  - Loop protection: three loads within 30 s pause retention for 60 s.
  - Since 0.8.2, the unload itself, `AE#13721`, is also hooked: in 0.8.1, the next preloaded interior pushed the tavern three places down the buffer (`AE#13434`, from worker threads). Only a cell in `kUnattached` is blocked, while `g_keepActive` is set (decided by the main thread: player outside, door nearby) and there is no loading screen. The first call from each caller is written to `preload.block`. A loaded retained interior is not queued for loading again: it may be outside the buffer.
- **F8 stages (since 0.8.12):**
  - 0 — off;
  - 1 — portal (5.12): the interior's Havok world steps together with the player's world, interior NPCs are promoted to high, and the interior is rendered by a second camera.
  - Stages 1–5 from versions 0.8.0–0.8.11 were removed from the cycle and from the code at the user's request on 25.10.2026.
    - What they did, their outcomes, and the code for restoring them are in `docs/INTERIOR_SIM_STAGES.md`.
    - Full 0.8.11 sources are in `docs/archive/interior-sim-0.8.11/`.
- **Physics step:**
  - detour on `bhkWorld::vfunc[0x32]` (`AE#77851`): after the player's world steps, **on the physics thread** (on 1.6.1170 the player's world does not step on the main thread), the same original function steps the interior world (byte `+0xC5F9` set to 1 for the duration of the step);
  - the main thread holds `InteriorSim::GuardWorlds()` around `SetCurrentCell`, `PurgeBufferedCells`, `DetachAll`, `AE#13370`, and `AE#13434`, while the physics thread only tries to acquire it;
  - there are no steps between `Loading Menu` opening and closing.
- **NPCs:** `Actor::MoveToHigh()` once per second for interior NPCs.
- **Attachment (0.8.16, `[InteriorSim] bAttachInterior`).** User request of 25.10.2026: the interior behind the portal must behave like an adjacent exterior cell. The game's sequence is in FINDINGS, run 0.8.15.
  - **Attachment.** When the portal starts, the interior is attached using the game's own calls, as done by `TES::SetCurrentCell` (`AE#13316`):
    - `AE#18976(cell, root)`, but without the portal graph: detour `AE#106385` (`ShadowSceneNode::SetPortalGraph`) skips the call while we attach;
    - world package `AE#77905` / `AE#77906`;
    - `AE#19063(cell, 1)` → `kAttached`.
  - **Where the 3D is.** Since 0.8.17, the interior 3D is under its own node outside the scene (`g_attachRoot`), invisible to the main camera.
    - In 0.8.16 it was under `TES::objRoot` with `AppCulled`, and the light clones collected no shapes.
    - `ShadowSceneNode::AddLight` (`AE#106326`) only queues a light; shapes are collected later (`AE#106342`).
  - **Detachment** — as in `AE#13317`: first the 3D is removed from our node, then `AE#19130(cell, 1)`, then `AE#19046(cell, 1, 0)`.
    - Order matters: `AE#19130` calls `TES::objRoot->DetachChild(cell3D)`, and `NiNode::DetachChild` (`AE#70292`) takes the child index from the child itself (`+0x38`) without checking its parent.
    - In 0.8.17 this removed someone else's node (`TES::tempNodeManager`) from `objRoot`, and the game crashed upon entering the tavern.
    - Detour `AE#19130` also removes the 3D from our node when the game itself detaches the cell (`cellDetachedByGame`). This happens before `Loading Menu`, in the `SetCurrentCell` and `PurgeBufferedCells` detours, on `DetachAll`, and on SKSE `kSaveGame`.
  - **Per-frame traversals** use the player's interior or the grid. Detour `AE#13291` calls `AE#18890(cell, AE#400333)`; detour `AE#13325` calls `AE#18900(cell)` for the attached interior. Both only try to acquire `g_attachLock`: the main thread holds it during attachment and detachment.
  - **Interaction with `Preload`.** An interior attached by us does not remove the portal (`OwnsAttachedCell`) and is not unloaded (`BlockCellUnload`).
  - **The Havok step** remains ours. Steps performed by the game itself (`gameInteriorStepsTotal`) cancel ours for that frame.
  - **If attachment does not reach `kAttached`**, the interior is detached again. The portal works with an unattached interior, and attachment is not retried in that session.
- **Hidden objects:** the exterior door and the interior door now occupying the doorway.
- **Culling counters:** hooks on `OnVisible` (`vfunc 0x34`) of `NiNode`, `BSFadeNode`, `BSMultiBoundNode`, `BSMultiBoundRoom`, `BSTriShape`, `BSDynamicTriShape`, and `BSSubIndexTriShape` count calls for watched objects.
  - Watched objects: exterior door node, portal quad, interior root and room, 3 interior references near the door, actor node, and 2 actors.
  - Every `fStatusSeconds`, the values are displayed and written to `interior.sim status.visible`.
- If the door to the interior leaves the loaded cells, the portal is removed.
- **Safety:**
  - the node is removed on `Loading Menu open`, `GridCellArray::DetachAll`, and any interior state change;
  - `NiUpdateData` with `kDisableCollision`;
  - the interior does not step during warm-up (`Physics::InWarmStart`) or the loading screen.
- **Screen:** every `fStatusSeconds` — state (physics steps, high NPCs, rendering, frame duration). `interior.sim` records.
- In the probe, worlds have `worldActive` (byte `+0xC5F9`), and `worlds` contains the number of steps on the main thread.

---

### 5.12 Portal: interior through a second camera (`Portal.*`, user request of 24.10.2026)

In 0.8.5, the tavern interior attached to the exterior scene turned out to be larger than the building and protruded through its walls. The user asked to render it with a second camera and display it in place of the door, every frame, accounting for view direction.

- **Rectangle.** `meshes\CellLoadResearch\portalquad.nif`: one quad with `BSEffectShaderProperty`, unlit, with texture `textures\CellLoadResearch\portal.dds` (magenta placeholder).
  - Both files are generated by `tools/make_portal_assets.py` in `res/data/`; `build.ps1` places `res/data/*` at the archive root next to `SKSE/`.
  - The quad is attached to the exterior door node on its street-facing side. Its size comes from the door vertices (`Detectors::WorldPoints`), and its shape is set through the rotation-matrix columns because `NiTransform` has only one scale.
  - The quad texture's `resourceView` is replaced with the portal SRV.
- **Camera** — a clone of the world camera in interior coordinates:
  - positioned at the player's eye transformed through the door (`InteriorToExterior`);
  - looks across the door plane;
  - the view frustum (`NiFrustum` at distance 1) is shifted so that it passes through the door rectangle; the near plane is 2 units in front of the door.
  - The quad's 0..1 UVs match this frustum, so the image is correct from any viewpoint.
- **Pass.** Before 0.8.22 — detour `Main::RenderWorld` (`AE#107142`) before the original; since 0.8.22 — the scene-list boundary (below, 0.8.22), and since 0.8.26 only that boundary. Main thread. Steps:
  1. copy shadow state;
  2. our texture (`R16G16B16A16_FLOAT`) and depth buffer are temporarily substituted into engine slots `iColorSlot`/`iDepthSlot`;
  3. `StartAccumulating`, then `CullingContext` with `kIgnoreMultiBounds` over the interior `cell3D` (not attached anywhere), then `SubmitAccumulator`;
  4. restore state and mark everything "dirty", restore slots.
  - The engine call order is in FINDINGS (run 0.8.5).
- **Resolution (0.8.8).** Texture the size of the screen. Each frame renders only the visible part of the door (the rectangle is clipped by the main camera's view frustum), at its on-screen size. Through the quad material's UV offset and scale (`texCoordOffset/Scale[0]`), only this part is mapped onto the door. No pass when the door is off-screen.
- **Lighting (0.8.8):**
  - clones of the interior `NiPointLight`/`NiSpotLight` are added to `ShadowSceneNode[0]` with `restrictedNode` = interior `cell3D` (without shadows). Originals are untouched: the game will add them itself upon entry;
  - dynamic light sources near the player (torch, "Candlelight") are cloned and transformed through the door into interior coordinates every frame;
  - during the pass — interior ambient light (`AE#18966` → `AE#105643`, saving and restoring 0x44 bytes of `BSShaderManager::State+0xC8`) and sun color equal to the interior directional color.
- **NPCs (0.8.8–0.8.9):** during portal culling, `AppCulled` is cleared from interior actors' nodes down to depth 3 below the root, and the root's `fadeAmount`/`currentFade` are raised; the status includes `actorStates`.
- **Lighting in the pass (0.8.9):**
  - all interior lights are cloned, even if the game already has them in `ShadowSceneNode[0]` (their lit geometry was collected from the street scene);
  - during the pass, our clones have `frustrumCull = 0`, `lodDimmer = 1`, and current `worldTranslate`; values are restored after the pass;
  - geometry for the transformed clones of player lights is recollected (`AE#106342`) when they have moved 16 units.
- **Quad UVs (0.8.9):** offset and scale are written to both `texCoordOffset/Scale[0..1]` buffers (the game alternates them).
- **Lights are selected during culling (0.8.10).** An object's light sources are selected by `GetRenderPasses` (`AE#106516` → `AE#107784`), skipping `frustrumCull == 0xFF`.
  - Therefore, clones are "woken" before `StartAccumulating` and put to sleep after `SubmitAccumulator`.
  - At startup, `lightData->lightListChanged` is set on all interior geometry so that passes are rebuilt.
- **`kMAIN` depth (0.8.10).** After effects, the game restores the `kMAIN` depth buffer as current (`AE#107522`, `AE#77457` = 0). Therefore, the entire `kMAIN` slot is replaced for the pass: texture, `views`, `readOnlyViews`, SRV, dimensions. It is then restored.
- **Culling (0.8.11):** the portal context sets `ignorePreprocess`. Otherwise, `BSCullingProcess::Process` (`AE#76553`) skips nodes with `kPreProcessedNode` and `kNotVisible` — stale game marks for a cell without the player. In 0.8.10 this made NPCs disappear.
- **Depth modes (0.8.11, `[Portal] iDepthMode`, F4 cycles; since 0.8.26 only mode 2, key removed):**
  - 0 — own slot and `kMAIN` SRV (0.8.9);
  - 1 — entire `kMAIN` slot (0.8.10, no effects);
  - 2 — entire `kMAIN`, while soft particles read a copy of the depth. It is captured by the `BSEffectShader::SetupTechnique` hook (vfunc 2) before the pass's first effect.
  - The same hook counts effects and soft effects in the pass (`effectSetups`, `softEffectSetups` in the status).
- **Effects without light (0.8.12).** Hook on `BSEffectShaderProperty::GetRenderPasses` (vfunc `0x2A`, `AE#106876`): while our culling registers an effect, light clones are "asleep" (`frustrumCull = 0xFF`).
  - Effects with `kEffectLighting` otherwise take up to 5 sources. In 0.8.10–0.8.11 there was no fire at all.
  - `[Portal] bLightEffects` toggles this; effect passes are rebuilt (`lastRenderPassState = 0x7FFFFFFF`). Run 0.8.12: hearth effects have no `kEffectLighting`, so lighting is not the cause.
  - The status includes `effects`: 8 effects on the strongest light (hearth).
  - Since 0.8.12, depth mode was set only in the INI (`iDepthMode`, default 2); modes 0 and 1 were removed in 0.8.26.
- **NPC animation (0.8.12, `bAnimateActors`, `bSyncActorPositions`).** The game advances high actors' animation (`AE#41377`) only with `BOOL_BITS::kProcessMe`. Before the pass, the portal:
  - `Update3DPosition(false)` if the 3D is more than 1 unit from the actor position;
  - `Actor::UpdateAnimation(dt)` (vfunc `0x7D`);
  - `root->Update` with `kDisableCollision`.
  - The status includes `animated`, `poseChanged` (bone `NPC Spine [Spn0]`), `positionsSynced`, and actors' `boolBits`, `processMe`, `rootOffset`.
- **Full NPC animation (0.8.13).** `Actor::ModifyAnimationUpdateData` (`AE#38054`) treats an actor with `kNotVisible` as invisible, and `BShkbAnimationGraph::Update` (`AE#63587` → `AE#63575`) does not move any bones (FINDINGS, run 0.8.12).
  - A hook on `VTABLE_Character` vfunc `0x79` sets `unk2A = 1` (full update) and `unk2C = 1` (visible) for actors in the portal interior.
  - The actor list for the hook is an atomic array: the game does not necessarily update actors on the main thread.
  - The portal itself calls `UpdateAnimation` only for actors the game has not updated since the previous pass.
  - The status includes `animatedByGame`, `animated`, `forcedAnimationUpdates`, `poseChanged`/`poseChecks` (pose changes between passes).
- **Full NPC processing (0.8.14, `[Portal] bFullActorProcessing`).** Detour `AE#37277`: high processing (`AE#41369`, in jobs) otherwise gives NPCs beyond the camera fade distance a reduced update without AI or movement (FINDINGS, run 0.8.13). The status includes `fullProcessingGranted`.
- **NPC animation by game jobs (0.8.15, `[Portal] bGameAnimatesActors`).** Detour `AE#41466`: after the original, portal NPCs whose cell is not attached are added to the animation job list (`AE#41496`), just as the game adds NPCs from attached cells. The portal itself updates only those skipped by the game. The status includes `gameAnimationAdded`.
- **NPC movement (0.8.15, diagnostics).**
  - Detour `AE#37350` counts movement updates for portal NPCs (`movementUpdates`).
  - Actor status includes `charController`, `controllerPos`, `velocity`, `package`.
- **Transformation zone (0.8.17, user request of 27.10.2026, 5.13).** `Portal::IntoInterior(object, point)`, from any thread: if `object` is the 3D of a reference in the portal interior, the point (player camera, exterior coordinates) is transformed through the door into interior coordinates. `Start` publishes the transformation (atomic pointer, never freed); `Stop` removes it.
  - First check: the character controller's `lodDistance` (`+0x29C`) — distance from the camera (`AE#50785`) to the actor 3D bounds. It is written by `AE#78274`; the call in `Actor::Update` (`AE#37348 + 0x1F3`) is hooked with `write_call` (64-byte SKSE trampoline; call and target checked before writing).
  - Movement and animation read it (`AE#37363`, `AE#37690`, `AE#37919`, `AE#38009`). In 0.8.16, interior NPCs considered "far away" in their own coordinates sank through the floor.
  - The status includes `pointsCarried`; NPCs include `lodDistance`, `controllerState`, `fallTime`, `controllerInInteriorWorld`, `zMin`/`zMax`.
- **Watched effects (0.8.15).**
  - At portal startup, up to 64 interior effects not hidden by nodes are watched. `Portal::NoteVisible` from the `OnVisible` hooks and the `GetRenderPasses` hook count portal culling passes for them.
  - The status includes `watchedEffects`: `visits`, `parentVisits`, `passCalls`, `noPasses`, parent chain.
- **Soft particles (0.8.14, `[Portal] bSoftEffects`; F4 until 0.8.19).** Run 0.8.14: fire and smoke look the same with and without them. When disabled, the `BSEffectShader::SetupTechnique` hook clears bit 18 (`kSoftEffectBit`) from the effect technique in the portal pass. The status includes `softEffects`, `softEffectsDropped`, and `hiddenEffects` — interior effects not rendered, with reasons.
  - Run 0.8.24 confirmed there is no difference. In 0.8.26 (T02), the switch was removed: soft particles are always enabled, and `softEffectSetups` remains in the status.
- **Interior controllers (0.8.13, `[Portal] iControllerUpdate`; F4 only in 0.8.13).** Fire and smoke particles and candle flames are moved by controllers, which in turn are moved by scene updates that exclude a cell without the player. Before culling, the portal calls:
  - mode 1: `UpdateSelectedDownwardPass` (vfunc `0x2D`) on `cell3D`;
  - mode 2: `NiAVObject::Update` with bit 0 (controllers).
  - Time is the game cells' time `AE#400333` (since 0.8.16; previously `GetDurationOfApplicationRunTime() / 1000`, 18 s ahead), with flag `kDisableCollision`.
  - The status includes `controllers`: nodes with selective-update flags, particles, their `lastUpdate`, and the `lastUpdate` of particles in the player's cell (the time supplied by the game).
- **Facing the camera (0.8.18).** The portal culling context sets `cameraRelatedUpdates` (`NiCullingProcess + 0x11C`): without it, `NiBillboardNode::OnVisible` (`AE#71063`) does not rotate flames and glows toward the portal camera, and the fire looked "small" (0.8.13–0.8.17).
- **Fade and particles through the door (0.8.19).** Per-frame cell traversals (`AE#18894`, `AE#18900`) call `AE#19384` for animated references: fade (`BSFadeNode + 0x130`) and camera distance (`+0x144`) from world camera `AE#36609` through `AE#105486`. The game does not update a reference without fade.
  - From `+0x144`, `BSPSysLODModifier` (`AE#108480`) sets particle emission (`NiParticleSystem + 0x180`) and size (`+0x188`).
  - In 0.8.16–0.8.18 the camera was about 27,000 units away: hearth flames had 0 particles, and candles had no flames.
  - Call `AE#19384 + 0xE4` is hooked with `write_call`: for portal interior references, the camera is transformed through the door (`IntoInterior`, `Carried::kFade`). The status includes `fadesCarried`; watched effects include `fadeDistance`, `emitScale`, `sizeScale`.
- **Double image (0.8.18–0.8.20, fixed in 0.8.21).** `BSEffectShader::SetupGeometry` computes motion vectors for TAA from geometry `previousWorld` (`+0xB0`). The game does not update the static quad, and on repeated placement `NiAVObject::Update` left the previous location in `previousWorld`: after exiting the tavern, it was offset because the door had still been moving.
  - After positioning the quad, `previousWorld = world` is set throughout its tree; the status includes `quadPreviousOffset`.
  - F4 toggles the game's TAA in memory: `ImageSpaceManager` (`AE#414660`, pointer) `+ 0x1F0 → + 0x18`, `portal.taa` record.
- **Particle budget (0.8.21, `[Portal] bIgnoreParticleBudget`).** `AE#74651` (`NiPSysEmitter` vfunc `0x2A`) emits `count × AE#388630 + remainder`. `AE#71099` writes the multiplier once per frame: `(iMaxDesired:Particles / live particles)²` if there are more than 750, otherwise 1. Outdoors in Whiterun it is about 0.46, and the hearth behind the portal burned at half strength compared with the view from inside.
  - Detour `AE#74651` adds `count × (1 − multiplier)` to the remainder (field immediately after `NiPSysModifier`) for emitters under the portal interior 3D. The global has not been modified since 0.8.22. The status includes `emissionScale`, `emissionsWithoutBudget`.
- **Portal accumulator and the 0.8.21 hang (0.8.22).** Details are in FINDINGS, section "Run 0.8.21".
  - `SubmitAccumulator` (`AE#106436`) does not render the refraction normal group: only the game frame renders it. After the pass, `AE#106600(accumulator, 5)` is required, as in `AE#52727`. Without it, passes remain in the accumulator.
  - `Render` checks the accumulator's `PassInventory` before and after the pass. Leftovers in geometry groups are cleaned up; leftovers in technique groups or the transparency group produce `portal.fault`, and the portal stops for the entire session.
  - `RenderPlayerView` (`AE#36559`) starts world-culling jobs before `RenderWorld` and finishes them afterward. Thus, game jobs are running during our pass. Objects shared with the game can be registered only at the scene-list boundary.
  - Scene-list boundary: `AE#36559 + 0x17E`, call `AE#69353`, hook `SceneListFence`. Before 0.8.26 it was enabled by `[Portal] bPassAtSceneListFence=1`. Since 0.8.26 (T02), the hook is always installed and the `RenderWorld` detour is removed: if the call is not found, there is no pass.
  - The portal-culling flag is `thread_local`: game jobs call the same hooks at the same time.
  - `PassPhase` marks show the watchdog the pass step.
- **Particles facing the camera (0.8.23, `[Portal] iParticleFacing`).** Flames, sparks, and smoke are `NiParticleSystem`, not `NiBillboardNode`: the 0.8.18 rotation did not affect them. Particle quads are built on the CPU by `AE#77463`, called from `AE#107637`, using `RendererShadowState::posAdjust` and `cameraData.viewUp/viewRight`.
  - Detour `AE#77463`; these fields are replaced during our pass and restored afterward. Value 2 — player view transformed through the door: the portal camera always looks across the door, so at an oblique viewing angle the quads would be edge-on to the eye.
  - The status includes `packCalls`, `packHeldPortal`, `packHeldMain`, `packHeldOther`, `packOriginDrift`: what was in the state before replacement.
- **Interior readiness (0.8.23).** `kUnattached` occurs immediately after loading is queued; references and actors appear later. The portal waits 1.5 s after the loading screen and cell load (`kSettleUs`) and for actor 3D, but no longer than 8 s. If lights still have not collected shapes, `Portal::Tick` recollects them (`shapeRecollects`).
- **Visibility instead of distance (0.8.24).** `HighActorCuller` (`AE#40335`) sets `kNotVisible` using the main camera frustum; only `BSFadeNodeCuller` (`AE#107507`) clears it, and this does not happen for 3D outside the scene. The twelve readers of this bit are in `docs/DISTANCE_CHECKS.md`, section 4.
  - `AE#41375` and `AE#41447` skip Foot IK feedback `AE#43690`, while `AE#43691` keeps the actor's Z from the Foot IK base: a walking NPC hangs in the air or below the floor.
  - `AE#37992` copies the bit into `kFarAway`: this flag means "not visible", not "far away".
  - `[Portal] bActorsSeen`: the mark is cleared in `Tick` and after `HighActorCuller`. The status includes `notVisibleCleared`, `footFeedback`.
- **Virtual camera (0.8.24, `[Portal] bVirtualCamera`).** `Portal::ActorScope` on `Character::Update`, `AE#37350`, and `ModifyAnimationUpdateData`. Within this scope, detour `AE#36609` returns the portal camera. `CarryCamera` keeps it at the player's eye transformed through the door even when the door is off-screen. The status includes `worldCameraCarried`.
- **Distance through the door (0.8.24, `[Portal] bDistanceThroughDoor`).** Detours `AE#19823` (`GetDistance`) and `AE#19824` (its square, 98 call sites): for the player and an interior reference instead of `FLT_MAX`. The status includes `distancesCarried`.
- **Sound through the door (0.8.24, `[Portal] bSoundThroughDoor`).** `BSXAudio2GameSound::SetEmitterPositionImpl` (vfunc `0x0F`, `AE#68007`): sound within the interior bounds is transformed into street coordinates. Music, 2D sounds, and sounds without a position are untouched. The status includes `soundsCarried`, `lastSound`; the probe includes `sounds`.
- **Subtitles through the door (0.8.25, `[Portal] bSubtitlesThroughDoor`).** Every frame, `AE#52638` measures the distance from the speaker to the player using "world position" `AE#19784` (a point and a space: interior cell or world). `AE#52637` shows only subtitles closer than `fMaxSubtitleDistance:Interface` (1250). The `AE#19784` call for the speaker (`AE#52638 + 0x122`) is hooked: a speaker in the portal interior is transformed through the door into the player's space. The status includes `subtitlesCarried`.
- **Bard song (0.8.25, removed).** The vanilla quest `BardSongStarter` (`000BE159`) is started by Story Manager on a player location change (`CLOC`) and changes the song through `PlaySongRequest`. In 0.8.25, its fragment was replaced. The run showed that something else stops the requested song, and editing the quest violates 5.14, item 7. The replacement was removed; task T10 (`docs/tasks/`) is investigating the cause.
- **Interior ImageSpace (0.8.25, F2).** Run 0.8.24: through the portal the whole interior is brighter and grayer than from inside, with dim brown flames. The portal image goes through outdoor color grading: eye adaptation, bloom, saturation, tint (`docs/FINDINGS.md`).
  - While the player is inside the interior, `Portal::Tick` captures `ImageSpaceManager` data (`GetImageSpaceData`).
  - On F2, in frames with a portal pass, these data are substituted after the pass. On the next tick, game data is restored unless the game has written its own.
  - The status includes `imageSpace`: current values, captured values, `swaps`, and `held` — how many replacements lasted until the end of `RenderPlayerView`.
- **HDR frame capture (0.8.25).** F11 also writes `main-N.dds`: `kMAIN` after `RenderPlayerView` (call `AE#36558 + 0x11D`), before ImageSpace. Fire from inside and through the portal can be compared in the same units. `portal.dump` record with `imageSpace`.
- **Candle flames (0.8.21, `[Portal] bMasterParticles`).** In 0.8.21, this traversal immediately before `RenderWorld` hung the game; since 0.8.22 it runs only at the scene-list boundary, and since 0.8.23 these are the default settings. The candle model has only `BSValueNode` `AddOnNode49` (emitter). Particles live in `BSMasterParticleSystem` under `BSParticleSystemManager` (`AE#411270`, child 5 of the world root), in world coordinates.
  - The portal pass calls `BSCullingProcess::Process` a second time with this manager's scene: `AE#106921` does not reset the accumulator.
  - The `fires` sample (particles: size, radius, opacity, location) and `masters` (emitters in the interior, particles) are written to portal status and the probe while the player is inside.
- **NPC lighting in an attached interior (0.8.18).** `ShadowSceneNode` lights actors in an attached cell as moving objects (`AE#106349`), with lights that are awake for the main camera. Our clones are asleep at that time, and in 0.8.17 NPCs were almost black.
  - During the pass, our lights that reach an NPC shape (light radius + shape bounds) are added to its `lightData->lights`, then removed.
  - The status includes `actorLightsLent`.
- **Window focus (0.8.11).** No pass while the game window is not in the foreground or is minimized. Focus changes are written to the trace (`window.focus`). If the main trace file is busy, `CellLoadResearch.trace-<pid>.jsonl` is written.
- **NPCs (0.8.10):**
  - `AppCulled` is cleared for the entire pass from the actor root's ancestors up to the interior root, and their bounds are recalculated;
  - `currentFade` is raised on all of the actor's `BSFadeNode`s;
  - the status includes `ancestors` and `firstShape` (opacity, passes, pass sources, frame of last registration);
  - `OnVisible` is counted on the actor node and two actors.
- **Fire (0.8.8):** during the pass, the depth SRV of the `kMAIN` slot is replaced with the portal depth.
- `cell3D` node bounds are recalculated at startup: in a cell without the player they are stale (radius 1 at the origin).
- **F11 at stage 6** writes the visible part of the portal texture to `logs\portal-N.dds`. `tools/portal_dump_to_png.py` converts it to PNG.
- **Trace:** `portal.init`, `portal.dump`, the `portal` field in `interior.sim status` (passes, skips, CPU ms, eye-to-door distance, frustum), attachment with mode 6.

### 5.13 Full portal (user goal of 27.10.2026)

The portal must become a "transformation zone" between exterior and interior coordinates and work in both directions and autonomously:
- **light** passes through the door in both directions;
- **distance checks** (rendering, AI processing, etc.) for objects on the other side use player coordinates transformed through the door;
- **arrows, spells, and other objects pass through the portal**, followed later by NPCs and the player.

Engine limitations:
- **The Havok world** of an interior is separate, and interior coordinates overlap the exterior. Therefore, an object passing through the door must be moved from cell to cell: position and velocity through the transformation, body into the other world.
- **Distance in the game code** is measured in many places. Every check must be found and redirected through `Portal::IntoInterior` (or the inverse transformation).

Step-by-step plan, each step as a separate version:
1. **Distances through the door.**
   - Done: character controller `lodDistance` (0.8.17), fade and animated-reference distance on which fire particles depend (0.8.19), NPC visibility, world camera, and `GetDistance` (0.8.24).
   - Full list and status — `docs/DISTANCE_CHECKS.md`.
   - Next:
     - full processing (`AE#37277`) based on the transformed camera instead of "always full";
     - animation LOD (eye point in `BSAnimationUpdateData`);
     - processing level in `ProcessLists`;
     - sound.
2. **Light in both directions.**
   - Interior lights illuminate the street near the door: clones in exterior coordinates.
   - Street lights near the door illuminate the interior: like the already transformed player light.
3. **Interior sound outdoors:** bard, fire. First version in 0.8.24: sound position is transformed through the door, ignoring walls. Since 0.8.25, subtitles also pass through the door. The opening-based attenuation scheme is in `docs/RELATED_MODS.md` (Audio Occlusion). Previously there was no sound outdoors: the user heard the bard only in versions where objects from the interior cell were moved into the exterior cell. That was the wrong solution (see 5.14).
4. **View from inside to outside:** a second portal on the interior door. It requires an attached exterior grid while the player is inside.
5. **Projectiles through the door:** arrows, spells. Intersection with the door rectangle, moving the reference to the other cell with position and velocity transformed. Door collision must not stop them.
6. **NPCs and player through the door without a loading screen:** this is the original NoLoads goal. Entering an attached interior already takes 70 ms (run 0.8.16).


### 5.15 What was taken from Realistic Reflections - Mirrors

The mod solves the same second-view problem (265 files, 117,000 lines, `src/SecondView.cpp`). Analyzed on 01.11.2026, commit `2927c18`.

**Code must not be copied:** the mod's own source code has no license, and `THIRD_PARTY_NOTICES.md` explicitly states this. Ideas and facts about the game binary (addresses, call order) may be used, with our own implementation.

Already applied in 0.8.22:
- clearing group 5 and checking that the accumulator is empty before and after the pass;
- the scene-list boundary as a safe point for the second view;
- pass-phase marks for investigating hangs.

Useful for 5.14:

| Idea | Location in the mod | Our use |
|---|---|---|
| Target pool and resolution selection | `MirrorCaptureTargetPool`, `MirrorCaptureSizing`, `MirrorAdaptiveQuality` | Many portals, adaptive resolution |
| Pass scheduler | `MirrorsOfSkyrimCaptureScheduler`, `MirrorCaptureWorkPolicy` | Many portals: not all updated every frame |
| D3D occlusion queries | `MirrorOcclusionQueries`, `MirrorOcclusionPolicy` | Do not render a portal behind a wall |
| Nested panels | `nested*` counters in `SecondView.cpp` | Recursion: the mod does not recurse, but renders another mirror's finished image in the capture |
| State lease with exact restoration | `HandMirrorBillboardStateLease`, `HandMirrorParticleCameraLease`, `LeaseRegistry` | Facing the camera for flames and particles for each view. The culling traversal modifies shared state (fade, frame history) |
| Fault protection | `ExecuteNativePassSEH`, `LatchFault` | A pass fault disables the feature instead of crashing the game |
| GPU timing | `MirrorGpuTimer`, `MirrorNativeCpuTiming` | Pass cost |
| Versions and signatures | `SupportedRuntimePolicy`, `RenderPlayerViewSceneListFenceSignature` | Version allowlist and byte checks around the hook site |
| Neighboring mods | `PeerDetection`, call chain at the scene-list boundary | Community Shaders and ENB. The mod accepts someone else's hook at this boundary, so `write_call` is compatible with it |
| Pure logic separated | `*Policy.h` headers | Like `PortalGeometry`: logic can be tested without the game |

