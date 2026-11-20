# Карта загрузки ячеек: от двери до `kAttached` (T39)

SkyrimSE.exe 1.6.1170, база Ghidra (T33–T36), трассы `logs/run-0.2.0` … `run-0.8.32`. Номера — Address Library AE; `+0x…` — место вызова внутри функции. Псевдокода здесь нет, только выводы.

Пометки достоверности: **[код]** — по коду игры (цепочка вызовов в базе), **[игра]** — строка трассы, **[?]** — предположение. Имена без NG — рабочие (кандидаты куратору в `docs/tasks/results/T39.md`).

Потоки: **M** — главный (`Main::Update`), **IO** — поток `BSTaskManagerThread` (один на запуск), **J** — рабочие потоки заданий (5–6 разных `tid`), **X** — не главный, какой именно — не выяснено.

## 1. Точки входа

Все переходы игрока, кроме ходьбы, сходятся в одну функцию: `AE#40438` («обработать `queuedTargetLoc`»). Её вызывает `Main::Update` (`AE#36564+0x6E`) каждый кадр, если `queuedTargetLoc.isValid` (`player+0x68A`) **[код]**.

| Вход | Цепочка | Поток |
|---|---|---|
| Дверь с загрузкой | `TESObjectDOOR::Activate` (`AE#17922`, vtable `0x37`) ничего не ставит в очередь **[код]**. Телепорт ставит `BGSOpenCloseForm::HandleOpen` двери (`AE#17926`, слот 1 подтаблицы `+0x88`): сразу `AE#17924` или через затемнение `NormalDoorFadeCallback` (`AE#17968`) → `AE#17924+0x1B1` → `PlayerCharacter::AE_40442` (поставить `queuedTargetLoc`) → следующий `Main::Update` → `AE#40438` | `Activate` — X (2 330 из 2 343 вызовов, `AE#19796+0x6C0`) **[игра]**; `HandleOpen` и колбэк затемнения — X **[?]**; `AE#40438` — M |
| `coc` | `ConsoleCommand::CenterOnCell` (`AE#22873`) → `CenterOnCell_Impl` (`AE#40437`): ставит `isValid` (`+0x263`) и сразу зовёт `AE#40438` (`+0x26A`) — синхронно, не через кадр **[код]** | M (7 из 7, `AE#22873+0x60`) **[игра]** |
| `MoveTo` игрока | `MoveTo_Impl` (`AE#56626+0x18B`) → `AE#40442` → кадр → `AE#40438` | вызывающий; переход — M |
| Быстрое перемещение | `MapMenu::vf_0x4` (`AE#53102+0x47D`), `ScriptCommand::FastTravel` (`AE#22612`), `Papyrus::Game::FastTravel` (`AE#55457`) → `AE#40442`; в `AE#40438` ветка маркера `fastTravelMarker` (`+0x684`) → `Actor::AE_40445` (`+0x5C7`) | меню — X **[?]**; переход — M |
| Ходьба (сетка) | `Main::Update+0x825` → `TES::AE_13288` (обновить сетку) | M |
| Смена мира (ворота) | дверь → `AE#40438` → `AE#40745` (цель с миром) → `AE#40744` → `TES::AE_13315(world,1)` (`+0x155`) и `TES::AE_13317` (`+0x16E`) | M |
| Загрузка сохранения | `Main::Update+0x9A` → `BGSSaveLoadManager::AE_35772` (очередь сохранений и загрузок) `+0x465` → `AE#442580` → `AE#35600` → … → `AE#35652` (чтение позиции игрока): `TES::AE_13315`, `AE#40744`, `AE#13288`, `AE#13317`, `AE#40745`, `TES::AE_13295` (`+0x67A`, `DetachAll`) | M; `DetachAll` из `AE#13295+0x64` 44 раза из 126 — X **[игра]** |
| Старт игры | `AE#36544` → `AE#36549` → `AE#40442`, `AE#40438`, `SetCurrentCell` (`+0x1F0`) | M |
| Предзагрузка интерьеров | `PlayerCharacter::Update` (vtable `0xAD`) → `AE#40845` → `AE#13378` → `AE#18540(loader, cell, true)` → `CellLoaderTask` (`interior=1`) | M ставит, IO грузит |

## 2. Ядро перехода `AE#40438` (M)

Порядок **[код]** и в игре: дверь в Bannered Mare, 0.7.1, `seq 713–732`, всё в кадре 3936 **[игра]**.

```
AE#40438(player)
 ├ +0x3C7/+0x4D0 TES::AE_13425      все ли ячейки сетки цели уже в exteriorBuffer
 ├ +0x4FB/+0x534/+0x642 AE#13363(1, location, …)   открыть Loading Menu   (seq 713)
 │        (не открывает, если AE#400337: цель = текущая ячейка, +0x53F)
 ├ +0x550 AE#32491, +0x55A AE#41051(kPre=0)        PositionPlayerEvent    (seq 716)
 ├ одна из трёх веток:
 │   +0x5C7 Actor::AE_40445        к маркеру/ссылке (быстрое перемещение)
 │   +0x6D4 AE#40744               цель — интерьер (queuedTargetLoc.interior)
 │   +0x741 AE#40745 → AE#40744    цель — мир и позиция
 ├ +0x82D AE#41294, +0x839 BGSStoryTeller::AE_32484
 ├ +0x845…+0x86E  цикл: пока AE#404445 ≠ 0 → AE#41313 + Sleep(10)   (ждёт ProcessLists)
 ├ +0x885 AE#41051(kPost=3)  → TES (AE#13419) прогрев Havok (FINDINGS, H4+)
 ├ +0x8B2 IOManager::AE_75708(5)   ждать очередь ввода-вывода
 ├ +0x8DD AE#41051(kFinish=4)
 ├ +0x916/+0x927 автосохранение (AE#13291, BGSSaveLoadManager::AE_35769) — если флаг 0x80 в player+0xBE4
 ├ +0x952 AE#13363(0, …, 1)        закрыть Loading Menu   (seq 732)
 └ +0x95E TES::AE_13388; очистка queuedTargetLoc (+0x98B…+0xA28)
```

`AE#40744(player, pos, rot, cell, resetSky)` — перенос игрока **[код]**:
`+0xE6` `AE#19032` (уйти из старой ячейки; в игре `ActorCell leave`, `seq 719`) → `+0xF7` `SetPosition` → `+0x131` поле `parentCell = cell` (`AE#19818`, не vfunc) → интерьер: `+0x14E` **`TES::SetCurrentCell`** / экстерьер: `+0x155` `TES::AE_13315(world,1)`, `+0x16E` **`TES::AE_13317`** → `+0x18D` поле `parentCell = null` → `+0x19B` `TESObjectCELL::AE_19411(cell, player)`, внутри `+0x251` vfunc `0x98` = `PlayerCharacter::SetParentCell` (`AE#40466`, `CLOC` и `OnLocationChange`; в игре `ActorCell enter` и `ActorLocationChange` после `SetCurrentCell.leave`, `seq 721–724`) → `+0x1F6` `AE#75708(5)`, если у игрока нет 3D → `+0x5C4…+0x66E` 3D игрока, `Actor::Update`, `BGSTerrainManager::Update` → `+0x72C` `TES::AE_13291` → `+0x7A0` `TES::AE_13398`.

**Итог для T20/T16:** `SetParentCell` (vfunc `0x98`) идёт **после** подключения ячейки, один раз на `AE#40744` **[код + игра]**.

## 3. Интерьер: `TES::SetCurrentCell` (`AE#13316`, M)

Параметры в коде — только `cell` и `pos` (декомпилятор видит три аргумента с `this`); четвёртый аргумент NG (`unk4`, H8) не читается **[код]**. В игре всегда зовётся из `AE#40744+0x14E` (106 из 106) **[игра]**.

```
+0x1E   exteriorWorld(AE#400440)+0xC5F9 = 0      мир Havok экстерьера неактивен
+0x44   cell == interiorCell → AE#13288(pos,0) и выход
+0x7A…+0x221  только при bPreemptivelyUnloadCells (AE#358811; по умолчанию 0):
        из экстерьера: выгрузить exteriorBuffer (AE#13721);
        из интерьера: DetachAll сетки (+0x154), выгрузить exteriorBuffer, мир → null (AE#20463)
+0x273  Main::AE_36555 (если нет экрана загрузки и сохранение не грузится)
+0x288  BGSEncounterZone::AE_18199;  +0x347 BGSGrassManager::RemoveCellGrass (радиус травы)
+0x37C  ShadowSceneNode::SetPortalGraph(SSN, null);  +0x38A AE#106335
+0x3A1  AE#19130(oldInterior, 1)                 отключить старый интерьер
+0x3BB…+0x456 interiorBuffer: вынуть новую ячейку или освободить последнее место (AE#13721)
+0x466  TES::interiorCell = cell;  +0x464 AE#105631(1)
только из экстерьера (saveGridX == 0x7FFFFFFF):
  +0x49C saveGrid = currentGrid; +0x4D0 bhkWorldM::AE_77908;
  +0x50E AE#19130(gridCell, 0) для каждой kAttached ячейки сетки (остаются загруженными);
  +0x52C AE#77909; +0x53B AE#31814; +0x54C TESWorldSpace::AE_20517; lodLandRoot Update
+0x58E  AE#19130(skyCell, 1)
+0x5BF  currentGridX/Y = pos / 4096
+0x5FB  AE#53142;  +0x603 TESObjectCELL::AE_19111(cell)       загрузка данных, kDataLoaded
+0x62A  PurgeBufferedCells — при bPreemptivelyUnloadCells и ссылок > iLargeIntRefCount
+0x65C  AE#13363(1, location, 1) — экран загрузки, если у ячейки есть ссылки и она не kUnattached
+0x661  AE#18976(cell, TES::objRoot)             kBuilding3D…kAttaching, граф порталов, события
+0x678  LoadedAreaBound::Clear (TES+0x2B8)
+0x694  bhkWorld::AE_77905(cellWorld) … +0x70F AE#77906    пакет добавлений в мир ячейки
+0x6A0  ProcessLists AE#41313;  +0x6AA AE#19063(cell, 1)   kAttached
+0x6B5  TES::AE_13326 (режим неба)
+0x6CB/+0x700 AE#75708(2) вокруг AE#41313/41314/41315   ждать IO
+0x71B  TES::AE_13415;  +0x739 AE#13759;  +0x741 AE#18978 и Update 3D
+0x76D  AE#19059(cell, 1);  +0x778 AE#13435 (ячейка — в начало interiorBuffer)
+0x7B5  AE#13363(0,0,0,1) — закрыть, если открывал;  +0x7E5 NavMeshObstacleManager::AE_30451
```

Только для интерьера: вся функция (экстерьерная цель её не вызывает, H2). Сетка при входе в интерьер **не выгружается** — только отключается (`kUnattached`) и запоминается в `saveGridX/Y` **[код]**, совпадает с H12.

## 4. Экстерьер: `TES::AE_13317` (выход в мир) и `TES::AE_13315` (смена мира) (M)

`AE#13317(pos)` **[код]**: `+0x1E` `AE#105631(0)`; `+0x2D` мир Havok экстерьера активен; `+0x78` `SetPortalGraph(null)`, если граф — интерьера; `+0x82` `AE#19130(interior,1)`; `+0x1F3` `AE#19046(interior,1,0)` (в буфере) или `+0xBC` `AE#13721` (выгрузить); `+0xC1` `interiorCell = null`. Если сетка сохранена и тот же мир: `+0x16C` `GridCellArray::AE_15962` + `+0x187` `AE#19063(cell,0)` для ячеек в `kUnattached` — сетка подключается без загрузки. Иначе центр сетки = `0x7FFFFFFF`. Дальше `+0x392` `PurgeBufferedCells` и `+0x3A0` `AE#13315` (условие как в 3), `+0x3BA` **`AE#13288(pos,1)`** (полная загрузка сетки, если центр сброшен), `+0x43E` `AE#13363(1,…)`, `+0x476/+0x48A` небесная ячейка `AE#18976`/`AE#19063`, `+0x4A0…+0x4D5` `AE#75708(4)`, `+0x4DA` `GridCellArray::AE_15959`, `+0x54E` `AE#13440`, `+0x5F9` `ShadowSceneNode::AE_106355`, `+0x69F` `AE#13363(0,…)`.

`AE#13315(world, resetCenter)` — смена мира **[код]**: если мир другой — `+0x7E` `DetachAll` сетки (если есть интерьер), выгрузка `interiorBuffer` (при `bPreemptivelyUnloadCells`), смена небесной ячейки (`+0x190` `AE#19130`, `+0x19F` `AE#13721`, `+0x1BC` `AE#18976`), `TES::worldSpace = world` (`+0x202`), `+0x224` `DetachAll` (в игре 97 раз из `AE#13315+0x22E`, M **[игра]**), сброс центров `0xB0…0xE4`.

Только для смены мира: `AE#13315`. Старый мир: формы его ячеек экстерьера освобождаются (FINDINGS, 0.4.0).

## 5. Сетка: `TES::AE_13288(pos, force)` (M, каждый кадр)

**[код]**, в игре все `QueueCellLoad` (945) и `SetCenter` при ходьбе — M **[игра]**.

- `+0x98` `LoadedAreaBound::SetCurrentCell` (`AE#25946`); при загрузке сохранения `+0x52` `Clear`.
- Игрок внутри центральной ячейки (запас `TES+0x140/0x144`): при `bBackgroundCellLoads` (`AE#360413`) и без интерьера — предзагрузка полосы `uGridsToLoad` ячеек: `ExteriorCellLoader::QueueCellLoad` `+0x463`, `+0x4C3`, `+0x4F7` (в игре `+0x468`, `+0x4C8`), `currentQueuedX/Y` (H5).
- Вышел за ячейку: если есть незаконченные загрузки (`AE#18548`) и нет экрана загрузки — ждёт, пока игрок не уйдёт на 20% ячейки дальше. Затем центр, вода, `BSShaderManager` `gridArrayCenter`, `AE#13440`, `ExteriorCellLoader` `AE#18545`/`18543`/`18544`, `+0x77A` `AE#13315` (мир по умолчанию, если мира нет), `bhkWorld` `AE#77906`/`77909`/`77908`, **`+0x807` `GridCellArray::SetCenter`** (vtable 3 → `Shift` → `Detach`/`ClearItem`/`MoveItem`), прыжок > 1 ячейки — `+0x9A9` `TES::AE_13362(0)` (синхронная загрузка ячеек сетки, `TESWorldSpace::LoadCell`), `+0x9C5` **`TES::AE_13430`** (подключить сетку), трава (`RemoveCellGrass`, `AddCellGrassTask::CreateGrassLoadTaskForCell` `AE#13277`), `BGSTerrainManager::Update`.
- Центр сброшен (`0x7FFFFFFF`): `+0xC88` `AE#13363(1,…)` (экран загрузки сам), `+0xCB1` `IOManager::AE_75712` (приостановить IO, если вызов не из потока IO), `+0xE12` **`TES::AE_13428`** (полная загрузка: `DetachAll`, `SetCenter`, `AE#13362`, `AE#13430`, `AE#75708(4)`), небесная ячейка, `+0xE5B` `AE#75713`.

`TES::AE_13430` (M): для каждого места сетки `AE#13427`; ячейки в `kAttaching` → `AE#19063(cell, !loadScreenUp)`, `AE#13434` (в буфер), `AE#18978`; вокруг — `bhkWorld` `AE#77905/77906` мира экстерьера (`AE#400440`) и `AE#35567` (ожидание IO при загрузке).

## 6. Подключение и отключение ячейки

| Функция | Что делает | Состояние | Кто зовёт |
|---|---|---|---|
| `TESObjectCELL::AE_19111` | загрузка данных из файла (под `IOManager::AE_75712/75713`) → `AE#19112` | `+0x181` → `kDataLoaded` (2) | `SetCurrentCell+0x603`, `CellLoaderTask::Run` (интерьер), `AE#13315` |
| `TESObjectCELL::AE_18978` | построить или вернуть 3D ячейки | `+0x79` `kBuilding3D` (3), `+0xC2C` `kUnattached` (4) | `Run`, `SetCurrentCell+0x741`, `AE#18976+0x71`, `AE#13430` |
| `TESObjectCELL::AE_18976(cell, objRoot)` | 3D под `TES::objRoot`, мир Havok активен, граф порталов `AE#106385`, события `AE#19361` (FINDINGS) | `+0x76` `kAttaching` (6) | `SetCurrentCell+0x661`, `AE#15962` (сетка), небесная ячейка |
| `TESObjectCELL::AE_19063(cell, bool)` | ссылки ячейки, актёры в `ProcessLists`, события `AE#19055` | `+0x33C` `kAttached` (7) | `SetCurrentCell+0x6AA` (1), `AE#13317` (0), `AE#13430`, `AE#13288` |
| `TESObjectCELL::AE_19130(cell, bool)` | 3D из `objRoot` (vfunc `0x38`), мир неактивен | `+0x96` `kDetaching` (5), `+0x4BB` `kUnattached` | `SetCurrentCell`, `AE#13317`, `AE#13315`, `AE#13721`, `GridCellArray::Detach` |
| `TESDataHandler::AE_13721(dh, cell)` | выгрузка: `AE#19130`, MOPP, земля, актёры, трава | `+0xC3` `kUnloading` (1); `AE#18981` → `kUnloaded` | ~20 мест: буферы, `SetCurrentCell`, `Cancel`, сетка |
| `TESObjectCELL::AE_19046(cell,…)` | актёры ячейки в `ProcessLists` при отключении (под замком ячейки `+0x120`) | — | `AE#13317`, `AE#13721` |

Таблица состояний (`TESObjectCELL+0x44`):

| Состояние | Кто ставит | Поток |
|---|---|---|
| `kUnloaded` 0 | `AE#18981` (из `AE#13721`) | M или J (выгрузка из `PostProcess`) |
| `kUnloading` 1 | `AE#13721+0xC3` | M или J |
| `kDataLoaded` 2 | `AE#19112+0x181` | M (под экраном) или IO (`Run`, интерьер) |
| `kBuilding3D` 3 → `kUnattached` 4 | `AE#18978+0x79` / `+0xC2C` | IO (`Run`) или M |
| `kDetaching` 5 → `kUnattached` 4 | `AE#19130+0x96` / `+0x4BB` | M |
| `kAttaching` 6 | `AE#18976+0x76` | M |
| `kAttached` 7 | `AE#19063+0x33C` | M |

## 7. Буферы (`TES+0xC8`, `TES+0xD0`)

Длины — значения INI **[код]** (H9): `interiorBuffer` — `uInterior Cell Buffer` (`AE#358814`, 3), `exteriorBuffer` — `uExterior Cell Buffer` (`AE#358817`, 36).

- `AE#13434` — положить ячейку в буфер: интерьер → `TES::AE_13435` (в начало `interiorBuffer`, последний выгружается `AE#13721`), экстерьер → в начало `exteriorBuffer`, вытесненная выгружается.
- `TES::AE_13370` — убрать интерьер из буфера и выгрузить (только из `AE#40845`). `TES::AE_13436(exceptCurrent)` — выгрузить весь `interiorBuffer`.
- `TES::PurgeBufferedCells` (`AE#13299`): из `SetCurrentCell+0x62A`, `AE#13317+0x392`, `Main::AE_36601` (`+0x367`, `+0x84D`).
- **`CellLoaderTask::PostProcess` (`AE#18529`) зовёт `AE#13434`** — буфер `TES` меняется и ячейки выгружаются (`AE#13721`) на потоках J **[код + игра]**. Замка вокруг буфера в `AE#13434` нет **[код]**; совпадение с фазой кадра главного потока — **[?]** (ниже).

## 8. Потоки загрузки

```
ставит (M): AE#13288 → ExteriorCellLoader::QueueCellLoad (AE#18541)      экстерьер, ключ (x,y)
            AE#40845 → AE#13378 → AE#18540(loader, cell, true)            интерьер, ключ formID
   оба: CellLoaderTask (vtable AE#189967) в карту загрузчика (+0x10) и IOManager vfunc ResubmitTask
IO:  BSTaskManagerThread::vf_0x1 (AE#75651+0x1A0) → CellLoaderTask::Run (AE#18527)
       state 0→1; интерьер AE#19111 / экстерьер TESWorldSpace::LoadCell (AE#20460) или AE#13710;
       AE#18978 (3D), земля, вода; AE#441574 (ссылки, под замком ячейки +0x120); state 2
     BSTaskManager::vf_0x13 (AE#75636+0x20) → CellLoaderTask::Finish (AE#18528)  state 2→3, в очередь готовых
J/M: IOManager::AE_75705 (+0xEF) → CellLoaderTask::PostProcess (AE#18529)
       state 3→4; из карты загрузчика (AE#18546 / AE#18547); AE#13434 (буфер TES); state 5
       AE#75705 зовут: Main::Update+0x5C7 (M), AE#36565 ← Job_IO (AE#39084) (J), AE#75708 (M)
Cancel (AE#18530): из карты; TES::AE_13308 → AE#13721
```

В игре **[игра]**: `Run` и `Finish` — один `tid` за запуск (IO), `PostProcess` — 5–6 разных `tid` (J), 1 428 из 1 428. Job_IO оборачивает `AE#75705` в те же вызовы `bhkWorld`, что `Main::Update+0x5AC…+0x5E9` (`AE#36565`) **[код]** — вероятно, это одна и та же фаза кадра, выполняемая заданием **[?]**.

Кто ждёт (M, все — `Sleep` в цикле):
- `IOManager::AE_75708(priority)` — ждать очередь IO до приоритета, внутри `AE#75705` и `AE#75686`; вызывают `SetCurrentCell` (2), `AE#13317` (4), `AE#13428` (4), `AE#40438` (5), `AE#40744` (5), загрузка сохранения `AE#35567`, `AE#36549`, `Main::AE_36601`. В профиле холодной загрузки 70% отсчётов — `AE#75686 < AE#75708 < AE#13316/13317/35567` **[игра]** (0.5.0–0.7.1).
- `AE#40438+0x845` — цикл `Sleep(10)` по счётчику `ProcessLists` (`AE#404445`).
- `AE#13363` при закрытии — `Sleep(30)` × 2.
- `IOManager::AE_75712/75713` — приостановить/возобновить IO вокруг синхронного чтения (`AE#19111`, `AE#13288`).

Модели (`QueuedFile`), Havok, навмеш, трава: в этой задаче прочитаны только входы — трава (`AE#13277`, `BGSGrassManager::AddCellGrass` `AE#15372`) и навмеш (`NavMeshObstacleManager::AE_30451` в конце переходов); `QueuedFile`/`QueuedReference` (`AE#13015`, `AE#13005`) зовут `AE#13355` → `AE#18978` — **пачка 3** в результате T39.

## 9. События

| Событие | Откуда | Поток **[игра]** |
|---|---|---|
| `PositionPlayerEvent` 0/3/4 | `AE#41051` из `AE#40438+0x55A/+0x885/+0x8DD`; 1 и 2 не рассылаются (H2) | M (222/221/221) |
| `BGSActorCellEvent` leave/enter | leave — `AE#40744+0xE6` (`AE#19032`), enter — `AE#19411` | M |
| `TESActorLocationChangeEvent`, `CLOC` | `AE#40466` из `AE#19411+0x251` | M |
| `TESCellAttachDetachEvent` | `AE#19361` (из `AE#18976`), `AE#19055` (из `AE#19063`) — FINDINGS | M (36) |
| `TESCellFullyLoadedEvent` | отправитель не найден (пачка 2) | интерьер — M (56/56); экстерьер — X 1 624, M 52 |

## 10. Экран загрузки

`AE#13363(open, location, allBuffered, …)` — открыть/закрыть **[код]**:
- открыть: флаги `AE#400336` (экран поднят), `BGSTerrainManager::LoadScreenUp`, небо `Sky::AE_26265`, `Rumble::DisableRumble`, `SubtitleManager::KillSubtitles`, `AE#52852` → `UIMessageQueue::AddMessage(Loading Menu, kShow, LoadingMenuData{location, flag})`, `AE#35407(1)`, `ImageSpaceManager AE#105685`;
- закрыть: обновить ландшафт, `AE#19118`, `UIMessageQueue::AddMessage(Loading Menu, 3)`, затемнение и Mist Menu, `Sleep(30)` × 2, `BGSSaveLoadManager::AE_35772`, `AE#400336 = 0`.
- Кто открывает: `AE#40438` (каждый переход, если цель не текущая ячейка), `SetCurrentCell+0x65C`, `AE#13317+0x43E`, `AE#13288+0xC88`, `Actor::AE_40445`, `PlayerCharacter::ServePrisonTime`.

Что делается **только под экраном** или зависит от него: `AE#13288` сдвигает сетку без ожидания очереди; `AE#13430` передаёт `!AE#400336` в `AE#19063`; `SetCurrentCell` зовёт `Main::AE_36555` только без экрана; все ожидания `AE#75708`, цикл `Sleep(10)`, прогрев Havok на `kPost` (FINDINGS H4+). Обязательного для бесшовного перехода среди них нет: экран — UI и флаги, а работа (подключение, события, `SetParentCell`) делается и без него **[код]**; ожидания нужны, только если ячейка не загружена заранее **[?]**, проверка — T40.

## 11. Что рассчитано на один экземпляр (5.14)

| Место | Один на игру | Следствие |
|---|---|---|
| `TES::interiorCell` (`+0xC0`), `saveGridX/Y` | один текущий интерьер, одна сохранённая сетка | `SetCurrentCell` отключает старый интерьер и сетку: интерьер ↔ интерьер и экстерьер ↔ интерьер без отключения — только мимо него (как `InteriorSim`) |
| `gridCells` (`+0x78`), `worldSpace` (`+0x148`), `currentGrid`/`currentQueued` | одна сетка одного мира | второй мир за воротами игра держать не умеет; `AE#13315` делает `DetachAll` |
| `ExteriorCellLoader` карта задач | ключ `(x,y)` без мира, интерьер — `formID` в той же карте | две сетки двух миров с теми же `(x,y)` сольются |
| Мир Havok экстерьера `AE#400440`, флаг `+0xC5F9` | один, выключается при входе в интерьер | экстерьер ↔ экстерьер, жизнь улицы изнутри — нужен свой шаг |
| `ShadowSceneNode::portalGraph` (`SSN+0x228`) | один граф | `SetCurrentCell+0x37C` и `AE#13317+0x78` его сбрасывают |
| `LoadedAreaBound` (`TES+0x2B8`), `BSShaderManager gridArrayCenter`, небо `TES+0x100` (`AE#13326`) | одна область, один центр, один режим неба | зависят от игрока, не от камеры |
| Буферы `interiorBuffer` 3, `exteriorBuffer` 36 | общие | много порталов вытесняют друг друга (H4, 0.8.1) |
| Обходы `AE#13291`, `AE#13325`, `AE#13388`, `AE#13415` | «интерьер **или** сетка + небесная ячейка» | подключённый второй интерьер игра не обновляет |

Только интерьер: `SetCurrentCell`, `interiorBuffer`, `AE#13435/13436/13370`, граф порталов ячейки. Только экстерьер: `AE#13288/13428/13430/13362`, `exteriorBuffer`, `QueueCellLoad`, трава, `LoadedAreaBound::SetCurrentCell`, мир `AE#400440`. Только смена мира: `AE#13315`. Сетку целиком сбрасывают ещё загрузка сохранения (`AE#13295+0x64`) и разрушение `TES` (`AE#13280+0x1AC` ← `~TES` `AE#13627`; в игре 36 раз на M — когда именно, не выяснено).

## 12. Перехваты плагина на карте

| Место | Перехват | Что меняет |
|---|---|---|
| `AE#13316` | detour `HooksTes.cpp` | до оригинала `InteriorSim::Detach` (отключить наш интерьер), оригинал под `GuardWorlds` |
| `AE#13299` | detour | то же; снимок сетки |
| `AE#18541`, `AE#40437`, `AE#56626` | detour | только трасса |
| `AE#13419` (TES как `BSTEventSink<PositionPlayerEvent>`), `AE#61410` | detour | замер прогрева |
| `AE#13370`, `AE#18547` (из `AE#40845`), `AE#13721` | detour (`Preload`) | не выгружать удерживаемый интерьер; `AE#13721` срабатывает и на J |
| `AE#13434` | detour | оригинал под `GuardWorlds`; на J тоже |
| `TESObjectDOOR` `0x37`; `GridCellArray` 1–8; `CellLoaderTask` 1,2,3,4,6 | vfunc `HooksCells.cpp` | трасса |
| `AE#106385`, `AE#19130`, `AE#13291`, `AE#13325`, `AE#77851` | detour `HooksInteriorSim.cpp` | граф порталов при нашем подключении; снять наш 3D; обходы для подключённого интерьера |
| вызовы: `AE#18976`, `AE#19063`, `AE#77905/77906`, `AE#19130`, `AE#19046`, `AE#18540` | `InteriorSim.cpp`, `Preload.cpp` | подключение интерьера в обход `SetCurrentCell` |

## 13. Карта и старые трассы

Совпали **[игра]**: `SetCurrentCell` только из `AE#40744`, только M, только для интерьера; порядок `Loading Menu` → `kPre` → `leave` → `SetCurrentCell` → `enter` → `LocationChange` → `kPost` → `kFinish` → закрытие в одном кадре (0.7.1, `seq 713–732`); `QueueCellLoad`/`SetCenter` из `AE#13288`/`AE#13428` на M; `Run`+`Finish` на одном IO-потоке, `PostProcess` на J; `DoorActivate` на X за ~38 кадров до перехода (H1); ожидание `AE#75708` в профиле. Расхождение одно: `DetachAll` из `AE#13295` (загрузка сохранения) 44 раза не на M — поток загрузки сохранения в коде не прослежен (пачка 1).
