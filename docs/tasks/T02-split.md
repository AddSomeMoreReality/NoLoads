# T02. Split `Portal.cpp` and remove dead code

Branch `task/T02-split`. Code: all of `src/`. A control game run is required: behavior must not change. Comes after T00. Do not start other code branches until T02 is merged.

## Why

`src/Portal.cpp` is 3,300 lines and 147 KB. A chat that needs part of the portal spends a lot of context reading it, and parallel branches conflict in the same file. Disabled experiments also remain in the code.

## Tasks

1. **Remove dead code.** First provide a list with evidence in the result (why it is dead: disabled in INI, no callers), then remove it. Candidates:
   - automated-test settings: `[AutoTest]` in `Settings.*` and INI, F10 key. The automated test itself is absent from the code and was removed from the requirements on 01.11.2026;
   - windows in `Detectors`: `bWindows` and everything that searches for windows. Keep door search: `NearestDoorTo` is needed by `Preload` and `InteriorSim`;
   - exterior in `Preload`: `sExteriorCell`, `iExteriorRadius`;
   - remnants of `InteriorSim` stages 1–5 (archive — `docs/INTERIOR_SIM_STAGES.md`);
   - portal pass immediately before `RenderWorld` (`bPassAtSceneListFence=0`), depth modes 0 and 1 (`iDepthMode`). First verify that the working path does not depend on them;
   - soft-particle toggle `bSoftEffects`: run 0.8.24 showed that they have no effect. Keep the default behavior;
   - detour `AE#41466` (`bGameAnimatesActors`). Candidate list only: the coordinator decides.
2. **Split `Portal.cpp`** by responsibility, moving code as-is without changing logic. Approximately as follows:

   | File | Contents |
   |---|---|
   | `Portal.cpp` | lifecycle (`Start`/`Stop`/`Tick`), transformation through the door |
   | `PortalRender.cpp` | pass: render targets, state, accumulator, depth, ImageSpace |
   | `PortalLights.cpp` | lamp copies, player light, NPC light |
   | `PortalActors.cpp` | NPCs: visibility, animation, world camera, distances |
   | `PortalEffects.cpp` | particles, controllers, fade, budget, master systems |
   | `PortalAudio.cpp` | sound and subtitles |
   | `PortalStatus.cpp` | status and JSON for the trace |

   - Shared state from the anonymous namespace moves into the internal header `PortalState.h` (`namespace Portal::detail`).
   - No file larger than ~800 lines.
   - `xmake.lua` builds `src/**.cpp`; no files need to be added.
3. **`Hooks.cpp`** (1,200 lines) — split by subsystem if straightforward. Otherwise describe a plan in the result.
4. **Build without warnings.** Two commits: removal, then splitting. Provide commands for the user.
5. **Control game run** (steps for the user):
   - Whiterun tavern, F8, look through the portal;
   - enter and exit twice;
   - F2, F11.

   Compare the summary (`tools/run_digest.py`, T01) with `logs/run-0.8.25/`: counters must be of the same order of magnitude.

## Not allowed

- Changing behavior. Put any bugs found under “Open Questions”; do not fix them.
