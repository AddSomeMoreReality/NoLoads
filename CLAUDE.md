# CLAUDE.md — CellLoadResearch

An SKSE plugin for Skyrim AE: it began as research into cell loading and is now an experiment in "seeing the interior alive through the door" (a portal), with the goal of a world without loading screens. It produces an archive for Vortex.

Accept user prompts in Russian, but always respond to the user in English. Write identifiers, code, and commits in English.
Do all reasoning (thinking) in English: it is faster and cheaper in tokens. This applies to subagents and task executors as well.

**Git.** Read-only and diagnostic commands are allowed. Only the user performs `commit`, `push`, `merge`, and anything else that changes history or the remote repository. At the end of every iteration with code changes, provide a ready-to-use commit command in English. If a merge or push is needed, say so and provide the commands. Repositories: `origin` = `AddSomeMoreReality/NoLoads`, `template` = the original template.

**Every plan must account for the final mod requirements in section 5.14 (below).**

## Roles and reading order

- The **coordinator** reads `docs/HANDOFF.md`, `docs/tasks/README.md`, and `docs/tasks/results/`.
- The **executor** (one chat — one task from `docs/tasks/`) reads only its own brief and `docs/tasks/README.md`. It does not read large documents in full (the rules are in the task README).

## Document map

| File | Responsibility |
|---|---|
| `docs/HANDOFF.md` | Project status, immediate plan, links |
| `docs/tasks/README.md`, `docs/tasks/T*.md`, `docs/tasks/results/` | Tasks for separate chats: rules, briefs, results |
| `docs/ARCHITECTURE.md` | Code structure, threads, hooks, tools, false leads, shortcomings, workflow |
| `docs/FEATURES.md` | Sections 5.4–5.13 and 5.15: INI, modules, portal (table of contents by module) |
| `docs/ENGINE_MAP.md` | Section 3: engine map (`TES`, cells, functions, events, transition flow) |
| `docs/ORIGINAL_TASK.md` | Sections 0, 4, 5.1–5.3, 6, 8: original assignment, hypotheses H1–H16, trace, automated test, analysis |
| `docs/HISTORY.md` | Section 9: version history 0.1.0–0.8.31 and outcomes of runs 0.8.22–0.8.25 |
| `docs/FINDINGS.md` | Conclusions and evidence from runs |
| `docs/PORTABILITY.md` | Outstanding work for game versions |
| `docs/DISTANCE_CHECKS.md` (+ `_APPENDIX`) | Distance and visibility checks |
| `docs/RELATED_MODS.md` | Similar mods, licenses |
| `docs/LOCATION_TRIGGERS.md` | Game and script reactions to player location changes (T15) |
| `docs/INTERIOR_SIM_STAGES.md`, `docs/archive/` | Removed InteriorSim stages 0.8.0–0.8.11 |
| `docs/FAST_DOOR_TRANSITIONS.md` | Results of the 0.7.x experiments for a separate mod |
| `docs/GHIDRA.md` | Ghidra base, pseudocode tools, curator rules |

References such as "5.12" without a path: 3.x — `ENGINE_MAP.md`, 4 and 5.1–5.3 — `ORIGINAL_TASK.md`, 5.4–5.13 and 5.15 — `FEATURES.md`, 5.14 and 7 — this file, 9 — `HISTORY.md`.

---

## 1. User environment

- **Game:** Skyrim Special Edition / Anniversary Edition, **Steam, runtime 1.6.1170**.
  - In August 2026, Steam updated the game to 1.7.99 and then to 1.7.104. The user deliberately remains on 1.6.1170.
  - Do not suggest updating the game. Do not touch `appmanifest_489830.acf`.
- **SKSE64 for 1.6.1170** (the latest release for this runtime is 2.2.6): `skse64_loader.exe` and `skse64_1_6_1170.dll` are in the game root.
- **Address Library for SKSE Plugins**, AE version: `Data\SKSE\Plugins\versionlib-1-6-1170-0.bin`. The plugin will not load without it.
- **Mod manager — Vortex.** Mods are deployed as hard links into `<Game>\Data`. The plugin must be installed only from an archive, never by copying it into `Data`.
- **Actual paths** (memory and `docs/ARCHITECTURE.md`, 5.3): the game is at `E:\Games\Skyrim\The Elder Scrolls V - Skyrim` (not in the Steam library), and Vortex staging is at `E:\Vortex Mods\skyrimse`. If you do not know the paths, the `SkyrimSE.exe` version (FileVersion 1.6.1170.0), or the state of the tools (VS 2022, xmake ≥ 3.0, git), ask the user or check.
- Determine the Documents folder through `[Environment]::GetFolderPath('MyDocuments')` (it is often in OneDrive). Logs and the trace are written to the project's `logs/` directory (`sLogDirectory`, set by `build.ps1`); by default, they go to `<MyDocuments>\My Games\Skyrim Special Edition\SKSE\`.
- The automated test was removed from the requirements on 01.11.2026: do not implement it.

## 2. Project and build

- **Base:** the https://github.com/libxse/commonlibsse-ng-template template (xmake, C++23). The `lib/commonlibsse-ng` submodule is CommonLibSSE-NG **v13.1.0** (commit `12d61f3c39f7`; in `.gitmodules` it is listed as `alandtse/CommonLibVR` — the old name of `alandtse/CommonLibSSE-NG`, branch `ng`). To update: `git -C lib/commonlibsse-ng fetch --tags origin`, `checkout v13.1.0`, `submodule update --init --recursive`.
- **`xmake.lua`:** the target and plugin name is `CellLoadResearch` (the target name determines the DLL name). **Bump the version for every build given to the user** (Vortex distinguishes archives by name and offers Replace).
- **Configuration:** `xmake f -m releasedbg --skyrim_vr=n -y` (SE+AE without VR, `EXCLUSIVE_SKYRIM_FLAT`). INI — `--rex_ini=y` or WinAPI `GetPrivateProfileString*`. Mid-function hooks — `--skse_xbyak=y`. spdlog with `std_format = true`: `SKSE::log::*` uses `std::format` syntax.
- **Do not set `XSE_TES5_GAME_PATH` or `XSE_TES5_MODS_PATH`:** otherwise xmake will copy the DLL into `Data`, bypassing Vortex.
- **WSL:** MSVC through `powershell.exe` and the VS Dev Shell; details are in `lib/commonlibsse-ng/CLAUDE.md`.
- **Packaging:** `scripts/build.ps1` — `xmake f` + `xmake build`, a staging folder, and `dist/CellLoadResearch-<version>.zip` (the version comes from `xmake.lua`). The archive contains `SKSE/Plugins/` (`.dll`, `.pdb`, `.ini`); since 0.8.6, `Meshes/` and `Textures/` (from `res/data/`) are included alongside it. There are no other top-level folders. There are no Papyrus scripts or `.esp` files (5.14, item 7). A branch other than `main` and `feature/interior-portal` is appended to the archive name; the branch and commit are written to the log and to `info.plugin` (`src/BuildInfo.h`, generated by `build.ps1`).
  - **There is no `Data/` folder in the root.** Do not use `xmake package` for Vortex.
  - Archive with `tar.exe -a -c -f <zip> -C <stage> ...` or `ZipFile::CreateFromDirectory` (PowerShell 7). `Compress-Archive` from PS 5.1 writes `\` — do not use it.
  - After packaging, print `tar -tf <zip>`: separators must be `/`, and the top level must be as above.
- **Installation (user):** Vortex → Install From File → Enable → Deploy; for an update — Replace. Verification: `<Game>\Data\SKSE\Plugins\CellLoadResearch.dll` exists, and `CellLoadResearch.log` appears after launch.
- **Run:** the user launches the game. An autonomous launch (`skse64_loader.exe`, editing the INI in `Data` in place, `bQuitWhenDone=1`) is allowed only with the user's explicit permission; restore the values after the run.
- **Trace analysis:** first run `tools/run_digest.py <run folder>` (summary in `digest.md`); for details, use `tools/analyze_trace.py` (Python 3 stdlib), and for function names, `tools/name_functions.py`. Details are in `docs/ORIGINAL_TASK.md`, section 6.

---

## 7. Safety rules and pitfalls

- **Game mutations** (teleports, `purge`, `Activate`, loading a save) — only on the main thread, from an `Update` tick (`MainThread::Post` → `MainThread::Drain` in the `PlayerCharacter::Update` hook). Never inside event handlers.
  - **Do not use `SKSE::GetTaskInterface()->AddTask` for deferred work:** in 0.3.0, a task that requeued itself hung the game.
  - On 1.6.1170, the following arrive **off the main thread**: input events, `TESObjectDOOR::Activate`, exterior `CellFullyLoaded`, and SKSE `kDataLoaded`.
- **Do not retain `TESObjectCELL*` pointers to exterior cells beyond the current call** (0.4.0 crashed on `coc` to another world). Store the FormID, coordinates, or a string. Interiors and `TESWorldSpace` are permanent.
- **The profiler (`Profiler.*`)** suspends the main thread: between `SuspendThread` and `ResumeThread`, use only system calls and `memcpy` — no heap operations, locks, or logging.
- **`<windows.h>`** (included in the PCH through spdlog) breaks code with the `MAX_PATH`, `min`, `max`, `CP_ACP`, `near`, and `far` macros: write `(std::min)(...)` and do not use those names for constants or variables.
- `skyrim_cast` and `netimmerse_cast` in NG are in the global namespace, not in `RE`.
- **`RE::BSGraphics::RenderTargetManager::GetSingleton()` is incorrect in NG:** it dereferences `AE#411451`, where the manager itself resides (this crashed the game in 0.8.6). Use `REL::Relocation<RenderTargetManager*>{ RELOCATION_ID(524970, 411451) }.get()`. Before trusting an NG singleton, inspect how the game passes the address: `lea` means an object, `mov` means a pointer.
- **`Renderer::GetRuntimeData().renderTargets` in the FLAT build has 125 elements (`kVRTOTAL`), but there are only 114 (SE) or 116 (1.6.1130+) real render targets.** `depthStencils` follows them. Iterate using `RenderTargetCount` (`PortalGrade.cpp`), and validate D3D objects from foreign structures with `QueryInterface`: in 0.8.28, an SRV mistaken for a texture wrote through the wrong vtable slot to a garbage address, and F11 hung the system.
- **`NiNode::DetachChild` (`AE#70292`) does not check the parent** (the index comes from `NiAVObject + 0x38`). Do not call it, or game functions that call it (such as `AE#19130`), on a node with a different parent: in 0.8.17, this detached and released another node's child.
- **Game machine code:** use `tools/ghidra/gq.py` (`docs/GHIDRA.md`) for pseudocode and cross-references: `decomp AE#id[+off]`, `asm`, `xrefs`, `fieldrefs`; output goes only to the scratchpad. Only the curator modifies the Ghidra base (T33 chat, `..\CLR-GHIDRA`): send requests for names and types to the curator. The pseudocode and project are Bethesda code; only conclusions and addresses go into git and docs. Before launching the game, run `python tools/ghidra/gq.py stop`; do not run Ghidra analysis (up to 10 GB) at the same time as the game. The fallback is `D:\VS\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe /disasm:nobytes /range:<VA>,<VA> SkyrimSE.exe` (base `0x140000000`) and `tools/name_functions.py` (`--xref ID`).
- **`CellLoaderTask::Run` hooks** run on IO threads: access only task fields, tracing, and the original function. Do not access `TES`, the player, or the UI.
  - `CellLoaderTask::PostProcess` (`AE#18529`) also runs on `BSJobs` job threads: from there, `interiorBuffer`/`exteriorBuffer` (`AE#13434`) are modified and cells (`AE#13721`) are unloaded without a lock. Hooks for these functions must not assume the main thread (`docs/CELL_LOADING_MAP.md`, 8).
- **No locks or heavy work in hooks.** The grid snapshot is bounded by `length²`; `bLogCellAttachPerRef` is disabled by default.
- **Access `TES` fields beyond 0x128 only through `GetAERuntimeData()` / `GetRuntimeData2()`** (the layout changes starting with 1.6.1130). Access player fields through `GetPlayerRuntimeData()`. Only log `unkXXX` fields.
- **Cell buffers (H9):** read them only when `bReadCellBuffers=1`, up to `min(INI, 64)` elements, with a `nullptr` check and `__try/__except` in a separate function containing no objects with destructors. If this crashes, leave it disabled and record it in FINDINGS.
- **Runtime version at startup:** `REL::Module::get().version()`; if it is not 1.6.1170, emit a warning but do not disable the plugin.
- **Do not modify `Skyrim.ini` / `SkyrimPrefs.ini` without permission.** If an experiment requires it (`uGridsToLoad`, buffers), make a backup and tell the user what you are changing and how to roll it back; the INI in Documents may be write-protected.
- **Saves:** the plugin must not save the game. Autosaving on transition is safe, but a test character is preferable.
- **Verify builds in the game:** CommonLib layout errors compile without errors and manifest as crashes.
- **"This is game behavior" must be proven by a control run without the portal, not by reasoning.** The 0.8.22 conclusion about Idolaf was correct but had no control; a control was found only in old traces.
- **NPC and scene behavior is often driven by vanilla scripts and Story Manager**, not the engine. Script sources are in `<Game>\Data\Source\Scripts`; bindings and quest startup events (`ENAM`, such as `CLOC`) are in `Skyrim.esm`. They may be read but not modified: fixes must use only engine hooks (5.14, item 7).
- **Player location changes must go only through `PlayerCharacter::SetParentCell` (`AE#40466`, vtable `0x98`):** it generates `CLOC` for Story Manager and `OnLocationChange` (T15, `docs/LOCATION_TRIGGERS.md`). A seamless transition (T20, T21, T27) must call it exactly once: otherwise, 148 story reactions will fail to fire or will fire twice.
- **Distances in the trace are in game units** (1 unit ≈ 1.43 cm, 70 ≈ 1 m), not centimeters.
- **Before blaming distance, check visibility.** `kFarAway` and `kNotVisible` depend on the camera, not distance (`docs/DISTANCE_CHECKS.md`).
- **Avoid hard-coded offsets.** First use an NG field, then `RELOCATION_ID(se, ae)`, then a verifiable signature. If none is available, record it in `docs/PORTABILITY.md` and do not delay the experiment.
- **Second view and `BSRenderPass`.** Render passes come from a shared pool (with Engine Fixes and `bOverrideRenderPassCache`, they come from tbbmalloc without a pool; T14), and `GetRenderPasses` uses one list per shader property. Therefore, the accumulator must be empty after a pass (`DrainPasses`), and an object also rendered by the game must not be registered while the game's jobs are running: `RenderPlayerView` completes them only after `RenderWorld`.
- **A hang instead of a crash.** Crash Logger does not detect it, and records are lost for the 250 ms before the stop. See `CellLoadResearch.hang.txt` (written by `Watchdog`); Crash Logger 1.25 provides `Ctrl+Shift+F12` (thread dump), and `taskkill /F /IM SkyrimSE.exe` can be run from a second desktop or through `Win+R`.
- **Put INI comments on separate lines:** `GetPrivateProfileString` does not strip `; ...` after a value.
- **F9 in the game is quickload.** Available keys: F1–F4, F6–F8.

### 5.14 Final mod requirements (user, 01.11.2026)

This is the framework for all future plans. Implementation is not required now, but decisions must not conflict with it.

1. **Many portals simultaneously.** The goal is a world without loading screens, so portals work at every door at once. A single instance, global state, and one retained interior are temporary solutions.
2. **Any cell pairs.** The logic applies to caves, dungeons, and city gates. A portal connects an exterior to an interior, an exterior to an exterior (another world, gates), and an interior to an interior. The code must not assume "a street outside, an interior inside."
   - The two sides of a portal are equivalent: transformation, lighting, distances, and object passage work in both directions.
   - The cell grid of the world beyond the gates will have to be retained and rendered in the same way as an interior.
3. **There is currently no sound from inside.** It cannot be heard outside the tavern. The user heard it only when all objects in the interior cell were moved into the exterior cell, which was the wrong solution. The statement "hears the bard through the door" in early runs refers to those versions. Sound must be transmitted through the door separately.
4. **Any camera.** Cell rendering logic works for any camera, not only the player camera. This is needed for other mods, cutscenes, and recursive portals, where another portal is visible through one portal's window.
   - The portal pass accepts the camera and target as parameters and does not read the global player camera.
   - The image depends on the camera: each "portal + view" pair needs its own texture, and a pool of such textures is required.
   - Recursion needs a depth limit and a plan for when that depth is exhausted.
5. **Compatibility with Realistic Reflections - Mirrors.** The final version works together with this mod (`https://github.com/1001Bits/Realistic-Reflections-Mirrors`). A portal in a mirror must show the correct image, and a mirror beyond a portal must show a reflection. The mod's code must not be copied (`docs/FEATURES.md`, 5.15).
6. **All game versions.** At minimum, SE 1.5.97 and newer, up to the current AE. Outstanding work is tracked in `docs/PORTABILITY.md`.
7. **Game data is not modified** (user, 03.11.2026). The mod does not edit quests, scenes, Papyrus scripts, AI packages, or other forms, so it remains compatible with other mods. Behavior is changed only by hooking engine handlers (detour, `write_call`, vfunc). The exception is doors replaced by a portal.
8. **Compatibility with Engine Fixes** (user, 04.11.2026). The mod works both with and without it; Engine Fixes is not made a dependency (T14, run 0.8.31). Its techniques (MIT) may be used as ideas. A run with Engine Fixes is performed at the end of every stage.
