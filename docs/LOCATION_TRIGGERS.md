# Реакции игры и скриптов на смену локации игрока

Задача T15, 04.11.2026. Только исследование: данные игры прочитаны, ничего не изменено. Источники: `Skyrim.esm`, `Update.esm`, `Dawnguard.esm`, `HearthFires.esm`, `Dragonborn.esm` (FormID приведены к порядку загрузки 00–04), исходники Papyrus из `Data\Source\Scripts`, `SkyrimSE.exe` 1.6.1170 (dumpbin, `tools/name_functions.py`). USSEP 4.3.8a сверен отдельно (раздел 4.4). Скрипты разбора лежат в scratchpad T15, не в репозитории.

Обозначения: `L1` — старая локация в событии, `L2` — новая; `в X` — текущая локация игрока равна `X` или вложена в неё (`GetInCurrentLoc`); `L2∋K` — у `L2` есть ключевое слово `K`; `здесь∋K` — `LocationHasKeyword` на игроке.

## 1. Механизмы

| Механизм | Что сообщает | Сколько в данных | Где рождается в движке | Что значит для портала |
|---|---|---|---|---|
| Story Manager, событие `CLOC` | игрок сменил локацию: `R1` = игрок, `L1`, `L2` | 186 квестов в узлах `CLOC` (172 с `ENAM=CLOC`, ещё 14 с пустым `ENAM`); 657 узлов SM всего | только `PlayerCharacter::SetParentCell` (`AE#40466`, vfunc `0x98`) | главный источник «реакций на вход», в том числе песни барда |
| Papyrus `OnLocationChange` | то же, для актёра и его псевдонимов | 77 скриптов, 60 привязаны, 144 привязки: 101 псевдоним квеста, 38 `NPC_`, 5 `ACHR` | для игрока — тот же вызов `AE#40466`; для NPC — `AE#19799` (перенос через дверь), `AE#37313`, `AE#20541`, `AE#20542` | почти всё — ход сюжетных квестов; гасить нельзя (раздел 6) |
| Papyrus `OnStoryChangeLocation` | квест запущен событием `CLOC` | 2 скрипта (`CWMapTableSceneScript`, `dunTalkingStoneChangeLocationScript`), 5 квестов | диспетчер `AE#53828` (сверка с индексом `CLOC`) | следствие `CLOC`, отдельно не перехватывается |
| `OnCellLoad`, `OnCellAttach`/`OnCellDetach`, `OnLoad`/`OnUnload`, `OnAttachedToCell`/`OnDetachedFromCell` | ячейка или 3D объекта загружены/подключены | 110 / 184 / 85 / 432 / 116 / 4 / 10 скриптов; 1 834 / 4 051 / 1 471 / 4 232 / 1 890 / 466 / 494 привязки | подключение ячейки, не локация | срабатывают, когда портал подключает интерьер, а не на входе. Если вход отключает и снова подключает интерьер, они сработают повторно (это T20) |
| `OnTriggerEnter`/`OnTriggerLeave` | игрок вошёл в объём | 611 / 140 скриптов; 2 713 объёмов с `OnTriggerEnter`, из них 608 в пределах 512 единиц от загрузочной двери | физика (фантомы Havok) | работают как в ванили, пока игрок физически проходит через объём (раздел 7) |
| Условия `CTDA` | проверяют текущую локацию при вычислении | `GetInCurrentLoc` 1 590, `GetInCell` 1 286, `GetKeywordDataForLocation` 783, `LocationHasKeyword` 752 и др. (раздел 8) | вычисляются по запросу | событий не рождают; читают `currentLocation` игрока, который меняет тот же `AE#40466` |

Другого события Story Manager про локацию нет. Функцию регистрации `AE#32348` игра вызывает 31 раз: 23 события в `AE#5163`…`AE#5220`, ещё 8 о преступлениях и отношениях в `AE#32284`…`AE#32323`. Про смену локации только `CLOC` (`AE#5197`: члены `Actor`, `Old Location`, `New Location`).

## 2. Путь `CLOC` в движке (1.6.1170)

1. **`PlayerCharacter::SetParentCell(newCell)`** — `AE#40466`, vtable `PlayerCharacter` слот `0x98` (в NG `TESObjectREFR::SetParentCell`, `// 98`). Достоверность высокая (дизассемблер).
   - `+0x29`: если ячейка не сменилась, выход.
   - `+0xe3`: `TESObjectCELL::GetLocation` (`AE#18905`, в NG `RELOCATION_ID(18474, 18905)`) — новая локация.
   - `+0xeb`: сравнение с `currentLocation` игрока (`[player+0xAD0]` на 1.6.1170; в NG `GetPlayerRuntimeData().currentLocation`). Равны — ни `CLOC`, ни `OnLocationChange`.
   - `+0x105`: `AE#32288` собирает событие `{handle игрока, BGSLocation* старая, BGSLocation* новая}`. Другого вызова у `AE#32288` нет.
   - `+0x10a`…`+0x11c`: `BGSStoryEventManager::GetSingleton` (`AE#22790`) и `AddEvent_Impl` (`AE#32359`, в NG `RELOCATION_ID(31576, 32359)`) с индексом из глобала `AE#380069` (его пишет регистрация `AE#5197+0x188`).
   - `+0x130`…`+0x154`: `ScriptEventSourceHolder` (`AE#14298`) `+0x108` → `AE#20039` рассылает `TESActorLocationChangeEvent {игрок, старая, новая}` синхронно.
   - `+0x19b`: `currentLocation = новая`.
2. **`AddEvent_Impl` только ставит событие в очередь** `BGSStoryEventManager::events` (`BGSStoryEvent`, 0x38 байт: `+0x00` id, `+0x04` индекс типа, `+0x08…` члены; для `CLOC`: `[0]` handle игрока, `[1]` старая `BGSLocation*`, `[2]` новая). Разбор очереди отложен: `AE#40438+0x839` → `AE#32484` → `AE#32483` (цикл событий). Достоверность средняя: поток, из которого зовётся `AE#40438`, не проверен.
3. **Выбор квеста** — посетитель `BGSStoryManagerQuestFinder` (vtable в NG `VTABLE_BGSStoryManagerQuestFinder`, `RELOCATION_ID(256099, 205026)`; создаётся в `AE#32370`, конструкторе `BGSStoryManager`):
   - слот 1 `VisitBranchNode` — `AE#32428`, слот 2 `VisitQuestNode` — `AE#32429` (условия узлов, флаги «случайно», «общее событие»);
   - слот 3 **`VisitQuest(TESQuest&)` — `AE#32430`**: проверяет `TESQuest::storyManagerConditions` (`+0x110`), затем `AE#25048(quest, event = finder+0x48, …)` запускает квест и пишет `quest->startEventData`. Возврат: 0 — запущен; 2 — условия не выполнены (код журнала 9) или запуск не удался (код `0xA`); 1 — `AE#25048` вернул «повторить позже» (код `0xB`). Достоверность средне-высокая (дизассемблер, семантика возврата не проверена в игре).
4. **`OnLocationChange`**: приёмник `SkyrimVM` (`SkyrimVM[+0x20]` слот 1, `AE#53961`, строка `"OnLocationChange"`) рассылает событие скриптам актёра и его псевдонимов.

Отсюда тайминг из прогона 0.8.27: `CLOC` уходит в очередь в кадре входа, квест стартует через кадр-другой, фрагмент `BardSongStarter` зовёт `PlaySongRequest`, тот через `Utility.Wait(1)` — `StopAllSongs`. Отсюда +1.5 с и +2.2 с.

## 3. Двери, ворота и локации

Скрипт сопоставил каждую загрузочную дверь (`REFR` с `XTEL`) с локацией её ячейки: у интерьера — `XLCN` ячейки, у экстерьера — `XLCN` ячейки сетки, затем `LCEC` локаций, затем `XLCN` мира.

- Переходов через двери 2 204 (в одну сторону). Локация **не меняется** в 1 174: дверь в погреб, пещера и её внешняя площадка в одной локации, ворота города. Тогда нет ни `CLOC`, ни `OnLocationChange`.
- Локация меняется в 922 переходах (ещё в 82 одна из сторон без локации): `int→ext` 415, `ext→int` 415, `int→int` 62, `ext→ext` 30.
- **Ворота городов почти никогда не меняют локацию.** У Вайтрана, Маркарта, Солитьюда (2 из 3), Рифтена (2 из 4) и Виндхельма (1 из 2) снаружи ворот уже та же локация города (`WhiterunLocation` и т. д.). `CLOC` срабатывает раньше, когда игрок пешком входит в ячейки города у стен. Через ворота меняют локацию: двери Рифтена к Мистволю, склад Восточной имперской компании, Чёрный Предел, части Лабиринтиана, Солстхейм.
- Квестов, которые не срабатывают ни на одной двери (только при ходьбе по экстерьеру или после быстрого перемещения), 55 из 186.

Для портала это значит: правило нужно на дверях всех трёх видов, но на воротах обычно срабатывать нечему.

## 4. Правило без FormID

### 4.1 Категории (ручная разметка, столбец «Категория»)

- **окружение** (26): барды, приветствия, сценки, `WI*`. При входе через работающий портал реакция не нужна или мешает.
- **сюжет** (148): квесты с журналом и целями, логика подземелий, служебные мониторы (Соратники, гарнизоны Гражданской войны, «Ясное небо», Древние свитки). Должны сработать.
- **неясно** (12): радиантные нападения и спавны (вампиры, пепельные призраки), праздник короля Олафа, предложение управляющего, сценка Мола.

### 4.2 Правило R8

«Окружение» по данным квеста и его узлов, без FormID:

```
гасить(quest, event) =
    event — CLOC, порождённый входом через портал, который показывал L2   (раздел 5)
  ∧ тип квеста (QUST DNAM) — None или Misc
  ∧ ни одна стадия не пишет в журнал, у квеста нет целей (QOBJ)
  ∧ нет флага RunOnce
  ∧ ни одно условие узлов SM и квеста не проверяет ход квеста:
        GetStage, GetStageDone, GetQuestRunning, GetQuestCompleted,
        GetVMQuestVariable, GetQuestVariable (кроме ссылок на сам квест)
  ∧ нет положительной проверки конкретного места:
        GetEventData(GetIsID|IsInList, L1|L2)==1, GetInCurrentLoc(X)==1,
        LocationHasKeyword(K)==1, GetInCell, GetIsEditorLocation и т. п.
        (отрицания вида «не в Совнгарде» — разрешены)
  ∧ ( есть GetEventData(HasKeyword, L2, K)==1   — «вид места»: таверна, жилище, поселение
    ∨ есть GetRandomPercent                      — «случайная встреча» )
```

Все признаки доступны в игре: `TESQuest::data` (тип, флаги), `objectives`, `executedStages`/`waitingStages` (`logEntry`), `storyManagerConditions`, `BGSStoryManagerNodeBase::conditions` с цепочкой `parent`.

### 4.3 Ошибки R8 на таблице

| Разметка → правило | гасить | оставить |
|---|---|---|
| окружение (26) | 8: `BardSongStarter`, `WITavern`, `WIGreeting`, `WICourier`, `WIChangeLocation01`, `02`, `08`, `09` | 18 |
| сюжет (148) | **1**: `FreeformWinterholdCollegeA` | 147 |
| неясно (12) | 8: `DLC1EclipseAttack1`–`6`, `DLC1RadiantDisguisedVampireLordSpawner`, `BYOHHouseStewardOffer` | 4 |

- **Опасная ошибка одна.** `FreeformWinterholdCollegeA` (поручение Нирьи с записками, `FreeformCollegeANotesScript`) без журнала, целей и `RunOnce`, условие — `L2∋LocTypeHabitation`. Если его погасить, он сработает при следующей смене локации.
- **Пропущено 18 «окружений»:**
  - 14 сценок с конкретным местом и одноразовым флагом (`Dialogue*`, `CW*MapTableScene`, `MS13*`, `DA07MuseumScenes`). Они не перезапускают то, что уже идёт: при входе через портал сценка просто начнётся на входе, как в ванили;
  - `WIChangeLocation03`, `04`, `05` — с целями или типом Main;
  - `WIThief01` — условие на ход `TG00`.
- **8 «неясных» гасятся.** Радиантные спавны после пропуска сработают на следующей смене локации.
- **Ни один квест подземелья, поручение, монитор или квест DLC не гасится.**

Другие варианты я проверил на той же таблице. Сюжет в окружение / окружение в сюжет:

| Вариант | Сюжет → окружение | Окружение → сюжет |
|---|---|---|
| R0 «нет журнала» | 68 | 3 |
| R1 R0 + «нет условий на ход квеста» | 46 | 13 |
| R2 R1 + «нет RunOnce» | 30 | 13 |
| R4 R1 + «нет `LocTypeDungeon`» (ссылка на конкретную форму) | 12 | 14 |
| R7 R8 без «случайных встреч» | 1 | 21 |
| R9 R8 + «есть сцены, ≤ 3 стадий» | 4 | 16 |

Квесты подземелий (`dun*`) не отличить от окружения по журналу: у большинства его нет. Отличают их условия: конкретное место (`в X`) и `здесь∋LocTypeDungeon`.

### 4.4 USSEP 4.3.8a

- USSEP переопределяет 60 из 186 квестов и 19 узлов `CLOC`. 14 узлам он добавляет условия: `RiverwoodKidsSceneNode`, `DLC1VQ00Node`, `MS05KingOlafsFestivalNode`, `C00LocationMonitoringHardcoreNode`, `SkyHavenTempleNode`, `FreeformOldHroldanNode` и восьми узлам подземелий.
- Флаги меняет у трёх квестов: `dunHighGateRuinsQST` и `dunRebelsCairnQST` получают `RunOnce`, у `dunDarklightQST` снят `AllowRepeatedStages`. Тип и цели не меняет ни у одного. Эти три квеста и так «сюжет».
- Из восьми «окружений», которые гасит R8, USSEP трогает только `BardSongStarter` (`DNAM`, `FNAM`, `VMAD`: псевдоним `Lurbuk` стал Optional). Тип, флаги, журнал и условия у него те же.
- **Итог: R8 даёт один и тот же результат с USSEP и без него.**
- `BardSongsScript` USSEP заменяет, но правило работает на уровне квеста-стартера, до скрипта.

## 5. Точки перехвата

| Что | Где | Как перехватить | Переносимость | Достоверность |
|---|---|---|---|---|
| Отметить вход через портал | `PlayerCharacter::SetParentCell` `AE#40466` | vfunc `0x98` в `VTABLE_PlayerCharacter[0]` | NG, SE и AE | высокая |
| Подменить старую локацию в `CLOC` | `AE#40466+0x105` (`call AE#32288`) или `+0x11c` (`call AE#32359`) | `write_call`, правка `oldLoc` в событии | смещения AE → PORTABILITY | высокая по коду, но бесполезно (ниже) |
| Отменить весь `CLOC` | `AE#40466+0x11c` | `write_call` без вызова оригинала | то же | высокая, но ломает 148 сюжетных квестов |
| **Не запускать один квест** | `BGSStoryManagerQuestFinder::VisitQuest` `AE#32430` | vfunc 3 в `VTABLE_BGSStoryManagerQuestFinder` (`256099`/`205026`); вернуть 2, не вызывая оригинал | NG, SE и AE; поле `finder+0x48` (событие) — проверить на SE | средне-высокая |
| Условие узла | `VisitQuestNode` `AE#32429` | vfunc 2 | то же | средняя: узел может запускать несколько квестов, правило про квест удобнее |
| `OnLocationChange` игрока | `AE#40466+0x154` (`call AE#20039`) или `SkyrimVM` слот 1 подтаблицы `+0x20` (`AE#53961`) | `write_call` / vfunc | смещение AE / vtable NG | высокая, но гасить не нужно (раздел 6) |

**Почему не подмена `L1`.**
- Условия `CLOC` смотрят и на `L1`, и на `L2`. `BardSongStarter` проверяет только `L2∋LocTypeInn`, его подменой `L1` не выключить.
- Подставить `L1 = L2` — значит сломать квесты с условием «`L1≠X`, `L2=X`» (`MS11Kicker`, `MGR01Trigger`, `MS10CompanySwapPostQuest`, `T03PostQuest`). Это сюжет.
- Решение должно приниматься по квесту, а это `VisitQuest`.

**Как перехват узнаёт «вход через портал, который уже показывал `L2`».**
1. Хук `SetParentCell` до оригинала:
   - берёт `old = player->parentCell`, `new = a_cell`;
   - ищет в реестре порталов запись с ближней ячейкой `old` и дальней `new`, у которой картинка уже рисовалась (например, ≥ 1 с за последние 5 с);
   - если нашлась, кладёт метку `{oldLocID, newLocID, portalID, время}` в маленький список;
   - метки хранят FormID, а не указатели (`CLAUDE.md`, раздел 7);
   - `GetLocation` старой и новой ячеек считается тем же `TESObjectCELL::GetLocation`.
2. Хук `VisitQuest`:
   - событие `e = *(BGSStoryEvent**)(finder+0x48)`;
   - если `e->index` — индекс `CLOC` (найти один раз по `BGSStoryEventManager::registeredEvents[i].uniqueID == "CLOC"`), а `members[1]`/`members[2]` совпадают с меткой, применяется R8 к квесту;
   - метка снимается через 5 с или при следующем `SetParentCell`.
3. Несколько порталов (5.14, п. 1): метка привязана к паре ячеек и к своему порталу, глобального «текущего портала» нет. Два входа подряд дают две метки. Рекурсивные порталы не важны: `SetParentCell` бывает только при реальном переходе игрока.
4. Любые пары ячеек (5.14, п. 2): правило смотрит на квест и узел, а не на «интерьер/экстерьер». `int→ext`, `ext→ext` и `int→int` обрабатываются одинаково. Выход наружу через портал — тоже `CLOC`, и его гасит то же правило (`WIChangeLocation*`, `WICourier` на выходе).

**Требование к будущему бесшовному входу (T20, T27).** `CLOC` и `OnLocationChange` игрока зависят только от `SetParentCell`. Если бесшовный проход не вызовет `SetParentCell(newCell)` ровно один раз, не сработает ни один из 148 сюжетных квестов. Если вызовет дважды (туда-обратно), реакции сработают дважды.

## 6. `OnLocationChange`: гасить не нужно

- 60 привязанных скриптов. 49 из них ведут ход квестов: `MQ*`, `DLC2MQ*`, `CW*`, `TG*`, `BQPlayerScript`, `JailQuestPlayerScript` и др. Они ставят стадии, останавливают квест, когда игрок ушёл.
- «Окружение» здесь только реагирует на уход:
  - `WITavernPlayerScript` останавливает `WITavern`;
  - `WIPlayerScript` (17 квестов) останавливает встречу;
  - `RemoveSilenceMUS` снимает `MUSTavernSilence` на любой смене. Это важно для T23: тишина в таверне снимается и на выходе через портал.
- Гасить их не нужно: после выхода игрок действительно в другой локации.
- Скрипты на `NPC_` срабатывают, когда NPC переходит через дверь (`AE#19799`): `dragonActorSCRIPT`, `CartDriverScript`, `DLC2NelothScript`. Для переноса NPC через портал (T11, T20) это значит, что событие придёт, если движок перенесёт NPC штатно.

## 7. Маркеры-триггеры вместо `CLOC` (оценка)

Идея: гасить все реакции окружения и запускать их своими динамическими «маркерами», например когда игрок подходит к порталу или видит его.

- **Что даёт.** Время реакции можно отвязать от `SetParentCell`. Это нужно миру без загрузок, где граница «вошёл» размыта: бард начинает песню, когда игрок подходит к двери, а не когда пересекает её. Это же работает для многих порталов сразу и для камер не игрока.
- **Чего стоит.**
  - Маркеры — это либо формы и ссылки (`PlaceAtMe`, `ACTI` с примитивом), а для них нужен `.esp`, что запрещено (5.14, п. 7), и они копятся в сохранении. Либо «виртуальные» зоны в C++, которые сами ставят в очередь событие, как `CLOC`, через `AddEvent_Impl`.
  - Во втором случае условия `в X` и `здесь∋K` смотрят на `currentLocation` игрока, который ещё снаружи. Заранее сработают только квесты с `L2∋K` или без условий на место, то есть ровно множество R8.
  - Понадобятся: защита от повторного срабатывания на входе (тот же хук `VisitQuest`), сохранение состояния зон, совместимость с модами, которые ждут `CLOC` в момент входа.
- **Чем рискует.** Двойной запуск, расхождение с ванилью в квестах с `RandomPercent` и таймерами сброса (`RNAM`), поддержка своей «модели локаций». Для `BardSongStarter` раннее срабатывание тоже плохо: бард уже играет, видно через портал.
- **Вывод.** Сейчас не нужно. Хук R8 закрывает исходную проблему меньшей ценой. К маркерам стоит вернуться, когда бесшовный вход (T20, T27) перестанет вызывать `SetParentCell` на границе двери. Тогда виртуальные зоны в C++ заменят эту границу, а R8 решит, что можно запускать заранее.

## 8. Условия `CTDA` с локацией (по типам записей)

| Функция | Всего | По типам записей |
|---|---|---|
| `GetInCurrentLoc` | 1590 | INFO: 902, LSCR: 191, QUST: 181, SMQN: 171, PACK: 76, SMBN: 54 |
| `GetInCell` | 1286 | INFO: 777, QUST: 293, PACK: 99, SCEN: 76, SMQN: 37, SMBN: 2 |
| `GetKeywordDataForLocation` | 783 | INFO: 595, SMQN: 154, PACK: 30, QUST: 4 |
| `LocationHasKeyword` | 752 | QUST: 380, INFO: 360, PACK: 5, SMQN: 4, SMBN: 3 |
| `GetIsEditorLocAlias` | 443 | QUST: 441, PACK: 2 |
| `GetInCurrentLocAlias` | 411 | QUST: 307, INFO: 85, PACK: 18, SCEN: 1 |
| `LocAliasIsLocation` | 232 | INFO: 121, QUST: 101, SCEN: 8, PACK: 2 |
| `GetEventData` | 173 | SMQN: 122, QUST: 37, SMBN: 14, SCEN: 0 |
| `GetIsEditorLocation` | 165 | QUST: 84, SMQN: 37, SMBN: 35, PACK: 9 |
| `HasSameEditorLocAsRefAlias` | 147 | QUST: 147 |
| `LocationHasRefType` | 118 | QUST: 118 |
| `GetKeywordDataForCurrentLocation` | 61 | QUST: 49, INFO: 7, SMQN: 5 |
| `LocAliasHasKeyword` | 41 | INFO: 26, QUST: 15 |
| `GetInCurrentLocFormList` | 17 | QUST: 12, SPEL: 5 |
| `IsInSameCurrentLocAsRefAlias` | 7 | QUST: 5, PACK: 2 |
| `IsLocationLoaded` | 2 | INFO: 1, PACK: 1 |
| `GetLocationCleared` | 1 | INFO: 1 |
| `HasSameEditorLocAsRef` | 1 | QUST: 1 |

Для `GetEventData` учтены только проверки `L1`/`L2`. `QUST` включает условия псевдонимов и диалогов квеста.

`INFO` и `PACK` вычисляют их на месте по текущей локации игрока или NPC. Пока игрок снаружи и смотрит в портал, жители таверны «не видят» его в таверне. На входе всё переключается вместе с `currentLocation`. Для портала это согласованно и отдельной правки не требует. `LSCR` (191) — выбор экрана загрузки.

## 9. Таблица: квесты, запускаемые `CLOC`

Столбцы:
- «Условие» — узлы SM от события до квеста и условия Story Manager самого квеста. Если узлов несколько, условия объединены.
- «Признаки» — тип `DNAM`, записи журнала, цели `QOBJ`, `RunOnce`, сцены (`SCEN` с этим квестом), «ход квеста» (условия на чужие стадии), «ENAM пуст».
- «R8» — что сделает правило при входе через портал.
- «Двери» — сколько загрузочных дверей со сменой локации проходят условия про место (`L1`, `L2`, `в X`, `здесь∋K`; прочие условия считаются выполненными).

| # | Квест (FormID, файл) | Условие запуска (узлы SM + условия квеста) | Что делает | Признаки | Категория | R8 | Двери со сменой локации |
|---|---|---|---|---|---|---|---|
| 1 | `BardSongStarter` `000BE159` Skyrim | L2∋LocTypeInn | бард: StopAllSongs и новая песня (PlaySongRequest) | None | окружение | гасить | ext→int 25 |
| 2 | `CW00SolitudeMapTableScene` `000D5165` Skyrim | GetQuestRunning(CWFinale)==0; GetVMQuestVariable(CW,3377E0)==0; GetStageDone(CW00A,20)==0; GetStageDone(CW01A,1)==0; L2=SolitudeCastleDourLocation | сценка генералов у карты | None, сцены 1, ход квеста | окружение | — | ext→int 2 int→int 1 |
| 3 | `CW00WindhelmMapTableScene` `000E1B36` Skyrim | GetQuestRunning(CWFinale)==0; GetVMQuestVariable(CW,2EE0F0)==0; GetStageDone(CW00B,20)==0; GetStageDone(CW01B,1)==0; L2=WindhelmPalaceOfTheKingsLocation | сценка генералов у карты | None, сцены 1, ход квеста | окружение | — | ext→int 2 int→int 1 |
| 4 | `CW01SolitudeMapTableScene` `000D5F0A` Skyrim | GetQuestRunning(CWFinale)==0; GetStageDone(CW01A,100)==1; GetStageDone(CW02A,30)==0; L2=SolitudeCastleDourLocation; GetStageDone(CW02A,40)==0; GetStageDone(CW02B,40)==0 | сценка генералов у карты | None, сцены 1, ход квеста | окружение | — | ext→int 2 int→int 1 |
| 5 | `CW01WindhelmMapTableScene` `000E2D3A` Skyrim | GetQuestRunning(CWFinale)==0; GetStageDone(CW01B,100)==1; GetStageDone(CW02B,30)==0; L2=WindhelmPalaceOfTheKingsLocation; GetStageDone(CW02A,40)==0; GetStageDone(CW02B,40… | сценка генералов у карты | None, сцены 1, ход квеста | окружение | — | ext→int 2 int→int 1 |
| 6 | `DA07MuseumScenes` `00096FD7` Skyrim | GetQuestCompleted(DA07MuseumScenes)==0; GetQuestRunning(DA07MuseumScenes)==0; L2=DawnstarSilussHouseLocation | сценки в доме Силуса | None, сцены 6 | окружение | — | ext→int 1 |
| 7 | `DialogueMarkarthPostAttackScene01` `00055A76` Skyrim | GetStageDone(MS01IntroWorldScene,500)==1; L1=MarkarthLocation; GetStageDone(DialogueMarkarthPostAttackScene01,100)==0 | сценка после нападения в Маркарте | None, RunOnce, сцены 1, ход квеста | окружение | — | ext→int 23 |
| 8 | `DialogueRiftenSS04` `000B668C` Skyrim | L2=RiftenMistveilKeepLocation; GetGlobalValue(DialogueRiftenScene04)==0 | сценка в Мистволь | Misc, сцены 1 | окружение | — | ext→ext 1 ext→int 4 int→ext 3 |
| 9 | `DialogueRiftenSS05` `0005CE25` Skyrim | L2=RiftenBeeandBarbLocation; GetGlobalValue(DialogueRiftenScene05)==0 | сценка в «Пчеле и жале» | Misc, сцены 1 | окружение | — | ext→int 2 |
| 10 | `DialogueRiverwoodIntroScene` `000954D9` Skyrim | GetStage(MQ102)<50; GetStageDone(DialogueRiverwoodIntroScene,10)==0; GetCurrentTime()>=8; GetCurrentTime()<=20; L2=RiverwoodLocation | сценка при первом приходе в Ривервуд | None, RunOnce, сцены 1, ход квеста | окружение | — | int→ext 7 |
| 11 | `DialogueRiverwoodKidsScene` `000340A4` Skyrim | (2 узла) L2=RiverwoodLocation; GetCurrentTime()>=5; GetCurrentTime()<=19; GetStage(MQ102A)>=40 ИЛИ; GetStage(MQ102B)>=40 ИЛИ | сценка детей Ривервуда | None, RunOnce, сцены 1, ход квеста | окружение | — | int→ext 7 |
| 12 | `DialogueWhiterunArgueScene` `00037CC2` Skyrim | GetGlobalValue(56)>=8; GetGlobalValue(56)<=20; GetVMQuestVariable(MS09,D0ED30)==0; GetStage(MS09)<=40; L2=WhiterunArcadiasCauldronLocation ИЛИ; L2=WhiterunBelethorsGenera… | спор о войне в лавках Вайтрана | None, RunOnce, сцены 1, ход квеста | окружение | — | ext→int 6 |
| 13 | `DialogueWhiterunHeimskrPreachScene` `0003A15A` Skyrim | L2=WhiterunLocation; в WhiterunLocation; GetGlobalValue(56)>5; GetGlobalValue(56)<20 | проповедь Хеймскра | None, сцены 1 | окружение | — | int→ext 38 |
| 14 | `MS13BleakFallsBarrowCampfireScene` `0003A17C` Skyrim | L2=BleakFallsBarrowLocation; здесь∋LocTypeDungeon | сценка бандитов у костра | None, сцены 1 | окружение | — | int→ext 1 |
| 15 | `MS13LucanCamillaScene` `00038386` Skyrim | L2=RiverwoodLocation; GetStageDone(MS13,5)==0 | сценка Лукан/Камилла | None, RunOnce, сцены 1, ход квеста, ENAM пуст | окружение | — | int→ext 7 |
| 16 | `WIChangeLocation01` `000350F4` Skyrim | не в SovngardeLocation; шанс WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; игрок.GetInCell(CidhnaMine01)==0; игрок.Ge… | реплика NPC о виде игрока | None, сцены 1 | окружение | гасить | ext→ext 29 ext→int 411 int→ext 411 int→int 62 |
| 17 | `WIChangeLocation02` `0006C9C6` Skyrim | не в SovngardeLocation; шанс WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; игрок.IsWeaponMagicOut()==1; игрок.GetEqui… | реплика стражи об оружии в руках | None, сцены 1 | окружение | гасить | ext→ext 29 ext→int 411 int→ext 411 int→int 62 |
| 18 | `WIChangeLocation03` `0006E028` Skyrim | не в SovngardeLocation; шанс WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; не в WinterholdCollegeLocation; игрок.GetK… | встреча: вызов на бой (WI) | Misc, цели 1 | окружение | — | ext→ext 29 ext→int 402 int→ext 400 int→int 55 |
| 19 | `WIChangeLocation04` `0006F211` Dragonborn | не в SovngardeLocation; шанс WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; не в DLC2SolstheimLocation; не в Winterhol… | встреча: ученик просит научить (WI) | Misc, цели 1, сцены 1 | окружение | — | ext→ext 29 ext→int 369 int→ext 367 int→int 49 |
| 20 | `WIChangeLocation05` `00070E30` Skyrim | не в SovngardeLocation; шанс WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; игрок.GetLevel()>=5; L1∌LocTypeDwelling | встреча: курьер/подарок (WI) | Main | окружение | — | ext→ext 26 ext→int 407 int→ext 65 int→int 24 |
| 21 | `WIChangeLocation08` `0004BCF1` HearthFires | не в SovngardeLocation; шанс WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; игрок.GetKeywordDataForCurrentLocation(WIC… | встреча на улице днём, шлёт SendStoryEvent (WI) | None | окружение | гасить | ext→ext 29 ext→int 411 int→ext 411 int→int 62 |
| 22 | `WIChangeLocation09` `00050AF1` Skyrim | не в SovngardeLocation; шанс WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; игрок.GetLevel()>=10; игрок.GetKeywordData… | атака дракона на поселение (WI) | None, сцены 1 | окружение | гасить | ext→ext 3 int→ext 362 |
| 23 | `WICourier` `00039F82` Skyrim | GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WICourierItemCount)>=1; L2∋LocTypeHabitation; L2∌LocTypeOrcStronghold; игрок.GetKeywordDataForCurrentLocation(WIComplex… | курьер с письмом; без события сработает при следующей смене локации | None | окружение | гасить | ext→ext 3 int→ext 342 |
| 24 | `WIGreeting` `000C7919` Skyrim | игрок.IsTrespassing()==0; L2∋LocTypeDwelling | хозяин жилища приветствует игрока | None, сцены 1 | окружение | гасить | ext→ext 3 ext→int 346 int→ext 4 int→int 38 |
| 25 | `WITavern` `000DEE92` Skyrim | L2∋LocTypeInn | хозяин таверны подходит/предлагает сесть, сцены | Misc, сцены 2 | окружение | гасить | ext→int 25 |
| 26 | `WIThief01` `00023EEB` Skyrim | игрок.GetLevel()>=5; GetStageDone(TG00,200)==1; шанс WIThiefChance; GetGlobalValue(56)>22 ИЛИ; GetGlobalValue(56)<4 ИЛИ | ночной вор (WI) | None, ход квеста | окружение | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 27 | `BYOHHouseStewardOffer` `0300308B` HearthFires | L2∋BYOHHouseLocationKeyword; GetGlobalValue(PlayerFollowerCount)>0 | спутник предлагает стать управляющим | None, сцены 1 | неясно | гасить | ext→int 39 int→ext 39 |
| 28 | `DialogueRiftenMaulHandler` `000EC199` Skyrim | GetQuestRunning(DialogueRiftenMaul)==1; GetStageDone(DialogueRiftenMaul,30)==1; GetStageDone(DialogueRiftenMaul,35)==0; GetStageDone(DialogueRiftenMaul,40)==0 | ход вступительной сценки Мола в Рифтене | None, RunOnce, ход квеста | неясно | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 29 | `DLC1EclipseAttack1` `0201629F` Dawnguard | GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; шанс DLC1EclipseAttackNextChanceEclipse ИЛИ; GetGlobalValue(DLC1Eclips… | нападение вампиров на поселение (радиант) | None | неясно | гасить | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 30 | `DLC1EclipseAttack2` `02017648` Dawnguard | (3 узла) GetGlobalValue(TRUEGlobal)==0; GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; шанс DLC1EclipseAttackNextChan… | нападение вампиров на поселение (радиант) | None | неясно | гасить | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 31 | `DLC1EclipseAttack3` `02017649` Dawnguard | GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; шанс DLC1EclipseAttackNextChanceEclipse ИЛИ; GetGlobalValue(DLC1Eclips… | нападение вампиров на поселение (радиант) | None | неясно | гасить | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 32 | `DLC1EclipseAttack4` `0201764A` Dawnguard | GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; шанс DLC1EclipseAttackNextChanceEclipse ИЛИ; GetGlobalValue(DLC1Eclips… | нападение вампиров на поселение (радиант) | None | неясно | гасить | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 33 | `DLC1EclipseAttack5` `0201764B` Dawnguard | (3 узла) GetGlobalValue(TRUEGlobal)==0; GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; шанс DLC1EclipseAttackNextChan… | нападение вампиров на поселение (радиант) | None | неясно | гасить | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 34 | `DLC1EclipseAttack6` `0201764C` Dawnguard | GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; шанс DLC1EclipseAttackNextChanceEclipse ИЛИ; GetGlobalValue(DLC1Eclips… | нападение вампиров на поселение (радиант) | None | неясно | гасить | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 35 | `DLC1RadiantDisguisedVampireLordSpawner` `0200420E` Dawnguard | шанс DLC1RadiantDisguisedVampireLordChance; игрок.GetInFaction(DLC1HunterFaction)==1; GetGlobalValue(DLC1HarkonDead)==0; GetGlobalValue(56)>=22 ИЛИ; GetGlobalValue(56)<4 … | переодетый вампир ночью (радиант) | None | неясно | гасить | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 36 | `DLC1ScoutPatrol` `020041CF` Dawnguard | GetStageDone(DLC1VQ02,20)==1; GetStageDone(DLC1VQ08,200)==0; шанс DLC1ScoutPatrolChance; в DLC1HunterHQLocation ИЛИ; в DLC1VampireCastleLocation ИЛИ | патруль-разведчик (радиант) | None, ход квеста | неясно | — | ext→int 13 int→ext 12 int→int 1 |
| 37 | `DLC2AshSpawnAttack` `04038423` Dragonborn | L2=DLC2RavenRockLocation; L1∌LocTypeDwelling; GetStage(DLC2RR01)>=40; GetStage(DLC2RR01)<90; шанс DLC2AshSpawnAttackChance | нападение пепельных призраков (радиант) | None, ход квеста | неясно | — | int→ext 2 |
| 38 | `KingOlafsFestivalStarter` `0001703C` Skyrim | GetDayOfWeek()==6; L2=SolitudeLocation; GetQuestCompleted(MS05)==1 | запуск праздника в Солитьюде | None, ход квеста | неясно | — | ext→ext 2 int→ext 35 |
| 39 | `BQ01` `00095125` Update | (9 узлов) L2∋LocTypeHabitation; в ReachHoldLocation; GetKeywordDataForLocation(ReachHoldLocation,BQActiveQuest)==0; в RiftHoldLocation; GetKeywordDataForLocation(RiftHold… | заказ на награду (цели) | Misc, цели 3 | сюжет | — | ext→ext 3 int→ext 333 |
| 40 | `BQ02` `000BD77F` Update | (9 узлов) L2∋LocTypeHabitation; в ReachHoldLocation; GetKeywordDataForLocation(ReachHoldLocation,BQActiveQuest)==0; в RiftHoldLocation; GetKeywordDataForLocation(RiftHold… | заказ на награду (цели) | Misc, цели 3 | сюжет | — | ext→ext 3 int→ext 333 |
| 41 | `BQ03` `000BD78C` Update | (9 узлов) L2∋LocTypeHabitation; в ReachHoldLocation; GetKeywordDataForLocation(ReachHoldLocation,BQActiveQuest)==0; в RiftHoldLocation; GetKeywordDataForLocation(RiftHold… | заказ на награду (цели) | Misc, цели 3 | сюжет | — | ext→ext 3 int→ext 333 |
| 42 | `BQ04` `000CBA9A` Update | (9 узлов) L2∋LocTypeHabitation; в ReachHoldLocation; GetKeywordDataForLocation(ReachHoldLocation,BQActiveQuest)==0; в RiftHoldLocation; GetKeywordDataForLocation(RiftHold… | заказ на награду (цели) | Misc, цели 3 | сюжет | — | ext→ext 3 int→ext 333 |
| 43 | `BYOHRelationshipAdoptableOrphanageCL` `0300E29A` HearthFires | в RiftenHonorhallOrphanageLocation | Hearthfire: усыновление/дом | None, RunOnce | сюжет | — | ext→int 2 |
| 44 | `C00LocationMonitoring1` `000A3EC5` Skyrim | L2≠WhiterunLocation; L1∌LocTypeDwelling; GetVMQuestVariable(C00,5BCA60)==0; GetStage(C00)>=40 | Соратники: служебные мониторы радиантов | Companions, ход квеста | сюжет | — | ext→ext 27 ext→int 411 int→ext 66 int→int 24 |
| 45 | `C00LocationMonitoring2` `000A3EE4` Skyrim | L2≠WhiterunJorrvaskrLocation; GetStage(C00)==100 | Соратники: служебные мониторы радиантов | Companions, ход квеста | сюжет | — | ext→ext 30 ext→int 411 int→ext 415 int→int 61 |
| 46 | `C00LocationMonitoring3` `000A3EE5` Skyrim | (2 узла) игрок.GetInFaction(CompanionsFaction)==1; GetQuestCompleted(C03)==1; GetQuestRunning(C04)==0; L2=WhiterunLocation; L1≠WhiterunLocation; L1∌LocTypeDwelling; GetVM… | Соратники: служебные мониторы радиантов | Companions, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 47 | `ClearSkiesChangeLocationQuest` `0001E3C8` Skyrim | GetQuestRunning(ClearSkiesQuest)==1 | механика: снять эффект крика «Ясное небо» | None, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 48 | `CR08EditorMonitoring` `0005478C` Skyrim | GetQuestRunning(CR08)==1; GetStage(CR08)==21 | Соратники: служебные мониторы радиантов | Companions, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 49 | `CRHoldExpansion` `000F9075` Skyrim | — | Соратники: служебные мониторы радиантов | None | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 50 | `CRSilverHandNameChangerQuest` `0009D6F6` Skyrim | GetVMQuestVariable(C00,106E90)==1; игрок.GetKeywordDataForCurrentLocation(SilverHandTarget)==1 | Соратники: служебные мониторы радиантов | None, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 51 | `CWFortObserver` `000D5F08` Skyrim | L1∋CWFort; L1≠WhiterunWatchtowerLocation | Гражданская война: учёт гарнизонов | CivilWar | сюжет | — | 0 |
| 52 | `CWGarrisonObserver1` `0002994C` Skyrim | GetGlobalValue(TRUEGlobal)==0; игрок.GetKeywordDataForCurrentLocation(CWGarrison)>=0; здесь∋CWGarrison ИЛИ; здесь∋CWGarrisonDefenderOnly ИЛИ; здесь∋CWGarrisonNonContestab… | Гражданская война: учёт гарнизонов | None | сюжет | — | ext→int 1 int→ext 60 |
| 53 | `CWGarrisonObserver2` `0002E8F0` Skyrim | GetGlobalValue(TRUEGlobal)==0; игрок.GetKeywordDataForCurrentLocation(CWGarrison)>=0; здесь∋CWGarrison ИЛИ; здесь∋CWGarrisonDefenderOnly ИЛИ; здесь∋CWGarrisonNonContestab… | Гражданская война: учёт гарнизонов | None | сюжет | — | ext→int 1 int→ext 60 |
| 54 | `CWGarrisonObserver3` `0002E8F1` Skyrim | GetGlobalValue(TRUEGlobal)==0; игрок.GetKeywordDataForCurrentLocation(CWGarrison)>=0; здесь∋CWGarrison ИЛИ; здесь∋CWGarrisonDefenderOnly ИЛИ; здесь∋CWGarrisonNonContestab… | Гражданская война: учёт гарнизонов | None | сюжет | — | ext→int 1 int→ext 60 |
| 55 | `DA02ChangeLoc` `000B18E1` Skyrim | GetQuestRunning(DA02)==1; GetStage(DA02)>=50; L1≠SolitudeLocation; L2=SolitudeLocation ИЛИ; L2=SolitudeBluePalaceLocation ИЛИ | даэдрический квест: завязка или ход | None, ход квеста | сюжет | — | ext→ext 2 int→ext 35 int→int 1 |
| 56 | `DA04SeptimusSendForPlayerQuest` `0010481C` Skyrim | GetStage(DA04)==35; игрок.GetLevel()>=DA04MinLevel | даэдрический квест: завязка или ход | None, RunOnce, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 57 | `DA05LocationCheck` `0005D330` Skyrim | L2=FalkreathLocation; GetStage(DA05)<10 | даэдрический квест: завязка или ход | Daedric, ход квеста | сюжет | — | int→ext 12 |
| 58 | `DA07intro` `00094D89` Skyrim | GetQuestRunning(DA07intro)==0; GetQuestCompleted(DA07intro)==0; игрок.GetLevel()>=20 | даэдрический квест: завязка или ход | Misc, цели 1, сцены 1 | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 59 | `DA09ChangeLocation` `00081C59` Skyrim | GetQuestRunning(DA09)==1; шанс DA09ChangeLocChance | даэдрический квест: завязка или ход | None, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 60 | `DA10` `00022F08` Skyrim | GetStageDone(MS01IntroWorldScene,300)==1; L1=MarkarthLocation; GetQuestRunning(DA10)==0; GetQuestCompleted(DA10)==0 | даэдрический квест: завязка или ход | Daedric, журнал 5, цели 8, сцены 7, ход квеста | сюжет | — | ext→int 23 |
| 61 | `DA11HallScene` `0007D950` Skyrim | GetStage(DA11Intro)==10; L2=MarkarthHalloftheDeadLocation | сцена-завязка DA11, двигает стадию | None, сцены 1, ход квеста | сюжет | — | ext→int 1 int→int 1 |
| 62 | `DA11Intro` `0007D949` Skyrim | L2=MarkarthLocation; GetQuestCompleted(DA11Intro)==0 | даэдрический квест: завязка или ход | Misc, цели 3 | сюжет | — | int→ext 23 |
| 63 | `DA11IntroScene` `000AEC22` Skyrim | L2=MarkarthLocation; GetQuestCompleted(DA11IntroScene)==0 | даэдрический квест: завязка или ход | Misc, сцены 1, ENAM пуст | сюжет | — | int→ext 23 |
| 64 | `DA14Start` `0003FA17` Skyrim | игрок.GetLevel()>=14; GetStage(DA14)<5; L2∋LocTypeHabitation | даэдрический квест: завязка или ход | Misc, цели 1, сцены 2, ход квеста | сюжет | — | ext→ext 3 int→ext 362 |
| 65 | `DA16WalkSceneMonitor` `000E16C6` Skyrim | GetStage(DA16)==25; GetGlobalValue(DA16Walk)==1; GetGlobalValue(DA16OnTheWay)==0 | даэдрический квест: завязка или ход | None, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 66 | `DB11_dunKatariahQST` `000C6020` Skyrim | в KatariahLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, сцены 2 | сюжет | — | 0 |
| 67 | `DLC1dunRedwaterDenQST` `02007C4F` Dawnguard | в DLC1RedwaterDenLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, RunOnce, сцены 6 | сюжет | — | 0 |
| 68 | `DLC1HunterBaseEnableSet1` `020198B2` Dawnguard | GetStage(DLC1HunterBaseIntro)==200; GetStage(DLC1HunterBaseEnableSet1)<200; L2≠DLC1HunterHQLocation; L1=DLC1HunterHQLocation | ход квеста DLC | None, ход квеста | сюжет | — | ext→int 4 |
| 69 | `DLC1HunterBaseEnableSet2` `0201991F` Dawnguard | GetStage(DLC1HunterBaseStage2)==200; GetStage(DLC1HunterBaseEnableSet2)<200; L2≠DLC1HunterHQLocation; L1=DLC1HunterHQLocation | ход квеста DLC | None, ход квеста | сюжет | — | ext→int 4 |
| 70 | `DLC1HunterBaseFlorentiusChange` `02011F0D` Dawnguard | GetStage(DLC1HunterBaseFlorentiusChange)<10; DLC1FlorentiusRef.в DLC1HunterHQLocation; GetStage(DLC1HunterBaseStage2)==200 | ход квеста DLC | None, RunOnce, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 71 | `DLC1LD_Arkngthamz` `02004D6B` Dawnguard | в DLC1ArkngthamzLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, RunOnce, сцены 30 | сюжет | — | 0 |
| 72 | `DLC1LD_Bthalft` `0200CFFF` Dawnguard | в BthalftLocation; GetStageDone(DLC1LD,20)==1; GetStageDone(DLC1LD,30)==1; GetStageDone(DLC1LD,40)==1; GetStageDone(DLC1LD,50)==1; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, RunOnce, сцены 16, ход квеста | сюжет | — | ext→int 1 int→ext 1 |
| 73 | `DLC1LD_ChangeLocation` `02004D6C` Dawnguard | в DLC1ArkngthamzLocation ИЛИ; в RaldbtharLocation ИЛИ; в DeepFolkCrossingLocation ИЛИ; в MzulftLocation ИЛИ; в BthalftLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None | сюжет | — | ext→ext 1 ext→int 2 int→ext 1 |
| 74 | `DLC1ScrollHandlingChangeLoc` `02012F92` Dawnguard | GetVMQuestVariable(DialogueWinterholdCollege,218FBC8)==0 | механика: Древние свитки в Арканеуме | None, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 75 | `DLC1VQ00ChangeLocation` `0200D90B` Dawnguard | игрок.GetLevel()>=DLC1VQMinLevel; GetStage(DLC1VQ01MiscObjective)<10; L2∋LocTypeHabitation; L2∌LocTypeOrcStronghold; L1∌LocTypeDwelling; L2≠WinterholdCollegeLocation; не … | ход квеста DLC | Misc, ход квеста | сюжет | — | ext→ext 1 int→ext 23 |
| 76 | `DLC1VQ08HunterMove` `0201A8BB` Dawnguard | GetStage(DLC1VQ08)==40; L2≠DLC1HunterHQLocation; L1=DLC1HunterHQLocation | ход квеста DLC | None, RunOnce, ход квеста | сюжет | — | ext→int 4 |
| 77 | `DLC2CultistAmbush` `0403546D` Dragonborn | не в DLC2SolstheimLocation; GetStage(MQ105)>=160; GetStage(DLC2MQ01)<5; GetStageDone(DLC2WE09,1)==0 | ход квеста DLC | Misc, ход квеста | сюжет | — | ext→ext 30 ext→int 382 int→ext 382 int→int 56 |
| 78 | `DLC2dunFrostmoonQST` `0401F9E6` Dragonborn | в DLC2FrostmoonCragLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, RunOnce, сцены 3 | сюжет | — | 0 |
| 79 | `DLC2dunKarstaagQST` `040247F8` Dragonborn | в DLC2CastleKarstaagRuinsLocation ИЛИ; в DLC2GlacialCaveLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, RunOnce, сцены 3 | сюжет | — | 0 |
| 80 | `DLC2dunKolbjornQST` `0401810B` Dragonborn | в DLC2KolbjornBarrowLocation ИЛИ; в DLC2RavenRockLocation ИЛИ | логика подземелья/места: боссы, засады, сцены, стадии | Side, журнал 28, цели 24, RunOnce, сцены 1 | сюжет | — | ext→int 12 int→ext 13 int→int 3 |
| 81 | `DLC2dunNchardakTracking` `040173BE` Dragonborn | в DLC2NchardakLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, RunOnce, ENAM пуст | сюжет | — | 0 |
| 82 | `DLC2EbonyWarriorQuest` `0402C49E` Dragonborn | игрок.GetLevel()>=80; L1≠DLC2SolstheimLocation; L2=WhiterunLocation ИЛИ; L2=RiftenLocation ИЛИ; L2=WindhelmLocation ИЛИ; L2=MarkarthLocation ИЛИ; L2=SolitudeLocation ИЛИ;… | ход квеста DLC | Side, журнал 3, цели 2, RunOnce | сюжет | — | ext→ext 30 ext→int 411 int→ext 415 int→int 62 |
| 83 | `DLC2RR03MineCL` `0403332C` Dragonborn | GetStageDone(DLC2RR03,200)==1; L1=DLC2RavenRockLocation | ход квеста DLC | None, RunOnce, ход квеста | сюжет | — | ext→int 13 |
| 84 | `DLC2ThirskRieklingSpawner` `040208CC` Dragonborn | GetGlobalValue(DLC2ThirskRieklingOwned)==1; L2=DLC2ThirskMeadHallLocation; L1≠DLC2ThirskMeadHallInteriorLocation; GetStage(DLC2MH01)!=20 | ход квеста DLC | None, ход квеста | сюжет | — | 0 |
| 85 | `DLC2TTR1` `0401B65F` Dragonborn | L2=DLC2TelMithrynApothecaryLocation ИЛИ; L2=DLC2TelMithrynKitchenLocation ИЛИ; L2=DLC2TelMithrynStewardLocation ИЛИ; L2=DLC2TelMithrynTowerLocation ИЛИ; GetQuestCompleted… | ход квеста DLC | Side, журнал 2, цели 2, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 86 | `DLC2TTR2` `0401C06E` Dragonborn | L2=DLC2TelMithrynApothecaryLocation ИЛИ; L2=DLC2TelMithrynKitchenLocation ИЛИ; L2=DLC2TelMithrynStewardLocation ИЛИ; L2=DLC2TelMithrynTowerLocation ИЛИ; GetStage(DLC2TT1)… | ход квеста DLC | Side, журнал 1, цели 2, RunOnce, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 87 | `DLC2TTR3a` `0401B65D` Dragonborn | L2=DLC2TelMithrynApothecaryLocation ИЛИ; L2=DLC2TelMithrynKitchenLocation ИЛИ; L2=DLC2TelMithrynStewardLocation ИЛИ; L2=DLC2TelMithrynTowerLocation ИЛИ; GetStage(DLC2TT1)… | ход квеста DLC | Side, журнал 3, цели 2, RunOnce, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 88 | `DLC2TTR3b` `0401D8D1` Dragonborn | L2=DLC2TelMithrynApothecaryLocation ИЛИ; L2=DLC2TelMithrynKitchenLocation ИЛИ; L2=DLC2TelMithrynStewardLocation ИЛИ; L2=DLC2TelMithrynTowerLocation ИЛИ; GetStage(DLC2TT1)… | ход квеста DLC | Side, журнал 3, цели 2, RunOnce, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 89 | `DLC2TTR4a` `0401B664` Dragonborn | L2=DLC2TelMithrynApothecaryLocation ИЛИ; L2=DLC2TelMithrynKitchenLocation ИЛИ; L2=DLC2TelMithrynStewardLocation ИЛИ; L2=DLC2TelMithrynTowerLocation ИЛИ; GetStage(DLC2TT1)… | ход квеста DLC | Side, журнал 3, цели 2, RunOnce, сцены 1, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 90 | `DLC2TTR4b` `0401CDE1` Dragonborn | L2=DLC2TelMithrynApothecaryLocation ИЛИ; L2=DLC2TelMithrynKitchenLocation ИЛИ; L2=DLC2TelMithrynStewardLocation ИЛИ; L2=DLC2TelMithrynTowerLocation ИЛИ; GetStage(DLC2TT1)… | ход квеста DLC | Misc, цели 1, RunOnce, сцены 1, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 91 | `DLC2TTR7` `0401BD18` Dragonborn | L2=DLC2TelMithrynApothecaryLocation ИЛИ; L2=DLC2TelMithrynKitchenLocation ИЛИ; L2=DLC2TelMithrynStewardLocation ИЛИ; L2=DLC2TelMithrynTowerLocation ИЛИ; GetStage(DLC2TT1)… | ход квеста DLC | Side, журнал 3, цели 2, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 92 | `DLC2TTR8` `0401F13B` Dragonborn | L2=DLC2TelMithrynApothecaryLocation ИЛИ; L2=DLC2TelMithrynKitchenLocation ИЛИ; L2=DLC2TelMithrynStewardLocation ИЛИ; L2=DLC2TelMithrynTowerLocation ИЛИ; GetQuestCompleted… | ход квеста DLC | Misc, цели 6, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 93 | `DLC2WaterStoneSailAwayQuest` `04027826` Dragonborn | DLC2PillarBuildSandboxMarkerTOGGLEWater.GetDisabled()==1; L2≠DLC2StoneWaterLocation; L1≠DLC2StoneWaterLocation | ход квеста DLC | None, RunOnce | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 94 | `dunAlftandQST` `0003B0FB` Skyrim | в AlftandLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, RunOnce, сцены 5 | сюжет | — | ext→ext 1 |
| 95 | `dunAnsilvundQST` `0003D721` Skyrim | в AnsilvundLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, цели 1, RunOnce, сцены 11 | сюжет | — | 0 |
| 96 | `dunBluePalaceWingQST` `00048364` Skyrim | в BluePalaceWingLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, ENAM пуст | сюжет | — | 0 |
| 97 | `dunBrokenOarQST` `0001E437` Skyrim | в BrokenOarGrottoLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, сцены 2 | сюжет | — | 0 |
| 98 | `dunDarklightQST` `0003FA89` Skyrim | (2 узла) в DarklightTowerLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Side, журнал 3, цели 2, RunOnce, сцены 6, ENAM пуст | сюжет | — | 0 |
| 99 | `dunFaldarsToothQST` `0004E667` Skyrim | в FaldarsToothLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, RunOnce, сцены 5 | сюжет | — | 0 |
| 100 | `dunFolgunthurQST` `0004CEDE` Skyrim | в FolgunthurLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, RunOnce | сюжет | — | 0 |
| 101 | `dunForelhostQST` `00035354` Skyrim | в ForelhostLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, журнал 6, цели 6, RunOnce, сцены 5 | сюжет | — | 0 |
| 102 | `dunFrostflowAbyssQST` `00048121` Skyrim | в FrostflowLighthouseLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, цели 1, RunOnce | сюжет | — | 0 |
| 103 | `dunFrostmereCryptQST` `000CF915` Skyrim | в FrostmereCryptLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Side, журнал 6, цели 4, сцены 9 | сюжет | — | 0 |
| 104 | `dunGauldursonChangeLocationQST` `000ECEB7` Skyrim | в ReachwaterRockLocation ИЛИ; в FolgunthurLocation ИЛИ; в SaarthalLocation ИЛИ; в GeirmundsHallLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None | сюжет | — | 0 |
| 105 | `dunGeirmundQST` `000E738F` Update | в GeirmundsHallLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, RunOnce | сюжет | — | 0 |
| 106 | `dunHalldirsCairnQST` `0003AD4A` Skyrim | в HalldirsCairnLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None | сюжет | — | 0 |
| 107 | `dunHaltedStreamCampQST` `000A409D` Skyrim | в HaltedStreamCampLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, RunOnce, сцены 1 | сюжет | — | 0 |
| 108 | `dunHighGateRuinsQST` `000443F4` Skyrim | в HighGateRuinsLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, журнал 5, цели 3, сцены 8 | сюжет | — | 0 |
| 109 | `dunHillgrundsTombQST` `0003E897` Skyrim | здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, журнал 1, цели 4, RunOnce, сцены 11 | сюжет | — | ext→ext 9 ext→int 12 int→ext 12 int→int 9 |
| 110 | `dunKarthspireRedoubtQST` `000357E5` Skyrim | в KarthspireLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None | сюжет | — | int→ext 1 |
| 111 | `dunLiarsRetreatQST` `000AA5D6` Skyrim | в LiarsRetreatLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, RunOnce, сцены 4 | сюжет | — | 0 |
| 112 | `dunLostValleyRedoubtQST` `000B0F5B` Skyrim | в LostValleyRedoubtLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, RunOnce, сцены 3 | сюжет | — | 0 |
| 113 | `dunMistwatchQST` `00030B36` Skyrim | в MistwatchLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, цели 3, сцены 3, ENAM пуст | сюжет | — | 0 |
| 114 | `dunMossMotherQST` `000376EE` Skyrim | в MossMotherCavernLocation ИЛИ; в FalkreathLocation ИЛИ | логика подземелья/места: боссы, засады, сцены, стадии | Misc, цели 4, сцены 1 | сюжет | — | ext→int 12 int→ext 12 int→int 2 |
| 115 | `dunNchuandZelQst` `000C3709` Skyrim | в NchuandZelLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, журнал 5, цели 6, RunOnce | сюжет | — | int→int 1 |
| 116 | `dunPinewatchQST` `000786B9` Skyrim | в PinewatchLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Main, RunOnce, сцены 1 | сюжет | — | 0 |
| 117 | `dunPOITundraWitchShackQST` `000DDF8D` Skyrim | в AnisesCabinLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None | сюжет | — | 0 |
| 118 | `dunRaldbtharQST` `000B7C01` Skyrim | в RaldbtharLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc | сюжет | — | ext→ext 1 ext→int 1 |
| 119 | `dunRannveigQST` `0005160C` Skyrim | в RannveigsFastLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, сцены 2 | сюжет | — | 0 |
| 120 | `dunRavenscarQST` `0003D26C` Skyrim | в RavenscarHollowLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, сцены 1 | сюжет | — | 0 |
| 121 | `dunRebelsCairnChangeLocationQST` `000EB9E2` Skyrim | в RebelsCairnLocation ИЛИ; в RedEagleRedoubtLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc | сюжет | — | 0 |
| 122 | `dunRebelsCairnQST` `000C1ACC` Skyrim | в RebelsCairnLocation ИЛИ; в RedEagleRedoubtLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, цели 2, ENAM пуст | сюжет | — | 0 |
| 123 | `dunRobbersGorgeQST` `000B8C9F` Skyrim | в RobbersGorgeLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, журнал 1 | сюжет | — | 0 |
| 124 | `dunSaarthal` `00049DB2` Skyrim | в SaarthalLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None | сюжет | — | 0 |
| 125 | `dunShipwreck04QST` `00043BAB` Skyrim | в PilgrimsTrenchLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, журнал 2, цели 2, ENAM пуст | сюжет | — | 0 |
| 126 | `dunShroudHearthBarrowQST` `00072206` Skyrim | в ShroudHearthBarrowLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, сцены 1 | сюжет | — | int→ext 1 |
| 127 | `dunSleepingTreeCampQST` `000E1A9D` Skyrim | в SleepingTreeCampLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, цели 1 | сюжет | — | 0 |
| 128 | `dunSunderstoneGorgeQST` `0008B72A` Skyrim | в SunderstoneGorgeLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None | сюжет | — | 0 |
| 129 | `dunTalkingStoneQST` `0004BA6C` Skyrim | в OrotheimLocation ИЛИ; в TalkingStoneCampLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, сцены 2 | сюжет | — | 0 |
| 130 | `dunTolvaldsCaveQST` `0003CD36` Skyrim | в TolvaldsCaveLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, сцены 4 | сюжет | — | 0 |
| 131 | `dunTrevasWatchQST` `0004B2A1` Skyrim | в TrevasWatchLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Side, журнал 4, цели 3, RunOnce, сцены 2 | сюжет | — | 0 |
| 132 | `dunValtheimKeepQST` `000ADFE7` Skyrim | в ValtheimKeepLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None | сюжет | — | 0 |
| 133 | `dunValthumeQST` `0003ED82` Skyrim | в ValthumeLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Side, журнал 4, цели 5, RunOnce, сцены 4, ENAM пуст | сюжет | — | 0 |
| 134 | `dunWhiteRiverWatchQST` `0001C3FB` Skyrim | в WhiteRiverWatchLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, сцены 3 | сюжет | — | 0 |
| 135 | `Favor001` `000403B0` Skyrim | (6 узлов) L2=MorthalLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=DarkwaterCrossingLocation; L2=AngasMillLocation; L2=FalkreathLocation; L2=WindhelmLocation; L2=… | поручение или мелкий квест (журнал, цели) | Misc, цели 1 | сюжет | — | int→ext 10 |
| 136 | `Favor013` `000CA209` Skyrim | (6 узлов) L2=WinterholdLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=KynesgroveLocation; L2=SolitudeLocation; L2=WindhelmLocation; L2=MarkarthLocation; L2=Whiter… | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | ext→ext 1 int→ext 11 |
| 137 | `Favor017` `000CA210` Dragonborn | (8 узлов) L2=MorthalLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=NarzulburLocation; L2=MorKhazgurLocation; L2=DushnikhYalLocation; L2=WindhelmLocation; L2=Marka… | поручение или мелкий квест (журнал, цели) | Misc | сюжет | — | int→ext 9 |
| 138 | `Favor018` `0003F46A` Skyrim | (3 узла) L2=WinterholdLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=FalkreathLocation; L2=WindhelmLocation | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | int→ext 4 |
| 139 | `Favor019` `0003F4EC` Skyrim | L2=WindhelmLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | 0 |
| 140 | `Favor104` `0006FD35` Skyrim | (4 узла) L2=DarkwaterCrossingLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=SolitudeLocation; L2=WindhelmLocation; L2=DLC2SkaalVillageLocation | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | ext→ext 1 int→ext 2 |
| 141 | `Favor109` `000940E0` Skyrim | L2=SolitudeLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; игрок.GetLevel()>=10 | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | ext→ext 1 int→ext 1 |
| 142 | `Favor110` `000CA439` Skyrim | (6 узлов) L2=MorthalLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=DawnstarLocation; L2=FalkreathLocation; L2=SolitudeLocation; L2=WindhelmLocation; L2=WhiterunLo… | поручение или мелкий квест (журнал, цели) | Misc, цели 1 | сюжет | — | ext→ext 1 int→ext 9 |
| 143 | `Favor151` `0006C824` Skyrim | L2=MarkarthLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | int→ext 6 |
| 144 | `Favor153` `00090E36` Skyrim | L2=DawnstarLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; игрок.GetLevel()>=22 | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | int→ext 2 |
| 145 | `Favor154` `0007172A` Skyrim | L2=FalkreathLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | int→ext 3 |
| 146 | `Favor157` `000A3504` Skyrim | L2=MarkarthLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; игрок.GetLevel()>=20 | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | int→ext 6 |
| 147 | `Favor158` `000940D0` Update | L2=WinterholdLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | int→ext 1 |
| 148 | `Favor204` `0007105B` Skyrim | (3 узла) L2=WindhelmLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=MarkarthLocation; L2=WhiterunLocation | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | int→ext 9 |
| 149 | `Favor205` `0006FE37` Skyrim | (5 узлов) L2=KynesgroveLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=DawnstarLocation; L2=FalkreathLocation; L2=SolitudeLocation; L2=WindhelmLocation | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | ext→ext 1 int→ext 6 |
| 150 | `Favor250` `000A2C86` Skyrim | L2=MarkarthLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | int→ext 6 |
| 151 | `Favor252` `000A2C9B` Skyrim | L2=SolitudeLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | ext→ext 1 int→ext 1 |
| 152 | `Favor253` `000A2C9E` Skyrim | L2=WhiterunLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | int→ext 3 |
| 153 | `Favor254` `000A2CA6` Skyrim | L2=WindhelmLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | 0 |
| 154 | `Favor255` `000A34CE` HearthFires | L2=MorthalLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 1 | сюжет | — | 0 |
| 155 | `Favor256` `000A34D4` HearthFires | L2=DawnstarLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 1 | сюжет | — | int→ext 2 |
| 156 | `Favor257` `000A34D7` Skyrim | L2=WinterholdLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 1 | сюжет | — | int→ext 1 |
| 157 | `Favor258` `000A34DE` HearthFires | L2=FalkreathLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 1 | сюжет | — | int→ext 3 |
| 158 | `FreeformCidhnaMineA` `000DB851` Skyrim | L2=CidhnaMineLocation; L2∋LocTypeHabitation | поручение или мелкий квест (журнал, цели) | Misc, цели 3, ENAM пуст | сюжет | — | 0 |
| 159 | `FreeformDragonBridge01` `000555DD` Skyrim | L2=DragonBridgeLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 2, RunOnce | сюжет | — | 0 |
| 160 | `FreeformFrostRiver` `000681D4` Skyrim | GetQuestRunning(FreeformFrostRiver)==0; L2=FrostRiverFarmLocation; GetGlobalValue(FreeformFrostRiverReset)<GameDaysPassed ИЛИ; GetGlobalValue(FreeformFrostRiverReset)==0;… | поручение или мелкий квест (журнал, цели) | None | сюжет | — | 0 |
| 161 | `FreeformHighHrothgarA` `000E76DE` Skyrim | L2=HighHrothgarLocation | поручение или мелкий квест (журнал, цели) | Misc, цели 1 | сюжет | — | 0 |
| 162 | `FreeformKarthwastenA` `000622E8` Skyrim | GetStageDone(FreeformKarthwastenA,10)==0; L2=KarthwastenLocation; L2∋LocTypeHabitation | поручение или мелкий квест (журнал, цели) | Misc, цели 4, сцены 1, ENAM пуст | сюжет | — | int→ext 6 |
| 163 | `FreeformKolskeggrA` `0001FD72` Skyrim | L2=LeftHandMineLocation; L2∋LocTypeHabitation; GetQuestRunning(FreeformKolskeggrA)==0 | поручение или мелкий квест (журнал, цели) | Misc, цели 2, ENAM пуст | сюжет | — | int→ext 4 |
| 164 | `FreeformKynesgroveA` `00069043` Skyrim | L2=KynesgroveLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 1 | сюжет | — | 0 |
| 165 | `FreeformMarkarthJ` `000790E1` Skyrim | L2=MarkarthLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; GetStageDone(FreeformMarkarthJ,20)==0 | поручение или мелкий квест (журнал, цели) | Misc, цели 1, RunOnce | сюжет | — | int→ext 6 |
| 166 | `FreeformMarkarthMPostQuest` `000647A6` Skyrim | GetStageDone(FreeformMarkarthM,20)==1; L1=MarkarthLocation; GetStageDone(FreeformMarkarthM,10)==0 | поручение или мелкий квест (журнал, цели) | None, RunOnce, ход квеста | сюжет | — | ext→int 23 |
| 167 | `FreeformOldHroldanB` `0006819A` Skyrim | L2=OldHroldanLocation; L2∋LocTypeHabitation | поручение или мелкий квест (журнал, цели) | Misc, цели 1, сцены 1 | сюжет | — | int→ext 1 |
| 168 | `FreeformRiverwood01` `00074A83` Skyrim | L2∋LocTypeHabitation; L2=RiverwoodLocation | поручение или мелкий квест (журнал, цели) | Misc, цели 6, RunOnce | сюжет | — | int→ext 7 |
| 169 | `FreeformShipsDawnstar` `00094152` Skyrim | L2=DawnstarLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | int→ext 2 |
| 170 | `FreeformShipsWindhelm` `00094158` Skyrim | L2=WindhelmLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | 0 |
| 171 | `FreeformSkyHavenTempleB` `000E3E67` Skyrim | L2=SkyHavenTempleLocation | поручение или мелкий квест (журнал, цели) | Misc, цели 2 | сюжет | — | ext→int 1 |
| 172 | `FreeformWinterholdCFin` `0006531D` Skyrim | GetStageDone(FreeformWinterholdC,40)==1; L2=WinterholdLocation; L2∋LocTypeHabitation | поручение или мелкий квест (журнал, цели) | Main, ход квеста | сюжет | — | int→ext 6 |
| 173 | `FreeformWinterholdCollegeA` `0005D2E9` Skyrim | FaraldaRef.GetDead()==0; NiryaRef.GetDead()==0; GetGlobalValue(FreeformCollegeAReset)<GameDaysPassed; игрок.GetInFaction(CollegeofWinterholdFaction)==1; игрок.GetRelation… | поручение Нирьи с записками, без журнала | Misc | сюжет | гасить | ext→ext 3 int→ext 362 |
| 174 | `MGR01Trigger` `000E8B50` Skyrim | GetQuestRunning(MGR01)==0; L2=WinterholdCollegeLocation; L1≠WinterholdCollegeLocation | ход сюжетного квеста | None, ход квеста | сюжет | — | int→ext 11 |
| 175 | `MGScrollSwap` `0010F4AC` Skyrim | GetVMQuestVariable(DialogueWinterholdCollege,92010)==1; L2≠WinterholdCollegeArcanaeumLocation | механика: вернуть Древний свиток на место | None, ход квеста | сюжет | — | ext→ext 30 ext→int 413 int→ext 415 int→int 60 |
| 176 | `MQ201ThalmorEmbassy` `00030B15` Skyrim | в ThalmorEmbassyLocation; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, сцены 1, ENAM пуст | сюжет | — | 0 |
| 177 | `MS02EscapeMadanachEnding` `00054880` Skyrim | MadanachREF.GetDead()==0; GetQuestRunning(MS02)==1; L2=MarkarthRuinsLocation | ход сюжетного квеста | Side, RunOnce, сцены 2, ход квеста | сюжет | — | ext→int 1 int→int 1 |
| 178 | `MS02EscapeThonarEnding` `0006A9A0` Skyrim | MadanachREF.GetDead()==1; GetQuestRunning(MS02)==1; L2=MarkarthLocation | ход сюжетного квеста | Side, RunOnce, ход квеста | сюжет | — | int→ext 23 |
| 179 | `MS05_dunDeadMensRespiteQST` `00016C8D` Skyrim | в DeadMensRespiteLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | Misc, RunOnce, сцены 12 | сюжет | — | 0 |
| 180 | `MS08ArgueQuest` `00055BFD` Skyrim | GetVMQuestVariable(MS08ArgueQuest,CCA670)==0; L1=WhiterunLocation; L2≠WhiterunLocation; GetStageDone(DialogueWhiterun,40)==1; GetStage(MS08)<25; GetStage(MQ104)>45 | подготовка квеста MS08 (Enable, MoveTo) | Main, RunOnce, сцены 1, ход квеста | сюжет | — | ext→int 38 |
| 181 | `MS10CompanySwapPostQuest` `0006BC1A` Skyrim | L2=WindhelmLocation; L1≠WindhelmLocation; GetQuestCompleted(MS10)==1 | ход сюжетного квеста | None, RunOnce, ход квеста | сюжет | — | int→ext 28 |
| 182 | `MS11Kicker` `000BAC24` Update | L2=WindhelmLocation; L1≠WindhelmLocation; L1∌LocTypeDwelling | ход сюжетного квеста | None | сюжет | — | 0 |
| 183 | `MS12b` `0002499C` Update | GetStageDone(MQ204,200)==1; GetStageDone(MS12,100)==1; GetQuestRunning(MS12b)==0; GetGlobalValue(57)>=MS12FollowUpStartDay | ход сюжетного квеста | Side, журнал 3, цели 5, RunOnce, ход квеста | сюжет | — | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 184 | `T03PostQuest` `000FE479` Skyrim | GetQuestCompleted(T03)==1; GetStageDone(T03PostQuest,100)==0; L2≠WhiterunLocation; L1≠WhiterunLocation | ход сюжетного квеста | None, ход квеста | сюжет | — | ext→ext 30 ext→int 377 int→ext 377 int→int 62 |
| 185 | `TG04Brinewater` `0007A282` Skyrim | в BrinewaterGrottoLocation ИЛИ; здесь∋LocTypeDungeon | логика подземелья/места: боссы, засады, сцены, стадии | None, RunOnce, сцены 2 | сюжет | — | 0 |
| 186 | `TG06_dunMarkarthWizardsQuartersQST` `00087105` Skyrim | в MarkarthLocation ИЛИ | логика подземелья/места: боссы, засады, сцены, стадии | None, сцены 6, ENAM пуст | сюжет | — | ext→int 22 int→ext 23 int→int 9 |

**Квесты с пустым `ENAM`** (14) стоят в узлах `CLOC`, но события у квеста нет: `DA11IntroScene`, `DLC2dunNchardakTracking`, `FreeformCidhnaMineA`, `FreeformKarthwastenA`, `FreeformKolskeggrA`, `MQ201ThalmorEmbassy`, `MS13LucanCamillaScene`, `TG06_dunMarkarthWizardsQuartersQST`, `dunBluePalaceWingQST`, `dunDarklightQST`, `dunMistwatchQST`, `dunRebelsCairnQST`, `dunShipwreck04QST`, `dunValthumeQST`.
- Часть из них запускается при старте игры (`StartGameEnabled`).
- В `AE#25048` сверки `eventID` нет. Запускает ли их узел на самом деле, не проверено.

## 10. `OnLocationChange`: привязанные скрипты

| Скрипт | К чему привязан (число, примеры) | Что делает в OnLocationChange | Категория |
|---|---|---|---|
| `BQPlayerScript` | QUST.alias 8: BQ01, BQ02, BQ03 | ход квеста: GetOwningQuest, PlayerChangedLocation | сюжет |
| `BYOHHouseBuildingPlayerScript` | QUST.alias 1: BYOHHouseBuilding | ход квеста: GetOwningQuest, PlayerChangeLocation | сюжет |
| `BYOHRelationshipAdoptionChildScript` | QUST.alias 2: BYOHRelationshipAdoption | ход квеста: ChildLocationChanged, GetActorRef | сюжет |
| `BYOHRelationshipAdoptionPlayerScript` | QUST.alias 1: BYOHRelationshipAdoption | ход квеста: PlayerLocationChanged | сюжет |
| `C00PlayerWatchingScript` | QUST.alias 2: C00 | ход квеста: GetOwningQuest, GetStage | сюжет |
| `C05WatchedPlayerScript` | QUST.alias 2: C05 | ход квеста: GetReference, IsInInterior, IsRunning, Stop | сюжет |
| `CartDriverScript` | NPC_ 1: CarriageDriverTemplate | извозчик говорит реплику | окружение |
| `CidhnaMineJailEventPlayerScript` | QUST.alias 1: CidhnaMineJailEventScene | ход квеста: GetOwningQuest, SetStage | сюжет |
| `CompanionsSingleCombatPlayerScript` | QUST.alias 1: CompanionsSingleCombatQuest | ход квеста: GetOwningQuest, SetStage | сюжет |
| `CR01PlayerWatchingScript` | QUST.alias 1: CR01 | ход квеста: GetActorReference, GetLocation, GetOwningQuest, GetReference, GetStage, Lock | сюжет |
| `CWFinalePlayerScript` | QUST.alias 1: CWFinale | ход квеста: GetOwningQuest, GetStageDone, stop | сюжет |
| `CWFortSiegeCapitalPlayerScript` | QUST.alias 1: CWFortSiegeCapital | ход квеста: GetLocation, GetOwningQuest, GetReference, GetStageDone, IsInLocation, Stop | сюжет |
| `CWGarrisonObserverPlayerScript` | QUST.alias 3: CWGarrisonObserver1, CWGarrisonObserver2, CWGarrisonObserver3 | ход квеста: GetLocation, GetOwningQuest, isChild, playerLeftLocation | сюжет |
| `CWPlayerScript` | QUST.alias 3: CWAttackCity, CWMission03, CWMission04 | ход квеста: GetLocation, GetOwningQuest, GetPlayer, GetStage, GetStageDone, IsInLocation | сюжет |
| `DA11IntroPlayerScript` | QUST.alias 1: DA11HallScene | ход квеста: IsPlaying, Stop | сюжет |
| `DCL1VampireTurnPlayerScript` | QUST.alias 1: DLC1VampireTurn | ход квеста: GetOwningQuest, PlayerChangedLocationCompleteChange | сюжет |
| `defaultAliasChangeOutfit` | QUST.alias 1: CW02A | смена одежды NPC | неясно |
| `DGIntimidatePlayerScript` | QUST.alias 1: DGIntimidateQuest | ход квеста: GetActorRef, GetCurrentLocation, GetOwningQuest, GetPlayer, IsInLocation, SetStage | сюжет |
| `DLC1EclipseAttackPlayerScript` | QUST.alias 6: DLC1EclipseAttack1, DLC1EclipseAttack2, DLC1EclipseAttack3 | ход квеста: GetLocation, GetOwningQuest, GetPlayer, IsInLocation, stop | сюжет |
| `DLC1RadiantPlayerLeaveScript` | QUST.alias 1: DLC1RadiantDisguisedVampireLordSpawner | ход квеста: GetActorReference, GetLocation, GetOwningQuest, IsInLocation, Stop | сюжет |
| `DLC1RadiantPlayerScript` | QUST.alias 1: DLC1Radiant | ход квеста: CreateQuestBasedOnLocation | сюжет |
| `DLC1RNPCAliasScript` | QUST.alias 1: DLC1NPCMentalModel | смена одежды NPC | неясно |
| `DLC2AshSpawnAttackChanceScript` | QUST.alias 1: DLC2Init | шанс нападения пепельных призраков по LocTypeDwelling | окружение |
| `DLC2EbonyWarriorPlayerScript` | QUST.alias 1: DLC2EbonyWarriorQuest | ход квеста: GetActorRef, GetPlayer, GetRef, GetStage, MoveTo, RemoveFromFaction | сюжет |
| `DLC2InitCrossDLCScript` | QUST.alias 1: DLC2Init | ход квеста: GetOwningQuest, GetStageDone, setstage | сюжет |
| `DLC2MQ04PlayerScript` | QUST.alias 1: DLC2MQ04 | ход квеста: GetOwningQuest, SetStage, getStageDone | сюжет |
| `DLC2MQ05PlayerScript` | QUST.alias 1: DLC2MQ05 | ход квеста: GetOwningQuest, GetStageDone, SetStage, getStageDone | сюжет |
| `DLC2MQ06PlayerScript` | QUST.alias 1: DLC2MQ06 | ход квеста: GetOwningQuest, GetStageDone, SetStage | сюжет |
| `DLC2NelothScript` | NPC_ 1: DLC2Neloth | Нелот запирает дверь | неясно |
| `DLC2PillarPlayerScript` | QUST.alias 1: DLC2Pillar | ход квеста: CheckLocationAndRegisterForUpdate | сюжет |
| `DLC2RavenRockDragonScript` | QUST.alias 1: DLC2Init | данные ключевого слова для дракона Вороньей Скалы | неясно |
| `DLC2TelMithrynPlayerScript` | QUST.alias 1: DLC2DialogueTelMithryn | фракция в Тель Митрине | сюжет |
| `DLC2ThirskFFHilundPlayerScript` | QUST.alias 1: DLC2ThirskFFHilund | ход квеста: Enable, GetOwningQuest, GetStage, Stop | сюжет |
| `DLC2TT1PlayerScript` | QUST.alias 1: DLC2TT1 | ход квеста: GetStageDone, SetStage | сюжет |
| `DLC2TTF1PlayerScript` | QUST.alias 1: DLC2TTF1 | ход квеста: GetLevel, GetPlayer, SetStage | сюжет |
| `DLC2TTR4bPlayerScript` | QUST.alias 1: DLC2TTR4b | ход квеста: SetStage | сюжет |
| `DLC2WECultistAmbushPlayerScript` | QUST.alias 1: DLC2CultistAmbush | ход квеста: StopQuestIfNotInLocationAndCultistsNotLoaded | сюжет |
| `dragonActorSCRIPT` | NPC_ 36, ACHR 5: BFBDragon, DLC1Durnehviir, DLC1DurnehviirSummon | дракон NPC: регистрация атаки, призрак | неясно |
| `JailQuestPlayerScript` | QUST.alias 1: JailQuest | ход квеста: GetLocation, GetOwningQuest, GetPlayer, GetStage, IsInLocation, SetStage | сюжет |
| `MarriageFINPlayerScript` | QUST.alias 2: RelationshipMarriageFIN | ход квеста: GetCurrentLocation, GetOwningQuest, GetRef, IsObjectiveDisplayed, SetStage | сюжет |
| `MQ102PlayerScript` | QUST.alias 2: MQ102A, MQ102B | ход квеста: GetOwningQuest, GetPlayer, GetStage, GetStageDone, IsInLocation, SetStage | сюжет |
| `MQ104PlayerScript` | QUST.alias 1: MQ104 | ход квеста: GetActorRef, GetOwningQuest, GetParentCell, GetPlayer, GetStageDone, GetValue | сюжет |
| `MQ105PlayerScript` | QUST.alias 2: MQ105 | ход квеста: GetOwningQuest, SetStage, getStageDone | сюжет |
| `MQ106DelphineScript` | QUST.alias 1: MQ106 | ход квеста: GetActorRef, GetDistance, GetOwningQuest, GetPlayer, GetStage, IsInCombat | сюжет |
| `MQ106PlayerScript` | QUST.alias 1: MQ106 | ход квеста: GetOwningQuest, SetStage, getStageDone | сюжет |
| `MQ201PlayerScript` | QUST.alias 2: MQ201 | ход квеста: GetCurrentLocation, GetOwningQuest, GetPlayer, GetStage, GetStageDone, setStage | сюжет |
| `MQ202MakeAggressiveInLocation` | QUST.alias 2: MQ202 | ход квеста: GetActorRef, GetActorValue, SetActorValue | сюжет |
| `MQ203EsbernScript` | QUST.alias 1: MQ203 | ход квеста: GetOwningQuest, GetStageDone, SetStage | сюжет |
| `MQ203PlayerScript` | QUST.alias 1: MQ203 | ход квеста: GetActorRef, GetOwningQuest, GetStage, GetStageDone, HasKeyword, Say | сюжет |
| `MQ302PlayerScript` | QUST.alias 2: MQ302 | ход квеста: GetOwningQuest, SetStage, getStageDone | сюжет |
| `MQ304PlayerScript` | QUST.alias 2: MQ304 | ход квеста: DisablePlayerControls, EnablePlayerControls, GetOwningQuest | сюжет |
| `MS02PlayerScript` | QUST.alias 2: MS02 | ход квеста: GetOwningQuest, GotoState, SetStage | сюжет |
| `MS14AlvaScript` | QUST.alias 1: MS14 | фракция преступлений Алвы | сюжет |
| `PlayerWeddingScript` | QUST.alias 1: RelationshipMarriageWedding | ход квеста: GetOwningQuest, GetStage, SetStage | сюжет |
| `RemoveSilenceMUS` | QUST.alias 1: BardSongs | снять тишину MUSTavernSilence (музыка) при любой смене | окружение |
| `SvenScript` | QUST.alias 1: DialogueRiverwood_Revised | ранг фракции Свена | неясно |
| `TG00PlayerScript` | QUST.alias 1: TG00 | ход квеста: GetCurrentLocation, GetPlayer, GetStage, SetStage | сюжет |
| `TG01PlayerAliasScript` | QUST.alias 2: TG01 | ход квеста: AddToFaction, Disable, EvaluatePackage, GetActorRef, GetStage, IsRunning | сюжет |
| `WIPlayerScript` | QUST.alias 17: WIAddItem01, WICastMagic01, WICastMagic03 | остановить WI-встречу при смене локации | окружение |
| `WITavernPlayerScript` | QUST.alias 1: WITavern | остановить WITavern, если игрок ушёл из таверны | окружение |

## 11. Объёмы-триггеры у загрузочных дверей

Объёмов с `OnTriggerEnter` в скриптах 2 713; в пределах 512 единиц (плюс половина размера объёма) от загрузочной двери той же ячейки или мира — 608. Первые 25 скриптов:

| У двери / всего | Скрипт | Пример ячейки |
|---|---|---|
| 176 / 819 | `FXCameraAttachScript` | GoldenglowEstate02 |
| 119 / 452 | `defaultSetStageTrigSCRIPT` | Ragnvald01 |
| 57 / 129 | `CWThreatTriggerScript` | DawnstarExterior04 |
| 33 / 33 | `BYOHHouseInteriorSwapTrigger` | 0xd74 |
| 30 / 181 | `defaultActivateSelf` | DaintySload01 |
| 28 / 87 | `defaultStartSceneTrigScript` | RiverwoodSleepingGiantInn |
| 23 / 216 | `defaultAddMusicSCRIPT` | BrokenTowerRedoubt02 |
| 23 / 178 | `WICommentTriggerScript` | LiarsRetreat01 |
| 9 / 19 | `DLC2ApocryphaBookMarkerTriggerScript` | DLC2Book01Dungeon |
| 7 / 14 | `defaultSetStageOnEnter` | RiverwoodSleepingGiantInn |
| 7 / 10 | `CWDisableHoldPositionMarkerTrigger` | RiftenOrigin |
| 6 / 25 | `defaultPlayerEnableDisableLinkedRef` | StillbornCave01 |
| 6 / 10 | `defaultOnEnter` | ThalmorEmbassy01 |
| 5 / 6 | `StartSceneTriggerSpecificTime` | WhiterunDragonsreachBasement |
| 5 / 11 | `dunCGEnableDisableScript` | HelgenExterior02 |
| 3 / 65 | `TrapTriggerBASE` | BlindCliffCave01 |
| 3 / 66 | `defaultSetStageTRIGSpecificActor` | HunterShack |
| 3 / 4 | `DA07MuseumTriggerScript` | DawnstarSilussHouse |
| 3 / 14 | `defaultEnableDisableLinkedRef` | DLC1VampireCastleExterior09 |
| 2 / 6 | `defaultActorEnableDisableLinkedRef` | SnaplegCave01 |
| 2 / 2 | `TG03BreadcrumbTriggerScript` | HonningbrewMeadery01 |
| 2 / 2 | `MQ101InsideHelgenSetStageScript` | HelgenKeep01 |
| 2 / 5 | `HelgenRandomSoundScript` | HelgenKeep01 |
| 2 / 2 | `DunBrokenOarMakeEnemySCRIPT` | BrokenOarGrotto01 |
| 2 / 2 | `dunGlenmorilWeather` | DarklightTowerExterior02 |

Скрипты `default*` ставят стадию, запускают сцену или включают ссылки, когда игрок впервые входит в объём. При портале они срабатывают так же, если игрок проходит через объём своим ходом. Опасно другое: перенос игрока туда и обратно через объём у двери (например, телепорт портала назад) даст `OnTriggerLeave`/`OnTriggerEnter` дважды.
