# Version history (former section 9 of `CLAUDE.md` and section 1 of `HANDOFF.md`)

The text was moved without changes.

## 9. Workflow

1. Environment check (section 1) → summary to the user.
2. Submodule update, renaming, starting-code build → first `0.1.0` zip. The user installs it in Vortex and confirms that the log appears (smoke test).
3. Trace + events + vfunc hooks → `0.2.0`, run.
4. Detour hooks + snapshots → `0.3.0`, run.
5. Fix F11 hang, Preload experiment (5.5), notifications (5.6) → `0.4.0`, run.
6. Fix crash (dangling cell pointer), loading-screen profiler, exterior in Preload disabled by default → `0.5.0`, run.
7. Havok tail (call tree, `name_functions.py`) and H11/H12 simulation probe → `0.6.0`, run.
8. Havok step counter, `TES` handler hook, `fWarmStartMaxTime` experiment (F9), physics in probe → `0.7.0`, run.
   - `0.7.1`: F9 → F7, `[Overrides]` presets (warm-up + fades), door-to-image breakdown (H13), profiler enabled again: find 55 ms after `TES` when exiting outside without warm-up.
   - `0.7.2`: preset with `fTeleportDoorActivateDelayTimer=0` (0.65 s pause before fade), `frame.hitch` record (stutters in the first 5 s after the loading screen, autosave), world-change check (Whiterun gates) with fades disabled.
9. Door and window detectors (5.10), interior keeping and InteriorSim (5.11) → `0.8.0`, run.
   - `0.8.1`: message queue, unload blocking by manager `AE#40845`, interior step on the physics thread under lock, culling and transparency removal during rendering, window-vertex position fix, F6 "what is under the crosshair".
   - `0.8.2`: blocking the unload itself `AE#13721` for the kept interior, control door copy and stage 5 (interior clone), hiding the exterior door, `fadeWatch`, Havok ray for F6, raw window-vertex bytes.
   - `0.8.3`: crash fixed (clones without actors and without freeing), 4 control door copies on different parents, registration in `ShadowSceneNode`, hiding the interior door, vertex positions as `float`, `lattice` in window keywords.
   - `0.8.4`: detectors disabled (doors are found silently), 5 control door copies in a row on the street, `OnVisible` counters for hung nodes, hiding building parts in the doorway, F6 ray through hidden references.
   - `0.8.5`: interior at the root of the exterior cell's nodes (renders only there), detachment before cell detach, 2 control copies closer to the door, hiding the building part with the door texture, reference search in the world's persistent cell and with rays.
   - `0.8.6`: portal (5.12): stage 6, second engine camera into a texture in place of the door, `Meshes/`/`Textures/` assets in the archive, portal snapshot on F11. Crashed when enabling stage 6 (incorrect `RenderTargetManager` singleton in NG).
   - `0.8.7`: `RenderTargetManager` address fixed. Portal works: the interior is visible through the door from any point.
   - `0.8.8`: portal lighting (interior light and player-light copies with `restrictedNode`, interior ambient light), NPCs in the portal, fire with portal depth, resolution based on the visible part of the door.
   - `0.8.9`: UVs in both buffers (duplicate image), copies of all interior lamps, copies "enabled" for the pass (`frustrumCull`, `lodDimmer`, `worldTranslate`), geometry rebuild for player light, NPC culling data.
   - `0.8.10`: light is "woken" before culling and interior passes are rebuilt, substitution of the entire `kMAIN` depth target (fire near the door), NPCs — parents and all fade nodes, culling data for their geometry.
   - `0.8.11`: `ignorePreprocess` during culling (NPCs), depth modes on F4 and effect counter (fire), pass skipped without window focus, fallback trace file.
   - `0.8.12`: stages 1–5 removed (F8: off / portal, archive in `docs/INTERIOR_SIM_STAGES.md`), effects without lamp light during culling (F4 — comparison), interior NPC animation and positions, effect diagnostics.
   - `0.8.13`: full interior NPC animation (`ModifyAnimationUpdateData` hook), interior controllers (fire, smoke, candles) before every pass, F4 — controller modes, particle diagnostics.
   - `0.8.14`: full high update for portal NPCs (detour `AE#37277`: AI and movement), F4 — soft particles in the portal, diagnostics of effects outside rendering. Alt+Tab postponed.
   - `0.8.15`: portal NPCs in game animation jobs (detour `AE#41466`), movement counter (detour `AE#37350`) and character-controller data, observation of effects in portal culling.
   - `0.8.16`: portal interior attaches like a neighboring cell (game calls `AE#18976` / `AE#19063`, detachment `AE#19130` / `AE#19046`), added to per-frame cell traversals (`AE#13291`, `AE#13325`), controller time — cell time `AE#400333`.
   - `0.8.17`: attached interior 3D under its own node outside the scene (light in the portal), transformation zone — character-controller `lodDistance` through the door (NPC under the floor), NPC-height and candle-shape diagnostics. Crashed on entering the tavern.
   - `0.8.18`: crash fixed (3D detached from our node before `AE#19130`, detour `AE#19130`), `cameraRelatedUpdates` in the portal pass (flame toward camera), our lamp light for NPCs during the pass, additional candle nodes in the trace.
   - `0.8.19`: animated-reference fading through the door (`AE#19384` → `AE#105486`: fire and candles), only interior NPCs in the portal NPC list, diagnostics of NPCs getting stuck on stairs and under flooring (controller flags) and image doubling.
   - `0.8.20`: F4 — game TAA in memory (doubling check), fire-shape sampling in portal status and in the probe from inside (flame comparison).
   - `0.8.21`: portal quad `previousWorld` (doubling with TAA), interior emitters outside the game particle budget (fire half as strong as inside), shared particle systems in the portal pass (candle sparks). Hung 2.4–2.9 s after F8.
   - `0.8.22`: hang cause: group 5 after the pass, accumulator check, shared particles only at the scene-list boundary (`bMasterParticles=0`, `bPassAtSceneListFence=0` by default), `Watchdog`, particle emission without writing to the global, `thread_local` culling flag, `[InteriorSim]` restored in INI, `Hooks::Ready()`, `Portal::Prepare` before cell attachment, `kPreLoadGame`, `toggleCollision` instead of `+0xC5F9`, `docs/PORTABILITY.md`.
   - `0.8.23`: particle quad rotation toward the view (detour `AE#77463`), waiting for interior readiness, lamp shapes collected anew, scene-list boundary and shared particles by default, NPC list on tick (E5), interior back into buffer (E6), detachment under lock (E8), no custom animation of attached NPCs (E13), buffer length in probe (E14), `scene.change` record.
   - `0.8.24`: NPC-height cause (`kNotVisible`, Foot IK), virtual camera for interior NPC updates, `GetDistance` through the door, sound through the door, position-access capture on F3 (`AccessWatch`), soft particles on F2, stack on NPC door activations, sounds in probe, `docs/DISTANCE_CHECKS.md`, `docs/RELATED_MODS.md`.
   - `0.8.25`: only key responses on screen, subtitles through the door, replacement of the `BardSongStarter` quest fragment (song does not cut off on transition), F2 — interior ImageSpace, F11 — game frame in HDR (`main-N.dds`), `actor.jump`, mod analysis (`docs/RELATED_MODS.md`).
   - `0.8.26` (T00–T02, task-based work): `BardSongStarter` fragment replacement removed (game data is not modified); `tools/run_digest.py`, branch and commit in archive name and log; autotest settings, window detection, exterior keeping, pass before `RenderWorld`, depth modes 0–1, and soft-particle toggle removed; `Portal.cpp` split into `Portal*.cpp` and `PortalState.h`. Control run matched 0.8.25.
   - `0.8.27` (T11, step 1 of T10): portal interior NPCs kept on high during transitions (`ActorHold`: detour `AE#37503`, skip reordering `AE#37621+0x348`), `actor.level`, `actor.keptHigh`, `actor.snap`, `scene.start/stop/line` records. No NPC jumps or song cutoff on exit.
   - `0.8.28` (T12, T13 diagnostics): custom portal color grading (`PortalGrade.cpp`: interior composite and interior view under exterior grading, F2 — stages), interior fog for effects, `iControllerUpdate`, `iParticleFacing`, `bLightEffects` removed. F11 hung the system.
   - `0.8.29`: hang cause — traversal of `renderTargets` beyond the actual targets (`RenderTargetCount`, `QueryInterface`); F11 snapshot switches, watchdog for 10 s after F11, `nan`/`inf` in trace → `null`.
   - `0.8.30` (T11 + T12 merge): interior fog from the door plane.
   - `0.8.31`: `iQuadFogModel=1` by default. Run with Engine Fixes 7.0.20: compatible. Stage 1 closed.
10. Autotest → `0.9.0`, run (option A or B).
11. Analysis, `FINDINGS.md`, README.

After each version, briefly report: what changed, the zip path, and what to do in Vortex.

---

## Status and results of runs 0.8.22–0.8.25 (from `docs/HANDOFF.md`, as of 03.11.2026)

## 1. Where we are now

**Project.** SKSE plugin for Skyrim AE 1.6.1170 on CommonLibSSE-NG v13.1.0. It began as cell-loading research (hypotheses H1–H13, mostly closed). It is now an experiment: the Bannered Mare tavern stays alive while the player is outside and is visible through its door as through an opening.

**User goal** (NoLoads project): door transitions without a loading screen. Intermediate goal — a "full portal" (`docs/FEATURES.md`, 5.13): the interior behaves like a neighboring exterior cell, and light, sound, projectiles, NPCs, and the player pass through the door.

**Works in game:**
- interior visible through the door from any point on the street, image updates every frame;
- NPCs inside walk, are animated and lit, and their AI runs fully;
- interior physics steps together with the player world;
- player torchlight passes inside;
- interior attached as a neighboring cell (`kAttached`, 2.5–4 ms), entering it takes 70 ms of loading screen;
- entry and exit do not crash the game (since 0.8.18).

**Runs 0.8.22** (FINDINGS): no more hang, pass at the scene-list boundary and candle sparks work, NPC passes through the door with the portal. Remaining: fire is small with the correct particle count, doubling only when running into an invisible wall (TAA limit), bard is interrupted (cause not found).

**Run 0.8.23** (FINDINGS): light after loading fixed. Quad rotation did not help the fire: particle simulation matches the interior, difference is in rendering. Idolaf confirmed as game behavior from old runs without the portal. NPC-height cause found.

**Run 0.8.24** (FINDINGS):
- NPC height fixed;
- soft particles are irrelevant;
- bard song is audible outside but cuts off on every transition: this is the vanilla `BardSongStarter` quest;
- no subtitles on the street: distance to the speaker;
- Idolaf receives the bard-listener package, and distant processing instantly moves him through the door;
- F3: few frequent uncovered player-position checks.

**Run 0.8.25** (FINDINGS):
- F2 confirmed that exterior color grading tints the portal image. Remaining: fire is smaller, no smoke;
- bard script replacement did not help and was removed: the mod does not modify game data (`CLAUDE.md`, 5.14, item 7);
- the game itself relocates Saadia and Idolaf while they are not on high: 1.7 s on exit before the portal attaches, catch-up on entry;
- subtitles not tested: only instrumental scenes played outside.

**Not done:** light in both directions, view from inside to outside, projectiles, player passage without loading, README, final FINDINGS. Autotest was removed from requirements on 01.11.2026; its settings are still in the code.

**Since 03.11.2026, work is organized as tasks for separate chats:** `docs/tasks/README.md`. It contains stages, briefs, branches, and chat rules. Results are in `docs/tasks/results/`.

**Coordinator's first actions:**
1. Read `docs/tasks/README.md` and new files in `docs/tasks/results/`.
2. Move merged results into FINDINGS, `CLAUDE.md`, and this file. Write briefs for the next stage.
3. Git: only the user makes commits and merges; give them the command at the end of the iteration (`CLAUDE.md`, beginning).
