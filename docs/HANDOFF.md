# Handoff: CellLoadResearch 0.8.31

State as of 04.11.2026. This document exists so work can be continued in a new chat without reading the previous conversation. Rules, environment, and the final mod requirements (5.14) — `CLAUDE.md`.

Reliability tags: **verified in game** — confirmed by the user or a trace; **from game code** — found in machine code, not checked in game; **assumption** — not verified by anything.

## 1. Where we are now

**Project.** An SKSE plugin for Skyrim AE 1.6.1170 on CommonLibSSE-NG v13.1.0. The Bannered Mare tavern stays alive while the player is on the street and is visible through its door, as if through an opening (portal: the interior is rendered by a second camera into a texture in place of the door).

**User's goal** (NoLoads): door transitions without a loading screen. The intermediate goal is a "full-featured portal" (`docs/FEATURES.md`, 5.13): light, sound, projectiles, and NPCs and the player pass through the door.

**Works in game (0.8.31):**
- the interior is visible through the door from anywhere on the street, updates every frame; it has its own color correction (T12): up close, fire, light, and brightness look as if from inside;
- NPCs inside walk around, are animated, lit, and have full AI; they are held in high on transitions, no popping (T11);
- the bard's song and subtitles play outside; the song does not cut off on exit;
- the player's torch light passes inside; the interior's sound is audible outside (walls not accounted for);
- the interior is attached as a neighboring cell (`kAttached`); entering it costs 70 ms of loading screen;
- entering and exiting do not crash the game, no hangs, F11 is safe (since 0.8.29);
- compatible with Engine Fixes 7.0.20 (run 0.8.31).

**Does not work / open:**
- the bard's song cuts off 1–2 s after entering: `BardSongStarter` on `CLOC`. The fix is a Story Manager filter, task T16;
- NPCs change direction of movement on entering (T20); Idolaf outside is teleported inside on the first entry: the street is not held (T21);
- the portal image is brighter and grey-hazed in the distance; smoke disappears when the hearth is not visible through the portal (T17);
- the portal lands on the nearest door at every launch (a portal registry is needed, T29);
- with Engine Fixes, new saves do not appear in the load list (assumption: its `bEnableAchievementsWithMods`, a `0` flag in the save name instead of `1`; no control run without our plugin was done, `docs/FINDINGS.md`, "Run 0.8.31");
- not done: light in both directions, view from inside out, projectiles, player passage without loading, autonomy (section 5), README.

**Stage 0 (0.8.26):** short `CLAUDE.md` and `HANDOFF.md`, `tools/run_digest.py`, `Portal.cpp` split into `Portal*.cpp`.

**Stage 1 closed (0.8.27–0.8.31).** Conclusions — `docs/FINDINGS.md` ("Run 0.8.27" … "Run 0.8.31"), results — `docs/tasks/results/T10.md`–`T15.md`.
- T10 + T11: holding the portal's NPCs in high (`ActorHold`), the song cutting off on exit fixed.
- T12 + T13: portal color correction (`PortalGrade.cpp`), interior fog from the door plane, quad model 1. The 0.8.28 hang on F11 — iterating `renderTargets` past the real targets (`CLAUDE.md`, section 7).
- T14: we do not take technical mods as dependencies; Engine Fixes — compatibility only (5.14, item 8).
- T15: location change — only via `PlayerCharacter::SetParentCell`; rule R8 without a FormID for T16 (`docs/LOCATION_TRIGGERS.md`).

## 2. Nearest plan

Work proceeds as tasks for separate chats: `docs/tasks/README.md` (stages, briefs, branches, rules). Results — `docs/tasks/results/`.

**Stage 2 — mod foundation, transitions without loading:** T28 (ready, 0.8.32) → T20, T16, T32 (rewrite of player reads per T31 results) → T22 (after T32) → T29 (one) → T21; T30 and T31 (research) are ready. **Stage 3 — improvements:** T17, T23, T24, T25, T26, T27. Briefs T16, T17, T20, T22, T29, T32 are ready, T21 — after T20 and T29.

**Stage 2 support tasks — Ghidra base and engine maps** (`docs/tasks/PLAN-T34-T41.md`, 07.11.2026). Ready at the base curator's: T35 (MCP `ghidra_*`, skill, `re-reader`/`re-reader-fast` agents), T36 (NG types), T34 (names, including 167 `reviewed`). T37 (Creation Kit) is **deferred** (user, 08.11.2026): not worth it just for names; "function → `.cpp` file" binding only if unclear ID clusters start blocking a task. On 08.11.2026, T41 (`docs/MULTIPLAYER_MODS.md`), T38 (`docs/PLAYER_MAP.md`), T39 (`docs/CELL_LOADING_MAP.md`) are ready, conclusions — `docs/FINDINGS.md`, "Researches T38, T39, T41"; bulk reading by their lists — `docs/tasks/results/T38-T39-batches.md` (all 14 batches ready, about 230 name candidates for the curator — there and in `results/T38.md`, `T39.md`, `T41.md`; the research chat coordinator — `docs/tasks/PROMPT-research-coordinator.md`). T40 (probe build) — a separate chat, one run for both; brief updated 08.11.2026: combined list of 151 IDs, questions Q1–Q3 (queueing loads in parallel with the main thread, double `Update` of the player, `DetachAll` not on main), branch from `exp/T32-player-reads` if T32 is not merged. Next code-less researches (briefs 08.11.2026, read code for a task and one step ahead of it): T42 — the "single instance" state (for T29, T21, gates), T43 — NPC passage through doors (for T20, T21, step 6), T44 — render map and pass with a camera parameter (for T17, T29, 5.14, items 4–5). On 09.11.2026 all three are ready (`SINGLE_INSTANCE.md`, `NPC_DOORS.md`, `RENDER_VIEW_MAP.md`; FINDINGS, "Researches T42, T43, T44"; batches — `results/T42-T44-batches.md`). The main points: gates are the engine's limit (one grid, one world, one Havok world for all exteriors); an attached interior is invisible to `TES::Pick`/`GetCell`/`Pathing` — interception is by the actor's cell; a seamless NPC passage is the vanilla door path (`SetPosition` + `MoveRefToNewSpace`) on the main thread, with the walker's repeated `ActivateDoor` suppressed; a nested render pass is impossible, views go one at a time, recursion "N−1"; our `WriteCall` at `+0x17E` must accept a foreign trampoline (mirrors). The main points for stage 2: all player transitions go through `AE#40438` → `AE#40744`; `SetParentCell` is the only writer of the player's location and the switcher of climate, grass, and camera collision; the `TES` cell buffer changes from job threads too (`PostProcess` → `AE#13434` → `AE#13721`).

**User decisions (04.11.2026):**
- T16: first R8 in game, then the tag policy (T30). The innkeeper's and tavern greetings (`WIGreeting`, `WITavern`) must stay; R8 suppresses them. On 05.11.2026 the T30 policy was accepted: only `BardSongStarter` is suppressed, and only while a song is playing; both policies in one T16 build, selected by an INI string;
- stage runs — without Engine Fixes; with it — a common build at the end of the stage;
- the save control with Engine Fixes (without our plugin, then with `bEnableAchievementsWithMods = false`) is not done yet: it will go into the Engine Fixes run at the end of the stage;
- 05.11.2026: T32 step 1 is supplemented with views of accesses to the player and a check of the player constructor (a second player object in a frame, "A + D"). The T32 executor waits for the user's command;
- 05.11.2026: Ghidra 12.2 is installed at `E:\Ghidra\ghidra_12.2_PUBLIC`, JDK 25 — `D:\Java`. The project, names, and pseudocode tools are task T33, a separate base-curator chat (`docs/tasks/T33-ghidra-base.md`). Do not commit the project or pseudocode to git.

**Git:** commits and merges are done by the user only (`CLAUDE.md`, beginning). Stage 1 worktrees are deleted.

## 3. Documents

| Purpose | File |
|---|---|
| Code structure, threads, hooks, tools, false leads, shortcomings, release cycle | `docs/ARCHITECTURE.md` |
| Modules, INI, portal by subsystems | `docs/FEATURES.md` |
| Engine map | `docs/ENGINE_MAP.md` |
| Original task, hypotheses, trace | `docs/ORIGINAL_TASK.md` |
| Version and run history | `docs/HISTORY.md` |
| Run conclusions | `docs/FINDINGS.md` |
| Game versions | `docs/PORTABILITY.md` |
| Distances and visibility | `docs/DISTANCE_CHECKS.md` |
| Similar and technical mods | `docs/RELATED_MODS.md` |
| Reactions to location change | `docs/LOCATION_TRIGGERS.md` |

## 4. "Full-featured portal" plan

The order was approved by the user. Steps are broken down into tasks in `docs/tasks/README.md`.

**Step 1. Distances through the door — T22.** Done: `lodDistance` of the controller (0.8.17), animated reference fade-out (0.8.19), NPC visibility, world camera, and `GetDistance` (0.8.24). Remaining (`docs/DISTANCE_CHECKS.md`, section 9): `AE#37277`; the eye point in `BSAnimationUpdateData` (`optionalEyePosition`, `AE#38054`); consumers of the camera `AE#36609` (`AE#37690`, `AE#38308`, `AE#43851`); processing level in `ProcessLists` instead of `PromoteActors`; distances for sound. Criterion: an NPC far from the door simplifies the same way as on the street at the same distance.

**Step 2. Light in both directions — T24.** Interior lamps light the street near the door, street lamps light the interior. Basis: `AddRestricted`, `MirrorPlayerLights`. The number of sources per figure is limited (`AE#107784`).

**Step 3. Sound through the opening — T23.** Since 0.8.24 the sound position is moved through the door, walls not accounted for. Attenuation through the opening is needed.

**Step 4. View from inside out — T29, T21.** A live exterior grid is needed while the player is inside (currently `kUnattached`, H12), plus a second portal. Before that, the portal state goes into a class and a registry (T29, `docs/ARCHITECTURE.md`, 8.3).

**Step 5. Projectiles — T25.** Intersection with the door rectangle, reference transfer with position and velocity transform, body in the other Havok world. A hidden door keeps its collision.

**Step 6. NPCs and the player without a loading screen — T20 and beyond.** Foundation: entering the attached interior takes 70 ms, fades are removed by settings (`docs/FAST_DOOR_TRANSITIONS.md`), a 0.65 s pause before the fade. The transition must call the player's `SetParentCell` exactly once (T15).

## 5. What autonomy requires

The portal is not autonomous yet; that is separate work outside steps 1–6:
- activated manually (F8), if `iStartStage=1` is not set;
- one interior per session: `sInteriorCell` in the INI;
- one door: `NearestDoorTo` at launch. For an interior with two exterior doors, the portal moves to the nearest one at every launch;
- no rule on which interiors to keep and when to release them.

Proposed order: portal registry (T29), autostart for one interior, interior selection by the nearest door, then all doors.
