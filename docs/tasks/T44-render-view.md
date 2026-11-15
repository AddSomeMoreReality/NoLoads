# T44. Frame rendering map: what is needed for the pass to take the camera as a parameter

Stage 2, research, foundation for stage 3. No code: the chat works in the main folder on `feature/interior-portal` and writes only new files. Test run: none. Model — Opus 5.5, effort — high; mass reading — `re-reader` agents (Sonnet, medium) and `re-reader-fast` (Haiku, low). Database — MCP `ghidra_*` or `tools/ghidra/gq.py` (`ghidra` skill, `docs/GHIDRA.md`).

## Why

5.14, item 4: cell rendering works for any camera — for other mods, cutscenes, and recursive portals. The portal pass must take the camera and target as parameters and not read the global player camera. A separate texture per "portal + view" pair and a recursion depth limit are needed.

5.14, item 5: compatibility with Realistic Reflections - Mirrors. A portal in a mirror shows the correct picture, a mirror beyond a portal shows the reflection.

Currently `Portal::Render` substitutes the camera, target slots, and lights around a single pass (`docs/ARCHITECTURE.md`, 3.2–3.3). What other frame-global state the pass implicitly takes from the player view is unknown. Hence T17: the distant picture is brighter and has a gray haze, smoke disappears when the hearth is out of view.

## What is known

- **Frame.** `AE#36558+0x11D` → `RenderPlayerView` (`AE#36559`; in the database it is named `Main::Draw`, verify the name). Scene-list jobs are finished at `+0x17E` (`write_call` on `JobList::Finish` `AE#69353`), which is where our pass runs. Then `Main::RenderWorld` (`AE#107142`) runs. Sources: `docs/ARCHITECTURE.md`, 3.2; `docs/FINDINGS.md`, Grep `AE#36559`.
- **`AE#36661` is not `RenderPlayerView`.** It collects references within a radius of the player (`results/T38-T39-batches.md`, "T38, batch 10"). It is named incorrectly in `results/T32.md`.
- The world camera is `Main::WorldRootCamera` (`AE#36609`). Render targets — rules of `CLAUDE.md`, section 7 (`RenderTargetManager`, `renderTargets` 114/116).
- `BSRenderPass` passes come from a shared pool; the accumulator must be empty after a pass. An object also rendered by the game must not be registered while its jobs are running (`CLAUDE.md`, section 7; T14).
- Renderer state held in a single instance — the list in T42 (`portalGraph`, slots 8/9 `SSN`, the interior flag, sky, LOD). T44 breaks down how the pass reads it.
- Realistic Reflections — `docs/FEATURES.md`, 5.15: what the mod does and where it hooks (scene-list boundary, `write_call` chain). **Do not copy the mod's code** — only ideas and facts about the game binary.

## To do

1. **Frame chain.** From `Main::Update` to presentation: culling, scene-list jobs, accumulators, shadows, `RenderWorld`, water, particles, ImageSpace and TAA, UI. For each stage — function, thread, what it takes as the "current view".
2. **View-global states.** Everything the pass reads as one view:
   - camera and frustum, viewport;
   - culling process;
   - accumulator;
   - sun shadow maps (cascades for the player camera);
   - figure light lists, limit `AE#107784`;
   - TAA history and ImageSpace;
   - fog;
   - particle and billboard orientation;
   - LOD.

   For each: where written, where read, and the class — "per view", "per frame", "per world".
3. **The game's own second views** — the main model of how the engine itself separates state: water reflection, the local map (top-down view), 3D in the inventory menu, cubemaps. What each saves and substitutes, which camera and accumulator it takes, what it does with shadows and TAA.
4. **Requirements for a "camera as a parameter" pass:**
   - what to substitute for the duration of the pass;
   - what to keep per "portal + view" pair;
   - what is shared per frame.

   Separately — re-entrancy (a pass inside a pass): accumulator, pass pool, saving and restoring state. What can be done when the depth limit is exhausted.
5. **Mirrors.** Where the scene-list boundary and the chain of foreign `write_call`s are. In what order our pass and the mirror's pass run. What is needed for a mirror, drawing the portal quad, to get its own view's texture, and vice versa.
6. **T17 from code.** Candidates for the haze and distant brightness (fog, LOD, ImageSpace from the player view) and for the disappearing smoke (particle culling by the player camera).
7. **Name candidates** to the curator — only for functions from the findings.

## Rules

- Do not put Bethesda pseudocode into files or answers: only findings, `AE#id`, offsets. The database is read-only.
- Verify NG singletons against how the game passes the address (`lea` — object, `mov` — pointer; `CLAUDE.md`, section 7).
- Verify "there is no call" and "reads only here" claims with `xrefs`/`asm`; take offsets from `asm`.
- Community Shaders and ENB hook the same places (`docs/ARCHITECTURE.md`, shortcomings): mark potential conflict locations.

## Result

- `docs/RENDER_VIEW_MAP.md` (≤ 250 lines): frame chain, view-state table, the game's second views, pass requirements, mirrors, findings for T17.
- `docs/tasks/results/T44.md` per the README template:
  - the main points for T17, T29 (texture pool per "portal + view") and 5.14, items 4–5;
  - list of IDs for T40, if a run is needed;
  - a "For the curator" section.

## Conflicts

None: new files. Related in scope — T17 and T29; boundary with T42 on rendering: T42 provides the list of global states, T44 — how the pass reads them.
