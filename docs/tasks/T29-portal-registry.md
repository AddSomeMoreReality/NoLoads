# T29. Portal registry: portal state as a class, many portals

Stage 2. Branch `task/T29-portal-registry`, worktree `..\CLR-T29`. Code: `src/Portal*`, `src/InteriorSim.*`, portal hooks. Run: control run. Start **after T20, T16 (first iteration), and T22 are merged**: like T28, this is a move throughout the portal code and runs alone. Needed by T21 (second portal on the interior door) and autonomy (`docs/HANDOFF.md`, section 5).

## What is known

- **Portal state** — about 90 `inline` variables in `src/PortalState.h` (`g_target`, `g_camera`, `g_accumulator`, `g_door`, `g_lights`, `g_interiorActors`, counters…). Hooks in game jobs read the atomic `g_space` (`Space`: cell, 3D, `toInterior`/`toExterior`, boundary). `Space` is never freed; there is one object per launch.
- **Connection state in `InteriorSim.cpp`** — about 60 globals: attached cell (`g_attached`, `g_attaching`, `g_attached3D`, `g_attachRoot`), hidden doors (`g_hidden`, `g_host`, `g_hostRoot`), interior Havok world, hold (`g_heldCell`), counters. Process-wide: `g_worldLock`, `g_attachLock`, `g_playerWorld`, transition (`g_transition`), stage.
- **One interior** `[Preload] sInteriorCell`; the door is `Detectors::NearestDoorTo` on every launch. As a result, the portal switches to the tavern's other door (run 0.8.27).
- **`ActorHold`** (T11) already holds up to 16 cells, one bit per portal.
- `docs/ARCHITECTURE.md`, 8.3: “gather state into a class, leave only the registry global”; “the transformation zone is one-sided and dissolved in `Portal`” (`PortalSpace`).

- **Research from 09.11.2026:** `docs/SINGLE_INSTANCE.md` (sections 4–6: “current location” helpers `TES::Pick`/`GetCell`/`Pathing` do not see the attached interior — hook by actor cell; what to keep per portal), `docs/RENDER_VIEW_MAP.md`, 5 (what to keep per “portal + view” pair, what to restore after the pass; recursion “N−1”; our `WriteCall` at `+0x17E` must accept a foreign trampoline — mirrors, 6).

## Tasks

1. **Portal class.** The state of one portal becomes an object (for example, `Portal::Instance`). Divide it internally into:
   - **connection**: door pair (FormIDs of both door references are the portal key), two sides, transformations in both directions, door rectangle. Side names are neutral (`near`/`far` or `a`/`b`), without “street/interior” in new code (5.14, item 2);
   - **view**: everything that depends on camera and target: pass camera, render target, pyramid, visible part of the door, `g_eyeDistance`, particle cameras. A portal currently has one view, but T27 will have many (“portal + view” pair, 5.14, item 4). A view does not read the global player camera: the camera is passed to it;
   - **diagnostics**: status counters.
   Shared resources (`PortalGrade` shaders, quad material) remain shared.
2. **Registry.** A fixed array, for example `kMaxPortals = 16` (as in `ActorHold`). For hooks on foreign threads — atomic pointers to published immutable `Space` objects by slot and an occupied-slot mask. Readers are lock-free, like `g_space` now. `IntoInterior`, `DistanceThroughDoor`, `SoundThroughDoor`, `InInterior` find the portal by object or cell and iterate only occupied slots.
3. **`InteriorSim`**: connection state (attached cell, root, hidden doors, Havok world, hold) goes into a per-portal object. Shared locks and transition remain shared.
4. **No behavior changes.** The game still has one portal using `sInteriorCell` and `NearestDoorTo`. New capabilities (second portal, pinning a door) belong to T21 and autonomy, not here.
5. **Trace and status.** The portal status format is preserved: `tools/run_digest.py` and `tools/analyze_trace.py` work without changes. If the format changes, update the tools in this same task. Add `portals` (number of occupied slots) and the portal key to the status.
6. **Documents.** `docs/ARCHITECTURE.md`: 3.3 (portal files), 3.5 (threads), 8.3 (lines “about 80 globals” and “one instance” — done).
7. **If the task exceeds the brief** (README rule 11): the first completed part is the class and registry in `Portal*` with a control run. `InteriorSim` is the second part.

## Safety

- `CLAUDE.md`, section 7: do not retain exterior-cell `TESObjectCELL*` pointers beyond a call — the portal object stores only FormIDs, coordinates, and persistent forms. Interiors and worlds are persistent.
- Do not free objects read by game jobs while a job may still hold them. `Space` is currently never freed, which is acceptable (8.2). A slot pool is also acceptable.
- No locks in hooks. Iterate the registry only through occupied slots.
- Build: zero errors and warnings from `src/`. Verify the move with a script, as in T02 and T28: function and hook lists before and after match.

## How to verify in the game (control run, without Engine Fixes)

1. T28 control-run scenario: tavern, F8, 2–3 entries and exits, F2, F11.
2. Additionally: move 2–3 cells away from the tavern and return; F9 (quick load) outside. The game does not crash, and the portal returns.
3. The summary matches the T28 control (`logs/run-0.8.32/digest.md`): passes, sound and distance transfer, NPC holding, jumps, `frame.hitch`.

## Conflicts

All of `src/Portal*`, `src/InteriorSim.*`, portal hooks. With T21 (which builds on this class), T27, T17, T23–T25.
