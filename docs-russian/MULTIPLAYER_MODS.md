# Моды мультиплеера: как они держат нескольких игроков в одном мире (T41)

Исследование без кода, 08.11.2026. Бриф — `docs/tasks/T41-multiplayer-mods.md`, итог для куратора и координатора — `docs/tasks/results/T41.md`.
Источники: исходники на GitHub (TiltedEvolution `dev`, SkyMP `main`, снимок 08.11.2026) и база Ghidra (`AE#id`, только имена и вызывающие). Исходники GPL/AGPL **не копировались**: взяты факты «какая функция за что отвечает» с указанием файла и строки. Пометки: **по исходникам** — прочитано, в игре не запускалось; **по базе** — проверено по вызывающим в Ghidra 1.6.1170; **не найдено** — искали и не нашли.

## 1. Моды

| Мод | Репозиторий | Лицензия | Игра | Состояние |
|---|---|---|---|---|
| Skyrim Together Reborn (TiltedEvolution, «TE») | [tiltedphoques/TiltedEvolution](https://github.com/tiltedphoques/TiltedEvolution) | GPL-3.0 (README) | SE; главная цель 1.7.104, также 1.6.1170 (`AGENTS.md`), VR нет; ID — Address Library **AE**, пар SE нет | Живой (push 06.11.2026, 73 открытых issue). Не плагин SKSE: свой лаунчер `SkyrimTogether.exe`, SKSE грузится после |
| SkyMP (Skyrim Multiplayer) | [skyrim-multiplayer/skymp](https://github.com/skyrim-multiplayer/skymp) | GPLv3 (клиент, SkyrimPlatform), AGPLv3 (`skymp5-server`) по подпроектам, `TERMS.md` | SE и AE: в `Offsets.h` пары `RELOCATION_ID(SE, AE)` | Живой (push 30.10.2026). Логика на TypeScript поверх SkyrimPlatform, сервер на C++ |
| Keizaal | [Keizaal/Keizaal](https://github.com/Keizaal/Keizaal) | CC BY-NC-SA 4.0 | SE, Wabbajack | **Это сборка модов, а не мультиплеер.** Кода движка нет. «Keizaal Online» встречается только в чужих установщиках (`miir`, `skyrim-linux-bootstrap`); что за сервер, не выяснено |
| Skyrim Together (первая версия, Tilted Online) | исходников нет | закрыт | SE | Закрытая бета 2019, предшественница TE. Только пресса, разбирать нечего |
| «Skyrim Online» / SkyOn | [Saint-Evil/SkyOn](https://github.com/Saint-Evil/SkyOn) (совпадение с «Skyrim Online» не подтверждено) | GPL-3.0 | ScriptDragon, 2016 | 8 коммитов, «very raw», про игроков и ячейки в README ничего. Не разбирался |
| Старые SkyMP 0.9–3 | gitlab `pospelov/skymp2`, `skymp3`, `skymp2-cellhost` (`docs/docs_project_history.md`) | не смотрел | SE | Есть «cellhost» и «параллельная обработка ячеек на сервере». **Не дошли**: исходники на gitlab |

## 2. Ответы на вопросы брифа

### 2.1 Кто чужие игроки в движке

Во всех мирах — **обычный `Character` (NPC)**; настоящий игрок на клиенте один, свой. Второго `PlayerCharacter` никто не делает (подтверждает T31, `docs/VIRTUAL_PLAYER.md`).
- **TE:** `Actor::Create(TESNPC::Create(буфер внешности))` (`Games/Skyrim/Actor.cpp:208`, `Services/Generic/CharacterService.cpp:1637`): свежая база `TESNPC` из сериализованной внешности. Сам `Character` выделен своей `Actor::New` с хвостом-расширением и размещён через `TESDataHandler::CreateReferenceAtLocation` (`AE#13723`, в TE «SpawnNewREFR»). Флаги: не сохранять, `SetRemote(true)`, существенный, без восстановления после нокаута, фракция игрока `0xDB1` ранг 1 (`Actor.cpp:317`). Характеристики приходят целиком (`SetActorValues`), урон считает хук `Actor::DoDamage` (`AE#37335`). Снаряжение — хуки внутренних функций `ActorEquipManager` (раздел 3). Анимации — хук `ActorMediator::PerformAction` (`AE#38949`): у чужих копий действия игры не исполняются, исполняются присланные (`Games/Animation.cpp:23`).
- **SkyMP:** клон ванильного NPC `0x10D13E` с пакетами ИИ «ничего не делать» (`0x654E2`), форма с ID `0xFF000000+` (`PapyrusTESModPlatform.cpp:299`). Размещение — `placeAtMe` у игрока, потом `setPosition`, `enable`, `resurrect` (`skymp5-client/src/view/formView.ts:170`, `spawnProcess.ts`). Внешность — собственные нативы `SetNpcRace/Sex/SkinColor/Tints`, снаряжение — `AddItemEx`, `UpdateEquipment`.
- Перемещение и перенос в ячейку у обоих — `TESObjectREFR::MoveTo_Impl` (`AE#56626`): TE `Games/Skyrim/TESObjectREFR.cpp:394`, SkyMP `MoveRefrToPosition`. **Это наш перехват** (`HooksTes.cpp:251`).

### 2.2 Единственность игрока

Игрок остаётся единственным и настоящим на своём клиенте; «чужие» подгоняются под то, что движок уже умеет с NPC. Перехватов мало и все точечные.
- **Бой и ИИ (TE):** хук `ActorKnowledge::UpdateDetectionState` (`AE#42704`) отменяет обнаружение «чужой игрок → свой игрок» (`Actor.cpp:1150`); `Actor::IsFleeing` для игроков всегда «нет» (`:1283`). Остальное — через фракцию игрока: NPC видят копию как союзника. Хуки `CombatController::UpdateTarget` (`#if 0`, `CombatController.cpp:80`) и пакетов ИИ (`#define AI_SYNC 0`, `References.cpp:82`) **выключены**.
- **Владение ИИ:** копия NPC на не-владельце имеет `SetRemote(true)`: `Actor::Update`-ветка `AE#37356` возвращает 0 (движения нет, только шаг контроллера), `TESObjectREFR::SetPosition` (`AE#19790`) возвращает 1, `Actor::ProcessTracking` (`AE#38009`) пропускается. **Наш перехват `SetPosition` совпадает** (`HooksTes.cpp:259`).
- **Диалоги и озвучка:** `MenuTopicManager::PlayDialogueOption` (`AE#35269`) шлёт выбранную реплику; `Actor::SpeakSound` (`AE#37542`) шлёт строки владельца; фоновые реплики идут только от владельца NPC (`Actor.cpp:1208`).
- **Квесты:** без Story Manager. События `TESQuestStartStop/Stage` ловятся через приёмники `EventDispatcher` (`AE#54425`/`AE#54522`), стадии задерживаются и применяются у членов группы (`Services/Generic/QuestService.cpp`). Ведёт лидер группы. Для этого TE везёт `SkyrimTogether.esp`, `SkyrimTogetherQuestPatches.esp` и скрипты Papyrus: **это правка данных, наш п. 7 5.14 её запрещает**.
- **Время и погода:** `AE#36291` (шаг времени) глушится вне режима одиночки; погода — `Sky::SetWeather/ForceWeather/UpdateWeather` (`AE#26241/26243/26231`), ведёт лидер.
- **Камера и ввод:** камера не трогается. `PlayerControls::ProcessEvent` (`AE#42338`) получает пустой список, пока открыто окно оверлея; опрос устройств пропускается без фокуса окна.
- **Преступления:** не найдено ни хука, ни кода. Issue [#309](https://github.com/tiltedphoques/TiltedEvolution/issues/309): законная кража члена группы даёт 1 золото розыска лидеру.
- **SkyMP** идёт другим путём: **ванильный мир убирают**. `WorldCleanerService` каждый кадр отключает и удаляет одного ближайшего актёра в радиусе 8192 (`skymp5-client/src/services/services/worldCleanerService.ts`), быстрое перемещение, выбор сложности и рост навыков отключены (`disable*Service.ts`). Всё ведёт сервер, ванильные ИИ и квесты не нужны. Для нас неприменимо.

### 2.3 Ячейки

**Своей загрузки ячеек нет ни у кого:** каждый клиент грузит ванильную сетку, а мод только знает, чья ячейка где.
- **TE, клиент:** ячейку игрока опрашивают каждый кадр по `TES::currentGridX/centerGridX` (`Services/Generic/DiscoveryService.cpp:60`, без перехвата); шлют `EnterExteriorCell`, `EnterInteriorCell` и `ShiftGridCell` со списком ячеек сетки. Размер сетки зашит: `m_gridsToLoad = 5` (дракон 20), `encoding/Structs/GridCellCoords.h:10`. **Если у игрока другой `uGridsToLoad`, расчёт расходится.**
- **TE, сервер:** «в зоне» (`CellIdComponent::IsInRange`, `server/Components/CellIdComponent.h`): интерьер — тот же ID ячейки; экстерьер — тот же мир и `|dx|,|dy| ≤ gridsToLoad/2`. Сервер не симулирует: хранит ячейки, владельцев, «кто кого видит».
- **Чужой игрок вне сетки:** актёра **нет**, есть запись без 3D (`WaitingFor3D`). Актёр создаётся, только когда ячейка чужого попала в сетку локального (`RunSpawnUpdates`, `CharacterService.cpp:2093`) и ставится в `parentCell` локального игрока. Исключение — перенос актёра в ячейку, которой нет: вызов `TESWorldSpace::LoadCell` (`AE#20460`, `CharacterService.cpp:1355`) создаёт ячейку на месте.
- **Держат ли ячейки сверх сетки:** **нет**, ни TE, ни SkyMP. Это главное, чего у них нет и что нужно нам (5.14, пп. 1–2).
- **Владелец ячейки:** в TE владеет **актёр**, а не ячейка: один клиент ведёт ИИ и физику NPC и шлёт состояние. При уходе владельца сервер берёт первого, у кого ячейка «в зоне» (`server/Services/CharacterService.cpp:895`), а если такого нет, актёр **убирается из синхронизации** (у каждого дальше свой ванильный). Выбывшие и отказавшиеся запоминаются, чтобы владение не прыгало между клиентами без ячейки.
- **Двери в TE:** ничего особого. У каждого клиента свой ванильный переход с экраном загрузки; `TESObjectREFR::ActivateRef` (`AE#19796`) шлёт состояние двери. В журнале правок — «the door problem (again)», «actor stuck on traveling through door» (`CHANGELOG.md`, 1.34.0).
- **SkyMP:** сервер **не грузит ячейки вообще**, он разбирает `.esm/.esp` (`libespm`) и держит сетку на каждый ID «мир или ячейка»: клетка 4096 единиц (`MpObjectReference.cpp:151`), соседи 3×3 (`WorldState.cpp:840`). Дверь телепорта активирует **сервер**: берёт пункт назначения из записи REFR, меняет у актёра «мир или ячейку» и шлёт клиенту `TeleportMessage` (`MpObjectReference.cpp:1466`). Клиент прячет чужого игрока, если его «мир или ячейка» отличается от своей (`formView.ts:66`). Хозяин ИИ — первый клиент, запросивший хостинг; переключение после 2 с без обновлений (`ActionListener.cpp:653`); остальные получают `isHostedByOther` и отключают ИИ.

## 3. Перехваты движка

Все ID TE — **AE**: `SetPosition` = `AE#19790` совпадает с нашим `RELOCATION_ID(19363, 19790)`, имена базы подходят по смыслу. SE-пар у TE нет, SkyMP даёт пары (раздел 5). «Наш» — перехват есть в `src/Hooks*.cpp`. Строки — `Code/client/...` в TE.

| Группа | `AE#id` Имя в базе (если есть) | Где в TE | Наш? |
|---|---|---|---|
| Ячейка, позиция | `19790` `TESObjectREFR::SetPosition` | `Games/Skyrim/Actor.cpp:1297` | **да**, `HooksTes.cpp:259` |
| | `56626` `TESObjectREFR::MoveTo_Impl` (вызов, не перехват) | `TESObjectREFR.cpp:394` | **да**, `HooksTes.cpp:251` |
| | `20460` `TESWorldSpace::LoadCell` (вызов), `13718` (`GetCell(x,y,мир)`, вызов), `13723` `TESDataHandler::CreateReferenceAtLocation` (вызов) | `TESWorldSpace.cpp:6`, `ModManager.cpp:68,76` | `20460` вызывается из нашего `CenterOnCell`, остальное **ново** |
| | `37313` (`AE_37313`, зовёт `MoveTo_Impl+0x1C6`), `19742` (`AE_19742`, зовут `TES::AE_13355`, `TESObjectCELL::AE_19061`) | `Actor.cpp:1303,1304` | **ново** (рядом с нашим `MoveTo`/привязкой ячейки) |
| Тик актёра | `37356` `AE_37356` (зовёт `AE#37350+0xFB`, `Actor::Update` — `AE#37348`) | `Actor.cpp:1296` | **рядом**: наш `37350` и `Actor::Update` vfunc `0xAD` |
| ИИ и бой | `42704` `ActorKnowledge::AE_42704` (зовёт `DoDetectionJob` `AE#42742`), `37577` (`IsFleeing`), `38009` `Actor::ProcessTracking`, `39643`, `39114`, `38959`, `33236` (выкл.) | `Actor.cpp:1311,1317,1318`, `References.cpp:136` | **ново** |
| Снаряжение | `38929/38934/38928/38933/38930/38935` — внутренности `ActorEquipManager::EquipObject/UnequipObject/EquipSpell/Unequip…/EquipShout` (**по базе** вызываются из оболочек `AE#38894/38901/38895/38897`) | `EquipManager.cpp:311` | ново |
| Предметы, магия | `AE#37525` `Actor::AddObjectToContainer`, `37521` `Actor::PickUpObject`, `40454` `PlayerCharacter::DropObject`, `19708`, `19689`, `34144` `ActorMagicCaster::SpellCast`, `34526` `MagicTarget::AddTarget`, `44108` `Projectile::Launch` | `Actor.cpp`, `Magic/`, `Projectiles/Projectile.cpp:117` | ново; `Projectile::Launch` пригодится T25 |
| Мир | `36291` (время), `26241/26243/26231` `Sky::*Weather` | `TimeManager.cpp:25`, `Sky.cpp:91` | ново |
| Вызов и тик | `19796` `TESObjectREFR::ActivateRef`, `36564` `Main::Update`, `53926` `SkyrimVM::ProcessRegisteredUpdates` (тик клиента TE), `54425/54522` (приёмники событий) | `TESObjectREFR.cpp:1146`, `SkyrimVM64.cpp:42` | **рядом**: у нас дверь через vfunc `TESObjectDOOR` `0x37` (`HooksCells.cpp:333`), тик через `PlayerCharacter::Update` |
| Ввод, рендер | `42338` `PlayerControls::ProcessEvent`, `68617`, `68545` `ControlMap::ToggleControls`, `77226` `Renderer::Init` | `PlayerControls.cpp:66` | ново, нужно TE для оверлея |

Не-перехваты: `AE#403567` (`ActorMediator`), `AE#400269` (`TESDataHandler::singleton`), `AE#400447` (`TimeData`). Метка TE «деструктор VM» на `AE#40412` в базе — `PlayerCharacter::dtor_impl`; в TE хук пустой (`SkyrimVM64.cpp:44`).

## 4. Что сломано или не решено и что нам грозит

- **[#610](https://github.com/tiltedphoques/TiltedEvolution/issues/610):** игрок становится невидимым после загрузки ячейки (ворота → таверна → дворец → ворота). Следствие: копия привязана к сетке, а сетка перестраивается при переходе. Нам грозит, когда несколько порталов обновляют «владельца» ячейки.
- **[#810](https://github.com/tiltedphoques/TiltedEvolution/issues/810):** при смене владельца NPC (`SetRemote(false)`) движок включает ИИ, и NPC зацикливаются на `Unequip`/`combatStanceStop`. Относится к T11/T20: возврат ИИ NPC интерьера после передачи надо делать с чистым состоянием.
- **[#741](https://github.com/tiltedphoques/TiltedEvolution/issues/741), [#645](https://github.com/tiltedphoques/TiltedEvolution/issues/645):** враги не гонятся за членами группы, существенные NPC не встают. Чужая копия не всегда читается ИИ как «игрок». Прямой аналог нашего «дальняя сторона видит не того игрока» (T32).
- **[#214](https://github.com/tiltedphoques/TiltedEvolution/issues/214), [#215](https://github.com/tiltedphoques/TiltedEvolution/issues/215), [#230](https://github.com/tiltedphoques/TiltedEvolution/issues/230):** актёр бывает и локальным, и удалённым; актёры и объекты не появляются у всех. Рассинхрон владельцев.
- **Жёстко зашитая сетка 5×5** (`GridCellCoords.h:10`) и правка данных (`.esp`, скрипты, квестовые патчи) не годятся нам: 5.14, п. 7.
- **SkyMP, `ROADMAP.md`:** «NPC … должны уметь ходить вне поля зрения игроков», физика требует «host switching», «Sounds and music» только в рамках ячейки, «Linked Refs»/ловушки в подземельях не готовы. Звук через дверь и NPC вне поля зрения **не решены ни у кого**.
- **Потоки:** TE (`AGENTS.md`): хуки часто идут в рабочих потоках, работа ставится в очередь главного. Совпадает с нашим `MainThread::Post` (7).

## 5. Что применимо к порталу

1. **Прецедента нет для главного:** ни один мод не держит ячейку сверх ванильной сетки, не рисует чужую ячейку и не ведёт ИИ без игрока рядом. TE убирает актёра из синхронизации, SkyMP не грузит ячейки.
2. **T32 (место игрока в рамке дальней стороны):** подтверждается, что подменять объект игрока нечем и никто не пробовал. Новая идея — точки «кто на кого смотрит» уже есть как перехваты: `ActorKnowledge::UpdateDetectionState` (`AE#42704`, зовётся из `DoDetectionJob`, он в пуле `BSJobs` по T31) — место, где «цель = игрок» сравнивается с хозяином. Годится как проверка у T22/T32; не проверено.
3. **5.14, п. 1–2 (много порталов, любые пары):** отношение TE «в зоне» (интерьер — тот же ID, экстерьер — сетка) можно расширить рёбрами «ячейка ↔ ячейка через портал». Владельца ИИ вести по ячейке «кто присоединён», а не по актёру.
4. **Передача ИИ:** урок #810 — при смене «хозяина» NPC сбрасывать анимационное состояние, не просто снимать флаг.
5. **5.14, п. 3 (звук):** TE и SkyMP синхронизируют **события** (`SpeakSound` `AE#37542`), а не смешивают звук между ячейками. Готового решения нет.
6. **5.14, п. 4 (любая камера):** камера у обоих нетронута, ничего не заимствуем.
7. **5.14, п. 6 (SE 1.5.97):** пары SE/AE из `Offsets.h` SkyMP (Main::Update `35565/36564`, `PlayerCharacter::DrawWeaponMagicHands` `40232/41235`, `Actor::DrawWeaponMagicHands` `36289/37279`, `NotifyAnimationGraph` `37020/38048`, `Actor::DoReset3D` `39181/40255`, `CreateReferenceAtLocation` `13625/13723`) — в `PORTABILITY.md` для T26. У SkyMP `13625` — это SE-ID; в базе `AE#13625` другая функция.
8. **5.14, п. 7–8:** TE правит данные (ESP, скрипты) и ломается при чужих модах, SkyMP удаляет ванильных актёров: оба пути закрыты. Совместимость с Engine Fixes в TE не проверялась (в источниках не найдено).
