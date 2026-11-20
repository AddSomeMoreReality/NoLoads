# T42. Census of the engine's "single instance" state

Stage 2, research. No code: the chat works in the main folder on `feature/interior-portal` and writes only new files. Test run: none (optional — with the T40 build). Model — Opus 5.5, effort — high; mass reading — `re-reader` agents (Sonnet, medium) and `re-reader-fast` (Haiku, low). Database — MCP `ghidra_*` or `tools/ghidra/gq.py` (`ghidra` skill, `docs/GHIDRA.md`).

## Why

5.14, items 1–2: many portals at once and any cell pairs, including exterior ↔ exterior (city gates, another world) and interior ↔ interior. The engine, however, keeps one current cell, one grid of one world, one sky, one portal graph. Each such state is future work for T29, T21, and the gates.

A complete "state → who writes → who reads → what to replace it with" table is needed so that the scope of this work is known before the code, not discovered through crashes.

## What is known

- `docs/CELL_LOADING_MAP.md`, section 11 — the first 8 rows of such a table (`TES::interiorCell`, `gridCells`, `worldSpace`, the `ExteriorCellLoader` key without a world, the Havok world `AE#400440`, `portalGraph`, `LoadedAreaBound`, buffers, walks `AE#13291`/`AE#13325`/`AE#13388`/`AE#13415`).
- `docs/PLAYER_MAP.md` — state tied to the player's location: `currentLocation`, `cachedWorldSpace` `+0x630`, music, region, water, weather. Do not repeat; reference it.
- `docs/FINDINGS.md`, "T38, T39, T41 research" (Grep): the renderer keeps one world — slots 8/9 of the root `ShadowSceneNode` (`AE#20516`), the shaders' interior flag (`AE#105631`, derivatives `AE#415263`/`AE#415264`), the sky (`AE#13326`), LOD roots (`AE#31813`/`AE#31814`).
- A technique that already works: substituting state for the duration of the far side's frame (`Portal::ActorScope`, `Portal::FarSideScope` in T32).

## To do

1. **List of states.** Start from the section 11 table and extend it. Sources:
   - `TES` fields (NG + T36): which of them describe the "current place" or "current world";
   - singletons that read the player's cell or world: `Sky`, `TESWaterSystem`, `BGSTerrainManager`, LOD, grass, `BSShaderManager::State`, `ShadowSceneNode`, the Havok world, `ExteriorCellLoader`, `LoadedAreaBound`;
   - global variables written by `SetCurrentCell` (`AE#13316`), `AE#13315`, `AE#13317`, `AE#13288`, `PlayerCharacter::SetParentCell` (`AE#40466`).

   Rendering in T42 — only the list of global states and their writers. What the drawing pass reads and what each view needs — T44.
2. **Writers and readers** of each state.
   - Singleton: `ghidra_xrefs` on the global, then `ghidra_fieldrefs --within` over the found functions. `fieldrefs` without a type gives candidates — confirm in code.
   - Sort readers with the `re-reader-fast` agent by subsystems: loading, AI, rendering, physics, sound, UI, saving.
   - For each reader, indicate the thread where possible: main, jobs, IO.
3. **For each state — a conclusion per 5.14:**
   - what a second instance breaks: a second interior, a second grid of the same world, a grid of another world;
   - **solution class:**
     - (a) substitution for the duration of the far side's frame;
     - (b) our own copy per portal;
     - (c) hooking the readers;
     - (d) engine limit: cannot be worked around, another path is needed;
   - size estimate: number of places or functions.
4. **Scenarios.** For the three cases, list which states get in the way and in what order to remove them:
   - many interiors from the street;
   - city gates (exterior ↔ exterior of another world, both grids alive);
   - interior ↔ interior.
5. **Name candidates** to the curator — only for functions that made it into the findings.

## Rules

- Do not put Bethesda pseudocode into files or answers: only findings, `AE#id`, offsets. The database is read-only.
- Verify "nobody writes it" and "the only reader" claims with `xrefs`/`asm`, not the pseudocode. Take offsets from `asm`. In the 14 batches of T38/T39, both agent errors were exactly of these kinds.
- Do not duplicate T32 (reads of the player pointer) and T38 (player map): reference them.

## Result

- `docs/SINGLE_INSTANCE.md` (≤ 250 lines): state table, scenarios from item 4, conclusion "what to do in T29, T21, and for the gates".
- `docs/tasks/results/T42.md` per the README template:
  - the main points for T29, T21, and 5.14;
  - functions for counting with a T40 build, if a run is needed;
  - a "For the curator" section.

## Conflicts

None: new files. Related in scope — T29 (portal registry) and T21: the coordinator moves the findings into their briefs. Boundary with T44 on rendering — item 1.
