# Tasks for separate chats

Work since 03.11.2026 is divided into tasks: **one chat — one task**, with its own brief, branch, and PR. The reason is context consumption:
- each chat receives all of `CLAUDE.md` (about 110 lines);
- the coordinator reads `docs/HANDOFF.md` first (about 90 lines);
- `src/Hooks.cpp` is 1,200 lines, `src/InteriorSim.cpp` is 1,000, and `docs/FINDINGS.md` is 1,300. After T02, the portal consists of `src/Portal*.cpp` files of 30–560 lines each, with shared state in `src/PortalState.h`.

A long chat quickly reaches context compaction, and details are lost. An executor chat knows only `CLAUDE.md`, this file, and its brief. The coordinator maintains the overall picture.

## Roles

- **Coordinator** — a separate chat. A new coordinator starts with `docs/HANDOFF.md`, this file, and `results/`. It:
  - writes briefs;
  - reads results and PRs;
  - decides the merge order;
  - transfers conclusions to `docs/FINDINGS.md`, `CLAUDE.md`, and `docs/HANDOFF.md`;
  - builds the combined test build.
- **Executor** — a new chat for one task. First message:
  `Complete task docs/tasks/T10-bard-song.md. Rules are in docs/tasks/README.md. Write the result to docs/tasks/results/T10.md.`

## Rules for the executor

1. **Do not read large documents in full:** `docs/ARCHITECTURE.md`, `docs/FEATURES.md`, `docs/FINDINGS.md`, `docs/INTERIOR_SIM_STAGES.md`, `docs/DISTANCE_CHECKS_APPENDIX.md`. Use Grep to search by ID (`AE#37618`) or heading and read the matching section.
2. **Read large source files in sections:** first Grep for the function name, then Read with `offset`/`limit`. Use Grep on `src/Portal*` to find portal code; see `docs/ARCHITECTURE.md`, 3.3, for which file handles what.
   - **Game machine code** — through `tools/ghidra/gq.py` (`docs/GHIDRA.md`), with `--out` directed to your scratchpad. If you need analysis, a name, or a type in the database, send a request to the database curator through the user. Do not put pseudocode in git or docs.
3. **Do not print traces line by line.** Write a short Python script that produces a summary (≤ 100 lines of output). After T01, `tools/run_digest.py` is available.
   - Cyrillic breaks in the Bash console: write output to a file in the scratchpad and read it with Read.
   - Keep scripts in the scratchpad, not in the repository, unless the brief says otherwise.
4. **Logs do not go in git.** A worktree has no `logs/` folder. `logs/run-…` paths in briefs are relative to the main folder: `E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\logs\`. A build writes logs to the `logs/` folder of the tree from which it was built.
5. **The user creates branches and worktrees** (the git rule in `CLAUDE.md`). Commands are in “Branch preparation” below. If a branch does not exist, provide the command and wait.
6. **Version.** If you provide a build for the game, increment the last digit in `xmake.lua` and state the version in the result. The coordinator will renumber versions when merging.
7. **Code and safety** — `CLAUDE.md`, section 7. **Game data must not be changed** — `CLAUDE.md`, 5.14, item 7: no edits to quests, scripts, forms, or `.esp`; only interception of engine handlers.
8. **Result** — `docs/tasks/results/T<nn>.md` following the template below, ≤ 80 lines.
   - Do not edit `FINDINGS.md`, `HANDOFF.md`, or `CLAUDE.md` unless the brief asks. The coordinator does that.
   - Still record new addresses that are hard-bound to a version in `docs/PORTABILITY.md`.
9. **At the end**, give the user commands in English: commit, push, and PR creation into `feature/interior-portal`. Include the line `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` in the commit.
   - The user does not have `gh` installed: the PR is created on GitHub using the link printed by `git push`.
10. **If a run is needed**, describe the verification steps in the result (≤ 6 items) and stop. Analyze the run in the same chat if enough context remains; otherwise use a new chat: brief + result.
11. **The task turned out larger than the brief** — complete the first finished part and describe the rest under “Open questions.”

## Result template

```markdown
# T<nn>. <title> — result

Branch: … · Build version: … · Status: research / ready to merge / awaiting run / rejected

## Findings
- fact — evidence (trace line, address, disassembler)

## Changes
- file — what and why

## How to verify in the game
1. …

## Open questions

## For the coordinator
- what to add to FINDINGS, CLAUDE.md, PORTABILITY
- merge conflicts
```

## Branches and merging

- **`feature/interior-portal`** — the shared branch into which task PRs are merged. The coordinator updates `main` at milestones.
- **`task/T<nn>-<word>`** — code that should be included.
- **`exp/T<nn>-<word>`** — an experiment. It may not be included or may be incompatible with others. The result file is always merged; code is merged at the coordinator's discretion.
- **Only one build is installed in the game at a time.** The coordinator merges compatible branches into `test/<date>` and provides them as one build: one run verifies several tasks. Incompatible branches are tested one at a time.
- **Order.** Stage 0 proceeds sequentially. Stage 1 code branches are created after T02 is merged; otherwise everything will conflict with the splitting of `Portal.cpp`. Research without code (logs, disassembler) can start immediately.

## Branch preparation

A chat without code works in the main folder. A chat with code works in its own worktree so that parallel builds do not interfere with each other. The first build in a new worktree recompiles CommonLib, which takes a long time. If the compiler runs out of heap during that build (T28: `Detectors.cpp`), run `xmake build -j 4 -y CellLoadResearch` once, then run `scripts/build.ps1` again.

```powershell
# stage 0, sequentially, in the main folder
git switch -c task/T00-slim-docs feature/interior-portal
# after merging T00:
git switch -c task/T01-run-digest feature/interior-portal
# after merging T01:
git switch -c task/T02-split feature/interior-portal

# stage 1, after merging T02: each task in its own worktree
git worktree add ..\CLR-T10 -b exp/T10-bard-song feature/interior-portal
git -C ..\CLR-T10 submodule update --init --recursive
```

Do the same for T11, T12, and T13 with their branch names. Remove the worktree after merging: `git worktree remove ..\CLR-T10`.

## Stages

| ID | Task | Code | Run | Depends on | Conflicts with |
|---|---|---|---|---|---|
| **Stage 0, sequential** |
| [T00](T00-slim-docs.md) | Shorten `CLAUDE.md` and `HANDOFF.md` | none | none | — | any documentation edits |
| [T01](T01-run-digest.md) | Run summary, branch in archive name and log | `tools/`, `build.ps1`, a little `src/` | none | — | — |
| [T02](T02-split.md) | Split `Portal.cpp`, remove dead code | all of `src/` | control run | T00 | everything with code |
| **Stage 1, parallel — closed (0.8.31)** |
| [T10](T10-bard-song.md) | Bard song cuts off during transition | hooks | yes | T02 (for code) | few |
| [T11](T11-npc-continuity.md) | NPCs jump during transition | `InteriorSim` | yes | T02 (for code) | T20 |
| [T12](T12-portal-color.md) | Separate color correction for the portal image | portal rendering | yes | T02 (for code) | T13 |
| [T13](T13-fire-smoke.md) | Fire is smaller and there is no smoke | portal particles | yes | T02 (for code) | T12 |
| [T14](T14-helper-mods.md) | Technical mods as a dependency or reference | none | based on results; user installs mods | — | — (`RELATED_MODS.md`) |
| [T15](T15-location-triggers.md) | Game and script reactions to player location changes (bard song on entry) | none | none | — | — |
| **Stage 2, mod foundation: transitions without loading** |
| [T28](T28-split-hooks.md) | Split `Hooks.cpp` according to the plan in `results/T02.md` | all of `Hooks.cpp` | control run | — | everything with hooks: **first and alone** |
| [T20](T20-attached-entry.md) | Entry without detaching the interior, recording NPC package changes | `InteriorSim`, hooks | yes | T28 | T16 (`SetParentCell`) |
| [T16](T16-story-filter.md) | Story Manager filter on portal entry: T30 tags and policy, R8 for comparison | `StoryFilter`, hooks | yes | T28, T30 | T20 |
| [T30](T30-location-tags.md) | Tags for location-change reactions and the “suppress or not” policy | none | none | — | — (`LOCATION_TRIGGERS.md`) |
| [T31](T31-virtual-player.md) | Virtual player beyond the portal instead of a virtual camera (research) | none | none | 0.8.32 run | — (complete: there will be no full virtual player, `VIRTUAL_PLAYER.md`) |
| [T33](T33-ghidra-base.md) | Ghidra database for `SkyrimSE.exe`: project, names from NG and documents, pseudocode tools for executors | `tools/ghidra/` | none | — | complete; chat is the database curator (`..\CLR-GHIDRA`) |
| [T32](T32-player-reads.md) | Inventory of player reads (2,843 locations) and the player's place in the far-side frame | `PlayerReads`, `ActorScope` | yes, in steps | T31; merge T20 earlier | T29 (`ActorScope`), T20 (`InteriorSim`) |
| [T22](T22-distance-rest.md) | Remaining distance checks (`docs/DISTANCE_CHECKS.md`, section 9), NPC level by distance through the door | hooks, `PortalActors` | yes | T28 | T20 (`InteriorSim`, `ActorHold`), T29 |
| [T29](T29-portal-registry.md) | Portal registry: portal state as a class, many portals (5.14, item 1; `docs/ARCHITECTURE.md`, 8.3) | `Portal*`, `InteriorSim` | control run | T20, T16, T22 merged | everything in `Portal*`: **alone, like T28** |
| T21 | Living street while the player is inside, and a second portal on the interior door (`docs/FEATURES.md`, 5.13, step 4) | `InteriorSim`, `Portal*` | yes | T20, T29 | T29 |
| **Stage 2, supporting: Ghidra database and engine maps** (plan — `PLAN-T34-T41.md`, research coordinator — `PROMPT-research-coordinator.md`) |
| [T35](T35-ghidra-interface.md) | Database interface: MCP server, `ghidra` skill, `re-reader`, `re-reader-fast` agents | `tools/ghidra/`, `.mcp.json`, `.claude/` | none | T33 | complete (curator) |
| [T36](T36-ghidra-types.md) | NG types in the database: structures, vtables, `this`, singletons | `tools/ghidra/` | none | T35 | complete (curator) |
| [T34](T34-ghidra-check-names.md) | Check discrepancies, names: NG, third-party tables, RTTI, Papyrus, commands, `reviewed` | `tools/ghidra/` | none | T35, T36 | complete (curator); new candidates from T37–T41 |
| [T37](T37-creation-kit.md) | Creation Kit: whether it is worthwhile for names | none | none | T35 | **postponed** (user, 08.11.2026): not worthwhile for names; create the “function → `.cpp`” mapping only if unclear clusters of IDs begin to hinder a task |
| [T41](T41-multiplayer-mods.md) | Multiplayer mods: multiple players in one world | none | none | — | — (`MULTIPLAYER_MODS.md`) |
| [T38](T38-player-map.md) | Player map: `PlayerCharacter`, fields, globals, subsystems | none | optional, T40 | T35, T36 | overlaps T32 in scope (`PLAYER_MAP.md`) |
| [T39](T39-cell-loading-map.md) | Cell loading map: from door to `kAttached`, threads, loading screen | none | old traces; optional T40 | T35, T36 | overlaps T20 in scope (`CELL_LOADING_MAP.md`) |
| [T40](T40-call-probe.md) | Probe build: call counters, samples of all threads; questions Q1–Q3 from T38/T39 | `src/CallProbe.*`, `src/CodePatch.*`, `Profiler` | yes, one for T38+T39 | ID lists ready (151); branch from T32 if not merged | T32 (stubs), per line — T16, T20 |
| [T42](T42-single-instance.md) | Inventory of “single-instance” engine state: who writes, who reads, what to replace it with (5.14, items 1–2) | none | optional, T40 | T38, T39 | complete 09.11.2026 (`SINGLE_INSTANCE.md`); overlaps T29, T21 in scope |
| [T43](T43-npc-doors.md) | How NPCs pass through doors with loading; components of seamless NPC passage | none | optional, T40 | T38, T39 | complete 09.11.2026 (`NPC_DOORS.md`); overlaps T20, T21, T11 in scope |
| [T44](T44-render-view.md) | Frame rendering map: view state, the game's secondary views, pass with camera parameter, mirrors (5.14, items 4–5) | none | none | — | complete 09.11.2026 (`RENDER_VIEW_MAP.md`); overlaps T17, T29 in scope |
| **Stage 3, improvements and additions** |
| [T17](T17-portal-picture.md) | Portal image: haze at a distance, smoke without a hearth in frame, adaptation support (remaining T12, T13) | portal rendering | yes | — | T27 |
| T23 | Sound through the opening, not through walls (Audio Occlusion in `docs/RELATED_MODS.md`) | sound | yes | T29 | — |
| T24 | Light in both directions (`docs/FEATURES.md`, 5.13, step 2) | `PortalLights` | yes | T21 | T27 |
| T25 | Projectiles through the door (5.13, step 5; STB Projectile Control in `docs/RELATED_MODS.md`) | hooks | yes | T29 | — |
| T26 | SE 1.5.97 (`docs/PORTABILITY.md`) | none, research | none | — | — |
| T27 | Any camera: camera and pass target as parameters, texture pool, recursion, mirrors (5.14, items 4–5) | portal rendering | yes | T29 | T17, T24 |

**Stage 2** is what a world without loading cannot exist without: entry and exit without detaching the interior, correct game reactions on entry, distances through the door, multiple portals, and a living street visible from inside. **Stage 3** is quality and completeness: image, sound, light, projectiles, other game versions, and any camera.

- **T28 first and alone.** All Stage 2 tasks add hooks to `Hooks.cpp` (1,200 lines). Splitting the file removes conflicts during parallel work. The control run must match 0.8.31.
- **T29 before T21:** a second portal on the interior door requires multiple simultaneous portals. T27 retains camera, recursion, and mirrors.
- **T29 runs alone**, after T20, T16, and T22 are merged: it moves state throughout `Portal*` and `InteriorSim`, just as T28 does for hooks.
- **T31 is complete** (user proposal dated 05.11.2026): there will be no full virtual player; substituting the player's location in the far-side frame is preserved. **T32 now, T22 after it** (user decision dated 05.11.2026).
- **T20 and T16 together:** seamless entry must call `PlayerCharacter::SetParentCell` exactly once per transition (T15), and T16 hooks that exact function.
- **T30 is research without code** (complete). The user accepted the T30 policy on 05.11.2026: T16 implements it immediately, with R8 available for comparison via an INI line.
- **Stage 2 runs are without Engine Fixes**; a combined build with it comes at the end of the stage (user decision dated 04.11.2026).
- The coordinator writes the T21 brief based on the results of T20, T29, and T31. T16, T17, T20, T22, T28, T29, T30, T31 are ready. Task numbers do not change when moving between stages.

```powershell
# stage 2: T28 and T30 immediately, other code tasks after T28 is merged
git worktree add ..\CLR-T28 -b task/T28-split-hooks feature/interior-portal
git -C ..\CLR-T28 submodule update --init --recursive
git worktree add ..\CLR-T30 -b exp/T30-location-tags feature/interior-portal
```
