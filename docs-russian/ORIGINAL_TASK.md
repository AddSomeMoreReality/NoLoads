# Исходное задание (бывшие разделы 0, 4, 5.1–5.3, 6 и 8 `CLAUDE.md`)

Текст перенесён без изменений. Нумерация разделов старая: ссылки вида «раздел 4», «5.3» относятся к этому файлу. Остальные подразделы 5.x — `docs/FEATURES.md`, 5.14 — `CLAUDE.md`.

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


## 5. Что должен делать плагин (начало)

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


## 8. Стартовый код

`src/main.cpp` (передан пользователем) — минимальный пример: подписка на события из 3.5, хук `TESObjectDOOR::Activate`, который в бою блокирует загрузочные двери, и хук `GridCellArray::SetCenter`. Он **не собирался**.

1. Начни с того, чтобы он собрался.
2. Хук-блокировку двери оставь за INI-флагом `bBlockDoorsInCombat=0`.
3. Затем разнеси код по файлам: `Trace.*`, `Events.*`, `Hooks.*`, `Snapshot.*`, `AutoTest.*`, `Settings.*`.

