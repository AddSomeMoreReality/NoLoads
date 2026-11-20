# Cell loading map: from the door to `kAttached` (T39)

SkyrimSE.exe 1.6.1170, Ghidra base (T33–T36), traces `logs/run-0.2.0` … `run-0.8.32`. Numbers are Address Library AE; `+0x…` — call site inside a function. No pseudocode here, only conclusions.

Confidence markers: **[code]** — from game code (call chain in the base), **[game]** — a trace line, **[?]** — a guess. Names without NG are working names (candidates for the curator in `docs/tasks/results/T39.md`).

Threads: **M** — main (`Main::Update`), **IO** — the `BSTaskManagerThread` thread (one per launch), **J** — job worker threads (5–6 different `tid`s), **X** — not the main thread; which exactly — not determined.

## 1. Entry points

All player transitions except walking converge on one function: `AE#40438` ("process `queuedTargetLoc`"). It is called by `Main::Update` (`AE#36564+0x6E`) every frame if `queuedTargetLoc.isValid` (`player+0x68A`) **[code]**.

| Entry | Chain | Thread |
|---|---|---|
| Door with loading | `TESObjectDOOR::Activate` (`AE#17922`, vtable `0x37`) does not queue anything **[code]**. The teleport is queued by the door's `BGSOpenCloseForm::HandleOpen` (`AE#17926`, slot 1 of subtable `+0x88`): immediately `AE#17924` or via the fade `NormalDoorFadeCallback` (`AE#17968`) → `AE#17924+0x1B1` → `PlayerCharacter::AE_40442` (set `queuedTargetLoc`) → next `Main::Update` → `AE#40438` | `Activate` — X (2,330 of 2,343 calls, `AE#19796+0x6C0`) **[game]**; `HandleOpen` and the fade callback — X **[?]**; `AE#40438` — M |
| `coc` | `ConsoleCommand::CenterOnCell` (`AE#22873`) → `CenterOnCell_Impl` (`AE#40437`): sets `isValid` (`+0x263`) and immediately calls `AE#40438` (`+0x26A`) — synchronously, not via a frame **[code]** | M (7 of 7, `AE#22873+0x60`) **[game]** |
| Player `MoveTo` | `MoveTo_Impl` (`AE#56626+0x18B`) → `AE#40442` → frame → `AE#40438` | caller; transition — M |
| Fast travel | `MapMenu::vf_0x4` (`AE#53102+0x47D`), `ScriptCommand::FastTravel` (`AE#22612`), `Papyrus::Game::FastTravel` (`AE#55457`) → `AE#40442`; in `AE#40438` the `fastTravelMarker` branch (`+0x684`) → `Actor::AE_40445` (`+0x5C7`) | menu — X **[?]**; transition — M |
| Walking (grid) | `Main::Update+0x825` → `TES::AE_13288` (update grid) | M |
| World change (gates) | door → `AE#40438` → `AE#40745` (target with world) → `AE#40744` → `TES::AE_13315(world,1)` (`+0x155`) and `TES::AE_13317` (`+0x16E`) | M |
| Save loading | `Main::Update+0x9A` → `BGSSaveLoadManager::AE_35772` (save/load queue) `+0x465` → `AE#442580` → `AE#35600` → … → `AE#35652` (reading player position): `TES::AE_13315`, `AE#40744`, `AE#13288`, `AE#13317`, `AE#40745`, `TES::AE_13295` (`+0x67A`, `DetachAll`) | M; `DetachAll` from `AE#13295+0x64` 44 times out of 126 — X **[game]** |
| Game start | `AE#36544` → `AE#36549` → `AE#40442`, `AE#40438`, `SetCurrentCell` (`+0x1F0`) | M |
| Interior preloading | `PlayerCharacter::Update` (vtable `0xAD`) → `AE#40845` → `AE#13378` → `AE#18540(loader, cell, true)` → `CellLoaderTask` (`interior=1`) | M queues, IO loads |

## 2. Transition core `AE#40438` (M)

Order **[code]** and in game: door in Bannered Mare, 0.7.1, `seq 713–732`, all in frame 3936 **[game]**.

```
AE#40438(player)
 ├ +0x3C7/+0x4D0 TES::AE_13425      are all target grid cells already in exteriorBuffer
 ├ +0x4FB/+0x534/+0x642 AE#13363(1, location, …)   open Loading Menu   (seq 713)
 │        (does not open if AE#400337: target = current cell, +0x53F)
 ├ +0x550 AE#32491, +0x55A AE#41051(kPre=0)        PositionPlayerEvent    (seq 716)
 ├ one of three branches:
 │   +0x5C7 Actor::AE_40445        to marker/reference (fast travel)
 │   +0x6D4 AE#40744               target — interior (queuedTargetLoc.interior)
 │   +0x741 AE#40745 → AE#40744    target — world and position
 ├ +0x82D AE#41294, +0x839 BGSStoryTeller::AE_32484
 ├ +0x845…+0x86E  loop: while AE#404445 ≠ 0 → AE#41313 + Sleep(10)   (waits for ProcessLists)
 ├ +0x885 AE#41051(kPost=3)  → TES (AE#13419) Havok warm-up (FINDINGS, H4+)
 ├ +0x8B2 IOManager::AE_75708(5)   wait for IO queue
 ├ +0x8DD AE#41051(kFinish=4)
 ├ +0x916/+0x927 autosave (AE#13291, BGSSaveLoadManager::AE_35769) — if flag 0x80 in player+0xBE4
 ├ +0x952 AE#13363(0, …, 1)        close Loading Menu   (seq 732)
 └ +0x95E TES::AE_13388; clear queuedTargetLoc (+0x98B…+0xA28)
```

`AE#40744(player, pos, rot, cell, resetSky)` — player relocation **[code]**:
`+0xE6` `AE#19032` (leave the old cell; in game `ActorCell leave`, `seq 719`) → `+0xF7` `SetPosition` → `+0x131` field `parentCell = cell` (`AE#19818`, not a vfunc) → interior: `+0x14E` **`TES::SetCurrentCell`** / exterior: `+0x155` `TES::AE_13315(world,1)`, `+0x16E` **`TES::AE_13317`** → `+0x18D` field `parentCell = null` → `+0x19B` `TESObjectCELL::AE_19411(cell, player)`, inside `+0x251` vfunc `0x98` = `PlayerCharacter::SetParentCell` (`AE#40466`, `CLOC` and `OnLocationChange`; in game `ActorCell enter` and `ActorLocationChange` after `SetCurrentCell.leave`, `seq 721–724`) → `+0x1F6` `AE#75708(5)`, if the player has no 3D → `+0x5C4…+0x66E` player 3D, `Actor::Update`, `BGSTerrainManager::Update` → `+0x72C` `TES::AE_13291` → `+0x7A0` `TES::AE_13398`.

**Bottom line for T20/T16:** `SetParentCell` (vfunc `0x98`) runs **after** the cell is attached, once per `AE#40744` **[code + game]**.

## 3. Interior: `TES::SetCurrentCell` (`AE#13316`, M)

Parameters in code — only `cell` and `pos` (the decompiler sees three arguments with `this`); the fourth NG argument (`unk4`, H8) is not read **[code]**. In game it is always called from `AE#40744+0x14E` (106 of 106) **[game]**.

```
+0x1E   exteriorWorld(AE#400440)+0xC5F9 = 0      exterior Havok world inactive
+0x44   cell == interiorCell → AE#13288(pos,0) and exit
+0x7A…+0x221  only with bPreemptivelyUnloadCells (AE#358811; default 0):
        from exterior: unload exteriorBuffer (AE#13721);
        from interior: grid DetachAll (+0x154), unload exteriorBuffer, world → null (AE#20463)
+0x273  Main::AE_36555 (if no loading screen and a save is not loading)
+0x288  BGSEncounterZone::AE_18199;  +0x347 BGSGrassManager::RemoveCellGrass (grass radius)
+0x37C  ShadowSceneNode::SetPortalGraph(SSN, null);  +0x38A AE#106335
+0x3A1  AE#19130(oldInterior, 1)                 detach old interior
+0x3BB…+0x456 interiorBuffer: take out the new cell or free the last slot (AE#13721)
+0x466  TES::interiorCell = cell;  +0x464 AE#105631(1)
only from exterior (saveGridX == 0x7FFFFFFF):
  +0x49C saveGrid = currentGrid; +0x4D0 bhkWorldM::AE_77908;
  +0x50E AE#19130(gridCell, 0) for each kAttached grid cell (stay loaded);
  +0x52C AE#77909; +0x53B AE#31814; +0x54C TESWorldSpace::AE_20517; lodLandRoot Update
+0x58E  AE#19130(skyCell, 1)
+0x5BF  currentGridX/Y = pos / 4096
+0x5FB  AE#53142;  +0x603 TESObjectCELL::AE_19111(cell)       data load, kDataLoaded
+0x62A  PurgeBufferedCells — with bPreemptivelyUnloadCells and refs > iLargeIntRefCount
+0x65C  AE#13363(1, location, 1) — loading screen if the cell has references and is not kUnattached
+0x661  AE#18976(cell, TES::objRoot)             kBuilding3D…kAttaching, portal graph, events
+0x678  LoadedAreaBound::Clear (TES+0x2B8)
+0x694  bhkWorld::AE_77905(cellWorld) … +0x70F AE#77906    add-to-cell-world batch
+0x6A0  ProcessLists AE#41313;  +0x6AA AE#19063(cell, 1)   kAttached
+0x6B5  TES::AE_13326 (sky mode)
+0x6CB/+0x700 AE#75708(2) around AE#41313/41314/41315   wait for IO
+0x71B  TES::AE_13415;  +0x739 AE#13759;  +0x741 AE#18978 and Update 3D
+0x76D  AE#19059(cell, 1);  +0x778 AE#13435 (cell — to the head of interiorBuffer)
+0x7B5  AE#13363(0,0,0,1) — close if opened;  +0x7E5 NavMeshObstacleManager::AE_30451
```

Interior only: the whole function (an exterior target does not call it, H2). The grid is **not unloaded** when entering an interior — only detached (`kUnattached`) and remembered in `saveGridX/Y` **[code]**, matches H12.

## 4. Exterior: `TES::AE_13317` (going out to the world) and `TES::AE_13315` (world change) (M)

`AE#13317(pos)` **[code]**: `+0x1E` `AE#105631(0)`; `+0x2D` exterior Havok world active; `+0x78` `SetPortalGraph(null)`, if the graph is the interior's; `+0x82` `AE#19130(interior,1)`; `+0x1F3` `AE#19046(interior,1,0)` (into the buffer) or `+0xBC` `AE#13721` (unload); `+0xC1` `interiorCell = null`. If the grid is saved and the world is the same: `+0x16C` `GridCellArray::AE_15962` + `+0x187` `AE#19063(cell,0)` for cells in `kUnattached` — the grid attaches without loading. Otherwise the grid center = `0x7FFFFFFF`. Then `+0x392` `PurgeBufferedCells` and `+0x3A0` `AE#13315` (condition as in 3), `+0x3BA` **`AE#13288(pos,1)`** (full grid load if the center is reset), `+0x43E` `AE#13363(1,…)`, `+0x476/+0x48A` sky cell `AE#18976`/`AE#19063`, `+0x4A0…+0x4D5` `AE#75708(4)`, `+0x4DA` `GridCellArray::AE_15959`, `+0x54E` `AE#13440`, `+0x5F9` `ShadowSceneNode::AE_106355`, `+0x69F` `AE#13363(0,…)`.

`AE#13315(world, resetCenter)` — world change **[code]**: if the world is different — `+0x7E` grid `DetachAll` (if there is an interior), unloading `interiorBuffer` (with `bPreemptivelyUnloadCells`), sky cell change (`+0x190` `AE#19130`, `+0x19F` `AE#13721`, `+0x1BC` `AE#18976`), `TES::worldSpace = world` (`+0x202`), `+0x224` `DetachAll` (in game 97 times from `AE#13315+0x22E`, M **[game]**), reset of centers `0xB0…0xE4`.

World change only: `AE#13315`. Old world: forms of its exterior cells are released (FINDINGS, 0.4.0).

## 5. Grid: `TES::AE_13288(pos, force)` (M, every frame)

**[code]**, in game all `QueueCellLoad` (945) and `SetCenter` while walking — M **[game]**.

- `+0x98` `LoadedAreaBound::SetCurrentCell` (`AE#25946`); when loading a save `+0x52` `Clear`.
- Player inside the central cell (margin `TES+0x140/0x144`): with `bBackgroundCellLoads` (`AE#360413`) and no interior — preloading a strip of `uGridsToLoad` cells: `ExteriorCellLoader::QueueCellLoad` `+0x463`, `+0x4C3`, `+0x4F7` (in game `+0x468`, `+0x4C8`), `currentQueuedX/Y` (H5).
- Crossed out of the cell: if there are unfinished loads (`AE#18548`) and no loading screen — waits until the player moves 20% of a cell further. Then center, water, `BSShaderManager` `gridArrayCenter`, `AE#13440`, `ExteriorCellLoader` `AE#18545`/`18543`/`18544`, `+0x77A` `AE#13315` (default world if there is no world), `bhkWorld` `AE#77906`/`77909`/`77908`, **`+0x807` `GridCellArray::SetCenter`** (vtable 3 → `Shift` → `Detach`/`ClearItem`/`MoveItem`), jump > 1 cell — `+0x9A9` `TES::AE_13362(0)` (synchronous grid cell load, `TESWorldSpace::LoadCell`), `+0x9C5` **`TES::AE_13430`** (attach grid), grass (`RemoveCellGrass`, `AddCellGrassTask::CreateGrassLoadTaskForCell` `AE#13277`), `BGSTerrainManager::Update`.
- Center reset (`0x7FFFFFFF`): `+0xC88` `AE#13363(1,…)` (the loading screen itself), `+0xCB1` `IOManager::AE_75712` (pause IO if the call is not from an IO thread), `+0xE12` **`TES::AE_13428`** (full load: `DetachAll`, `SetCenter`, `AE#13362`, `AE#13430`, `AE#75708(4)`), sky cell, `+0xE5B` `AE#75713`.

`TES::AE_13430` (M): for each grid slot `AE#13427`; cells in `kAttaching` → `AE#19063(cell, !loadScreenUp)`, `AE#13434` (into the buffer), `AE#18978`; around — exterior world (`AE#400440`) `bhkWorld` `AE#77905/77906` and `AE#35567` (waiting for IO during load).

## 6. Attaching and detaching a cell

| Function | What it does | State | Who calls |
|---|---|---|---|
| `TESObjectCELL::AE_19111` | load data from file (under `IOManager::AE_75712/75713`) → `AE#19112` | `+0x181` → `kDataLoaded` (2) | `SetCurrentCell+0x603`, `CellLoaderTask::Run` (interior), `AE#13315` |
| `TESObjectCELL::AE_18978` | build or return cell 3D | `+0x79` `kBuilding3D` (3), `+0xC2C` `kUnattached` (4) | `Run`, `SetCurrentCell+0x741`, `AE#18976+0x71`, `AE#13430` |
| `TESObjectCELL::AE_18976(cell, objRoot)` | 3D under `TES::objRoot`, Havok world active, portal graph `AE#106385`, events `AE#19361` (FINDINGS) | `+0x76` `kAttaching` (6) | `SetCurrentCell+0x661`, `AE#15962` (grid), sky cell |
| `TESObjectCELL::AE_19063(cell, bool)` | cell references, actors into `ProcessLists`, events `AE#19055` | `+0x33C` `kAttached` (7) | `SetCurrentCell+0x6AA` (1), `AE#13317` (0), `AE#13430`, `AE#13288` |
| `TESObjectCELL::AE_19130(cell, bool)` | 3D out of `objRoot` (vfunc `0x38`), world inactive | `+0x96` `kDetaching` (5), `+0x4BB` `kUnattached` | `SetCurrentCell`, `AE#13317`, `AE#13315`, `AE#13721`, `GridCellArray::Detach` |
| `TESDataHandler::AE_13721(dh, cell)` | unload: `AE#19130`, MOPP, terrain, actors, grass | `+0xC3` `kUnloading` (1); `AE#18981` → `kUnloaded` | ~20 places: buffers, `SetCurrentCell`, `Cancel`, grid |
| `TESObjectCELL::AE_19046(cell,…)` | cell actors into `ProcessLists` on detach (under cell lock `+0x120`) | — | `AE#13317`, `AE#13721` |

State table (`TESObjectCELL+0x44`):

| State | Who sets | Thread |
|---|---|---|
| `kUnloaded` 0 | `AE#18981` (from `AE#13721`) | M or J (unload from `PostProcess`) |
| `kUnloading` 1 | `AE#13721+0xC3` | M or J |
| `kDataLoaded` 2 | `AE#19112+0x181` | M (under the screen) or IO (`Run`, interior) |
| `kBuilding3D` 3 → `kUnattached` 4 | `AE#18978+0x79` / `+0xC2C` | IO (`Run`) or M |
| `kDetaching` 5 → `kUnattached` 4 | `AE#19130+0x96` / `+0x4BB` | M |
| `kAttaching` 6 | `AE#18976+0x76` | M |
| `kAttached` 7 | `AE#19063+0x33C` | M |

## 7. Buffers (`TES+0xC8`, `TES+0xD0`)

Lengths — INI values **[code]** (H9): `interiorBuffer` — `uInterior Cell Buffer` (`AE#358814`, 3), `exteriorBuffer` — `uExterior Cell Buffer` (`AE#358817`, 36).

- `AE#13434` — put a cell into the buffer: interior → `TES::AE_13435` (to the head of `interiorBuffer`, the last one is unloaded by `AE#13721`), exterior → to the head of `exteriorBuffer`, the evicted one is unloaded.
- `TES::AE_13370` — remove the interior from the buffer and unload it (only from `AE#40845`). `TES::AE_13436(exceptCurrent)` — unload the whole `interiorBuffer`.
- `TES::PurgeBufferedCells` (`AE#13299`): from `SetCurrentCell+0x62A`, `AE#13317+0x392`, `Main::AE_36601` (`+0x367`, `+0x84D`).
- **`CellLoaderTask::PostProcess` (`AE#18529`) calls `AE#13434`** — the `TES` buffer is changed and cells are unloaded (`AE#13721`) on J threads **[code + game]**. There is no lock around the buffer in `AE#13434` **[code]**; matching the main thread's frame phase — **[?]** (below).

## 8. Loading threads

```
queues (M): AE#13288 → ExteriorCellLoader::QueueCellLoad (AE#18541)      exterior, key (x,y)
            AE#40845 → AE#13378 → AE#18540(loader, cell, true)            interior, key formID
   both: CellLoaderTask (vtable AE#189967) into the loader map (+0x10) and IOManager vfunc ResubmitTask
IO:  BSTaskManagerThread::vf_0x1 (AE#75651+0x1A0) → CellLoaderTask::Run (AE#18527)
       state 0→1; interior AE#19111 / exterior TESWorldSpace::LoadCell (AE#20460) or AE#13710;
       AE#18978 (3D), terrain, water; AE#441574 (references, under cell lock +0x120); state 2
     BSTaskManager::vf_0x13 (AE#75636+0x20) → CellLoaderTask::Finish (AE#18528)  state 2→3, into the ready queue
J/M: IOManager::AE_75705 (+0xEF) → CellLoaderTask::PostProcess (AE#18529)
       state 3→4; from the loader map (AE#18546 / AE#18547); AE#13434 (TES buffer); state 5
       AE#75705 callers: Main::Update+0x5C7 (M), AE#36565 ← Job_IO (AE#39084) (J), AE#75708 (M)
Cancel (AE#18530): from the map; TES::AE_13308 → AE#13721
```

In game **[game]**: `Run` and `Finish` — one `tid` per launch (IO), `PostProcess` — 5–6 different `tid`s (J), 1,428 of 1,428. Job_IO wraps `AE#75705` in the same `bhkWorld` calls as `Main::Update+0x5AC…+0x5E9` (`AE#36565`) **[code]** — probably it is the same frame phase, executed by a job **[?]**.

Who waits (M, all — `Sleep` in a loop):
- `IOManager::AE_75708(priority)` — wait for the IO queue down to priority, inside `AE#75705` and `AE#75686`; callers: `SetCurrentCell` (2), `AE#13317` (4), `AE#13428` (4), `AE#40438` (5), `AE#40744` (5), save loading `AE#35567`, `AE#36549`, `Main::AE_36601`. In the cold-load profile 70% of samples — `AE#75686 < AE#75708 < AE#13316/13317/35567` **[game]** (0.5.0–0.7.1).
- `AE#40438+0x845` — `Sleep(10)` loop over the `ProcessLists` counter (`AE#404445`).
- `AE#13363` on close — `Sleep(30)` × 2.
- `IOManager::AE#75712/75713` — pause/resume IO around synchronous reads (`AE#19111`, `AE#13288`).

Models (`QueuedFile`), Havok, navmesh, grass: in this task only the entry points are read — grass (`AE#13277`, `BGSGrassManager::AddCellGrass` `AE#15372`) and navmesh (`NavMeshObstacleManager::AE_30451` at the end of transitions); `QueuedFile`/`QueuedReference` (`AE#13015`, `AE#13005`) call `AE#13355` → `AE#18978` — **batch 3** in the T39 result.

## 9. Events

| Event | From | Thread **[game]** |
|---|---|---|
| `PositionPlayerEvent` 0/3/4 | `AE#41051` from `AE#40438+0x55A/+0x885/+0x8DD`; 1 and 2 are not sent (H2) | M (222/221/221) |
| `BGSActorCellEvent` leave/enter | leave — `AE#40744+0xE6` (`AE#19032`), enter — `AE#19411` | M |
| `TESActorLocationChangeEvent`, `CLOC` | `AE#40466` from `AE#19411+0x251` | M |
| `TESCellAttachDetachEvent` | `AE#19361` (from `AE#18976`), `AE#19055` (from `AE#19063`) — FINDINGS | M (36) |
| `TESCellFullyLoadedEvent` | sender not found (batch 2) | interior — M (56/56); exterior — X 1,624, M 52 |

## 10. Loading screen

`AE#13363(open, location, allBuffered, …)` — open/close **[code]**:
- open: flags `AE#400336` (screen raised), `BGSTerrainManager::LoadScreenUp`, sky `Sky::AE_26265`, `Rumble::DisableRumble`, `SubtitleManager::KillSubtitles`, `AE#52852` → `UIMessageQueue::AddMessage(Loading Menu, kShow, LoadingMenuData{location, flag})`, `AE#35407(1)`, `ImageSpaceManager AE#105685`;
- close: update terrain, `AE#19118`, `UIMessageQueue::AddMessage(Loading Menu, 3)`, fade and Mist Menu, `Sleep(30)` × 2, `BGSSaveLoadManager::AE_35772`, `AE#400336 = 0`.
- Who opens: `AE#40438` (every transition if the target is not the current cell), `SetCurrentCell+0x65C`, `AE#13317+0x43E`, `AE#13288+0xC88`, `Actor::AE_40445`, `PlayerCharacter::ServePrisonTime`.

What is done **only under the screen** or depends on it: `AE#13288` shifts the grid without waiting for the queue; `AE#13430` passes `!AE#400336` to `AE#19063`; `SetCurrentCell` calls `Main::AE_36555` only without the screen; all `AE#75708` waits, the `Sleep(10)` loop, Havok warm-up on `kPost` (FINDINGS H4+). None of them is mandatory for a seamless transition: the screen is UI and flags, and the work (attaching, events, `SetParentCell`) is done without it too **[code]**; the waits are needed only if the cell is not loaded in advance **[?]** — the check is T40.

## 11. What is designed for a single instance (5.14)

| Place | One per game | Consequence |
|---|---|---|
| `TES::interiorCell` (`+0xC0`), `saveGridX/Y` | one current interior, one saved grid | `SetCurrentCell` detaches the old interior and grid: interior ↔ interior and exterior ↔ interior without detaching — only past it (like `InteriorSim`) |
| `gridCells` (`+0x78`), `worldSpace` (`+0x148`), `currentGrid`/`currentQueued` | one grid of one world | the game cannot hold a second world behind the gates; `AE#13315` does `DetachAll` |
| `ExteriorCellLoader` task map | key `(x,y)` without world, interior — `formID` in the same map | two grids of two worlds with the same `(x,y)` will merge |
| Exterior Havok world `AE#400440`, flag `+0xC5F9` | one, switched off when entering an interior | exterior ↔ exterior, street life from inside — a custom step is needed |
| `ShadowSceneNode::portalGraph` (`SSN+0x228`) | one graph | `SetCurrentCell+0x37C` and `AE#13317+0x78` reset it |
| `LoadedAreaBound` (`TES+0x2B8`), `BSShaderManager gridArrayCenter`, sky `TES+0x100` (`AE#13326`) | one area, one center, one sky mode | depend on the player, not the camera |
| Buffers `interiorBuffer` 3, `exteriorBuffer` 36 | shared | many portals evict each other (H4, 0.8.1) |
| Traversals `AE#13291`, `AE#13325`, `AE#13388`, `AE#13415` | "interior **or** grid + sky cell" | the game does not update a second attached interior |

Interior only: `SetCurrentCell`, `interiorBuffer`, `AE#13435/13436/13370`, cell portal graph. Exterior only: `AE#13288/13428/13430/13362`, `exteriorBuffer`, `QueueCellLoad`, grass, `LoadedAreaBound::SetCurrentCell`, world `AE#400440`. World change only: `AE#13315`. The grid is also reset by save loading (`AE#13295+0x64`) and `TES` destruction (`AE#13280+0x1AC` ← `~TES` `AE#13627`; in game 36 times on M — when exactly, not determined).

## 12. Plugin hooks on the map

| Place | Hook | What it changes |
|---|---|---|
| `AE#13316` | detour `HooksTes.cpp` | before the original `InteriorSim::Detach` (detach our interior), original under `GuardWorlds` |
| `AE#13299` | detour | same; grid snapshot |
| `AE#18541`, `AE#40437`, `AE#56626` | detour | trace only |
| `AE#13419` (TES as `BSTEventSink<PositionPlayerEvent>`), `AE#61410` | detour | warm-up measurement |
| `AE#13370`, `AE#18547` (from `AE#40845`), `AE#13721` | detour (`Preload`) | do not unload the held interior; `AE#13721` also fires on J |
| `AE#13434` | detour | original under `GuardWorlds`; on J too |
| `TESObjectDOOR` `0x37`; `GridCellArray` 1–8; `CellLoaderTask` 1,2,3,4,6 | vfunc `HooksCells.cpp` | trace |
| `AE#106385`, `AE#19130`, `AE#13291`, `AE#13325`, `AE#77851` | detour `HooksInteriorSim.cpp` | portal graph when we attach; remove our 3D; traversals for the attached interior |
| calls: `AE#18976`, `AE#19063`, `AE#77905/77906`, `AE#19130`, `AE#19046`, `AE#18540` | `InteriorSim.cpp`, `Preload.cpp` | attaching the interior bypassing `SetCurrentCell` |

## 13. Map and old traces

Matched **[game]**: `SetCurrentCell` only from `AE#40744`, only M, only for interior; order `Loading Menu` → `kPre` → `leave` → `SetCurrentCell` → `enter` → `LocationChange` → `kPost` → `kFinish` → close in one frame (0.7.1, `seq 713–732`); `QueueCellLoad`/`SetCenter` from `AE#13288`/`AE#13428` on M; `Run`+`Finish` on one IO thread, `PostProcess` on J; `DoorActivate` on X ~38 frames before the transition (H1); the `AE#75708` wait in the profile. One discrepancy: `DetachAll` from `AE#13295` (save loading) 44 times not on M — the save loading thread is not traced in code (batch 1).
