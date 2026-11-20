# Engine map (former section 3 of `CLAUDE.md`)

Source — CommonLibSSE-NG v13.1.0 headers. The text is copied unchanged; the subsection numbers 3.x are kept. Rules and environment — `CLAUDE.md`.

**Since 08.11.2026, based on machine code (Ghidra base):** the path from the door to `kAttached`, threads, buffers, cell states, loading screen — `docs/CELL_LOADING_MAP.md` (T39); the player, his fields, globals, and subsystems — `docs/PLAYER_MAP.md` (T38). Thread 3.6 is confirmed and refined there: the `AE#40442` queue → `AE#40438` → `AE#40744` → `SetCurrentCell`/`AE#13317` → `SetParentCell`. Buffer lengths (3.2) — INI `uInterior/uExterior Cell Buffer`.

## 3. What is already known: the engine map

Source — CommonLibSSE-NG v13.1.0 headers. IDs are given as SE / **AE**; for 1.6.1170 the AE IDs are used via `RELOCATION_ID(se, ae)` or `REL::VariantID`.

### 3.1 Concepts

- **Loading happens per cell (`TESObjectCELL`), not per location.**
  - An exterior cell is a 4096×4096 unit square in a `TESWorldSpace`. A world has `cellMap`, `persistentCell`, and `skyCell`.
  - An interior is a standalone `TESObjectCELL`. The list of interiors is `TESDataHandler::interiorCells`.
- **`BGSLocation`** — a logical label (`parentLoc`, `cleared`). Bound to cells via `TESObjectCELL::GetLocation()` (18474 / **18905**). It loads nothing by itself, but affects `OnLocationChange` and the loading-screen selection.
- **Cell states (`TESObjectCELL::CellState`):** `kUnloaded=0, kUnloading=1, kDataLoaded=2, kBuilding3D=3, kUnattached=4, kDetaching=5, kAttaching=6, kAttached=7`. `IsAttached()` checks `== kAttached`.
- The data of a loaded cell — `TESObjectCELL::loadedData` (`LOADED_CELL_DATA`): `cell3D`, `encounterZone`, counters `queuedRefCount` / `criticalQueuedRefCount` / `queuedDistantRefCount`, and `refsFullyLoaded`.

### 3.2 `RE::TES` (singleton 516923 / **403450**)

Fields up to 0x128 are the same in all runtimes:

| Field | Offset | Meaning |
|---|---|---|
| `gridCells` (`GridCellArray*`) | 0x78 | exterior grid `length × length`; `length` is taken from `uGridsToLoad` |
| `currentGridX/Y` | 0xB0/0xB4 | center of the grid |
| `currentQueuedX/Y` | 0xB8/0xBC | **hypothesis:** the center that has been queued (preload) |
| `interiorCell` | 0xC0 | current interior or null |
| `interiorBuffer` / `exteriorBuffer` (`TESObjectCELL**`) | 0xC8/0xD0 | cache of vacated cells; **array lengths unconfirmed** |
| `saveGridX/Y` | 0xE0/0xE4 | |
| `sky` | 0x100 | |

On 1.6.1170 (≥ 1.6.1130) `AE_RUNTIME_DATA` applies from 0x128: `gridBufferDistanceX/Y`, `allowUnusedPurge`. `RUNTIME_DATA2` starts at **0x148** (`worldSpace`, `playerCharacter`, `loadedAreaBound`, ...).

**Read everything past 0x128 only through `GetAERuntimeData()` and `GetRuntimeData2()`. Direct access to these fields is forbidden.**

### 3.3 Key functions

| Function | SE | **AE** | Type |
|---|---|---|---|
| `TES::SetCurrentCell(TESObjectCELL*, const NiPoint3&, float)` — per the header comment, the core of all transitions (interior, `coc`, world load) | 13171 | **13316** | regular |
| `TES::PurgeBufferedCells()` — the analog of the console `pcb` | 13159 | **13299** | regular |
| `TES::GetCell(const NiPoint3&)` | 13177 | **13322** | regular |
| `ExteriorCellLoader` singleton | 514741 | **400899** | pointer |
| `ExteriorCellLoader::QueueCellLoad(TESWorldSpace*, int16 x, int16 y, bool ?)` | 18150 | **18541** | regular |
| `IOManager` singleton | 524542 | **411121** | pointer |
| `PlayerCharacter::CenterOnCell_Impl(const char*, TESObjectCELL*)` — this is `coc` | 39365 | **40437** | regular |
| `TESObjectREFR::MoveTo_Impl(const ObjectRefHandle&, TESObjectCELL*, TESWorldSpace*, const NiPoint3& pos, const NiPoint3& rot)` | 56227 | **56626** | regular |
| `BGSSaveLoadManager::LoadMostRecentSaveGame()` | 34856 | **35766** | regular |

**Virtual functions** (hook via `REL::Relocation<std::uintptr_t>{ RE::VTABLE_X[0] }.write_vfunc(idx, thunk)`):

| Class / vtable | Index | Function |
|---|---|---|
| `TESObjectDOOR` (`VTABLE_TESObjectDOOR[0]`) | 0x37 | `Activate(TESObjectREFR* target, TESObjectREFR* activator, uint8, TESBoundObject*, int32)` |
| `GridCellArray` (`VTABLE_GridCellArray[0]`) | 1 / 2 / 3 / 4 / 5 / 6 / 7 / 8 | `DetachAll` / `KillAll` / `SetCenter(x,y)` / `Shift(x,y)` (from base `GridArray`) / `Detach(x,y)` / `ClearItem` / `MoveItem` / `SwapItem` |
| `CellLoaderTask` (`VTABLE_CellLoaderTask[0]`) | 1 / 2 / 3 / 4 / 6 | `Run` (background thread) / `Finish` / `Cancel` / `GetDescription` / `PostProcess` |
| `PlayerCharacter` (`VTABLE_PlayerCharacter[0]`) | 0xAD | `Actor::Update(float)` — stepwise tick for the automated test |

**`CellLoaderTask` fields** (via `GetRuntimeData()`): `loader`, `world`, `cell`, `x`, `y`, `state`, `interior`.

The `interior` flag, per the header comment, selects the interior-loading path. This means **interiors can also be loaded through `CellLoaderTask`** — this is hypothesis H7.

### 3.4 Teleport data

- **`ExtraTeleport` → `DoorTeleportData`:** `linkedDoor` (`ObjectRefHandle`), `position`, `rotation`, `flags`. Present on load doors.
- **`PlayerCharacter::GetPlayerRuntimeData().queuedTargetLoc` (`PLAYER_TARGET_LOC`):** `world`, `interior`, `location`, `angle`, `arrivalFunc`, `arrivalFuncData`, `furnitureRef`, `fastTravelMarker`, `resetWeather`, `allowAutoSave`, `isValid`.
- **Also there:**
  - `currentLocation` (`BGSLocation*`);
  - `playerFlags.travelUseDoor`, `playerFlags.fastTraveling`, `playerFlags.isLoading`.
- **`LoadingMenu`** (`MENU_NAME = "Loading Menu"`) holds `currentLocation`, `loadScreens`, `loadScreen`. `LoadingMenuData` has `currentLocation`.

### 3.5 Events

| Event | Source | Fields |
|---|---|---|
| `PositionPlayerEvent` | `player->AsPositionPlayerEventSource()` | `type`: `kPre=0, kPreUpdatePackages=1, kPostUpdatePackages=2, kPost=3, kFinish=4`. Listened to by both `TES` itself and `Main` |
| `BGSActorCellEvent` | `player->AsBGSActorCellEventSource()` | `actor`, `cellID`, `flags` (`kEnter=0` / `kLeave=1`; this is a value, not a bitmask) |
| `TESCellAttachDetachEvent` | `ScriptEventSourceHolder` | `reference`, `attached` |
| `TESCellFullyLoadedEvent` | `ScriptEventSourceHolder` | `cell` |
| `TESMoveAttachDetachEvent` | `ScriptEventSourceHolder` | `movedRef`, `isCellAttached` |
| `TESActorLocationChangeEvent` | `ScriptEventSourceHolder` | `actor`, `oldLoc`, `newLoc` |
| `TESFastTravelEndEvent` | `ScriptEventSourceHolder::GetEventSource<>()` (SE/AE) | `fastTravelEndHours` |
| `TESLoadGameEvent` | `ScriptEventSourceHolder` | |
| `MenuOpenCloseEvent` | `RE::UI` | `menuName`, `opening` |

### 3.6 Reconstructed door-transition flow (confirmed and refined against code in `docs/CELL_LOADING_MAP.md`, sections 2 and 13)

```
TESObjectDOOR::Activate → ExtraTeleport → queuedTargetLoc (isValid=1)
→ LoadingMenu open
→ PositionPlayerEvent kPre
→ TES::SetCurrentCell (old → buffer, new one loads and attaches)
→ kPreUpdatePackages → kPostUpdatePackages → kPost → kFinish
→ TESCellFullyLoadedEvent / BGSActorCellEvent / TESActorLocationChangeEvent
→ LoadingMenu close
```

On-foot transition across the exterior (also unconfirmed):

```
GridCellArray::SetCenter → Detach of the edge cells → QueueCellLoad
→ CellLoaderTask::Run (IO thread) → Finish / PostProcess (main thread)
```

---
