# Карта движка (бывший раздел 3 `CLAUDE.md`)

Источник — заголовки CommonLibSSE-NG v13.1.0. Текст перенесён без изменений; номера подразделов 3.x сохранены. Правила и окружение — `CLAUDE.md`.

**С 08.11.2026 по машинному коду (база Ghidra):** путь от двери до `kAttached`, потоки, буферы, состояния ячейки, экран загрузки — `docs/CELL_LOADING_MAP.md` (T39); игрок, его поля, глобалы и подсистемы — `docs/PLAYER_MAP.md` (T38). Поток 3.6 там подтверждён и уточнён: очередь `AE#40442` → `AE#40438` → `AE#40744` → `SetCurrentCell`/`AE#13317` → `SetParentCell`. Длины буферов (3.2) — INI `uInterior/uExterior Cell Buffer`.

## 3. Что уже известно: карта движка

Источник — заголовки CommonLibSSE-NG v13.1.0. IDs даны как SE / **AE**; для 1.6.1170 используются AE-ID через `RELOCATION_ID(se, ae)` или `REL::VariantID`.

### 3.1 Понятия

- **Загрузка идёт по ячейкам (`TESObjectCELL`), не по локациям.**
  - Экстерьерная ячейка — квадрат 4096×4096 единиц в `TESWorldSpace`. У мира есть `cellMap`, `persistentCell` и `skyCell`.
  - Интерьер — самостоятельная `TESObjectCELL`. Список интерьеров — `TESDataHandler::interiorCells`.
- **`BGSLocation`** — логическая метка (`parentLoc`, `cleared`). Привязана к ячейкам через `TESObjectCELL::GetLocation()` (18474 / **18905**). Сама ничего не грузит, но влияет на `OnLocationChange` и выбор экрана загрузки.
- **Состояния ячейки (`TESObjectCELL::CellState`):** `kUnloaded=0, kUnloading=1, kDataLoaded=2, kBuilding3D=3, kUnattached=4, kDetaching=5, kAttaching=6, kAttached=7`. `IsAttached()` проверяет `== kAttached`.
- Данные загруженной ячейки — `TESObjectCELL::loadedData` (`LOADED_CELL_DATA`): `cell3D`, `encounterZone`, счётчики `queuedRefCount` / `criticalQueuedRefCount` / `queuedDistantRefCount` и `refsFullyLoaded`.

### 3.2 `RE::TES` (синглтон 516923 / **403450**)

Поля до 0x128 одинаковы во всех runtime:

| Поле | Смещение | Смысл |
|---|---|---|
| `gridCells` (`GridCellArray*`) | 0x78 | сетка экстерьера `length × length`, `length` берётся из `uGridsToLoad` |
| `currentGridX/Y` | 0xB0/0xB4 | центр сетки |
| `currentQueuedX/Y` | 0xB8/0xBC | **гипотеза:** центр, поставленный в очередь (предзагрузка) |
| `interiorCell` | 0xC0 | текущий интерьер или null |
| `interiorBuffer` / `exteriorBuffer` (`TESObjectCELL**`) | 0xC8/0xD0 | кэш покинутых ячеек; **длина массивов не подтверждена** |
| `saveGridX/Y` | 0xE0/0xE4 | |
| `sky` | 0x100 | |

На 1.6.1170 (≥ 1.6.1130) действует `AE_RUNTIME_DATA` с 0x128: `gridBufferDistanceX/Y`, `allowUnusedPurge`. `RUNTIME_DATA2` начинается с **0x148** (`worldSpace`, `playerCharacter`, `loadedAreaBound`, ...).

**Всё, что дальше 0x128, читай только через `GetAERuntimeData()` и `GetRuntimeData2()`. Прямой доступ к этим полям запрещён.**

### 3.3 Ключевые функции

| Функция | SE | **AE** | Тип |
|---|---|---|---|
| `TES::SetCurrentCell(TESObjectCELL*, const NiPoint3&, float)` — по комментарию в заголовке, ядро всех переходов (интерьер, `coc`, загрузка мира) | 13171 | **13316** | обычная |
| `TES::PurgeBufferedCells()` — аналог консольной `pcb` | 13159 | **13299** | обычная |
| `TES::GetCell(const NiPoint3&)` | 13177 | **13322** | обычная |
| `ExteriorCellLoader` синглтон | 514741 | **400899** | указатель |
| `ExteriorCellLoader::QueueCellLoad(TESWorldSpace*, int16 x, int16 y, bool ?)` | 18150 | **18541** | обычная |
| `IOManager` синглтон | 524542 | **411121** | указатель |
| `PlayerCharacter::CenterOnCell_Impl(const char*, TESObjectCELL*)` — это `coc` | 39365 | **40437** | обычная |
| `TESObjectREFR::MoveTo_Impl(const ObjectRefHandle&, TESObjectCELL*, TESWorldSpace*, const NiPoint3& pos, const NiPoint3& rot)` | 56227 | **56626** | обычная |
| `BGSSaveLoadManager::LoadMostRecentSaveGame()` | 34856 | **35766** | обычная |

**Виртуальные функции** (хук через `REL::Relocation<std::uintptr_t>{ RE::VTABLE_X[0] }.write_vfunc(idx, thunk)`):

| Класс / vtable | Индекс | Функция |
|---|---|---|
| `TESObjectDOOR` (`VTABLE_TESObjectDOOR[0]`) | 0x37 | `Activate(TESObjectREFR* target, TESObjectREFR* activator, uint8, TESBoundObject*, int32)` |
| `GridCellArray` (`VTABLE_GridCellArray[0]`) | 1 / 2 / 3 / 4 / 5 / 6 / 7 / 8 | `DetachAll` / `KillAll` / `SetCenter(x,y)` / `Shift(x,y)` (из базового `GridArray`) / `Detach(x,y)` / `ClearItem` / `MoveItem` / `SwapItem` |
| `CellLoaderTask` (`VTABLE_CellLoaderTask[0]`) | 1 / 2 / 3 / 4 / 6 | `Run` (фоновый поток) / `Finish` / `Cancel` / `GetDescription` / `PostProcess` |
| `PlayerCharacter` (`VTABLE_PlayerCharacter[0]`) | 0xAD | `Actor::Update(float)` — поэтапный тик для автотеста |

**Поля `CellLoaderTask`** (через `GetRuntimeData()`): `loader`, `world`, `cell`, `x`, `y`, `state`, `interior`.

Флаг `interior` по комментарию в заголовке выбирает путь загрузки интерьера. Значит, **интерьеры тоже могут грузиться через `CellLoaderTask`** — это гипотеза H7.

### 3.4 Данные телепорта

- **`ExtraTeleport` → `DoorTeleportData`:** `linkedDoor` (`ObjectRefHandle`), `position`, `rotation`, `flags`. Есть у загрузочных дверей.
- **`PlayerCharacter::GetPlayerRuntimeData().queuedTargetLoc` (`PLAYER_TARGET_LOC`):** `world`, `interior`, `location`, `angle`, `arrivalFunc`, `arrivalFuncData`, `furnitureRef`, `fastTravelMarker`, `resetWeather`, `allowAutoSave`, `isValid`.
- **Там же:**
  - `currentLocation` (`BGSLocation*`);
  - `playerFlags.travelUseDoor`, `playerFlags.fastTraveling`, `playerFlags.isLoading`.
- **`LoadingMenu`** (`MENU_NAME = "Loading Menu"`) хранит `currentLocation`, `loadScreens`, `loadScreen`. В `LoadingMenuData` есть `currentLocation`.

### 3.5 События

| Событие | Источник | Поля |
|---|---|---|
| `PositionPlayerEvent` | `player->AsPositionPlayerEventSource()` | `type`: `kPre=0, kPreUpdatePackages=1, kPostUpdatePackages=2, kPost=3, kFinish=4`. Его слушают и сами `TES`, и `Main` |
| `BGSActorCellEvent` | `player->AsBGSActorCellEventSource()` | `actor`, `cellID`, `flags` (`kEnter=0` / `kLeave=1`, это значение, а не битовая маска) |
| `TESCellAttachDetachEvent` | `ScriptEventSourceHolder` | `reference`, `attached` |
| `TESCellFullyLoadedEvent` | `ScriptEventSourceHolder` | `cell` |
| `TESMoveAttachDetachEvent` | `ScriptEventSourceHolder` | `movedRef`, `isCellAttached` |
| `TESActorLocationChangeEvent` | `ScriptEventSourceHolder` | `actor`, `oldLoc`, `newLoc` |
| `TESFastTravelEndEvent` | `ScriptEventSourceHolder::GetEventSource<>()` (SE/AE) | `fastTravelEndHours` |
| `TESLoadGameEvent` | `ScriptEventSourceHolder` | |
| `MenuOpenCloseEvent` | `RE::UI` | `menuName`, `opening` |

### 3.6 Реконструированный поток перехода через дверь (подтверждён и уточнён по коду в `docs/CELL_LOADING_MAP.md`, разделы 2 и 13)

```
TESObjectDOOR::Activate → ExtraTeleport → queuedTargetLoc (isValid=1)
→ LoadingMenu open
→ PositionPlayerEvent kPre
→ TES::SetCurrentCell (старое → буфер, новое грузится и attach)
→ kPreUpdatePackages → kPostUpdatePackages → kPost → kFinish
→ TESCellFullyLoadedEvent / BGSActorCellEvent / TESActorLocationChangeEvent
→ LoadingMenu close
```

Пеший переход по экстерьеру (тоже не подтверждён):

```
GridCellArray::SetCenter → Detach крайних ячеек → QueueCellLoad
→ CellLoaderTask::Run (IO-поток) → Finish / PostProcess (главный поток)
```

---

