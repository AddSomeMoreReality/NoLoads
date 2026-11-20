# Plugin architecture (from `docs/HANDOFF.md`)

Sections 2–5 and 8 were moved from the former HANDOFF; the numbering is old. Section 9 was replaced by the task plan. Former sections 6.2 and 7 are sections 4 and 5 of `docs/HANDOFF.md`.

## 2. Terms

| Term | Meaning |
|---|---|
| Portal | The entire mechanism: the interior is rendered by a second camera into a texture, and the texture is displayed in place of the door |
| Quad | `portalquad.nif`: a rectangle on the exterior face of the door onto which the portal texture is mapped |
| Pass | Rendering the interior into the portal texture, once per frame at the scene-list boundary (`AE#36559 + 0x17E`), before world accumulation |
| Attached interior | A cell in the `kAttached` state without the player. Its 3D hangs under our node outside the scene, so the main camera does not see it |
| Transformation zone | Mapping a point through the door: exterior coordinates into interior coordinates (`Portal::IntoInterior`) |
| Keeping | The interior is not unloaded while the player is near the door (`Preload`) |
| `AE#N` | Function or data identifier in Address Library for 1.6.1170 |
| 27 000 units | Approximate distance between interior NPCs and the camera on the street in raw coordinates. This is the source of almost all distance problems |


## 3. Architecture

### 3.1 Modules

All code is in `src/`, approximately 10,400 lines (0.8.26). The plugin is built around hooks: modules have no loop of their own and run from game hooks and events.

| Module | Lines | Responsibility |
|---|---:|---|
| `main.cpp` | 252 | Plugin entry point: logging, tracing, hook installation, responses to SKSE messages. Removes the portal on `kSaveGame` |
| `Hooks*.cpp`, `Hooks.h`, `HooksCommon.h` | 1438 | Hooks, almost no logic: a hook counts something or calls the required module (T28, section 3.6). `Hooks.cpp` (98) — `Install`, one detour transaction, `Ready`, `BufferInterior`, the `PlayerCharacter::Update` tick; `HooksCells.cpp` (352) — doors, `GridCellArray`, `CellLoaderTask`; `HooksTes.cpp` (267) — `TES` functions, `MoveTo`, `SetPosition`, Havok, unloading; `HooksInteriorSim.cpp` (127) — attached interior, `OnVisible`; `HooksPortal.cpp` (391) — rendering, particles, NPCs, distances, sound, subtitles; `HooksCommon.h` (183) — helpers |
| `Portal*.cpp`, `PortalState.h` | 3369 | Second camera and everything needed for the pass, eight files (section 3.3) |
| `InteriorSim.cpp` | 1033 | When the portal is enabled: attaching and detaching the cell, interior physics stepping, NPCs on high, hiding doors, doorway geometry, status |
| `Preload.cpp` | 339 | Keeps the `sInteriorCell` interior: queues it and blocks unloading. Without it, the portal has nothing to render |
| `Detectors.cpp` | 578 | Door detection (needed by `Preload` and `InteriorSim`), door vertices, ray under the crosshair. Window detection was removed in 0.8.26 |
| `Events.cpp` | 525 | Game event subscriptions, hotkeys, door-to-image timing |
| `Trace.cpp` | 367 | JSONL tracing and caller annotation via `AE#` |
| `Probe.cpp` | 218 | Simulation probe every 5 s: cells, actors, Havok worlds, fire from inside |
| `Profiler.cpp` | 348 | Main-thread sampling under the loading screen |
| `Snapshot.cpp` | 149 | Snapshot of `TES` and grid state |
| `Physics.cpp` | 75 | Havok step counting, warm-up value |
| `Overrides.cpp` | 149 | In-memory setting presets on F7 |
| `Hud.cpp` | 79 | On-screen message queue: since 0.8.25, only responses to keys |
| `ActorJumps.cpp` | 130 | Records `actor.jump`: NPC jumps and the `SetPosition` stack (0.8.25) |
| `MainThread.cpp` | 28 | Deferred-work queue for the main thread |
| `Settings.cpp` | 199 | INI reading |

Dependencies run top to bottom: `Hooks` and `Events` call `InteriorSim`, which calls `Portal`, `Preload`, and `Detectors`. `Portal` does not know about `InteriorSim`: it receives a ready-made `Portal::Door` structure.

### 3.2 What happens in a frame

```
PlayerCharacter::Update (hook, main thread)
  original game function
  MainThread::Drain, Hud::Tick, Preload::Tick, Detectors::Tick
  InteriorSim::Tick     decides whether the portal can be kept; starts or removes it;
                        raises interior NPCs to high once per second
  Probe::Tick

AE#13291 / AE#13325 (game jobs): traversals of loaded cells
  after the original, InteriorSim adds the attached interior (AE#18890, AE#18900)

Player world step (physics thread, detour AE#77851)
  after the original, InteriorSim steps the interior world using the same original function

RenderPlayerView (AE#36559, main thread)
  + 0x17E: scene-list jobs complete (write_call AE#69353)
    Portal::Render      portal pass: game jobs are stopped, world accumulation has not started
  Main::RenderWorld     renders the street; the quad on the door displays the finished texture
```

Before 0.8.22, the pass ran immediately before `Main::RenderWorld` (detour `AE#107142`); until 0.8.26, this could be restored with a setting. Game jobs are still running there, and shared particle systems hung the game (0.8.21).

`Portal::Render` steps, in order:
1. Mirror the player's lights through the door. Find the visible part of the door (`SeeDoor`). If there is none, there is no pass.
2. Aim the camera (`AimCamera`) and update the quad UVs (`MapQuad`).
3. Save the render state and substitute our textures into the engine's slots (`LendSlots`).
4. Wake the lamp copies, advance NPC animation and controllers, show NPCs, and lend lamps to them.
5. Cull by the interior `cell3D`, then by `BSParticleSystemManager`.
6. Set the interior ambient light and render the accumulator (`SubmitAccumulator`).
7. Restore everything in reverse order and mark the render state dirty.

### 3.3 Portal implementation (`Portal*.cpp`)

Since 0.8.26 (T02), the former `Portal.cpp` has been split by responsibility, with the code moved as-is. The public interface is `Portal.h`.

| File | Lines | Contents |
|---|---:|---|
| `PortalState.h` | 333 | Shared state (`inline` variables), types, functions called between files. Everything is in `Portal::detail` |
| `Portal.cpp` | 496 | `Prepare`/`Start`/`Stop`/`Tick`, quad (`LoadQuad`, `PlaceQuad`), door on screen (`SeeDoor`, `ClipPolygon`), camera through the door (`AimCamera`, `MapQuad`), `IntoInterior` |
| `PortalRender.cpp` | 555 | Render target (`CreateTarget`), engine slots (`LendSlots`/`ReturnSlots`), culling (`t_culling`), `Render`, `DrainPasses`, scene-list boundary (`OnSceneListFence`), `BeforeEffectTechnique` |
| `PortalImage.cpp` | 250 | Interior ambient light and ImageSpace (`UseInteriorLook`/`RestoreLook`, F2), TAA (F4), F11 dumps (`WriteDump`) |
| `PortalLights.cpp` | 513 | Lamp copies (`AddInteriorLights`, `WakeLights`/`SleepLights`), player light (`MirrorPlayerLights`), NPC light (`LendActorLights`), lamps and effects (`BeforeEffectPasses`/`AfterEffectPasses`) |
| `PortalActors.cpp` | 523 | NPCs: `RefreshActors`, `AnimateActors`, `ShowActors`/`HideActors`, visibility, world camera, distances, `GrantFullProcessing`, `AdjustAnimationUpdate`, `AddAnimatedActors` |
| `PortalEffects.cpp` | 563 | Particles, controllers (`UpdateControllers`), budget (`BeforeEmission`), shared particle systems, observed effects (`NoteVisible`) |
| `PortalAudio.cpp` | 30 | Sound and subtitles through the door |
| `PortalStatus.cpp` | 106 | `StatusJson` |

`namespace Portal` sees `detail` through `using namespace detail;` in `PortalState.h`. To find portal code, Grep `src/Portal*`.

### 3.4 Starting and removing the portal

**Startup.** `InteriorSim::Tick`, then `UpdateRender`, then `AttachPortal`. At most one attempt per second, under these conditions:
- stage 1 (F8 or `iStartStage=1`);
- player outside;
- interior in `kUnattached`;
- a door to it exists in the loaded cells.

`AttachPortal` calculates the door rectangle and the `toExterior` transform, attaches the cell (`AttachCell`), starts `Portal::Start`, and hides both doors.

**Removal** (`DetachNow`, then `DetachCell`) occurs before:
- `Loading Menu`, `TES::SetCurrentCell`, `TES::PurgeBufferedCells`;
- `GridCellArray::DetachAll` and `KillAll`, `Detach` and `ClearItem` of the cell containing the door;
- `kSaveGame` (the portal returns after a second);
- a stage change, loss of 3D or the door, or a cell-state change by the game.

Shutdown order matters and has already crashed the game once: the 3D is detached from our node **before** `AE#19130` (`CLAUDE.md`, section 7, on `DetachChild`).

### 3.5 Threads and locks

| Thread | What runs there | Protection |
|---|---|---|
| Main | `Update` tick, `Portal::Render`, all state changes | Holds the `g_worldLock` and `g_attachLock` locks |
| `BSJobs` jobs (Havok step) | Interior world step immediately after the player world. There is no separate physics thread: each step runs on a different worker thread (T31, `stepThread` in 0.8.32 — five different threads) | Only `try_lock` on `g_worldLock`: if the lock is held, the step is skipped |
| Game jobs | Cell traversals, actor updates, animation, particle emission | `try_lock` on `g_attachLock`, atomic `g_space`, `g_animatedActors`, and counters |
| IO | `CellLoaderTask::Run` | Only task fields and tracing |
| Input | Hotkeys | Immediately `MainThread::Post` |

Doors are activated off the main thread, and `SKSE AddTask` is forbidden (`CLAUDE.md`, section 7).

### 3.6 Hooks

There are 60 in `Hooks*.cpp` (0.8.32): 29 vfunc, 26 detours (Microsoft Detours, one transaction), 5 `write_call`. They are always installed, even with the portal disabled. Each file installs its own: `InstallVfuncs`, `AttachDetours`, `InstallCalls` in the `Hooks::Cells`, `Tes`, `Interior`, `Portals` namespaces; the order is in `Hooks::Install`. Another 10 are installed by the modules themselves, each in its own transaction: `ActorHold` (detour, `write_call`, 5 vfunc), `Scenes` (2 detours, vfunc).

**Rule for new modules** (T16 `StoryFilter`, T20, T22). A module's hooks reside in its `.cpp`, in an anonymous namespace; `Module::Install()` installs them using `HooksCommon.h` helpers: `DetourTransaction` (its own transaction, errors logged with the module prefix), `WriteCall` (call-byte validation, errors logged), `WriteVfunc`. One call line is added to `Hooks::Install`. No new hooks are added to `Hooks*.cpp`.

| Purpose | Hooks |
|---|---|
| Loading trace | `TESObjectDOOR::Activate`, 8 `GridCellArray` methods, 5 `CellLoaderTask` methods, `QueueCellLoad`, `CenterOnCell`, `MoveTo` |
| Portal removal and protection | `SetCurrentCell`, `PurgeBufferedCells`, `GridCellArray::DetachAll/KillAll/Detach/ClearItem`, `AE#19130`, `AE#13434` |
| Keeping the interior | `AE#13370`, `AE#13721`, `AE#18547` |
| Cell attachment | `AE#13291`, `AE#13325`, `AE#106385` |
| Physics | `AE#61410` (counting), `AE#13419` (warm-up), `AE#77851` (interior step) |
| Rendering | scene-list boundary `AE#36559 + 0x17E`, `RenderPlayerView` from `AE#36558 + 0x11D`, `BSEffectShader::SetupTechnique`, `BSEffectShaderProperty::GetRenderPasses`, `OnVisible` on 7 classes (counters) |
| Actors | `AE#37277` (full processing), `AE#41466` (animation jobs), `AE#37350` (movement counting), `ModifyAnimationUpdateData` |
| Transformation zone | `AE#78274` from `AE#37348+0x1F3` (`lodDistance`), `AE#105486` from `AE#19384+0xE4` (fading) |
| Particles | `AE#74651` (budget) |
| Tick | `PlayerCharacter::Update` |

### 3.7 Settings, keys, tracing

| Key | Action |
|---|---|
| F8 | Portal enabled or disabled. Main key |
| F11 | Snapshot, probe, game frame in HDR `logs/main-N.dds`, and portal texture `logs/portal-N.dds` |
| F4 | Game TAA in memory |
| F3 | Capture accesses to player and camera positions (`AccessWatch`) |
| F2 | Interior ImageSpace across the whole screen while the portal is visible (since 0.8.25; soft particles in 0.8.24) |
| F7 | `[Overrides]` setting presets |
| F6 | What is under the crosshair |

Safety settings in `[Portal]` and `[InteriorSim]`:

| Setting | What a value of 0 disables |
|---|---|
| `bMasterParticles` | Traversal of shared particle systems, first candidate for a crash in 0.8.21 |
| `bIgnoreParticleBudget` | Interior particle emission outside the budget |
| `bAttachInterior` | Cell attachment: the portal works with `kUnattached` |
| `bFullActorProcessing`, `bGameAnimatesActors`, `bAnimateActors` | Parts of NPC processing |

Main trace records:
- `interior.sim` with an `action` field: `status` (every 10 s, contains `portal`), `attach`, `detach`, `cellAttach`, `cellDetach`, `cellDetachedByGame`, `cellLost`, `promote`, `renderFail`, `stage`;
- `portal.start`, `portal.dump`, `portal.taa`, `window.focus`;
- `probe`, `snapshot`, `preload.*`, `loading.summary`, `transition.visible`, `frame.hitch`.

### 3.8 Tools

| Tool | Purpose |
|---|---|
| `scripts/build.ps1` | Build and zip for Vortex. Writes `src/BuildInfo.h` (branch and commit, in `.gitignore`), appends the branch to the archive name except for `main` and `feature/interior-portal` |
| `tools/run_digest.py` | Short run summary: `python tools/run_digest.py logs/run-X.Y.Z [--full]` → `digest.md` in the run folder. Transitions, keys, portal, `actor.jump`, `scene.change`, failures. First analysis step |
| `tools/analyze_trace.py` | Full trace analysis: `python tools/analyze_trace.py <trace> --timeline`. Needed for cell-loading research (0.2–0.7); `run_digest.py` is sufficient for the portal |
| `tools/name_functions.py` | `AE#id` annotation, call tree, reference search (`--xref`) |
| `tools/portal_dump_to_png.py` | Portal DDS to PNG next to the source file |
| `tools/make_portal_assets.py` | Quad and texture placeholder in `res/data/` |
| `tools/re/*.py` | Reverse engineering: see below. Game and `tools/` paths are hard-coded at the beginning of the files |
| `tools/re/scan_player_access.py`, `distance_report.py`, `scan_distance_settings.py` | List of distance checks: `docs/DISTANCE_CHECKS_APPENDIX.md`. Requires a `dumpbin /disasm:nobytes SkyrimSE.exe > full.asm` file, 330 MB |

| `tools/re/` script | What it does |
|---|---|
| `fn.py <id> [full]` | Disassembles an entire function, annotating targets via `AE#`. Requires `.pdata` data; otherwise use `calls.py` |
| `calls.py <VA> <VA> [full]` | Same for an address range |
| `vtab.py <Class> [Base]` | All virtual-function table slots, differences from the base class |
| `vslot.py <Class> <slot>` | Function in a slot |
| `scandisp.py <offset> [store]` | Functions accessing a field at an offset |
| `scanbits.py <offset> <bit>` | Functions that set, clear, or test a field bit |
| `riprefs.py <id>` | Who reads or writes global data |
| `va2id.py <VA>` | Address to `AE#` |
| `scanflag.py <offset> <bit>` | Bit readers through a register (`mov`, then `shr`, `bt`, `test`) |
| `scanfield.py <offset> [read\|write]` | Field reads and writes, any offset sizes |
| `vcalls.py <index>` | Virtual-function calls. Does not see short displacements up to `0x7F` |
| `settingname.py` | Draft: setting name by address. `scan_distance_settings.py` is more reliable |


## 4. What has already been tested and did not work

This list saves time: do not repeat these.

| Question | False lead | What actually happened |
|---|---|---|
| How to show the interior | Hang `cell3D` in the street scene (0.8.5), interior clone (stage 5) | The interior is larger than the building and sticks out through the walls. Hence the second camera. Restoration code is in `docs/INTERIOR_SIM_STAGES.md` |
| No fire | Depth modes (0.8.9–0.8.11), lamp light on effects (0.8.12), soft particles (0.8.14) | Controllers were not advancing (0.8.13) |
| Fire is small | `Glow:2/3` do not enter rendering (0.8.19, the `lastAccumulated` metric does not work), particle budget (0.8.21), quad rotation (0.8.23), `portal-N.png` screenshots with gamma, soft particles (0.8.24) | Particle simulation matches the interior (0.8.23). The entire interior through the portal is brighter and grayer: candidate — exterior color grading, F2 in 0.8.25 |
| No candle sparks | Flame is a lit model shape (0.8.16–0.8.17) | Particles live outside the cell tree, in a shared system (0.8.21, being checked) |
| Doubling | Extra pass, foreign material, texture alpha | All disproved. Stale quad `previousWorld` |
| NPC in A-pose | Animation was not called | `kNotVisible` produced an update without bone movement (0.8.13) |
| Idolaf froze | The game considered him inside, AI disabled because of 27 000 units | He legitimately exited into the street cell and stood there on a temporary package. The portal was irrelevant |
| Bard interrupts the song on transition | Portal detaches and reattaches the interior; on exit, `BardSongStarter` stops songs (0.8.24) | On exit: scene lines without `AIProcess::high` finish within frames, and the scene reaches its end by itself (T10, 0.8.27 control). `BardSongStarter` (`CLOC`, `LocTypeInn` condition) triggers only on entry and interrupts the song there (0.8.27) |
| Bard song after replacing the `BardSongStarter` fragment (0.8.25) | `PlaySongRequest` on location change was entirely responsible | Instrumental scenes still changed, and the requested song stopped 1.6 s after exit without a new one. Replacement removed: game data is not modified. Cause — T10 |
| Idolaf "teleports" inside | Portal breaks his path | Bard-listener package, and the game instantly executes the path of an NPC not on high (0.8.24) |
| `kFarAway` flag | Indication of distance through the door | Copy of `kNotVisible`: "not visible to the main camera" (`AE#37992`) |
| NPC height | Navmesh movement (`AE#38308`) | Branch disabled: `bUseNavMeshForMovement=0`. Cause — `kNotVisible` and Foot IK |
| `[rax+0xE0]` in `es_setupgeom` | `previousWorld` | This is an `ImageSpaceManager` field. Actual proof: `add rdx, 0B0h` before `AE#106462` |

Other pitfalls:
- F9 is the game's quickload key and cannot be used.
- `RenderTargetManager::GetSingleton()` is incorrect in NG.
- `<windows.h>` defines `near`, `far`, `min`, `max`.
- Bash heredocs consume `\\n`: write edit scripts via Write.


## 5. Workflow

### 5.1 Version cycle

1. Increment the version in `xmake.lua`, describe changes in `docs/FEATURES.md`, `docs/HISTORY.md`, and `docs/FINDINGS.md`. When working by tasks, the coordinator does this (`docs/tasks/README.md`).
2. Build: `powershell -File scripts/build.ps1`.
3. Give the user the zip path and a short test scenario.
4. The user installs the archive in Vortex (Replace, Deploy), plays, and reports observations.
5. Sort the logs (5.4), analyze the trace, record the conclusion in FINDINGS, and make the next version.

### 5.2 Restrictions

The full list is in `CLAUDE.md`. These matter most often:
- the user launches the game; autonomous launch only with explicit permission;
- only the user makes commits, pushes, and merges;
- do not modify `Skyrim.ini` or `SkyrimPrefs.ini` without permission;
- do not suggest updating the game;
- Alt+Tab has been postponed by the user.

### 5.3 Environment

- Game: `E:\Games\Skyrim\The Elder Scrolls V - Skyrim`, not in the Steam library.
- Vortex: staging `E:\Vortex Mods\skyrimse`.
- `xmake` and Python 3.12 in `PATH`; no `pwsh`.
- `dumpbin`: `D:\VS\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe`.

### 5.4 Logs

The plugin writes to `logs/`, as set by `sLogDirectory` at build time. The trace is overwritten on every launch, so after a run the files are moved to `logs/run-X.Y.Z/` together with screenshots. Crashes are stored as `logs/run-X.Y.Z-crash/`. The folder is in `.gitignore` and occupies 1.6 GB.


## 8. Shortcomings and improvements

### 8.1 Process

| Problem | Why it matters | What to do |
|---|---|---|
| No automatic checks | Every check costs the user a game launch | Extract door math (`InteriorToExterior`, `ClipPolygon`, `AimCamera`) and test it without the game. Automated test of the portal scenario |
| Analyzer shows numbers but no verdict | Comparison against expectations is manual | "Check" mode: compare status fields against expectations |

### 8.2 Code defects and risks

Sorted by likelihood of problems.

| Problem | Where | Consequence | What to do |
|---|---|---|---|
| **Race on the global emission multiplier.** Several jobs simultaneously save and restore `AE#388630`. If the second saves the already-substituted 1, the multiplier remains 1 until the end of the frame | `BeforeEmission`, `AfterEmission` | Street without a budget for a frame, or interior with reduced emission. The code comment describes only the mild case | Do not write to the global. According to game code, `AE#74651` computes `count × scale + [this+0x30]`. Before the original, add `count × (1 − scale)` to `this+0x30`: the result exactly equals emission without a budget |
| **Shared particles share render passes with the main accumulator.** New code, not tested in game | Second `Process` in `Render` | Possible crash or flickering sparks | Test in a run. Fallback — a separate interior particle list |
| **Shared system without location separation.** Particles are in world coordinates, and interior and exterior coordinates are one numerical space | Same call | Street sparks may enter the portal, interior sparks the main frame. In Whiterun they are 27 000 units apart; in the general case there is no guarantee (hypothesis) | Filter by interior emitters |
| **Shared-system emission remains under the budget.** Such an emitter's `target` is under the manager, not under the interior 3D | `BeforeEmission` | Sparks may burn more weakly than inside (hypothesis) | Check the emitter through `emitterObjs` |
| **NPCs get stuck in place.** Saadia in 0.8.16, Olava and Saadia in 0.8.18: the controller moves up, the actor does not. Cause since 0.8.23: actor Z does not follow the controller under `kNotVisible` | Interior NPC movement | Fixed in 0.8.24, not tested in game | Watch the height difference in status |
| **Attachment without the player has not been studied logically.** Scripts, `OnCellAttach`, quest conditions, player detection, and combat through the door. Attachment repeats on every save | `AttachCell` | Unknown. Important for steps 5–6 | Check tavern scripts and conditions, then detection and combat |
| **Save paths not tested.** Quickload with an active portal, exit to menu, death. `kPreLoadGame` is not handled: relies on `Loading Menu open` | `main.cpp`, `Events.cpp` | Possible crash | Add a scenario to testing |
| **Alt+Tab.** The window does not return. Breaks the game even before F8 is pressed (0.8.13). Not tested without the plugin. Postponed by the user | Unknown | The portal may be irrelevant here | When the user returns to this, start with a launch without the plugin |
| **`->parent` traversal in jobs without a lock.** `InInterior` is called for every particle emission | `InInterior` | Safe only if the main thread is not modifying the tree at that time. Not proven | Check where particle jobs run relative to portal removal |
| **32-actor limit.** The rest silently do not receive full processing and animation | `kMaxAnimatedActors` | Enough for the tavern (6 NPCs), not for a large interior | Write a warning to the trace. Replace with a set when splitting the code |
| **Game version is not checked strictly.** The portal requires only `IsAE()`; other versions get only a warning. There is no way to check offsets such as `+0xC5F9` and `+0x1F0` | `main.cpp`, `Start` | Silent memory corruption on another AE version | Disable experiments outside 1.6.1170 |
| **Trampoline reserve.** `trampolineSize = 128` with 5 `write_call` (14 bytes each) | `main.cpp` | New calls may not fit | Increase the size together with new hooks |
| **Pass only at the scene-list boundary** (since 0.8.26). There is no longer a fallback before `RenderWorld` | `HooksPortal.cpp`, `InstallCalls` | If the call bytes at `AE#36559 + 0x17E` do not match (another version), there is no portal and the log contains a `call hook` error | Signature inside the function (`docs/PORTABILITY.md`) |
| `Space` is not freed: one object per portal start | `Start` | Deliberate, to allow lock-free reads. Leak is small | Leave it or switch to a static array |

### 8.3 Architecture

| Problem | What to do |
|---|---|
| About 80 global portal variables in `PortalState.h`. Files have been split (T02, 0.8.26), state has not | Gather into a class together with the transition to multiple portals (row below) |
| ~~`Hooks.cpp` — 1224 lines, all hooks in one file~~ | Done (T28, 0.8.32): `Hooks*.cpp` by purpose, helpers in `HooksCommon.h`, new modules install their own hooks (section 3.6) |
| **Portal is designed for one instance.** Step 4 and autonomy (`docs/HANDOFF.md`, section 5) require two or more | Gather state into a class, leave a portal registry globally. Do before step 4 |
| **Transformation zone is one-way and dissolved in `Portal`.** Only street → interior exists, while steps 2–5 need both directions | Extract a separate module (`PortalSpace`): transforms in both directions, object membership, `Carried` counters |
| Research and product are mixed: all 60 hooks (and 10 in modules) are always installed, diagnostics always run, `StatusJson` is 32–38 KB every 10 s on the main thread | Split hooks into research and portal-required, enable by setting. Produce a separate build before release |
| Disabled experiments after T02: `iControllerUpdate=2` (0.8.13: no difference), `bLightEffects` (0.8.12), `iParticleFacing` 0/1, `bAttachInterior=0` (0.8.15 path), `[Detectors] bEnabled=1`. Stage remnants: `kStages`, `Cycle`, `g_*Read` flags | Decide after T13 (fire): it may need `bLightEffects` and `iParticleFacing` |
| `InteriorSim` decision logic is mixed with observation accounting (`Watch*`, `TakeVisibility`) | Move `OnVisible` counters to diagnostics |

### 8.4 Image quality

| Limitation | Status | What can be done |
|---|---|---|
| Image is flat: TAA sees door movement, not the scene behind it | Trails in motion possible, being checked in 0.8.21 | Write portal-camera values to the velocity buffer or exclude the quad from TAA |
| No shadows from interior lamps: lamp copies have no shadows | Confirmed by code | Expensive. Revisit after step 2 |
| Exterior exposure, fog, and weather may affect the quad; eye adaptation is exterior | Not tested | Compare portal and interior-view screenshots from the same point |
| SSAO, SSR, and other post-effects in the pass | Not tested | Same comparison |
| Eye closer than 10 units to the door: no pass, old image remains (`kMinEyeDistance`) | From code | Important for step 6; solve together with it |
| Compatibility with ENB and Community Shaders | Not tested | Render-slot substitution and hooks in `RenderPlayerView` are likely conflict points |

### 8.5 Performance

Run 0.8.20, 2560×1600 screen:
- pass takes 2.9 ms CPU on average, median of maxima 4.9 ms, peak 28.7 ms;
- controllers — 0.66 ms;
- frame is 17 ms with and without the portal, but it is capped at 60 FPS. **GPU cost is unknown.**

Improvements:
- **GPU timing.** D3D11 timestamp queries around the pass; otherwise decisions are blind.
- **Occlusion check.** `SeeDoor` clips only against the view frustum: a door behind a wall is still rendered in full.
- **Adaptive resolution.** Pass resolution currently equals the door size on screen; nearby, this is almost the entire screen.
- **Every-other-frame updates** at a large distance from the door.
- **Simulation levels.** The interior is fully simulated while the door is in loaded cells, even if the player is two cells away. Step 1 will partially solve this.


## 9. Recommended order

Replaced by the task plan in `docs/tasks/README.md`. Items from the former list have been done (commit, 0.8.21 analysis) or are in 8.2–8.3 and tasks T02, T22–T27.
