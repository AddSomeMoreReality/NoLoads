# T28. Split `Hooks.cpp` without changing behavior

Stage 2, **first and alone**: T16, T20, T22, T29 add hooks, and they conflict while `Hooks.cpp` remains whole. Branch `task/T28-split-hooks`, worktree `..\CLR-T28`. Code: `src/Hooks*`, small changes to `ActorHold.cpp`, `Scenes.cpp`. Run: control run; the summary must match 0.8.30-b / 0.8.31.

## What is known

- `src/Hooks.cpp` is 1,243 lines with about 54 hooks: 28 vfuncs in `Hooks::Install`, 21 detours in one transaction in `InstallDetours`, and 5 `write_call` hooks in `InstallCalls`. There is almost no logic: a hook counts or calls a module.
- Plan from `docs/tasks/results/T02.md` (“Open questions”):
  - `HooksCommon.h` — `WriteVfunc`, `AttachDetour`, `WriteCall`, `HookScope`, `AddTesState` (and `AddPlayerPos`);
  - `HooksCells.cpp` — door, `GridCellArray`, `CellLoaderTask`, `SetCurrentCell`, buffers, unloading, `QueueCellLoad`, `MoveTo`, Havok;
  - `HooksInteriorSim.cpp` — `BhkWorldStep`, `UpdateCells`, `UpdateAnimatedRefs`, `CellDetach`, `SetPortalGraph`, `OnVisible`;
  - `HooksPortal.cpp` — effects, particles, scene list boundary, `PlayerView`, subtitles, sound, NPC hooks (world camera, distances, controller LOD, fading, `HighActorCuller`, feet);
  - each file contains `InstallVfuncs()`, `AttachDetours(results&)`, `InstallCalls()`; `Hooks.cpp` retains **one** detour transaction, `Ready()`, and `BufferInterior`.
- `ActorHold.cpp` and `Scenes.cpp` install their own hooks, each in its own transaction. `ActorHold.cpp` has its own copy of `WriteCall` (without logging).

## Tasks

1. **Split according to the plan.** Move hook code as is: `thunk` bodies, comments, IDs, offsets, vfunc indices.
   - If a file exceeds ~500 lines (`HooksCells.cpp`), split it, for example, into `HooksCells.cpp` (door, grid, `CellLoaderTask`) and `HooksTes.cpp` (`TES` functions, Havok, unloading).
   - Hook structures go in their file's anonymous namespace. Expose only installation functions (`namespace Hooks::Cells`, etc.), declared in `HooksCommon.h` or their own header.
   - `PlayerUpdate` (frame counter and `MainThread::Drain`) remains in `Hooks.cpp`.
   - `g_detoursReady`, `g_callsReady`, and the meaning of `Ready()` do not change: `g_callsReady = lod && fade`.
2. **Shared helpers go in `HooksCommon.h`.** One copy of `WriteCall` for everyone: `ActorHold.cpp` takes it from there. If error messages differ, keep the variant that writes to the log. Add a helper for a module's separate transaction following `ActorHold::Install` and `Scenes::Install`: begin, add a named detour, commit with error logging. Their transactions remain separate.
3. **Rule for new modules** (T16 `StoryFilter`, T20, T22). A module's hooks reside in its `.cpp` and are installed by `Module::Install()`, following `ActorHold` and `Scenes`. One call line is added to `Hooks::Install`. Record the rule briefly in a comment in `Hooks.h` and in `docs/ARCHITECTURE.md`, 3.6.
4. **Log.** Lines `vfunc hooks installed`, `detours installed`, `call hooks installed: controller LOD …, fade …, pass at the scene list fence …, player view …, subtitles …` — same text and same values. Installation order within groups may change.
5. **Verify the move** (script in the scratchpad, as in T02):
   - multiset of nonempty lines in `HEAD:src/Hooks.cpp` versus the new files; differences only in `#include`, `namespace`, declarations, and installation;
   - hook lists before and after match: (vtable, index) pairs for `WriteVfunc`, IDs for `AttachDetour`, (function, offset, target) for `WriteCall`. Output both tables in the result as one line, “matched: N vfunc, N detour, N call.”
6. **Documents:** `docs/ARCHITECTURE.md` — the `Hooks.cpp` line in 3.1, number and location of hooks in 3.6, line in 8.3 (done). Do not touch other documents.
7. **Version** 0.8.32 in `xmake.lua`. Build with `scripts/build.ps1`: zero errors and warnings from `src/`, `tar -tf` of the archive as in `CLAUDE.md`, section 2.

## Safety

- No new hooks, addresses, or logic changes. If you find a bug along the way, record it under “Open questions”; do not fix it.
- `CLAUDE.md`, section 7: `CellLoaderTask::Run` hooks run on IO threads, no locks in hooks — add nothing while moving them.
- The first build in a new worktree recompiles CommonLib (slow): run `build.ps1` in the background.

## How to verify in the game (control run, without Engine Fixes)

1. Vortex: `CellLoadResearch-0.8.32-T28-split-hooks.zip` (Replace), Deploy. Engine Fixes disabled.
2. Save at the Prancing Mare, F8, look through the portal; enter and exit 2–3 times; press F2 once and F11 once.
3. In the log: `build: branch task/T28-split-hooks`, three installation lines as in item 4, `actor hold: …`, `scene trace: …`, no errors.
4. Move logs from `..\CLR-T28\logs\` to `logs\run-0.8.32\` in the main folder, then run `python tools/run_digest.py logs/run-0.8.32`.
5. Compare `digest.md` with `logs/run-0.8.30-b/digest.md` (without Engine Fixes, same quad model): `portal.passes`, `soundsCarried`, `distancesCarried`, `imageSpace.swaps`, `actor.keptHigh` during transitions, jumps, `frame.hitch` — same order of magnitude, no hangs.

## Conflicts

With everything that touches `src/Hooks.cpp`. Other Stage 2 code tasks start after T28 is merged.
