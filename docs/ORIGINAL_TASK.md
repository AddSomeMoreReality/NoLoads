# Original assignment (former sections 0, 4, 5.1–5.3, 6, and 8 of `CLAUDE.md`)

The text was moved without changes. Section numbering is old: references such as "section 4" and "5.3" refer to this file. Other 5.x subsections are in `docs/FEATURES.md`; 5.14 is in `CLAUDE.md`.

## 0. Task and completion criteria

Build a CommonLibSSE-NG SKSE plugin `CellLoadResearch` that:

1. **Traces the real loading algorithm** for cells and locations: engine events, hooks on key functions, snapshots of `RE::TES` state, timings, threads.
2. **Runs an automated transition scenario**: interior ↔ exterior, loading door, worldspace change, walking through the exterior, buffer purge.
3. **Is distributed as an archive** that installs in Vortex without manual repacking.

After the run, analyze the trace and confirm or refute the hypotheses in section 4.

**Definition of Done**

| # | Deliverable | Where |
|---|---|---|
| 1 | Vortex archive, root — `SKSE/Plugins/...` | `dist/CellLoadResearch-X.Y.Z.zip` |
| 2 | Build and packaging with one command | `scripts/build.ps1` |
| 3 | Trace analysis script: timelines, phase durations, call order, threads | `tools/analyze_trace.py` |
| 4 | Report for each hypothesis: confirmed / refuted / unclear + trace excerpts | `docs/FINDINGS.md` |
| 5 | Brief user instructions: Vortex installation, hotkeys, log locations | `README.md` |

---


## 4. Hypotheses to test

| ID | Hypothesis | How to test |
|---|---|---|
| H1 | `TESObjectDOOR::Activate` only fills `queuedTargetLoc`, and movement occurs on the next frame or later | Frame number (`Update` tick counter) and `isValid` on exit from `Activate` versus entry into `SetCurrentCell` |
| H2 | `SetCurrentCell` is called between `kPre` and `kPreUpdatePackages` | `seq` order in the trace |
| H3 | Interiors load synchronously on the main thread under `LoadingMenu`; exteriors load asynchronously while walking via `CellLoaderTask::Run` on IO threads | `tid` and `main` on `Run`/`Finish`; presence of `CellLoaderTask` on `coc` into an interior |
| H4 | Departed cells go into the buffer (`kUnattached`/`kDataLoaded`), re-entry is noticeably faster; after `PurgeBufferedCells`, the advantage disappears | `LoadingMenu` and `SetCurrentCell` durations in step pairs of the default scenario (5.3); cell states in snapshots |
| H5 | `currentQueuedX/Y` leads `currentGridX/Y`, meaning this is preloading | Snapshots while walking |
| H6 | Order of `TESCellFullyLoadedEvent`, `BGSActorCellEvent`, `TESActorLocationChangeEvent` relative to `kFinish` and `LoadingMenu` closing | Timeline |
| H7 | `CellLoaderTask` with `interior=true` is used for interiors (for example, preloading behind a door) | Log all `CellLoaderTask` instances with `interior` |
| H8 | Meaning of `a_unk4` in `SetCurrentCell` and the last `bool` in `QueueCellLoad` | Log values in all scenarios and correlate them |
| H9 | Length of `interiorBuffer` equals `uInterior Cell Buffer`, length of `exteriorBuffer` equals `uExterior Cell Buffer` | Careful reading (see section 7); comparison with INI |
| H10 | Which loading INI settings actually exist in 1.6.1170 | Try `INISettingCollection::GetSingleton()->GetSetting(name)` and `INIPrefSettingCollection` for candidates: `uGridsToLoad:General`, `uExterior Cell Buffer:General`, `uInterior Cell Buffer:General`, `bPreloadLinkedInteriors:General`, `uLargeRefLODGridSize:TerrainManager`. Log what is found and its value. Names other than the first three have not been verified |
| H11 | While the player is in an exterior, interiors behind doors in the loaded area are neither rendered nor physics-simulated, and their NPCs are processed only at low/middle-low level (coarse schedule AI, without animation or physics). Neighboring exterior cells in the 5×5 grid are fully simulated | Simulation probe (5.7): cell states, `inScene`, physics-world time, NPC processing levels, and movement by category |
| H12 | While the player is in an interior, the exterior grid remains loaded (`kUnattached`) but is neither rendered nor physics-simulated, and NPCs outside drop to low/middle | Same probe, `gridWhileInside` category |
| H13 | The visible door transition (3.5–3.8 s) consists almost entirely of pauses and fades rather than loading: pre-fade pause 0.65 s (`fTeleportDoorActivateDelayTimer`?), `fNormalDoorFadeSecs` 0.4 s, fade-in 2.37 s = `fMinSecondsForLoadFadeIn` + `fFadeToBlackFadeSeconds` (confirmed in 0.7.1) | `transition.visible` record; `[Overrides]` presets on F7 (5.9) |
| H14 | Calling `ExteriorCellLoader::QueueInteriorCellLoad` (`AE#18540`) every couple of seconds keeps the selected interior loaded (`kUnattached`) while the player is nearby: eviction from the 3-slot buffer no longer occurs | `preload.change` / `preload.keep` while walking around Whiterun (5.5) |
| H15 | If the interior Havok world is stepped together with the player world and its NPCs are raised via `MoveToHigh`, the interior is simulated like a neighboring exterior cell: NPCs are on high, move, and physics runs | InteriorSim stages 1–2 (5.11): `interior.sim`, probe |
| H16 | The `cell3D` of an interior in `kUnattached` can be hung in the exterior scene and seen: in place of the door or above the building | InteriorSim stages 3–4 (5.11) |

---


## 5. What the plugin must do (beginning)

### 5.1 Trace (JSONL)

File: `<MyDocuments>\My Games\Skyrim Special Edition\SKSE\CellLoadResearch.trace.jsonl`. Recreated at game startup. A regular text log via `SKSE::log` is written alongside it.

One line is one record:

```json
{"seq":42,"t_us":1234567,"frame":9876,"tid":4321,"main":true,"ev":"hook.TES.SetCurrentCell.enter","args":{"cell":"WhiterunBanneredMare [xxxxxxxx]","pos":[0,0,0],"unk4":0.0},"caller":"AE#40437+0x1A2"}
```

- **`t_us`** — `std::chrono::steady_clock` from plugin startup.
- **`main`** — comparison of `GetCurrentThreadId()` with `RE::Main::GetSingleton()->threadID`.
- **`frame`** — tick counter from the `PlayerCharacter::Update` hook.
- **`caller`** — `_ReturnAddress()` minus the module base, converted to "nearest lower AE-ID + offset" via `REL::Offset2ID`. This is a guide, not an exact function.
  - Build the table once, lazily, on the main thread.
- **File writing** — thread-safe (mutex or queue), with `flush` after every `LoadingMenu`/`kFinish` record and on exit.
- **`ev` prefixes:** `event.*` for events, `hook.*.enter` / `hook.*.leave` (with `dur_us`) for hooks, `snapshot`, `autotest.step.begin` / `autotest.step.end`, `info` for runtime version and INI.

### 5.2 Instrumentation

- **Events** — all from section 3.5.
- **vfunc hooks** — all from table 3.3: door, `GridCellArray`, `CellLoaderTask`, `PlayerCharacter::Update`.
- **Hooks at entry to regular functions:** `SetCurrentCell`, `PurgeBufferedCells`, `QueueCellLoad`, `CenterOnCell_Impl`, `MoveTo_Impl`.
  - A full detour with prologue relocation is required.
  - Microsoft Detours is preferred: check the package with `xmake require --info microsoft-detours`. If unavailable, integrate it via vcpkg or sources.
  - Alternatives: `SKSE::stl` ContextHook (`--skse_xbyak=y`) or `write_call<5>` at identified call sites.
  - **Do not write a homemade `write_branch` at the beginning of a function without relocating the prologue.**
- **State snapshots** (`snapshot`) — only on the main thread:
  - after `kFinish`, after `LoadingMenu` closes, after every `SetCenter`, at the end of every autotest step, and on hotkey;
  - contents: `interiorCell`, `worldSpace`, `currentGridX/Y`, `currentQueuedX/Y`, `length×length` grid (for each cell: ID, `cellX/cellY`, `cellState`, `refsFullyLoaded`, `queuedRefCount`), size of `ExteriorCellLoader::queuedCellMap` (under `BSReadLockGuard` on `lock`), player position, `GetParentCell()`, `GetCurrentLocation()`, `playerFlags.isLoading`, `queuedTargetLoc`.

### 5.3 Autotest

- **Launch:** F10 hotkey in game or `bEnabled=1` in INI.
  - With `bEnabled=1`, when `Main Menu` opens, the plugin loads a save itself: by INI name via `BGSSaveLoadManager::Load(name)` or the most recent one.
- **Scenario:** a finite-state machine running in the `PlayerCharacter::Update` hook.
  - Each step: start action → wait for "transition complete" → `fSettleSeconds` → snapshot.
  - **"Transition complete"** = `kFinish` observed, `LoadingMenu` closed, and `player->GetParentCell()->IsAttached()`. Timeout is 60 s; on timeout, log an error and proceed to the next step.
  - **Measure waiting time with `steady_clock`**: `Update` ticks may be absent during `LoadingMenu`.
- **Steps** (`sSteps` string in INI, `|` separator):

| Step | Action |
|---|---|
| `coc:<EditorID>` | `PlayerCharacter::CenterOnCell(name)`. First log whether `TESForm::LookupByEditorID<TESObjectCELL>` finds the cell (not all editor IDs are stored at runtime). `CenterOnCell` itself searches for the cell in its own way and returns `bool`: on `false`, skip the step with an error |
| `door:nearest` | nearest reference to the player with `ExtraTeleport` in the current interior, or in the loaded grid in an exterior, then `door->ActivateRef(player, 0, nullptr, 0, false)`. This tests the real door path |
| `walk:<+x\|-x\|+y\|-y>,<N>` | N steps of 4096 units: `player->SetPosition(...)` and wait for `SetCenter`. **Not confirmed** that `SetPosition` triggers recentering without a loading screen — investigate and record in FINDINGS |
| `purge` | `TES::PurgeBufferedCells()` |
| `wait:<sec>` | pause |
| `snapshot` | snapshot |

- **Default scenario.** Pairs for H4 (first entry → repeat → after `purge`):
  - interior through a door: 1 → 3; exterior through a door: 2 → 4;
  - interior via `coc`: 6 → 8 → 11; exterior via `coc`: 7 → 9 → 12.
  - `purge` and the measurement after it must occur when the player is already **outside** the tested cell: `coc` to the cell where the player stands does not trigger loading (0.1.0 run).
  - Run 0.2.0 showed that interiors reload every time via `coc`, but not through a door. Therefore door and `coc` are tested separately.

```
coc:RiverwoodSleepingGiantInn | door:nearest | door:nearest | door:nearest | walk:+x,4 | coc:WhiterunBanneredMare | coc:Riverwood | coc:WhiterunBanneredMare | coc:Riverwood | purge | coc:WhiterunBanneredMare | coc:Riverwood | coc:Whiterun
```

- If the trace shows that the scenario does not test what it should, correct it here, in `res/CellLoadResearch.ini`, and in `Settings.h` (user permission dated 23.10.2026).

- On completion: `autotest.done` record, summary in the log, flush. With `bQuitWhenDone=1` — `RE::Console::ExecuteCommand("qqq")`.


## 6. Run and analysis

**Option A — the user plays.**

1. The user installs the zip in Vortex, deploys, and launches the game through SKSE in Vortex.
2. Loads a test save and presses F10 or simply walks through doors.
3. After exiting the game, you read `CellLoadResearch.trace.jsonl` and `CellLoadResearch.log`.

**Option B — autonomously, only with the user's explicit permission.**

1. Ensure the latest archive version is installed and deployed in Vortex: the DLL of the required version is in `<Game>\Data\SKSE\Plugins`. Get the version from the DLL resources.
2. Temporarily set `bEnabled=1` and `bQuitWhenDone=1` in the INI in `Data`.
   - The INI is a hard link to a file in Vortex staging. Edit in place, without recreating the file.
   - Restore the values after the run.
3. Steam must be running. Launch `<Game>\skse64_loader.exe` and monitor the trace until `autotest.done` or a 15-minute timeout.
4. If the game crashes, find the crash log (`Crash Logger SSE AE VR` writes to the SKSE folder), correlate the stack with the `.pdb`, and fix it.

**Analysis (`tools/analyze_trace.py`, Python 3 stdlib only):**

- Segments the trace by transitions: from `autotest.step.begin` or `LoadingMenu open` to `kFinish` + `LoadingMenu close`.
- For each transition, outputs:
  - ordered `ev` sequence with `Δt`;
  - durations (`LoadingMenu`, `SetCurrentCell`, sum of `CellLoaderTask::Run`);
  - thread distribution;
  - callers (`caller`).
- Summary table by scenario step, comparison "with buffer / after purge".
- Markdown output to be inserted into `docs/FINDINGS.md`.

**`docs/FINDINGS.md`:** for each hypothesis H1–H10 — verdict, evidence (2–5 trace lines), open questions. At the end — a refined diagram of the real loading flow.

---


## 8. Starting code

`src/main.cpp` (provided by the user) — minimal example: subscriptions to events from 3.5, a `TESObjectDOOR::Activate` hook that blocks loading doors in combat, and a `GridCellArray::SetCenter` hook. It **did not compile**.

1. Start by making it compile.
2. Keep the door-blocking hook behind the INI flag `bBlockDoorsInCombat=0`.
3. Then split the code into files: `Trace.*`, `Events.*`, `Hooks.*`, `Snapshot.*`, `AutoTest.*`, `Settings.*`.

