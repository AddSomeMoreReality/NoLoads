# T40. Probe build: which engine functions run, when, and on which thread

Stage 2, a tool for T38 and T39. Branch `task/T40-call-probe`, worktree `..\CLR-T40` with the submodule. Code: a new module `src/CallProbe.*` and one line in `Hooks::Install` (rule T28 in `Hooks.h`), extension of `Profiler`. Run: yes, one shared run for T38 and T39. Does not change game behavior. Model — Opus 5.5, effort — high. Brief updated 2026-11-08 based on the results of T38, T39, and mass reading.

## Why

T38 and T39 built maps from code (`docs/PLAYER_MAP.md`, `docs/CELL_LOADING_MAP.md`). Three questions cannot be answered by code alone, and T20, T21, and T29 depend on them. The probe answers them and also counts what runs in each phase of the transition.

- **Q1. Do loading jobs run concurrently with the main thread.**
  - `CellLoaderTask::PostProcess` (`AE#18529`) → `TES::BufferCell` (`AE#13434`) → unload `AE#13721` run on `BSJobs` threads too (`Job_IO`, `AE#39084` → `AE#36565` → `AE#75705`).
  - Frame `Main::Update` (`AE#36564`): `SetJobTaskEvent(1…4)` (`AE#69514`) at `+0x82F…+0x859`, `WaitForJobTask(0,1)` (`AE#69516`) at `+0x8B3`, only if `AE#35406(1)`.
  - We need to know what the main thread is doing at the moment these functions are called from another thread. The answer determines whether our `AE#13721` detour is safe: currently it decides based on the main thread's state.
- **Q2. Is the player updated twice per frame.**
  - Main thread: `Main::Update+0x2A7` → `AE#36581` → vfunc `0xAD` (`PlayerCharacter::Update`).
  - Job: `UpdateActor` (`AE#41448`) → `AE#41369+0x2A6` → the same slot `0xAD`.
  - Count calls to `AE#41369` where `this == [AE#403521]`.
- **Q3. Where in old traces do 44 calls to `DetachAll` (from `AE#13295+0x64`) off the main thread come from.**
  - By code, savegame loading runs entirely on the main thread (`results/T38-T39-batches.md`, "T39, batch 1"). Another thread occurs only once — at startup (`InitTESThread`, `AE#36644`).
  - There are two hypotheses:
    - the old trace determined the main thread incorrectly: `Trace::IsMainThread` takes `RE::Main::threadID`;
    - those calls happened on exit to menu or out of the game (`docs/FINDINGS.md`, the line with "exit from the game (`DetachAll` from `AE#13295`)").

## What we have

- `Profiler` — samples only the main thread and only under the loading screen; phases `open→pre→enter→post→finish→close` (`Profiler.h`).
- **T32 step 1** (`exp/T32-player-reads`, 0.8.35, awaiting a run; module `src/PlayerReads.*`). Techniques that can be borrowed from it:
  - scanning `.text` by Address Library addresses;
  - site rejection (`changed`, `detoured`, `prologue`, `branchInto`, `outOfReach`);
  - a block of stubs above the image within `0x70000000` of the base;
  - frame marker in a `TlsAlloc` slot (`gs:[0x1480 + i*8]`);
  - stubs without calls, heap, or locks.
- `docs/tasks/results/T31.md`: cell and actor traversal runs on `BSJobs` worker threads; Havok too.
- Ghidra base: MCP `ghidra_*` or `tools/ghidra/gq.py` (`docs/GHIDRA.md`). Names by ID without the server — `%TEMP%\clr-ghidra\names-1.6.1170.tsv`.

## ID list (T38 + T39, 151 functions)

The T38 (70 functions) and T39 (84 functions) lists are merged (9 shared, 145 total). Added 6: the detection pair `AE#19784`, `AE#18517` and frame markers `AE#69514`, `AE#69516`, `AE#35406`, and `CellLoaderTask::PostProcess` `AE#18529`. Which function comes from where and why — sections "ID list for T40" in `results/T38.md` and `results/T39.md`.

```
13062,13277,13288,13295,13299,13315,13316,13317,13326,13362,13363,13370,13378,13388,13415,13419,13427,13428,13430,13434,13435,13436,13440,13721,15372,15375,15959,15962,17922,17924,17926,17968,18517,18529,18540,18541,18546,18547,18548,18889,18976,18978,18981,19032,19046,19055,19059,19063,19111,19112,19130,19280,19361,19411,19668,19784,19812,19823,19824,20039,20460,25946,25947,32288,32359,32484,35406,35567,35652,35769,35772,36564,36565,36581,36661,37288,37304,37350,39049,39055,39089,39637,39814,40437,40438,40442,40445,40447,40451,40453,40462,40463,40464,40466,40469,40471,40504,40505,40521,40522,40528,40570,40661,40664,40671,40691,40730,40740,40744,40745,40751,40807,40817,40818,40845,41023,41024,41034,41051,41059,41294,41313,41314,41315,41369,41371,41444,41445,41448,42742,50724,50833,51924,51929,52852,53102,56626,61410,69514,69516,75686,75705,75708,75712,75713,77905,77906,77908,77909,106385,442664
```

Vtable slots:
- `PlayerCharacter[+0x0]`: `0xAD`, `0x98`, `0x7D`, `0x6F`, `0x70`, `0x71`, `0x8D`, `0xC2`, `0xA4`, `0x5B`, `0x11F`, `0x86`, `0x11A`, `0xE3`, `0x46`, `0xE0`, `0x55`, `0x9C`;
- `PlayerCharacter[+0xB8]` `0x8`, `PlayerCharacter[+0xA0]` `0x4`, `PlayerCharacter[+0x2B8]` `0x1`;
- `Character[+0x0]` `0x91`;
- `TESObjectDOOR` `0x37`;
- `GridCellArray` `1`–`8`;
- `CellLoaderTask` `1`, `2`, `3`, `6`.

## To do

1. **Call counters** for the ID list from the INI (`[CallProbe] sIDs=…`, default is the list above).
   - At load, find all direct calls (`E8 rel32`) to an Address Library address in the entire `.text`.
   - Redirect each call to a stub of its own site. The stub counts the call by site, thread kind, and window, then `jmp`s to the target. The instruction length does not change, the function entry is not touched: foreign detours at the entry do not interfere. Counting by sites immediately shows who calls.
   - **Calls through vtable** — by patching the slot (list above). If our vfunc hook is installed, the stub wraps it.
   - **Functions without direct calls** (`BSJobs` jobs, pointers in data tables, e.g. `Job_IO` `AE#39084` and `AE#39055`) — find references to them in data (`ghidra_xrefs`) and patch the pointer. If there is no pointer — detour the entry, if the entry is not taken. Otherwise — 0 sites, and that is a line in the report.
   - A site where `E8` leads elsewhere (a foreign `write_call`, our hook) or whose bytes changed after the scan — skip and count by reason. The report gets found, patched, and skipped counts per ID and by reasons.
2. **Thread kind without API in the stub.**
   - Read the thread ID from the TEB (`gs:[0x48]`).
   - The kind comes from a fixed-size "thread ID → kind" table. Fill it outside the stubs:
     - main thread — by our own capture: the plugin load thread and the `PlayerCharacter::Update` hook thread, compare with `RE::Main::threadID` (Q3);
     - IO — from `CellLoaderTask::Run`;
     - `BSJobs` and others — by enumerating threads on a key press and at startup, by thread start address (`NtQueryInformationThread`, `ThreadQuerySetWin32StartAddress`) matched against the base.
   - Unknown thread — kind "other" and a separate count of its IDs.
3. **Main-thread markers for Q1.**
   - Site stubs inside `Main::Update` on the main thread write the site number to an atomic "where is the main thread" variable.
   - Stubs of `AE#18529`, `AE#13434`, `AE#13721`, `AE#75705` on other threads add a count to the "where was the main thread" histogram.
   - Separately count calls that arrived while the main thread was inside `AE#69516` (`WaitForJobTask`) or outside a frame.
4. **Checks for Q2 and Q3.**
   - In the `AE#41369` stub compare `rcx` with `[AE#403521]`.
   - For `AE#13295` and `DetachAll` (`GridCellArray` slot 1, call `AE#13295+0x64`; base — `GridArray::DetachAll` `AE#15939`) write every call to the trace: thread by our own capture, `RE::Main::threadID`, context (savegame loading in progress, exit to menu, exit from game — by events or flags already in the trace).
5. **Windows.**
   - By transition phases, as in `Profiler`.
   - By key: N seconds of normal gameplay (street, interior, portal).
   - **No free F-keys:** F1 is taken by T32, F2, F3, F4, F6, F7, F8 are taken. Take a combination with a modifier (e.g. `Shift+F6`) or a key outside F; the code is set in the INI. Check `Settings` and `Events.cpp`.
6. **All-threads profiler** (if the budget allows; otherwise — to "Open questions"). IO-thread and `BSJobs` samples on a key press and under the loading screen. `Profiler` rules from `CLAUDE.md`, section 7: between `SuspendThread` and `ResumeThread` no heap and no logging.
7. **Report** to the trace (`callprobe`, `profile` with thread) and a summary in `tools/run_digest.py`:
   - calls per frame by ID, thread kind, and window;
   - 10 most frequent call sites per ID;
   - Q1 histogram, Q2 count, Q3 list.

   Names by ID in the script come from the TSV base; the plugin has no names.
8. **INI.** Everything is disabled by default (`bEnabled=0`): without it nothing is patched, behavior is as without the probe. The ID list and the key are for the run.
9. **Overhead.** Frame time with and without the probe in the same place. If a function is too frequent (e.g. `AE#19812`), it can be excluded via the INI.
10. Version in `xmake.lua` +1, archive `dist/`, `tar -tf`.

## Rules

- `CLAUDE.md`, section 7. Off the main thread — counters only, no access to `TES`. No API calls, heap, locks, or logging in the stubs; counting via atomic addition or per-thread.
- **Do not modify `PlayerReads.*`.** Move the shared parts (stub block above the image, `rel32` scan) into a new `src/CodePatch.*`. Migrating `PlayerReads` onto them is a separate commit after both tasks are merged.
- Both modules are installed last in `Hooks::Install`; T40 after T32. T32 sites (`mov`/`cmp` with the player pointer) and T40 sites (`E8`) are different instructions. Verify that `bCensus=1` and `bEnabled=1` enabled together do not interfere with each other. This is a verification item, not a required run mode.
- Addresses only via `RELOCATION_ID`/`REL::ID`. The probe is research-only, AE only. If there is no SE number, write a line to `docs/PORTABILITY.md`.
- Do not put pseudocode into git, docs, or the result: conclusions, `AE#id`, offsets only.
- A run with Engine Fixes — at the end of the stage, not here.

## How to verify in game (steps for the result, ≤ 6)

1. Install the archive, `[CallProbe] bEnabled=1`, Engine Fixes disabled. Before launch — `python tools/ghidra/gq.py stop`.
2. Load the save at the Bannered Mare. Window by key on the street, 20 s.
3. Enter the tavern, 20 s window inside, exit. Transition phases are written automatically.
4. Walk around Whiterun changing grid cells; `coc Riverwood`.
5. F8 (portal), 20 s window at the door.
6. Load a save, exit to the main menu, exit the game (Q3). Logs — in `logs\run-<version>\`.

## Result

`docs/tasks/results/T40.md`, ≤ 80 lines:
- how many sites patched and skipped, by reasons;
- overhead;
- answers to Q1–Q3 with evidence (summary lines);
- table "phase → functions and threads" for `CELL_LOADING_MAP.md`, section 13, and `PLAYER_MAP.md`;
- what was disproved in the maps.

## Dependencies and conflicts

- **T32:** if T32 step 1 has not been merged into `feature/interior-portal`, branch from `exp/T32-player-reads` (commands below); the T40 PR is merged after T32. If merged — from `feature/interior-portal`.
- **Conflicts:** `Hooks.cpp`, `main.cpp`, `Events.cpp`, `Settings.*`, INI (by line, with T16, T20, T32); `Profiler.*` (T32 does not touch it).

```powershell
# T32 not merged:
git worktree add ..\CLR-T40 -b task/T40-call-probe exp/T32-player-reads
# T32 merged:
git worktree add ..\CLR-T40 -b task/T40-call-probe feature/interior-portal
git -C ..\CLR-T40 submodule update --init --recursive
```
