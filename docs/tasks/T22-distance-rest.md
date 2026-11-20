# T22. Remaining distance checks through the door

Stage 2. Branch `exp/T22-distance-rest`, worktree `..\CLR-T22`. Code: own hook module (the T28 rule in `Hooks.h`), `PortalActors.cpp`. Run: yes. Start **after T32** (the census of player reads and the player's place in the frame; user's decision of 05.11.2026): T22 takes its dump and frame and does what T32 does not cover (line of sight, camera, processing level). Step 1 of the "full portal" plan (`docs/HANDOFF.md`, section 4).

## What is known

- `docs/DISTANCE_CHECKS.md` (sections 4–9; full list — `_APPENDIX`, read via Grep by ID). Moved by 0.8.24: `kNotVisible`, world camera `AE#36609` in the actor update frame (`Portal::ActorScope`), `GetDistance`/square (`AE#19823`, `AE#19824`), sound location (`AE#68007`), subtitles, controller LOD (`AE#78274`), fading (`AE#105486`).
- **Right now interior NPCs get everything in full wherever the player stands:** the truncated update `AE#37277` is bypassed (0.8.14), `InteriorSim` promotes everyone to high once per second (`PromoteActors`), `ActorHold` (T11) keeps them high on transitions (detour `AE#37503`).
- **Remaining** (`docs/HANDOFF.md`, section 4, step 1; `DISTANCE_CHECKS.md`, section 9):
  - `AE#37277` — distance-based truncated update, not "always full";
  - the eye point in `BSAnimationUpdateData` (`optionalEyePosition`, `AE#38054`);
  - consumers of world camera `AE#36609` outside the actor frame: `AE#37690` (hit), `AE#38308` (navmesh, disabled in vanilla), `AE#43851` (explosions — step 5, record only);
  - processing level via `ProcessLists` (`AE#37503` computes the required level) instead of `PromoteActors`;
  - distances for sound (attenuation along the path through the door; muffling by walls — T23);
  - from F3 data (`logs/run-0.8.24/CellLoadResearch.access-*.txt`): `AE#18889 + 0x1BB`, `AE#36661 + 0xFE`, then `AE#41381`, `AE#41246`, `AE#37286`, `AE#37361`;
  - **NPC player detection behind the portal** (08.11.2026, `docs/FINDINGS.md`, "T38, T39, T41 research"): `DoDetectionJob` (`AE#42742`) measures distance not via `GetDistance`, but with the pair `AE#19784`/`AE#18517` (`+0x4E0…+0x520`), with different cells — `FLT_MAX`. Our hook `AE#19784` works only in subtitles. Skyrim Together disables detection in `AE#42704` (`DoDetectionJob+0x6D2`) — a point for comparison. Direct reads of the player position by AI — `results/T38-T39-batches.md`, "T38, batch 3".
- **Criterion:** NPCs far from the door simplify the same way as street NPCs at the same distance from the player.

- **LOS via `TES::Pick`** (`AE#13371`): takes `interiorCell` or the grid center and does not see the attached interior; called by quests too (`docs/SINGLE_INSTANCE.md`, 4).

## To do

1. **Research** (dumpbin, `tools/name_functions.py`, F3 data 0.8.24). For each place: what it does with the player position or camera, whether the portal needs it, how to transplant. Table in the result.
2. **Distance through the door — one function.** "Distance from the observer to an object on the other side of the portal" = minimum over the portals that connect the sides (5.14, items 1–2), from the eye transplanted through the door. The observer is a parameter (player, camera), not the global player camera (item 4). There is one portal now, the registry is T29: the function takes a portal or iterates what `Portal` provides.
3. **Distance-based processing level.** `AE#37503` for a portal actor computes the level by distance through the door, as for the street. `PromoteActors` and `AE#37277` stop giving everyone the full level. T11 hold on transitions is preserved: on the loading screen and for 10 s after the transition NPCs stay high if they were high.
4. Other places — based on the research. Each fix — under an INI switch (`[Portal]` or `[Distance]`), enabled by default; 0 = 0.8.31 behavior.
5. **Trace:** in the portal status — distribution of NPCs by level and by distance through the door, number of truncated updates. For comparison with the street — the same for street NPCs within radius 4096.

## Safety

- Hooks in game jobs: only the atomic state of the portal, no locks (`CLAUDE.md`, section 7). Bypass — via `ActorHold` or the portal's `Space`.
- `docs/DISTANCE_CHECKS.md`, section 6: "infinitely far" → "nearby" changes AI logic. Record in the trace who came closer than 1000 units through the door.
- New addresses — into `docs/PORTABILITY.md`.

## How to verify in the game

1. F8 enabled. At the tavern door: NPCs inside walk, animated, as in 0.8.31.
2. Step away from the door to 2000, 4000, 6000 units (≈ 30, 60, 90 m), stand 20 s each, F11 not needed. In the trace the levels of interior NPCs change with distance the same as street NPCs at the same distance.
3. Return to the door: NPCs are high again, no jumps (`actor.jump`, `actor.snap`).
4. Enter and exit twice: no jumps, the song on exit does not break off (T10/T11 not broken).
5. Control: T22 switches at 0 — 0.8.31 behavior.

## Conflicts

`InteriorSim.cpp` (`PromoteActors`), `ActorHold.cpp` (`AE#37503`) — with T20. `PortalActors.cpp` — with T29. Merge before T29.
