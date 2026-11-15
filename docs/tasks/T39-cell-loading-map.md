# T39. Cell loading map in code: from the door to `kAttached`

Stage 2, research. Branch `exp/T39-cell-map`, worktree `..\CLR-T39` (without the submodule). Code: none, scripts in the scratchpad. Run: old traces first, then optionally the T40 build. Reads the base via T35, better after T36. Based on `docs/ENGINE_MAP.md` (section 3), `FINDINGS.md` (Grep by `AE#`), `src/HooksCells.cpp`, `HooksTes.cpp`, `Preload.cpp`.

## Why

A world without loading (5.14) means cell loading without a loading screen and without detaching: for any cell pairs (item 2), for many portals (item 1). Right now the knowledge is scattered across documents and traces. A single map with addresses, names, and threads is needed.

## To do

1. **Entry points:**
   - door activation with loading (`TESObjectDOOR::Activate` → teleport);
   - `coc`/`MoveTo`, fast travel;
   - grid shift while walking (exterior), world change (city gates);
   - save loading.
   - For each — the call path to `TES::SetCurrentCell` (`AE#13316`) or to the grid shift.
2. **Main thread:**
   - `SetCurrentCell` and the grid shift;
   - attach and detach (`AE#19130`, `AE#19063`, `AE#18976`, `AE#13435`);
   - the `interiorBuffer`/`exteriorBuffer` buffers and their cleanup (`PurgeBufferedCells`);
   - `LoadedAreaBound`, `bhkWorld`, `ProcessLists`, `ShadowSceneNode::SetPortalGraph`;
   - which of this is interior-only, exterior-only, world-change-only.
3. **Loading threads:**
   - `CellLoaderTask::Run` and other `IOManager` tasks (`AE#75708`);
   - models (`QueuedFile`), Havok, navmesh, `BGSGrassManager`;
   - who queues tasks, who waits and where;
   - cell states (`kUnloaded` … `kAttached`) and where they change;
   - events `CellFullyLoaded`, `TESCellAttachDetachEvent`, `BGSActorCellEvent`.
4. **Loading screen:** who opens the `Loading Menu`, what runs only under it, what of that is mandatory for a seamless transition. Check against `ORIGINAL_TASK.md` (H1–H16).
5. **Old traces:** `profile` records in `logs/run-*` of the main folder. Convert stack addresses to base names via `gq.py info`/MCP, not `name_functions.py`. Does the map match what actually ran.
6. **Our hooks:** where the plugin's hooks (`Hooks*.cpp`) stand on the map, what they change.
7. **Name candidates** for the curator.
8. **Optionally — T40 run:** which map functions work on each transition phase, on which thread.

## Result

- `docs/CELL_LOADING_MAP.md` (≤ 300 lines: call tree with `AE#`, names, and threads, state table, no pseudocode). Do not edit `ENGINE_MAP.md` — propose links to the coordinator.
- `docs/tasks/results/T39.md`.

## Conflicts

T20 (entry without detaching) — by meaning: T39 provides the map, T20 uses it. Files — new.
