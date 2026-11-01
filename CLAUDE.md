# CLAUDE.md — CellLoadResearch

SKSE-плагин для экспериментальной проверки того, как Skyrim AE подгружает ячейки и локации при переходах, с выдачей готового архива для Vortex.

Общайся с пользователем по-русски. Идентификаторы, код и коммиты пиши по-английски.

**Текущее состояние, архитектура и план работ — в `docs/HANDOFF.md`. Прочитай его перед началом работы.**

---

## 0. Задача и критерии готовности

Нужно собрать на CommonLibSSE-NG SKSE-плагин `CellLoadResearch`, который:

1. **Трассирует реальный алгоритм загрузки** ячеек и локаций: события движка, хуки ключевых функций, снимки состояния `RE::TES`, тайминги, потоки.
2. **Прогоняет автоматический сценарий переходов**: интерьер ↔ экстерьер, загрузочная дверь, смена мирового пространства, шаги по экстерьеру, сброс буфера.
3. **Поставляется архивом**, который ставится в Vortex без ручной перепаковки.

После прогона нужно проанализировать трассу и подтвердить или опровергнуть гипотезы из раздела 4.

**Definition of Done**

| # | Результат | Где |
|---|---|---|
| 1 | Архив для Vortex, корень — `SKSE/Plugins/...` | `dist/CellLoadResearch-X.Y.Z.zip` |
| 2 | Сборка и упаковка одной командой | `scripts/build.ps1` |
| 3 | Скрипт анализа трассы: таймлайны, длительности фаз, порядок вызовов, потоки | `tools/analyze_trace.py` |
| 4 | Отчёт по каждой гипотезе: подтверждено / опровергнуто / неясно + выдержки из трассы | `docs/FINDINGS.md` |
| 5 | Краткая инструкция для пользователя: установка в Vortex, горячие клавиши, где логи | `README.md` |

---

## 1. Окружение пользователя

- **Игра:** Skyrim Special Edition / Anniversary Edition, **Steam, runtime 1.6.1170**.
  - В августе 2026 Steam обновил игру до 1.7.99, затем до 1.7.104. Пользователь сознательно сидит на 1.6.1170.
  - Не предлагай обновлять игру. Не трогай `appmanifest_489830.acf`.
- **SKSE64 для 1.6.1170** (последний релиз под этот runtime — 2.2.6). В корне игры должны лежать `skse64_loader.exe` и `skse64_1_6_1170.dll`.
- **Address Library for SKSE Plugins**, AE-версия. Нужен файл `Data\SKSE\Plugins\versionlib-1-6-1170-0.bin`.
  - CommonLibSSE-NG резолвит все ID через него. Без этого файла плагин не загрузится.
- **Менеджер модов — Vortex.**
  - Моды разворачиваются жёсткими ссылками в `<Game>\Data`, поэтому после Deploy файлы видны прямо в папке игры.
  - Плагин ставится в Vortex только как архив, а не копированием в `Data`.

**Спроси у пользователя в начале работы, если не знаешь:**

1. Путь к игре (например, `C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition`).
2. Имя тестового сохранения. Автотест телепортирует персонажа и сбрасывает буферы, поэтому основное прохождение использовать не стоит.
3. Можно ли запускать игру самостоятельно через `skse64_loader.exe` (раздел 6, вариант B).
4. Установлены ли Visual Studio 2022 (workload «Desktop development with C++» + Windows SDK), xmake ≥ 3.0 и git.

**Перед первой сборкой проверь и выведи сводку:**

- наличие `skse64_1_6_1170.dll` и `versionlib-1-6-1170-0.bin`;
- версию `SkyrimSE.exe` (FileVersion должна быть 1.6.1170.0);
- путь к «Документам». Используй `[Environment]::GetFolderPath('MyDocuments')`, потому что «Документы» часто перенаправлены в OneDrive.

**Где лежат логи и трасса:** `<MyDocuments>\My Games\Skyrim Special Edition\SKSE\`.

---

## 2. Проект и сборка

### 2.1 Основа

- **Шаблон:** https://github.com/libxse/commonlibsse-ng-template (xmake, C++23).
  - Сабмодуль `lib/commonlibsse-ng` указывает на `alandtse/CommonLibVR`. Это старое имя репозитория `alandtse/CommonLibSSE-NG`, ветка `ng`.
- **Шаблон закреплён на CommonLibSSE-NG v6.0.0 от 31.07.2026. Обнови сабмодуль до `v13.1.0`** (коммит `12d61f3c39f7`).
  - Анализ ниже сделан по v13.1.0.
  - После v6 исправляли раскладку `TES` (`fix: TES class`), аксессоры `PlayerCharacter` и добавили `REL::Offset2ID`.

**Если текущая папка ещё не клон шаблона:**

```powershell
git clone --recurse-submodules https://github.com/libxse/commonlibsse-ng-template CellLoadResearch
```

Затем перенеси в него `CLAUDE.md` и стартовый `main.cpp` (раздел 8).

**Обновление сабмодуля:**

```powershell
git -C lib/commonlibsse-ng fetch --tags origin
git -C lib/commonlibsse-ng checkout v13.1.0
git -C lib/commonlibsse-ng submodule update --init --recursive
```

### 2.2 Правки `xmake.lua`

- Переименуй target и plugin name в `CellLoadResearch`. Имя target определяет имя DLL.
- Задай author и description, версию начни с `0.1.0`.
- **Повышай версию при каждой сборке, которую отдаёшь пользователю.** Vortex различает архивы по имени и при установке новой версии предлагает Replace.
- Конфиг сборки:
  - `xmake f -m releasedbg --skyrim_vr=n -y` — SE+AE без VR, определяется `EXCLUSIVE_SKYRIM_FLAT`. AE-only сборку не делай без необходимости.
  - Для INI-конфига можно включить `--rex_ini=y` (simpleini через REX). Если не получится — читай INI через WinAPI `GetPrivateProfileString*`.
  - Для mid-function хуков есть `--skse_xbyak=y`, он включает `SKSE::stl` ContextHook.
- Логи: spdlog собран с `std_format = true`, поэтому в `SKSE::log::*` используется синтаксис `std::format`.
- **Не задавай `XSE_TES5_GAME_PATH`.** Иначе xmake скопирует DLL прямо в `Data` в обход Vortex. Переменную `XSE_TES5_MODS_PATH` тоже не используй.
- WSL: MSVC вызывается через `powershell.exe` и VS Dev Shell. Подробности в `lib/commonlibsse-ng/CLAUDE.md`, раздел «Building from WSL».

### 2.3 Упаковка для Vortex (`scripts/build.ps1`)

Скрипт делает три вещи:

1. `xmake f ...` и `xmake build`;
2. собирает stage-папку;
3. архивирует её в `dist/CellLoadResearch-<version>.zip`, версию берёт из `xmake.lua`.

Структура архива:

```
CellLoadResearch-0.1.0.zip
└─ SKSE/
   └─ Plugins/
      ├─ CellLoadResearch.dll
      ├─ CellLoadResearch.pdb      (нужен crash-логгерам)
      └─ CellLoadResearch.ini
```

С 0.8.6 рядом с `SKSE/` лежат `Meshes/` и `Textures/` (квадрат портала, 5.12). `build.ps1` берёт их из `res/data/` и проверяет, что первый уровень архива — только эти папки.

- **В корне архива не должно быть папки `Data/`.** Стандартный `xmake package` из правила `commonlib.plugin` кладёт файлы под `Data/` — его для Vortex не используй.
- Архивируй через `tar.exe -a -c -f <zip> -C <stage> SKSE` (встроен в Windows 10+) или через `[IO.Compression.ZipFile]::CreateFromDirectory` в PowerShell 7. `Compress-Archive` из Windows PowerShell 5.1 пишет пути с обратными слэшами — не используй его.
- После упаковки выведи содержимое архива (`tar -tf <zip>`) и убедись, что первый уровень — `SKSE/`, а разделители — `/`.

**Установка (это делает пользователь, опиши в README):**

- Vortex → Mods → **Install From File** (или перетащить zip в зону drop) → Enable → Deploy.
- Обновление: установить новый zip и выбрать **Replace**.
- Проверка: после Deploy существует `<Game>\Data\SKSE\Plugins\CellLoadResearch.dll`, а после запуска игры появляется `CellLoadResearch.log`.

---

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

### 3.6 Реконструированный поток перехода через дверь (НЕ подтверждён)

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

## 4. Гипотезы для проверки

| ID | Гипотеза | Как проверить |
|---|---|---|
| H1 | `TESObjectDOOR::Activate` только заполняет `queuedTargetLoc`, а перемещение происходит на следующем кадре или позже | Номер кадра (счётчик тиков `Update`) и `isValid` при выходе из `Activate` против входа в `SetCurrentCell` |
| H2 | `SetCurrentCell` вызывается между `kPre` и `kPreUpdatePackages` | Порядок `seq` в трассе |
| H3 | Интерьер грузится синхронно в главном потоке под `LoadingMenu`; экстерьер при ходьбе — асинхронно через `CellLoaderTask::Run` в IO-потоках | `tid` и `main` у `Run`/`Finish`; наличие `CellLoaderTask` при `coc` в интерьер |
| H4 | Покинутые ячейки уходят в буфер (`kUnattached`/`kDataLoaded`), повторный вход заметно быстрее; после `PurgeBufferedCells` выигрыш пропадает | Длительность `LoadingMenu` и `SetCurrentCell` в парах шагов сценария по умолчанию (5.3); состояния ячеек в снимках |
| H5 | `currentQueuedX/Y` опережает `currentGridX/Y`, то есть это предзагрузка | Снимки при ходьбе |
| H6 | Порядок `TESCellFullyLoadedEvent`, `BGSActorCellEvent`, `TESActorLocationChangeEvent` относительно `kFinish` и закрытия `LoadingMenu` | Таймлайн |
| H7 | `CellLoaderTask` с `interior=true` используется для интерьеров (например, предзагрузка за дверью) | Логировать все `CellLoaderTask` с `interior` |
| H8 | Смысл `a_unk4` у `SetCurrentCell` и последнего `bool` у `QueueCellLoad` | Логировать значения во всех сценариях и сопоставить |
| H9 | Длина `interiorBuffer` равна `uInterior Cell Buffer`, длина `exteriorBuffer` — `uExterior Cell Buffer` | Осторожное чтение (см. раздел 7); сравнение с INI |
| H10 | Какие INI-настройки загрузки реально существуют в 1.6.1170 | Пробовать `INISettingCollection::GetSingleton()->GetSetting(name)` и `INIPrefSettingCollection` для кандидатов: `uGridsToLoad:General`, `uExterior Cell Buffer:General`, `uInterior Cell Buffer:General`, `bPreloadLinkedInteriors:General`, `uLargeRefLODGridSize:TerrainManager`. Логировать, что найдено и со значением. Названия, кроме первых трёх, не проверены |
| H11 | Пока игрок в экстерьере, интерьеры за дверями загруженной области не рисуются и не считают физику, а NPC в них обрабатываются только на уровне low/middle-low (грубый ИИ по расписанию, без анимации и физики). Соседние ячейки экстерьера в сетке 5×5 симулируются полностью | Проба симуляции (5.7): состояние ячеек, `inScene`, время физического мира, уровни обработки и перемещения NPC по категориям |
| H12 | Пока игрок в интерьере, сетка экстерьера остаётся загруженной (`kUnattached`), но не рисуется и не считает физику, а NPC снаружи опускаются до low/middle | Та же проба, категория `gridWhileInside` |
| H13 | Видимый переход через дверь (3.5–3.8 с) почти целиком состоит из пауз и затемнений, а не из загрузки: пауза до затемнения 0.65 с (`fTeleportDoorActivateDelayTimer`?), `fNormalDoorFadeSecs` 0.4 с, проявление 2.37 с = `fMinSecondsForLoadFadeIn` + `fFadeToBlackFadeSeconds` (подтверждено в 0.7.1) | Запись `transition.visible`; наборы `[Overrides]` по F7 (5.9) |
| H14 | Вызов `ExteriorCellLoader::QueueInteriorCellLoad` (`AE#18540`) раз в пару секунд держит выбранный интерьер загруженным (`kUnattached`), пока игрок рядом: вытеснения из буфера на 3 места больше нет | `preload.change` / `preload.keep` при ходьбе по Вайтрану (5.5) |
| H15 | Если шагать мир Havok интерьера вместе с миром игрока и поднимать его NPC через `MoveToHigh`, интерьер симулируется как соседняя ячейка экстерьера: NPC на уровне high, двигаются, физика идёт | Стадии 1–2 InteriorSim (5.11): `interior.sim`, проба |
| H16 | `cell3D` интерьера в состоянии `kUnattached` можно подвесить в сцену экстерьера и увидеть: на месте двери или над зданием | Стадии 3–4 InteriorSim (5.11) |

---

## 5. Что должен делать плагин

### 5.1 Трасса (JSONL)

Файл: `<MyDocuments>\My Games\Skyrim Special Edition\SKSE\CellLoadResearch.trace.jsonl`. Пересоздаётся при старте игры. Рядом обычный текстовый лог через `SKSE::log`.

Одна строка — одна запись:

```json
{"seq":42,"t_us":1234567,"frame":9876,"tid":4321,"main":true,"ev":"hook.TES.SetCurrentCell.enter","args":{"cell":"WhiterunBanneredMare [xxxxxxxx]","pos":[0,0,0],"unk4":0.0},"caller":"AE#40437+0x1A2"}
```

- **`t_us`** — `std::chrono::steady_clock` от старта плагина.
- **`main`** — сравнение `GetCurrentThreadId()` с `RE::Main::GetSingleton()->threadID`.
- **`frame`** — счётчик тиков из хука `PlayerCharacter::Update`.
- **`caller`** — `_ReturnAddress()` минус база модуля, переведённое в «ближайший меньший AE-ID + смещение» через `REL::Offset2ID`. Это ориентир, а не точная функция.
  - Таблицу строй один раз, лениво, в главном потоке.
- **Запись в файл** — потокобезопасная (mutex или очередь), с `flush` после каждой записи `LoadingMenu`/`kFinish` и при выходе.
- **Префиксы `ev`:** `event.*` для событий, `hook.*.enter` / `hook.*.leave` (с `dur_us`) для хуков, `snapshot`, `autotest.step.begin` / `autotest.step.end`, `info` для версии runtime и INI.

### 5.2 Инструментирование

- **События** — все из раздела 3.5.
- **vfunc-хуки** — все из таблицы 3.3: дверь, `GridCellArray`, `CellLoaderTask`, `PlayerCharacter::Update`.
- **Хуки на вход в обычные функции:** `SetCurrentCell`, `PurgeBufferedCells`, `QueueCellLoad`, `CenterOnCell_Impl`, `MoveTo_Impl`.
  - Нужен полноценный detour с переносом пролога.
  - Предпочтительно Microsoft Detours: проверь пакет `xmake require --info microsoft-detours`. Если его нет — подключи через vcpkg или исходники.
  - Альтернативы: `SKSE::stl` ContextHook (`--skse_xbyak=y`) или `write_call<5>` на найденных местах вызова.
  - **Не пиши самодельный `write_branch` в начало функции без переноса пролога.**
- **Снимки состояния** (`snapshot`) — только в главном потоке:
  - после `kFinish`, после закрытия `LoadingMenu`, после каждого `SetCenter`, в конце каждого шага автотеста, по горячей клавише;
  - содержимое: `interiorCell`, `worldSpace`, `currentGridX/Y`, `currentQueuedX/Y`, сетка `length×length` (для каждой ячейки: ID, `cellX/cellY`, `cellState`, `refsFullyLoaded`, `queuedRefCount`), размер `ExteriorCellLoader::queuedCellMap` (под `BSReadLockGuard` на `lock`), позиция игрока, `GetParentCell()`, `GetCurrentLocation()`, `playerFlags.isLoading`, `queuedTargetLoc`.

### 5.3 Автотест

- **Запуск:** горячая клавиша F10 в игре или `bEnabled=1` в INI.
  - При `bEnabled=1`, когда открывается `Main Menu`, плагин сам загружает сохранение: по имени из INI через `BGSSaveLoadManager::Load(name)` или самое свежее.
- **Сценарий:** конечный автомат, который крутится в хуке `PlayerCharacter::Update`.
  - Каждый шаг: запустить действие → ждать «переход завершён» → `fSettleSeconds` → снимок.
  - **«Переход завершён»** = виден `kFinish`, `LoadingMenu` закрыт и `player->GetParentCell()->IsAttached()`. Таймаут 60 с, при таймауте логировать ошибку и переходить к следующему шагу.
  - **Время ожидания считай по `steady_clock`**: во время `LoadingMenu` тиков `Update` может не быть.
- **Шаги** (строка `sSteps` в INI, разделитель `|`):

| Шаг | Действие |
|---|---|
| `coc:<EditorID>` | `PlayerCharacter::CenterOnCell(name)`. Сначала залогируй, находит ли ячейку `TESForm::LookupByEditorID<TESObjectCELL>` (не все editor ID хранятся в рантайме). Сам `CenterOnCell` ищет ячейку по-своему и возвращает `bool`: при `false` пропусти шаг с ошибкой |
| `door:nearest` | ближайшая к игроку ссылка с `ExtraTeleport` в текущем интерьере, а в экстерьере — в загруженной сетке, затем `door->ActivateRef(player, 0, nullptr, 0, false)`. Это проверка настоящего пути через дверь |
| `walk:<+x\|-x\|+y\|-y>,<N>` | N шагов по 4096 единиц: `player->SetPosition(...)` и ожидание `SetCenter`. **Не подтверждено**, что `SetPosition` вызывает перецентрирование без экрана загрузки — выясни и зафиксируй в FINDINGS |
| `purge` | `TES::PurgeBufferedCells()` |
| `wait:<sec>` | пауза |
| `snapshot` | снимок |

- **Сценарий по умолчанию.** Пары для H4 (первый вход → повторный → после `purge`):
  - интерьер через дверь: 1 → 3; экстерьер через дверь: 2 → 4;
  - интерьер через `coc`: 6 → 8 → 11; экстерьер через `coc`: 7 → 9 → 12.
  - `purge` и замер после него должны идти, когда игрок уже **вне** проверяемой ячейки: `coc` в ячейку, где игрок стоит, не вызывает загрузки (прогон 0.1.0).
  - Прогон 0.2.0 показал, что через `coc` интерьер перезагружается каждый раз, а через дверь — нет. Поэтому дверь и `coc` проверяются отдельно.

```
coc:RiverwoodSleepingGiantInn | door:nearest | door:nearest | door:nearest | walk:+x,4 | coc:WhiterunBanneredMare | coc:Riverwood | coc:WhiterunBanneredMare | coc:Riverwood | purge | coc:WhiterunBanneredMare | coc:Riverwood | coc:Whiterun
```

- Если трасса покажет, что сценарий не проверяет то, что должен, исправляй его здесь, в `res/CellLoadResearch.ini` и в `Settings.h` (разрешение пользователя от 23.10.2026).

- По завершении: запись `autotest.done`, сводка в лог, flush. При `bQuitWhenDone=1` — `RE::Console::ExecuteCommand("qqq")`.

### 5.4 INI (`Data\SKSE\Plugins\CellLoadResearch.ini`)

```ini
[General]
bTraceEnabled=1
bSnapshotGrid=1
bReadCellBuffers=0        ; H9: читать interior/exteriorBuffer — выключено, пока длина не подтверждена
bLogCellAttachPerRef=0    ; TESCellAttachDetachEvent очень шумный

[Hotkeys]
iStartAutoTest=68         ; F10 (DirectInput scan code)
iSnapshot=87              ; F11

[AutoTest]
bEnabled=0
sSaveName=                ; пусто — самое свежее сохранение
fSettleSeconds=3.0
fStepTimeoutSeconds=60
bQuitWhenDone=0
sSteps=coc:RiverwoodSleepingGiantInn | door:nearest | door:nearest | door:nearest | walk:+x,4 | coc:WhiterunBanneredMare | coc:Riverwood | coc:WhiterunBanneredMare | coc:Riverwood | purge | coc:WhiterunBanneredMare | coc:Riverwood | coc:Whiterun
```

Клавиши лови через `BSTEventSink<RE::InputEvent*>` на `RE::BSInputDeviceManager`: `ButtonEvent::IsDown()` и `GetIDCode()`.

Фактический INI лежит в `res/CellLoadResearch.ini`. Комментарии там стоят на отдельных строках: `GetPrivateProfileString` не отрезает `; ...` после значения. Дополнительные ключи (добавлены по запросу пользователя 23.10.2026):

```ini
[General]
bShowCellNotifications=1  ; 5.6
bProfileLoadingScreen=1   ; сэмплирование главного потока под экраном загрузки (запись "profile")
sLogDirectory=            ; build.ps1 подставляет <проект>\logs

[Hotkeys]
iCycleOverrides=65        ; F7: перебор наборов [Overrides]. Не F9 — это быстрая загрузка игры
iDetectorsReport=64       ; F6: сводка детекторов дверей и окон (5.10)
iInteriorSimStage=66      ; F8: InteriorSim выключено / портал (5.11)
iToggleTemporalAA=62      ; F4: TAA игры включено / выключено в памяти (5.12, с 0.8.20; до того мягкие частицы портала)

[Preload]                 ; дополнительно к 5.5
bKeepInterior=1           ; звать QueueInteriorCellLoad (AE#18540) для sInteriorCell раз в fRefreshSeconds
bKeepInteriorNearDoorOnly=1

[Detectors]               ; 5.10
bEnabled=0                ; выключены 24.10.2026 по просьбе пользователя
bNotify=1
fDoorsIntervalSeconds=0.5
iNearestCount=5
bWindows=1
fWindowsIntervalSeconds=2.0
fWindowRadius=3000
sWindowKeywords=window
sWindowExclude=lod

[InteriorSim]             ; 5.11
iStartStage=0             ; 0 выключено, 1 портал
fStatusSeconds=10
bForceWorldActive=1
bAttachInterior=1         ; интерьер портала подключается, как соседняя ячейка (kAttached), 3D под своим узлом вне сцены

[Portal]                  ; 5.12: стадия 1 InteriorSim
bEnabled=1
iWidth=0                  ; размер текстуры портала, 0 — как экран
iHeight=0
iColorSlot=17             ; ячейки движка, которые портал занимает на время своего прохода
iDepthSlot=9
bInteriorLights=1
bMirrorPlayerLights=1
bInteriorAmbient=1
bLightEffects=0           ; эффекты со светом ламп интерьера
bAnimateActors=1
bSyncActorPositions=1
iDepthMode=2
iControllerUpdate=1       ; контроллеры интерьера (огонь, дым, свечи): 0 выключено, 1 выборочно, 2 всё дерево
bFullActorProcessing=1    ; NPC интерьера получают полное обновление high (ИИ, передвижение)
bGameAnimatesActors=1     ; NPC интерьера в заданиях анимации игры
bSoftEffects=1            ; мягкие частицы в проходе портала
bIgnoreParticleBudget=1   ; эмиттеры интерьера не подчиняются бюджету частиц игры
bMasterParticles=1        ; проход портала рисует и общие системы частиц (огоньки свечей)

[Overrides]               ; 5.9: наборы "имя:Секция=значение | ...", подставляются в память (настройки игры — без секции)
sSet1=fWarmStartMaxTime:HAVOK=0 | fNormalDoorFadeSecs:General=0.05 | fAutoDoorFadeSecs:General=0.05 | fMinSecondsForLoadFadeIn:Interface=0 | fFadeToBlackFadeSeconds:Interface=0.05
sSet2=<sSet1> | fTeleportDoorActivateDelayTimer=0

[Preload]                 ; 5.5
bEnabled=1
sInteriorCell=WhiterunBanneredMare
sExteriorCell=            ; Riverwood — экстерьер; выключен по просьбе пользователя после падения 0.4.0
iExteriorRadius=2
fRefreshSeconds=2.0
bPreloadLinkedInteriors=1
```

### 5.5 Эксперимент «держать ячейку в памяти» (`Preload.*`)

**Цель пользователя:** один интерьер и одна область экстерьера заранее загружены и удерживаются в памяти, чтобы вход в них был почти мгновенным. Это должно работать как при естественном входе (через дверь или подходя к ячейке экстерьера), так и при `coc`.

- **Экстерьер.** `(2r+1)²` ячеек вокруг `sExteriorCell` ставятся в очередь через `ExteriorCellLoader::QueueCellLoad`, если они выгружены. Повторная постановка той же ячейки — не чаще раза в 10 с.
- **Интерьер.**
  - В памяти игры включается её собственный механизм `bPreloadLinkedInteriors` (`Skyrim.ini` не меняется): игра предзагружает интерьеры за ближними дверями.
  - Готовой функции, которая загружает произвольный интерьер без входа в него, нет. Если интерьер выгружается, надо найти, кто это делает, и заблокировать именно это место.
- **Диагностика.** Каждая смена состояния удерживаемой ячейки пишется в трассу как `preload.change` с местом, где её заметили (`TES.SetCurrentCell`, `GridCellArray.DetachAll`, `LoadingMenu.close`, `tick` …).
- **Критерий успеха** — время `kPre → ActorCell enter` и длительность `Loading Menu` для удерживаемых ячеек против холодных. Базовые значения из FINDINGS: интерьер 383–642 мс, экстерьер 2.2–2.6 с.

### 5.6 Уведомления на экране

При каждом `BGSActorCellEvent enter` игрока в левом верхнем углу выводится сообщение (`RE::SendHUDMessage::ShowHUDMessage`):
- `CLR: <откуда> -> <куда> | load N ms`, где `N` = от `kPre` до `enter`;
- после экрана загрузки: `CLR: loading screen N ms`;
- после проявления (`Fader Menu` close): `CLR: door -> picture N ms = wait + fade-out + loading + fade-in`, то же в трассе как `transition.visible` (H13).

Названия ячеек — `GetName()`, иначе editor ID, для экстерьера плюс `(x,y)`. Строки игры в UTF-8, поэтому русский текст выводится как есть.

**Очередь сообщений (`Hud.*`, с 0.8.1).** В 0.8.0 очередь игры забилась на минуты.
- Сообщения ждут в своей очереди с каналами; в игру уходит не больше одного раза в 2 с.
- Новое сообщение того же канала заменяет ждущее; ответы на F6/F7/F8/F11 идут в начало очереди.
- Строки `loading screen` больше нет: время экрана загрузки входит в `door -> picture`.

### 5.7 Проба симуляции (`Probe.*`, запрос пользователя от 23.10.2026)

Вопрос пользователя: работает ли симуляция мира (ИИ NPC, физика, отрисовка) в интерьерах, пока игрок в экстерьере того же мира, и в экстерьере, пока игрок в соседнем интерьере. Это гипотезы H11 и H12.

Запись `probe` пишется каждые `fIntervalSeconds`, после каждого перехода и по F11.

**Ячейки** — с категорией: `player`, `grid`, `gridWhileInside`, `doorInterior` (интерьеры за дверями загруженной области), `bufferedInterior`. Для каждой ячейки:
- `state`;
- отрисовка: `inScene` (3D ячейки подключено к сцене) и `culled`;
- физика: `physicsTime` — время `hkpSimulation` мира ячейки.

`hkpSimulation` не описан в NG. Используется раскладка Havok 2010, она проверяется обратной ссылкой `world` по смещению `0x10`.

**Актёры** — все из `ProcessLists` (high / middleHigh / middleLow / low), не больше `iMaxActors`. Для каждого: ячейка, позиция, `3D`, `ai`, пакет ИИ, расстояние до игрока.

Анализатор сводит записи по категориям: сколько ячеек в сцене, идёт ли физика, уровни обработки и доля NPC, которые сдвинулись больше чем на 20 единиц.

### 5.8 Профиль экрана загрузки (`Profiler.*`) и имена функций (`tools/name_functions.py`)

- **Профилировщик.** Пока открыт экран загрузки, раз в ~1 мс плагин раскручивает стек главного потока: копирует стек и вызывает `RtlVirtualUnwind` на копии. Отчёт `profile` содержит дерево вызовов по фазам перехода, собственное и включающее время.
- **`tools/name_functions.py`.** Подписывает `AE#id` по данным самого `SkyrimSE.exe`:
  - RTTI-имена классов, в таблицах виртуальных функций которых стоит функция;
  - строки, на которые ссылается её код;
  - прямые вызывающие функции.

  Режим `--tree <фаза>` печатает дерево вызовов из трассы с этими подписями.

### 5.9 Прогрев физики (`Physics.*`)

Хвост `kPost → kFinish` — это `TES`, который в ответ на `PositionPlayerEvent kPost` прогоняет шаги физического мира (FINDINGS, H4). Для AE поставлены два detour-хука; ID найдены инструментом, сигнатуры сверены с машинным кодом:
- `AE#61410` = `hkpWorld::stepDeltaTime(float) -> int`: считает шаги по мирам для пробы и для отчёта экрана загрузки;
- `AE#13419` = `TES[+0x68]::ProcessEvent(PositionPlayerEvent)`: запись `hook.TES.ProcessEvent.PositionPlayer` с типом события, числом шагов и симулированным временем.

Результат 0.7.0: прогрев — ровно `fWarmStartMaxTime` (241 шаг по 1/60 с); при 0 вход в предзагруженный интерьер занимает 26 мс вместо 316.

**Наборы настроек (`Overrides.*`, с 0.7.1).** F7 перебирает: значения игры → `sSet1` → `sSet2` → … → значения игры. Значения пишутся в объекты `RE::Setting` в памяти (INI, INIPref, GameSetting; только числовые), `Skyrim.ini` не меняется. Номер активного набора пишется в `loading.summary` и `transition.visible`.
- До 0.7.1 это была клавиша F9. **F9 в игре — быстрая загрузка:** нажатие вызывало сообщение о повреждённом сохранении. Свободные в стандартной раскладке клавиши — F1–F4, F6–F8.
- Итог экспериментов 0.7.x для отдельного мода — `docs/FAST_DOOR_TRANSITIONS.md` (документ для передачи в другой чат).

### 5.10 Детекторы дверей и окон (`Detectors.*`, запрос пользователя от 23.10.2026)

**С 0.8.4 выключены** (`bEnabled=0`, просьба пользователя: «мутные» окна, скорее всего, часть больших текстур стен; эксперименты с окнами отложены).
- Окна не ищутся, записей `detector.*` и сообщений нет.
- Двери ищутся молча: `NearestDoorTo` нужен `Preload` и `InteriorSim`.
- F6 показывает только то, что под взглядом.

- **Двери.** Каждые `fDoorsIntervalSeconds` перебираются ссылки с `ExtraTeleport` в загруженных ячейках: интерьер игрока или присоединённая сетка экстерьера.
  - Поля каждого экземпляра: позиция, углы, `meshYawDeg` (+Y меша в мире), ячейка назначения и её состояние, маркер прибытия, связанная дверь, `automatic`, `locked`.
  - Запись `detector.doors` пишется при смене ближайших `iNearestCount`.
- **Окна.**
  - Геометрия ближних (до `fWindowRadius`) ссылок, у которой путь диффузной текстуры содержит `sWindowKeywords` и не содержит `sWindowExclude`.
  - Если у `BSGraphics::TriShape` есть `rawVertexData`/`rawIndexData`, геометрия делится на связные куски (по куску на окно): центр, размер, нормаль.
  - Иначе берётся граница всей геометрии. Ссылки кэшируются, кэш сбрасывается при смене интерьера или мира.
  - Запись `detector.windows`.
- **Экран.** При смене ближайшей двери или окна выводится сообщение (каналы `door`/`window`).
- **F6:** сводка плюс `detector.look`. Направление — локальная +X камеры, сверенная с прицелом игрока. Нужно, чтобы находить текстуры окон, которые не попали в `sWindowKeywords`.
  - С 0.8.2: луч Havok по коллизии (`bhkWorld::PickObject`, `COL_LAYER::kLOS`, `TESHavokUtilities::FindCollidableRef`); геометрия задетой ссылки сортируется по близости к точке попадания, сначала меньшие из тех, в чьи границы точка попала.
  - Если луч ничего не задел — по ограничивающим сферам, как в 0.8.1.
  - С 0.8.4 луч проходит сквозь ссылки со скрытым 3D (скрытая дверь таверны сохраняет коллизию) и пишет их в `lookedThroughHidden`.
- `rawVertexData` статических мешей хранит позиции полными `float`, даже без флага `VF_FULLPREC`. Точность определяется местом до следующего атрибута: смещения в полубайтах 2–9 дескриптора, ≥ 12 байт — `float` (0.8.2). Первые 96 байт пишутся в `detector.windows.rawSamples`.
- Окна Вайтрана: `wrwindows01.dds`, решётки окон — `wrwoodlattice01.dds` (`sWindowKeywords=window,lattice` с 0.8.3).
- **Формат вершин:** шаг = `(desc & 0xF) * 4`, позиция всегда со смещения 0. `GetAttributeOffset(VA_POSITION)` в NG для этого не годится (0.8.0).

### 5.11 Интерьер как соседняя ячейка (`InteriorSim.*`, H14–H16)

Пользователь хочет, чтобы интерьер был загружен, пока игрок в соседнем экстерьере, так же полно, как соседние ячейки экстерьера: ИИ NPC работает полностью, желательно с отрисовкой. Найденные функции движка описаны в FINDINGS (H14–H16).

- **Удержание:** `Preload` раз в `fRefreshSeconds` зовёт `QueueInteriorCellLoad` (`AE#18540`, только AE) — грузит или поднимает в начало буфера.
  - Менеджер `bPreloadLinkedInteriors` (`AE#40845`) выгружает интерьеры за дверями, которые не среди ближайших, уже на следующем кадре (0.8.0).
  - Поэтому detour-ы `AE#13370` (убрать из буфера и выгрузить, вызывает только он) и `AE#18547` (отмена загрузки; пропускается, только когда адрес возврата внутри `AE#40845`) не трогают удерживаемый интерьер, пока игрок не в нём и рядом с его дверью.
  - Защита от цикла: три загрузки за 30 с — пауза удержания на 60 с.
  - С 0.8.2 перехвачена и сама выгрузка `AE#13721`: в 0.8.1 таверну выталкивал из буфера на 3 места следующий предзагруженный интерьер (`AE#13434`, из рабочих потоков). Блокируется только ячейка в `kUnattached`, пока `g_keepActive` (решает главный поток: игрок снаружи, дверь рядом) и нет экрана загрузки. Первый вызов от каждого вызывающего пишется в `preload.block`. Загруженный удерживаемый интерьер повторно в загрузку не ставится: он может оказаться вне буфера.
- **Стадии F8 (с 0.8.12):**
  - 0 — выключено;
  - 1 — портал (5.12): мир Havok интерьера шагает вместе с миром игрока, NPC интерьера поднимаются на high, интерьер рисуется второй камерой.
  - Стадии 1–5 версий 0.8.0–0.8.11 убраны из переключения и из кода по просьбе пользователя от 25.10.2026.
    - Что они делали, итоги и код для возврата — в `docs/INTERIOR_SIM_STAGES.md`.
    - Полные исходники 0.8.11 — в `docs/archive/interior-sim-0.8.11/`.
- **Шаг физики:**
  - detour на `bhkWorld::vfunc[0x32]` (`AE#77851`): после шага мира игрока **в потоке физики** (на 1.6.1170 мир игрока шагается не в главном) тем же оригиналом шагается мир интерьера (байт `+0xC5F9` на время шага = 1);
  - главный поток держит `InteriorSim::GuardWorlds()` вокруг `SetCurrentCell`, `PurgeBufferedCells`, `DetachAll`, `AE#13370` и `AE#13434`, а поток физики только пробует её взять;
  - между открытием и закрытием `Loading Menu` шагов нет.
- **NPC:** `Actor::MoveToHigh()` раз в секунду для NPC интерьера.
- **Подключение (0.8.16, `[InteriorSim] bAttachInterior`).** Запрос пользователя от 25.10.2026: интерьер за порталом должен работать как соседняя ячейка экстерьера. Последовательность игры — в FINDINGS, прогон 0.8.15.
  - **Подключение.** При запуске портала интерьер подключается вызовами самой игры, как это делает `TES::SetCurrentCell` (`AE#13316`):
    - `AE#18976(cell, root)`, но без графа порталов: detour `AE#106385` (`ShadowSceneNode::SetPortalGraph`) пропускает вызов, пока мы подключаем;
    - пакет мира `AE#77905` / `AE#77906`;
    - `AE#19063(cell, 1)` → `kAttached`.
  - **Где 3D.** С 0.8.17 3D интерьера висит под собственным узлом вне сцены (`g_attachRoot`), основная камера его не видит.
    - В 0.8.16 оно висело под `TES::objRoot` с `AppCulled`, и копии ламп не собрали ни одной фигуры.
    - `ShadowSceneNode::AddLight` (`AE#106326`) только ставит лампу в очередь, фигуры собираются позже (`AE#106342`).
  - **Отключение** — как `AE#13317`: сначала 3D снимается с нашего узла, затем `AE#19130(cell, 1)`, затем `AE#19046(cell, 1, 0)`.
    - Порядок важен: `AE#19130` вызывает `TES::objRoot->DetachChild(cell3D)`, а `NiNode::DetachChild` (`AE#70292`) берёт номер ребёнка из него самого (`+0x38`) без проверки родителя.
    - В 0.8.17 это сняло с `objRoot` чужой узел (`TES::tempNodeManager`), и игра упала при входе в таверну.
    - Detour `AE#19130` снимает 3D с нашего узла и тогда, когда ячейку отключает сама игра (`cellDetachedByGame`). Происходит перед `Loading Menu`, в detour `SetCurrentCell` и `PurgeBufferedCells`, при `DetachAll` и при SKSE `kSaveGame`.
  - **Обходы за кадр** берут интерьер игрока или сетку. Detour `AE#13291` вызывает `AE#18890(cell, AE#400333)`, detour `AE#13325` — `AE#18900(cell)` для подключённого интерьера. Оба только пробуют взять `g_attachLock`: главный поток держит его при подключении и отключении.
  - **Связь с `Preload`.** Подключённый нами интерьер не снимает портал (`OwnsAttachedCell`) и не выгружается (`BlockCellUnload`).
  - **Шаг Havok** остаётся нашим. Шаги, которые игра сделала сама (`gameInteriorStepsTotal`), отменяют наш на этот кадр.
  - **Если подключение не дошло до `kAttached`**, интерьер отключается обратно. Портал работает с неподключённым, и в этой сессии подключение больше не пробуется.
- **Скрываются** наружная дверь и дверь интерьера, ставшая в проём.
- **Счётчики отсечения:** перехват `OnVisible` (`vfunc 0x34`) у `NiNode`, `BSFadeNode`, `BSMultiBoundNode`, `BSMultiBoundRoom`, `BSTriShape`, `BSDynamicTriShape` и `BSSubIndexTriShape` считает вызовы для наблюдаемых объектов.
  - Наблюдаемые объекты: узел наружной двери, квадрат портала, корень и комната интерьера, 3 ссылки интерьера у двери, узел актёров и 2 актёра.
  - Раз в `fStatusSeconds` значения выводятся на экран и пишутся в `interior.sim status.visible`.
- Если дверь в интерьер ушла из загруженных ячеек, портал снимается.
- **Безопасность:**
  - узел снимается при `Loading Menu open`, `GridCellArray::DetachAll` и любой смене состояния интерьера;
  - `NiUpdateData` с `kDisableCollision`;
  - шаг интерьера не делается во время прогрева (`Physics::InWarmStart`) и экрана загрузки.
- **Экран:** раз в `fStatusSeconds` — состояние (шаги физики, NPC high, отрисовка, длительность кадра). Записи `interior.sim`.
- В пробе у миров есть `worldActive` (байт `+0xC5F9`), а в `worlds` — число шагов в главном потоке.

---

### 5.12 Портал: интерьер второй камерой (`Portal.*`, запрос пользователя от 24.10.2026)

В 0.8.5 интерьер таверны, подвешенный в сцену экстерьера, оказался больше здания и вылезал из стен. Пользователь попросил рисовать его второй камерой и показывать вместо двери, каждый кадр, с учётом направления взгляда.

- **Прямоугольник.** `meshes\CellLoadResearch\portalquad.nif`: один квадрат с `BSEffectShaderProperty` без освещения и текстурой `textures\CellLoadResearch\portal.dds` (пурпурная заглушка).
  - Оба файла пишет `tools/make_portal_assets.py` в `res/data/`; `build.ps1` кладёт `res/data/*` в корень архива рядом с `SKSE/`.
  - Квадрат висит в узле наружной двери, на её уличной грани. Размер берётся по вершинам двери (`Detectors::WorldPoints`), а форма задаётся столбцами матрицы поворота, потому что у `NiTransform` один масштаб.
  - `resourceView` текстуры квадрата подменяется на SRV портала.
- **Камера** — клон камеры мира, в координатах интерьера:
  - стоит в глазу игрока, перенесённом через дверь (`InteriorToExterior`);
  - смотрит поперёк плоскости двери;
  - пирамида видимости (`NiFrustum` на расстоянии 1) смещена так, что проходит через прямоугольник двери; ближняя плоскость — на 2 единицы перед дверью.
  - UV квадрата 0..1 совпадают с этой пирамидой, поэтому картинка верна с любой точки.
- **Проход.** Detour `Main::RenderWorld` (`AE#107142`), перед оригиналом, в главном потоке. Шаги:
  1. копия теневого состояния;
  2. своя текстура (`R16G16B16A16_FLOAT`) и буфер глубины на время прохода подставляются в ячейки движка `iColorSlot`/`iDepthSlot`;
  3. `StartAccumulating`, затем `CullingContext` с `kIgnoreMultiBounds` по `cell3D` интерьера (не подвешенному никуда), затем `SubmitAccumulator`;
  4. возврат состояния и пометка всего «грязным», возврат ячеек.
  - Порядок вызовов движка — в FINDINGS (прогон 0.8.5).
- **Разрешение (0.8.8).** Текстура размером с экран. Каждый кадр рисуется только видимая часть двери (прямоугольник обрезается пирамидой видимости основной камеры), в её размер на экране. Через сдвиг и масштаб UV материала квадрата (`texCoordOffset/Scale[0]`) на дверь ложится только эта часть. Дверь вне экрана — прохода нет.
- **Освещение (0.8.8):**
  - копии `NiPointLight`/`NiSpotLight` интерьера добавлены в `ShadowSceneNode[0]` с `restrictedNode` = `cell3D` интерьера (без теней). Оригиналы не трогаются: игра добавит их сама при входе;
  - динамические источники рядом с игроком (факел, «Свет свечи») копируются и каждый кадр переносятся через дверь в координаты интерьера;
  - на время прохода — фоновый свет интерьера (`AE#18966` → `AE#105643`, сохранение и возврат 0x44 байт `BSShaderManager::State+0xC8`) и цвет солнца, равный направленному цвету интерьера.
- **NPC (0.8.8–0.8.9):** на время отсечения портала у актёров интерьера снимается `AppCulled` с узлов до глубины 3 под корнем и поднимаются `fadeAmount`/`currentFade` корня; в статусе — `actorStates`.
- **Свет в проходе (0.8.9):**
  - лампы интерьера копируются все, даже если игра уже держит их в `ShadowSceneNode[0]` (их освещённая геометрия собрана по сцене улицы);
  - на время прохода у наших копий `frustrumCull = 0`, `lodDimmer = 1` и `worldTranslate` текущий, после прохода значения возвращаются;
  - перенесённым копиям света игрока геометрия собирается заново (`AE#106342`), когда они сдвинулись на 16 единиц.
- **UV квадрата (0.8.9):** сдвиг и масштаб пишутся в оба буфера `texCoordOffset/Scale[0..1]` (игра их чередует).
- **Свет выбирается при отсечении (0.8.10).** Источники объекта выбирает `GetRenderPasses` (`AE#106516` → `AE#107784`), пропуская `frustrumCull == 0xFF`.
  - Поэтому копии «будятся» до `StartAccumulating` и спят после `SubmitAccumulator`.
  - При старте у всей геометрии интерьера ставится `lightData->lightListChanged`, чтобы проходы собрались заново.
- **Глубина `kMAIN` (0.8.10).** После эффектов игра возвращает текущим буфер глубины `kMAIN` (`AE#107522`, `AE#77457` = 0). Поэтому на время прохода подменяется вся ячейка `kMAIN`: текстура, `views`, `readOnlyViews`, SRV, размеры. Затем она возвращается.
- **Отсечение (0.8.11):** контекст портала ставит `ignorePreprocess`. `BSCullingProcess::Process` (`AE#76553`) иначе пропускает узлы с `kPreProcessedNode` и `kNotVisible` — это устаревшие отметки игры для ячейки без игрока. В 0.8.10 так пропадали NPC.
- **Режимы глубины (0.8.11, `[Portal] iDepthMode`, F4 по кругу):**
  - 0 — своя ячейка и SRV `kMAIN` (0.8.9);
  - 1 — вся ячейка `kMAIN` (0.8.10, эффектов нет);
  - 2 — вся `kMAIN`, а мягкие частицы читают копию глубины. Её снимает перехват `BSEffectShader::SetupTechnique` (vfunc 2) перед первым эффектом прохода.
  - Тот же перехват считает эффекты и мягкие эффекты прохода (`effectSetups`, `softEffectSetups` в статусе).
- **Эффекты без света (0.8.12).** Перехват `BSEffectShaderProperty::GetRenderPasses` (vfunc `0x2A`, `AE#106876`): пока наше отсечение регистрирует эффект, копии ламп «спят» (`frustrumCull = 0xFF`).
  - Эффекты с `kEffectLighting` иначе берут до 5 источников. В 0.8.10–0.8.11 огня не было совсем.
  - `[Portal] bLightEffects` включает и выключает это; проходы эффектов пересобираются (`lastRenderPassState = 0x7FFFFFFF`). Прогон 0.8.12: эффекты у очага без `kEffectLighting`, причина не в свете.
  - В статусе — `effects`: 8 эффектов у самой сильной лампы (очаг).
  - Режим глубины с 0.8.12 задаётся только в INI (`iDepthMode`, по умолчанию 2).
- **Анимация NPC (0.8.12, `bAnimateActors`, `bSyncActorPositions`).** Игра двигает анимацию актёров high (`AE#41377`) только с `BOOL_BITS::kProcessMe`. Портал перед проходом:
  - `Update3DPosition(false)`, если 3D дальше 1 единицы от позиции актёра;
  - `Actor::UpdateAnimation(dt)` (vfunc `0x7D`);
  - `root->Update` с `kDisableCollision`.
  - В статусе `animated`, `poseChanged` (кость `NPC Spine [Spn0]`), `positionsSynced`, у актёров `boolBits`, `processMe`, `rootOffset`.
- **Полная анимация NPC (0.8.13).** `Actor::ModifyAnimationUpdateData` (`AE#38054`) считает актёра с `kNotVisible` невидимым, и `BShkbAnimationGraph::Update` (`AE#63587` → `AE#63575`) не двигает ни одной кости (FINDINGS, прогон 0.8.12).
  - Перехват vfunc `0x79` у `VTABLE_Character` ставит актёрам интерьера портала `unk2A = 1` (полное обновление) и `unk2C = 1` (виден).
  - Список актёров для перехвата — атомарный массив: игра обновляет актёров не обязательно в главном потоке.
  - Портал сам зовёт `UpdateAnimation` только для актёров, которых игра не обновила с прошлого прохода.
  - В статусе `animatedByGame`, `animated`, `forcedAnimationUpdates`, `poseChanged`/`poseChecks` (смена позы между проходами).
- **Полная обработка NPC (0.8.14, `[Portal] bFullActorProcessing`).** Detour `AE#37277`: обновление high (`AE#41369`, в заданиях) иначе даёт NPC дальше дистанции затухания от камеры урезанное обновление без ИИ и передвижения (FINDINGS, прогон 0.8.13). В статусе `fullProcessingGranted`.
- **Анимация NPC заданиями игры (0.8.15, `[Portal] bGameAnimatesActors`).** Detour `AE#41466`: после оригинала NPC портала, чья ячейка не подключена, добавляются в список заданий анимации (`AE#41496`), как игра добавляет NPC подключённых ячеек. Портал сам обновляет только тех, кого игра пропустила. В статусе `gameAnimationAdded`.
- **Передвижение NPC (0.8.15, диагностика).**
  - Detour `AE#37350` считает обновления передвижения NPC портала (`movementUpdates`).
  - У актёров в статусе: `charController`, `controllerPos`, `velocity`, `package`.
- **Зона трансформации (0.8.17, запрос пользователя от 27.10.2026, 5.13).** `Portal::IntoInterior(object, point)`, из любого потока: если `object` — 3D ссылки интерьера портала, точка (камера игрока, координаты экстерьера) переносится через дверь в координаты интерьера. Преобразование публикует `Start` (атомарный указатель, не освобождается), `Stop` снимает.
  - Первая проверка: `lodDistance` контроллера персонажа (`+0x29C`) — расстояние от камеры (`AE#50785`) до границы 3D актёра. Его пишет `AE#78274`, вызов в `Actor::Update` (`AE#37348 + 0x1F3`) перехвачен через `write_call` (трамплин SKSE 64 байта, вызов и цель проверяются перед записью).
  - Его читают передвижение и анимация (`AE#37363`, `AE#37690`, `AE#37919`, `AE#38009`). В 0.8.16 NPC интерьера, «далёкие» в своих координатах, уходили под пол.
  - В статусе `pointsCarried`, у NPC `lodDistance`, `controllerState`, `fallTime`, `controllerInInteriorWorld`, `zMin`/`zMax`.
- **Эффекты под наблюдением (0.8.15).**
  - При старте портала до 64 эффектов интерьера, не скрытых узлами, попадают в наблюдение. `Portal::NoteVisible` из перехватов `OnVisible` и перехват `GetRenderPasses` считают для них проходы отсечения портала.
  - В статусе `watchedEffects`: `visits`, `parentVisits`, `passCalls`, `noPasses`, цепочка родителей.
- **Мягкие частицы (0.8.14, `[Portal] bSoftEffects`; F4 до 0.8.19).** Прогон 0.8.14: огонь и дым с ними и без них одинаковы. Выключенные — перехват `BSEffectShader::SetupTechnique` снимает бит 18 (`kSoftEffectBit`) с техники эффекта в проходе портала. В статусе `softEffects`, `softEffectsDropped` и `hiddenEffects` — эффекты интерьера, не попавшие в отрисовку, с причинами.
- **Контроллеры интерьера (0.8.13, `[Portal] iControllerUpdate`; F4 только в 0.8.13).** Частицы огня и дыма и пламя свечей двигают контроллеры, а их — обновление сцены, куда ячейка без игрока не входит. Перед отсечением портал вызывает:
  - режим 1: `UpdateSelectedDownwardPass` (vfunc `0x2D`) у `cell3D`;
  - режим 2: `NiAVObject::Update` с битом 0 (контроллеры).
  - Время — время ячеек игры `AE#400333` (с 0.8.16; до того `GetDurationOfApplicationRunTime() / 1000`, на 18 с впереди), флаг `kDisableCollision`.
  - В статусе `controllers`: узлы с флагами выборочного обновления, частицы, их `lastUpdate` и `lastUpdate` частиц ячейки игрока (время, которое даёт игра).
- **Поворот к камере (0.8.18).** Контекст отсечения портала ставит `cameraRelatedUpdates` (`NiCullingProcess + 0x11C`): без него `NiBillboardNode::OnVisible` (`AE#71063`) не поворачивает пламя и свечение к камере портала, и огонь казался «маленьким» (0.8.13–0.8.17).
- **Затухание и частицы через дверь (0.8.19).** Обходы ячейки за кадр (`AE#18894`, `AE#18900`) зовут для анимированных ссылок `AE#19384`: затухание (`BSFadeNode + 0x130`) и расстояние до камеры (`+0x144`) с камеры мира `AE#36609` через `AE#105486`. Ссылку без затухания игра не обновляет.
  - По `+0x144` `BSPSysLODModifier` (`AE#108480`) задаёт выпуск частиц (`NiParticleSystem + 0x180`) и размер (`+0x188`).
  - В 0.8.16–0.8.18 камера была в ~27 000 единиц: у пламени очага 0 частиц, на свечах нет огоньков.
  - Вызов `AE#19384 + 0xE4` перехвачен `write_call`: для ссылок интерьера портала камера переносится через дверь (`IntoInterior`, `Carried::kFade`). В статусе `fadesCarried`, у наблюдаемых эффектов `fadeDistance`, `emitScale`, `sizeScale`.
- **Двоение картинки (0.8.18–0.8.20, исправлено в 0.8.21).** `BSEffectShader::SetupGeometry` считает векторы движения для TAA из `previousWorld` геометрии (`+0xB0`). Игра не обновляет статичный квадрат, и `NiAVObject::Update` при повторной установке оставлял в `previousWorld` прошлое место: после выхода из таверны — смещённое, потому что дверь ещё двигалась.
  - После установки квадрата `previousWorld = world` у всего его дерева, в статусе `quadPreviousOffset`.
  - F4 выключает и включает TAA игры в памяти: `ImageSpaceManager` (`AE#414660`, указатель) `+ 0x1F0 → + 0x18`, запись `portal.taa`.
- **Бюджет частиц (0.8.21, `[Portal] bIgnoreParticleBudget`).** `AE#74651` (`NiPSysEmitter` vfunc `0x2A`) выпускает `count × AE#388630`. Множитель раз в кадр пишет `AE#71099`: `(iMaxDesired:Particles / живых частиц)²`, если их больше 750, иначе 1. На улице Вайтрана он около 0.46, и очаг за порталом горел вдвое слабее, чем изнутри.
  - Detour `AE#74651` для эмиттеров под 3D интерьера портала ставит множитель 1 на время вызова. В статусе `emissionScale`, `emissionsWithoutBudget`.
- **Огоньки свечей (0.8.21, `[Portal] bMasterParticles`).** В модели свечи только `BSValueNode` `AddOnNode49` (эмиттер). Частицы живут в `BSMasterParticleSystem` под `BSParticleSystemManager` (`AE#411270`, ребёнок 5 корня мира), в мировых координатах.
  - Проход портала вызывает `BSCullingProcess::Process` второй раз со сценой этого менеджера: `AE#106921` не сбрасывает накопитель.
  - Выборка `fires` (частицы: размер, радиус, прозрачность, место) и `masters` (эмиттеры в интерьере, частицы) пишутся в статусе портала и в пробе, пока игрок внутри.
- **Свет NPC подключённого интерьера (0.8.18).** Актёров подключённой ячейки освещает `ShadowSceneNode` как движущиеся объекты (`AE#106349`), лампами, которые не спят для основной камеры. Наши копии в это время спят, и в 0.8.17 NPC были почти чёрными.
  - На время прохода наши лампы, достающие до фигуры NPC (радиус лампы + граница фигуры), добавляются в её `lightData->lights`, затем убираются.
  - В статусе `actorLightsLent`.
- **Фокус окна (0.8.11).** Пока окно игры не на переднем плане или свёрнуто, прохода нет. Смена фокуса пишется в трассу (`window.focus`). Если основной файл трассы занят, пишется `CellLoadResearch.trace-<pid>.jsonl`.
- **NPC (0.8.10):**
  - на время всего прохода снимается `AppCulled` у родителей корня актёра до корня интерьера, их границы пересчитываются;
  - `currentFade` поднимается у всех `BSFadeNode` актёра;
  - в статусе `ancestors` и `firstShape` (прозрачность, проходы, источники прохода, кадр последней регистрации);
  - `OnVisible` считается у узла актёров и двух актёров.
- **Огонь (0.8.8):** на время прохода SRV глубины ячейки `kMAIN` заменяется глубиной портала.
- Границы узлов `cell3D` пересчитываются при старте: у ячейки без игрока они устаревшие (радиус 1 в начале координат).
- **F11 на стадии 6** пишет видимую часть текстуры портала в `logs\portal-N.dds`. `tools/portal_dump_to_png.py` переводит её в PNG.
- **Трасса:** `portal.init`, `portal.dump`, поле `portal` в `interior.sim status` (проходы, пропуски, CPU мс, расстояние глаза до двери, пирамида), подвешивание с режимом 6.

### 5.13 Полноценный портал (цель пользователя от 27.10.2026)

Портал должен стать «зоной трансформации» между координатами экстерьера и интерьера и работать в обе стороны и автономно:
- **свет** проходит через дверь в обе стороны;
- **проверки расстояния** (отрисовка, проработка ИИ и т. п.) для объектов по ту сторону используют координаты игрока, перенесённые через дверь;
- **сквозь портал проходят** стрелы, заклинания и прочие объекты, позже — NPC и сам игрок.

Ограничения движка:
- **Мир Havok** у интерьера свой, а координаты интерьера пересекаются с экстерьером. Поэтому объект, проходящий дверь, нужно перекладывать из ячейки в ячейку: позиция и скорость через преобразование, тело — в другой мир.
- **Расстояние в коде игры** меряется во многих местах. Каждую проверку нужно найти и перенаправить через `Portal::IntoInterior` (или обратное преобразование).

План по шагам, каждый отдельной версией:
1. **Расстояния через дверь.**
   - Сделано: `lodDistance` контроллера персонажа (0.8.17), затухание и расстояние анимированных ссылок, от которого зависят частицы огня (0.8.19).
   - Дальше:
     - полная обработка (`AE#37277`) по перенесённой камере вместо «всегда полная»;
     - LOD анимации (точка глаза в `BSAnimationUpdateData`);
     - уровень обработки в `ProcessLists`;
     - звук.
2. **Свет в обе стороны.**
   - Лампы интерьера освещают улицу у двери: копии в координатах экстерьера.
   - Уличные лампы у двери освещают интерьер: как уже перенесённый свет игрока.
3. **Звук интерьера снаружи:** бард, огонь.
4. **Вид изнутри наружу:** второй портал на внутренней двери. Для него нужна подключённая сетка экстерьера, пока игрок внутри.
5. **Снаряды через дверь:** стрелы, заклинания. Пересечение прямоугольника двери, перенос ссылки в другую ячейку с преобразованием позиции и скорости. Коллизия дверей не должна их останавливать.
6. **NPC и игрок через дверь без экрана загрузки:** это исходная цель NoLoads. Вход в подключённый интерьер уже занимает 70 мс (прогон 0.8.16).

## 6. Прогон и анализ

**Вариант A — играет пользователь.**

1. Пользователь ставит zip в Vortex, делает Deploy, запускает игру через SKSE в Vortex.
2. Загружает тестовое сохранение и жмёт F10 или просто ходит через двери.
3. После выхода из игры ты читаешь `CellLoadResearch.trace.jsonl` и `CellLoadResearch.log`.

**Вариант B — автономно, только с явного разрешения пользователя.**

1. Убедись, что последняя версия архива установлена и развёрнута в Vortex: DLL с нужной версией лежит в `<Game>\Data\SKSE\Plugins`. Версию бери из ресурсов DLL.
2. Временно выставь в INI в `Data` значения `bEnabled=1` и `bQuitWhenDone=1`.
   - INI — жёсткая ссылка на файл в staging Vortex. Правь на месте, без пересоздания файла.
   - После прогона верни значения обратно.
3. Steam должен быть запущен. Запусти `<Game>\skse64_loader.exe` и следи за трассой до записи `autotest.done` или до таймаута 15 мин.
4. Если игра упала — найди crash-лог (`Crash Logger SSE AE VR` пишет в папку SKSE), сопоставь стек с `.pdb` и исправь.

**Анализ (`tools/analyze_trace.py`, только stdlib Python 3):**

- Сегментирует трассу по переходам: от `autotest.step.begin` или `LoadingMenu open` до `kFinish` + `LoadingMenu close`.
- Для каждого перехода выводит:
  - упорядоченную последовательность `ev` с `Δt`;
  - длительности (`LoadingMenu`, `SetCurrentCell`, сумма `CellLoaderTask::Run`);
  - распределение по потокам;
  - вызывающих (`caller`).
- Сводная таблица по шагам сценария, сравнение «с буфером / после purge».
- Вывод в Markdown, который вставляется в `docs/FINDINGS.md`.

**`docs/FINDINGS.md`:** для каждой гипотезы H1–H10 — вердикт, доказательства (2–5 строк трассы), открытые вопросы. В конце — уточнённая схема реального потока загрузки.

---

## 7. Правила безопасности и подводные камни

- **Мутации игры** (телепорты, `purge`, `Activate`, загрузка сохранения) — только в главном потоке, из тика `Update` (очередь `MainThread::Post` → `MainThread::Drain` в хуке `PlayerCharacter::Update`). Не делай их внутри обработчиков событий.
  - **Не используй `SKSE::GetTaskInterface()->AddTask` для отложенной работы.** В 0.3.0 задача, которая ставила себя в эту очередь повторно, повесила игру: SKSE крутит очередь, пока она не опустеет.
  - На 1.6.1170 **не в главном потоке** приходят: события ввода, `TESObjectDOOR::Activate`, `CellFullyLoaded` экстерьера и SKSE-сообщение `kDataLoaded`.
- **Не храни `TESObjectCELL*` ячеек экстерьера дольше текущего вызова.** Игра удаляет их, когда выгружает мир; в 0.4.0 это привело к падению при `coc` в другой мир. Храни FormID, координаты или готовую строку. Интерьеры и `TESWorldSpace` — постоянные формы.
- **Профилировщик (`Profiler.*`)** приостанавливает главный поток. Между `SuspendThread` и `ResumeThread` разрешены только системные вызовы и `memcpy`: никакой кучи, блокировок и логирования.
- **`<windows.h>` подключён в PCH через spdlog, и его макросы ломают код:** `MAX_PATH`, `min`, `max`, `CP_ACP`. Пиши `(std::min)(...)` и не называй константы этими именами.
- `skyrim_cast` и `netimmerse_cast` в NG объявлены в глобальном пространстве имён, не в `RE`.
- **`RE::BSGraphics::RenderTargetManager::GetSingleton()` в NG неверен:** он разыменовывает `AE#411451`, а там лежит сам менеджер. В 0.8.6 это уронило игру. Бери `REL::Relocation<RenderTargetManager*>{ RELOCATION_ID(524970, 411451) }.get()`.
  - Прежде чем верить синглтону NG, посмотри, как его адрес передаёт игра: `lea` — это объект, `mov` — указатель.
- `<windows.h>` определяет пустые макросы `near` и `far`: так нельзя называть переменные.
- **`NiNode::DetachChild` (`AE#70292`) не проверяет родителя:** номер берётся из ребёнка (`NiAVObject + 0x38`). Не вызывай его (и функции игры, которые его зовут, например `AE#19130`) для узла с чужим родителем. В 0.8.17 так был снят и освобождён чужой узел.
- **Машинный код игры:** `D:\VS\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe /disasm:nobytes /range:<VA>,<VA> SkyrimSE.exe` (база `0x140000000`). RVA по ID и вызывающие — `tools/name_functions.py`, кто берёт адрес (vtable) — `--xref ID`.
- **Хуки `CellLoaderTask::Run`** выполняются в IO-потоках. Там можно читать только поля самой задачи, писать в трассу и вызывать оригинал. Никаких обращений к `TES`, игроку или UI.
- **Никаких блокировок и тяжёлой работы в хуках.** Снимок сетки ограничен `length²` ячеек; `bLogCellAttachPerRef` выключен по умолчанию.
- **Поля `TES` дальше 0x128 — только через аксессоры** (раскладка меняется с 1.6.1130). Поля игрока — только через `GetPlayerRuntimeData()`. Поля `unkXXX` только логируй, не используй в логике.
- **Буферы ячеек (H9):** читать только при `bReadCellBuffers=1` и только до `min(значение INI, 64)` элементов, с проверкой `nullptr` и `__try/__except` в отдельной функции без объектов с деструкторами. Если будет падение — оставь выключенным и запиши это в FINDINGS.
- **При старте проверь версию runtime:** `REL::Module::get().version()`. Если это не 1.6.1170, запиши в лог warning, но плагин не выключай — NG поддерживает несколько runtime.
- **Не меняй `Skyrim.ini` / `SkyrimPrefs.ini` без разрешения.** Если эксперимент требует сменить `uGridsToLoad` или буферы — сделай резервную копию и скажи пользователю, что меняешь и как откатить. Учти, что INI в «Документах» может быть защищён от записи.
- **Сохранения:** автотест не должен сохранять игру. Предупреди, что настройка «автосохранение при переходе» создаст autosave — это безопасно, но лучше использовать тестового персонажа.
- **Сборку проверяй в игре.** CommonLib прямо предупреждает: ошибки раскладки компилируются без ошибок и проявляются только как падение в игре.

---

## 8. Стартовый код

`src/main.cpp` (передан пользователем) — минимальный пример: подписка на события из 3.5, хук `TESObjectDOOR::Activate`, который в бою блокирует загрузочные двери, и хук `GridCellArray::SetCenter`. Он **не собирался**.

1. Начни с того, чтобы он собрался.
2. Хук-блокировку двери оставь за INI-флагом `bBlockDoorsInCombat=0`.
3. Затем разнеси код по файлам: `Trace.*`, `Events.*`, `Hooks.*`, `Snapshot.*`, `AutoTest.*`, `Settings.*`.

## 9. Порядок работы

1. Проверка окружения (раздел 1) → сводка пользователю.
2. Обновление сабмодуля, переименование, сборка стартового кода → первый zip `0.1.0`. Пользователь ставит его в Vortex и подтверждает, что лог появился (smoke-test).
3. Трасса + события + vfunc-хуки → `0.2.0`, прогон.
4. Detour-хуки + снимки → `0.3.0`, прогон.
5. Исправление зависания по F11, эксперимент Preload (5.5), уведомления (5.6) → `0.4.0`, прогон.
6. Исправление падения (висячий указатель на ячейку), профилировщик экрана загрузки, экстерьер в Preload выключен по умолчанию → `0.5.0`, прогон.
7. Хвост Havok (дерево вызовов, `name_functions.py`) и проба симуляции H11/H12 → `0.6.0`, прогон.
8. Счётчик шагов Havok, перехват обработчика `TES`, эксперимент `fWarmStartMaxTime` (F9), физика в пробе → `0.7.0`, прогон.
   - `0.7.1`: F9 → F7, наборы `[Overrides]` (прогрев + затемнения), разбивка «дверь → картинка» (H13), профилировщик снова включён: найти 55 мс после `TES` при выходе наружу без прогрева.
   - `0.7.2`: набор с `fTeleportDoorActivateDelayTimer=0` (пауза 0.65 с до затемнения), запись `frame.hitch` (подвисания в первые 5 с после экрана загрузки, автосохранение), проверка смены мира (ворота Вайтрана) с выключенными затемнениями.
9. Детекторы дверей и окон (5.10), удержание интерьера и InteriorSim (5.11) → `0.8.0`, прогон.
   - `0.8.1`: очередь сообщений, блок выгрузки менеджером `AE#40845`, шаг интерьера в потоке физики под блокировкой, снятие скрытия и прозрачности при отрисовке, исправление позиции вершин окон, F6 «что под взглядом».
   - `0.8.2`: блок самой выгрузки `AE#13721` для удерживаемого интерьера, контрольная копия двери и стадия 5 (клон интерьера), скрытие наружной двери, `fadeWatch`, луч Havok для F6, сырые байты вершин окон.
   - `0.8.3`: исправлено падение (клоны без актёров и без освобождения), 4 контрольные копии двери на разных родителях, регистрация в `ShadowSceneNode`, скрытие двери интерьера, позиции вершин как `float`, `lattice` в ключевых словах окон.
   - `0.8.4`: детекторы выключены (двери ищутся молча), 5 контрольных копий двери в ряд на улице, счётчики `OnVisible` для подвешенных узлов, скрытие частей здания в проёме, луч F6 сквозь скрытые ссылки.
   - `0.8.5`: интерьер в корне узлов ячейки экстерьера (рисуется только там), снятие перед отсоединением ячейки, 2 контрольные копии ближе к двери, скрытие куска здания с текстурой двери, поиск ссылок в постоянной ячейке мира и лучами.
   - `0.8.6`: портал (5.12): стадия 6, вторая камера движка в текстуру на месте двери, ассеты `Meshes/`/`Textures/` в архиве, снимок портала по F11. Упал при включении стадии 6 (неверный синглтон `RenderTargetManager` в NG).
   - `0.8.7`: исправлен адрес `RenderTargetManager`. Портал работает: интерьер виден через дверь с любой точки.
   - `0.8.8`: освещение портала (свет интерьера и копии света игрока с `restrictedNode`, фоновый свет интерьера), NPC в портале, огонь с глубиной портала, разрешение по видимой части двери.
   - `0.8.9`: UV в оба буфера (дубль картинки), копии всех ламп интерьера, копии «включены» на время прохода (`frustrumCull`, `lodDimmer`, `worldTranslate`), пересбор геометрии для света игрока, данные о скрытии NPC.
   - `0.8.10`: свет «будится» до отсечения и проходы интерьера пересобираются, подмена всей ячейки глубины `kMAIN` (огонь вблизи двери), NPC — родители и все узлы затухания, данные отсечения их геометрии.
   - `0.8.11`: `ignorePreprocess` при отсечении (NPC), режимы глубины по F4 и счётчик эффектов (огонь), пропуск прохода без фокуса окна, запасной файл трассы.
   - `0.8.12`: стадии 1–5 убраны (F8: выключено / портал, архив в `docs/INTERIOR_SIM_STAGES.md`), эффекты без света ламп при отсечении (F4 — сравнение), анимация и позиции NPC интерьера, диагностика эффектов.
   - `0.8.13`: полная анимация NPC интерьера (перехват `ModifyAnimationUpdateData`), контроллеры интерьера (огонь, дым, свечи) перед каждым проходом, F4 — режимы контроллеров, диагностика частиц.
   - `0.8.14`: полное обновление high для NPC портала (detour `AE#37277`: ИИ и передвижение), F4 — мягкие частицы в портале, диагностика эффектов вне отрисовки. Alt+Tab отложен.
   - `0.8.15`: NPC портала в заданиях анимации игры (detour `AE#41466`), счётчик передвижения (detour `AE#37350`) и данные контроллера персонажа, наблюдение за эффектами в отсечении портала.
   - `0.8.16`: интерьер портала подключается, как соседняя ячейка (вызовы игры `AE#18976` / `AE#19063`, отключение `AE#19130` / `AE#19046`), добавлен в обходы ячеек за кадр (`AE#13291`, `AE#13325`), время контроллеров — время ячеек `AE#400333`.
   - `0.8.17`: 3D подключённого интерьера под своим узлом вне сцены (свет в портале), зона трансформации — `lodDistance` контроллера персонажа через дверь (NPC под полом), диагностика высоты NPC и фигур свечей. Упал при входе в таверну.
   - `0.8.18`: исправлено падение (3D снимается с нашего узла до `AE#19130`, detour `AE#19130`), `cameraRelatedUpdates` в проходе портала (пламя к камере), свет наших ламп для NPC на время прохода, добавочные узлы свечей в трассе.
   - `0.8.19`: затухание анимированных ссылок через дверь (`AE#19384` → `AE#105486`: огонь и свечи), в списке NPC портала только NPC интерьера, диагностика застревания NPC на лестнице и под настилом (флаги контроллера) и двоения картинки.
   - `0.8.20`: F4 — TAA игры в памяти (проверка двоения), выборка фигур огня в статусе портала и в пробе изнутри (сравнение пламени).
   - `0.8.21`: `previousWorld` квадрата портала (двоение с TAA), эмиттеры интерьера вне бюджета частиц игры (огонь вдвое слабее, чем изнутри), общие системы частиц в проходе портала (огоньки свечей).
10. Автотест → `0.9.0`, прогон (вариант A или B).
11. Анализ, `FINDINGS.md`, README.

После каждой версии кратко сообщай: что изменилось, путь к zip, что сделать в Vortex.
