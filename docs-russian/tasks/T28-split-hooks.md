# T28. Разрезать `Hooks.cpp` без изменения поведения

Этап 2, **первая и одна**: T16, T20, T22, T29 добавляют перехваты, и пока `Hooks.cpp` целый, они конфликтуют. Ветка `task/T28-split-hooks`, worktree `..\CLR-T28`. Код: `src/Hooks*`, чуть `ActorHold.cpp`, `Scenes.cpp`. Прогон: контрольный, сводка должна совпасть с 0.8.30-b / 0.8.31.

## Что известно

- `src/Hooks.cpp` — 1 243 строки, около 54 перехватов: 28 vfunc в `Hooks::Install`, 21 detour одной транзакцией в `InstallDetours`, 5 `write_call` в `InstallCalls`. Логики почти нет: перехват считает или зовёт модуль.
- План из `docs/tasks/results/T02.md` («Открытые вопросы»):
  - `HooksCommon.h` — `WriteVfunc`, `AttachDetour`, `WriteCall`, `HookScope`, `AddTesState` (и `AddPlayerPos`);
  - `HooksCells.cpp` — дверь, `GridCellArray`, `CellLoaderTask`, `SetCurrentCell`, буферы, выгрузка, `QueueCellLoad`, `MoveTo`, Havok;
  - `HooksInteriorSim.cpp` — `BhkWorldStep`, `UpdateCells`, `UpdateAnimatedRefs`, `CellDetach`, `SetPortalGraph`, `OnVisible`;
  - `HooksPortal.cpp` — эффекты, частицы, граница списков сцены, `PlayerView`, субтитры, звук, перехваты NPC (камера мира, расстояния, LOD контроллера, затухание, `HighActorCuller`, ступни);
  - в каждом файле `InstallVfuncs()`, `AttachDetours(results&)`, `InstallCalls()`; `Hooks.cpp` держит **одну** транзакцию detour, `Ready()` и `BufferInterior`.
- `ActorHold.cpp` и `Scenes.cpp` ставят свои перехваты сами, каждый своей транзакцией. В `ActorHold.cpp` своя копия `WriteCall` (без записи в лог).

## Сделать

1. **Разрезать по плану.** Код перехватов переносится как есть: тела `thunk`, комментарии, ID, смещения, индексы vfunc.
   - Если файл выходит больше ~500 строк (`HooksCells.cpp`), раздели его, например, на `HooksCells.cpp` (дверь, сетка, `CellLoaderTask`) и `HooksTes.cpp` (функции `TES`, Havok, выгрузка).
   - Структуры перехватов — в анонимном пространстве имён своего файла. Наружу — только функции установки (`namespace Hooks::Cells` и т. п.), объявленные в `HooksCommon.h` или своём заголовке.
   - `PlayerUpdate` (счётчик кадров и `MainThread::Drain`) остаётся в `Hooks.cpp`.
   - `g_detoursReady`, `g_callsReady` и смысл `Ready()` не меняются: `g_callsReady = lod && fade`.
2. **Общие помощники — в `HooksCommon.h`.** Одна копия `WriteCall` для всех: `ActorHold.cpp` берёт её оттуда. Если сообщения об ошибке разные, оставь вариант с записью в лог. Помощник для отдельной транзакции модуля по образцу `ActorHold::Install` и `Scenes::Install`: начать, добавить detour с именем, закрыть с записью ошибки. Их транзакции остаются отдельными.
3. **Правило для новых модулей** (T16 `StoryFilter`, T20, T22). Перехваты модуля лежат в его `.cpp`, ставит их `Module::Install()` — по образцу `ActorHold` и `Scenes`. В `Hooks::Install` добавляется одна строка вызова. Запиши правило коротко в комментарий `Hooks.h` и в `docs/ARCHITECTURE.md`, 3.6.
4. **Лог.** Строки `vfunc hooks installed`, `detours installed`, `call hooks installed: controller LOD …, fade …, pass at the scene list fence …, player view …, subtitles …` — тот же текст и те же значения. Порядок установки внутри групп может измениться.
5. **Проверка переноса** (скрипт в scratchpad, как в T02):
   - мультимножество непустых строк `HEAD:src/Hooks.cpp` против новых файлов; отличия — только `#include`, `namespace`, объявления, установка;
   - список перехватов до и после совпадает: пары (vtable, индекс) у `WriteVfunc`, ID у `AttachDetour`, (функция, смещение, цель) у `WriteCall`. Выведи обе таблицы в результат как одну строку «совпали: N vfunc, N detour, N call».
6. **Документы:** `docs/ARCHITECTURE.md` — строка `Hooks.cpp` в 3.1, число и место перехватов в 3.6, строка в 8.3 (сделано). Остальные документы не трогай.
7. **Версия** 0.8.32 в `xmake.lua`. Сборка `scripts/build.ps1`: ноль ошибок и предупреждений от `src/`, `tar -tf` архива как в `CLAUDE.md`, раздел 2.

## Безопасность

- Никаких новых перехватов, адресов и изменений логики. Если по ходу найдёшь ошибку, запиши её в «Открытые вопросы», не исправляй.
- `CLAUDE.md`, раздел 7: хуки `CellLoaderTask::Run` в IO-потоках, никаких блокировок в хуках — при переносе ничего не добавляется.
- Первая сборка в новом worktree компилирует CommonLib заново (долго): запускай `build.ps1` в фоне.

## Как проверить в игре (контрольный прогон, без Engine Fixes)

1. Vortex: `CellLoadResearch-0.8.32-T28-split-hooks.zip` (Replace), Deploy. Engine Fixes выключен.
2. Сохранение у «Гарцующей кобылы», F8, посмотреть в портал; войти и выйти 2–3 раза; один раз F2, один раз F11.
3. В логе: `build: branch task/T28-split-hooks`, три строки установки как в п. 4, `actor hold: …`, `scene trace: …`, ошибок нет.
4. Логи из `..\CLR-T28\logs\` переложить в `logs\run-0.8.32\` основной папки, `python tools/run_digest.py logs/run-0.8.32`.
5. Сравнить `digest.md` с `logs/run-0.8.30-b/digest.md` (без Engine Fixes, та же модель квада): `portal.passes`, `soundsCarried`, `distancesCarried`, `imageSpace.swaps`, `actor.keptHigh` на переходах, прыжки, `frame.hitch` — того же порядка, зависаний нет.

## Конфликты

Со всем, что трогает `src/Hooks.cpp`. Другие задачи с кодом этапа 2 начинаются после слияния T28.
