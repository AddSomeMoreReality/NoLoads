# T16. Реакции на вход через портал: фильтр Story Manager по тегам

Этап 2. Ветка `task/T16-story-filter`, worktree `..\CLR-T16`. Код: новый `src/StoryFilter.*` (свои перехваты, одна строка в `Hooks::Install`, правило T28 в `Hooks.h`), `Settings`, INI. Прогон: да. Начинать после T28. Прогон 0.8.30 подтвердил: на входе песня обрывается через 1–2 с.

**Решения пользователя.**
- 04.11.2026: сначала посмотреть R8 в игре, затем политика по тегам (T30).
- 05.11.2026: принята политика T30 (`docs/tasks/results/T30.md`, `docs/LOCATION_TRIGGERS.md`, 4.5–4.9): гасится только `BardSongStarter` и только когда песня уже идёт. Приветствия (`WIGreeting`, `WITavern`), радиантные встречи, одноразовые сценки и сюжет остаются.

Политика — строка INI. Поэтому одна сборка: по умолчанию T30, R8 — заменой одной строки. В трассе для каждого запуска по `CLOC` пишутся решения обеих.

## Что известно (T15, T30)

- **Песню барда на входе обрывает `BardSongStarter`.** Его запускает Story Manager по `CLOC`, через +1.5 с `StopAllSongs`, через +2.2 с новая песня (прогон 0.8.27).
- **`CLOC` рождается в одном месте:** `PlayerCharacter::SetParentCell` (`AE#40466`, vtable `PlayerCharacter` слот `0x98`). Событие собирает `+0x105` (`AE#32288`), в очередь ставит `+0x11c` (`AE#32359`); на `+0x154` синхронно уходит `OnLocationChange`.
- **Квест выбирается позже**, при разборе очереди (`AE#40438` → `AE#32484` → `AE#32483`): `BGSStoryManagerQuestFinder` vfunc 3 (`AE#32430`, `VTABLE_BGSStoryManagerQuestFinder`, `RELOCATION_ID(256099, 205026)`). Возврат 0 — квест запущен, 2 — нет. Событие — `finder+0x48`.
- **Теги T30** (критерии — `LOCATION_TRIGGERS.md`, 4.5; что есть в памяти — 4.6, по заголовкам NG, в игре не проверено): `story`, `once`, `scene`, `greets`, `encounter`, `technical`, `restarts`, `placeKind`, `placeSpecific`. `restarts` — производный: `technical ∧ placeKind ∧ ¬placeSpecific ∧ ¬once ∧ ¬story ∧ ¬encounter`.
- **Политики теми же тегами:**
  - T30: `restarts & !story & !greets & !once` и «уже идёт»;
  - R8: `!story & !once & !placeSpecific & (placeKind | encounter)` (на 186 квестах совпадает с R8 из T15).
- **«Уже идёт»** (`LOCATION_TRIGGERS.md`, 4.7): у квеста-контроллера (свойство скрипта стартера, `BardSongStarter` → `BardSongs`) играет сцена (`BGSScene::isPlaying`), её актёр — в ячейке назначения. Без этой проверки в мире без загрузок молчавший бард не запоёт никогда.
- **Не делать:** подмену старой локации в событии (ломает `MS11Kicker`, `MGR01Trigger`, `T03PostQuest`) и гашение `OnLocationChange` (49 из 60 скриптов ведут ход квестов).

## Сделать

1. **`src/StoryFilter.h/.cpp`.** Хук vfunc `0x98` `VTABLE_PlayerCharacter[0]`:
   - до оригинала спросить `Portal::ObservedEntry(oldCell, newCell)`: есть ли портал, у которого ближняя сторона `old`, дальняя `new`, и он рисовал картинку ≥ 1 с за последние `fTagSeconds`;
   - если да — метка `{oldLocation, newLocation, newCell, portal, t}` в кольцо на 8 элементов. Только FormID, без указателей на ячейки экстерьера (`CLAUDE.md`, раздел 7).
2. **Хук vfunc 3 `BGSStoryManagerQuestFinder`.** Событие `*(BGSStoryEvent**)(this + 0x48)`, индекс `CLOC` один раз по `registeredEvents[i].uniqueID`. Если локации события (`members[1]`, `members[2]`) совпадают со свежей меткой и политика от маски квеста говорит «гасить» — вернуть 2, иначе оригинал.
3. **Кэш тегов** при `kDataLoaded`: для каждого квеста в узлах `CLOC` — маска тегов T30 (обход узлов с `parent`, `TESQuest::data`, `objectives`, стадии с журналом, сцены, псевдонимы, условия, `TESTopic::ownerQuest` для реплик). В лог — каждый квест: EditorID, маска, решение R8 и T30. Ожидание в ванили: R8 — 17, T30 — 1 (`BardSongStarter`). Сверить с таблицей раздела 12 `LOCATION_TRIGGERS.md`; расхождения — в результат.
4. **Политика из INI** — маленький разбор выражения (имена тегов, `!`, `&`, `|`, скобки) один раз при загрузке настроек, в хуке — только вычисление над маской.
5. **«Уже идёт».** Контроллер — из свойства скрипта стартера через VM при первом обращении (если читается), иначе из `sRestartsController`. Проверка в хуке — только поля сцены и FormID ячейки актёра, без блокировок. Если не удалось определить контроллер — не гасить и записать причину в трассу.
6. **INI `[Story]`** (комментарии на отдельных строках), по `LOCATION_TRIGGERS.md`, 4.7: `bSuppressOnPortalEntry=1` (0 — поведение игры), `sSuppressIf=restarts & !story & !greets & !once`, `bRestartsOnlyIfPlaying=1`, `sRestartsController=`, `sForceSuppress=`, `sForceKeep=`, `sTagOverride=`, `fTagSeconds=5`, `bTraceStory=1`.
7. **Трасса:** `story.tag` (метка входа), `story.skip`, `story.start` — все запуски по `CLOC`: EditorID, маска, решения R8 и T30, «уже идёт», id потока.
8. **`Portal::ObservedEntry`** — функция портала, а не глобальная проверка: сейчас один портал, в T29 — реестр (5.14, п. 1). Правило не делит переходы на «улица/интерьер», работает на входе и на выходе, для любых пар ячеек (п. 2).

## Безопасность

- В каком потоке разбирается очередь Story Manager (`AE#40438`), неизвестно: кольцо меток без блокировок (атомарные индексы), в хуке только сравнение FormID и маски.
- `ObservedEntry` читает состояние портала: если хук vfunc 3 идёт не в главном потоке, состояние для него копировать в атомарные поля на тике `Update`.
- Чтение свойств VM — не в хуке Story Manager, если оно берёт блокировки VM: заранее, в главном потоке (на `kDataLoaded`, `kPostLoadGame` или тике), результат — FormID в атомарном поле.
- Новые смещения (`finder+0x48`, раскладка `BGSStoryEvent`, `AE#40466+0x105/+0x11c/+0x154`, `TESQuest::formEditorID +0x230`, `BGSScene::isPlaying +0xB0`, `TESTopic::ownerQuest +0x40`) — в `docs/PORTABILITY.md`.
- Данные игры не меняются (`CLAUDE.md`, 5.14, п. 7): только перехват.

## Как проверить в игре

1. Политика T30 (по умолчанию), F8. В «Гарцующей кобыле» дождаться песни, выйти, войти через портал: `story.skip BardSongStarter`, песня не обрывается (нет `StopAllSongs` через +1.5 с). Трактирщик подходит (`story.start WITavern`).
2. Выйти, дождаться конца песни, когда бард молчит, войти: `story.start BardSongStarter` («не идёт»), бард начинает песню.
3. Войти через портал в лавку или жилой дом, если портал туда поставлен: `story.start WIGreeting`, хозяин здоровается.
4. R8: в INI `sSuppressIf=!story & !once & !placeSpecific & (placeKind | encounter)`, `bRestartsOnlyIfPlaying=0`, перезапуск игры, п. 1: `story.skip` у `BardSongStarter` и `WITavern`, трактирщик не подходит.
5. Контроль: F8 выключен, тот же вход — `story.start BardSongStarter`, песня меняется, как в 0.8.27.
6. По id потока в `story.start` записать, где идёт `AE#40438`.

## Конфликты

`Hooks::Install` (одна строка), `Settings.*`, INI, `Portal.h`. С T20 — если её вход без отключения изменит вызов `SetParentCell`: бесшовный вход должен вызывать его ровно один раз за переход.
