# Массовое чтение по спискам T42–T44

Пачки из результатов T42, T43, T44 (`results/T42.md`, `T43.md`, `T44.md`). Читали агенты `re-reader` (Sonnet, medium), координатор сверял 2–3 утверждения каждой пачки по базе (отметка «сверено»). Псевдокода нет: только выводы, `AE#id`, смещения из `asm`.

## T42, пачка A. Кто читает `TES::interiorCell` (`TES+0xC0`), 31 ID

Вопрос: какое решение зависит от `interiorCell` и ошибётся ли функция для интерьера, который подключён, но не текущий (портал с улицы).

Ложные совпадения поиска: `AE#13450`–`AE#13453` `+0x72`/`+0x1ED` и `AE#52971+0x860` — это `[rsp+0xC0]`/`[rbp+0xC0]`, не `TES`.

**Вывод.** Все решения бинарные: «игрок в интерьере» или «в экстерьере». Пока игрок на улице, `interiorCell == 0`, всё идёт по ветке экстерьера, и подключённый интерьер не видит ни одна из 31 функции: ни обходы ячеек, ни карта, ни звук воды. Для обратного направления (игрок внутри, улица видна через дверь) `interiorCell` выключает небо, воду и солнечный свет: для вида изнутри наружу его придётся подменять на время прохода (5.14, п. 2; T21, T44).

| Группа | ID | Чтение и решение | Для портала |
|---|---|---|---|
| Небо | `AE#26237` (сверено) | `+0xB6`: интерьер задан → режим неба `Sky+0x1BC = 3` (выключено) | изнутри наружу: небо выключено |
| Вода | `AE#36560` (сверено; единственный вызов — `Main::Draw+0x172`, `AE#36559`) | `+0x55`: интерьер без флага воды → обновление воды пропускается | изнутри наружу: вода улицы выключена |
| Свет солнца | `AE#32189` (цепочка `Main::Draw` → `AE#36560` → `AE#32174`, со слов агента), `AE#39946` (из `PlayerCharacter::Update+0x548`, `AE#41381`; и из заданий ИИ через `AE#40522`) | `+0x98` / `+0x1E9`: нет интерьера и мир с флагом `0x40` → логика солнечного света и теней | актёры подключённого интерьера получат ветку экстерьера (солнце) — **предположение**, по структуре |
| Массовая видимость | `AE#13365` (сверено), `AE#13366` | `+0xFF` / `+0x88`: флаг всем ячейкам сетки, интерьеру (`AE#13673`/`AE#13674`) и небесной ячейке | подключённый интерьер в переключение не попадёт |
| Ссылки и выгрузка | `AE#19456` (в `TESObjectCELL::AE_19442`), `AE#19826` (снятие ссылки, `ShadowSceneNode::RemoveLight`) | `+0xB4` / `+0x47E`: без интерьера — ветка экстерьера (мир ссылки) | ссылки подключённого интерьера идут веткой экстерьера; защита — мир ссылки `null` |
| Создание ссылки | `AE#13722` (обёртка `CreateReferenceAtLocation`) | `+0x1F8` высота по земле, `+0x23D` ячейка-родитель = `interiorCell` | создаётся только в текущем мире |
| Звук воды | `AE#32057` (хвост `Job_Water_audio`, задания) | `+0x95`: работа пропускается в интерьере | у подключённого интерьера звука воды нет |
| Погода | `AE#26261` | `+0xB`: интерьер → регион ячейки, иначе `lastKnownWeatherRegion` | для игрока на улице верно |
| Обход ячеек со скриптом | `AE#442673` (`Main::Update+0xA8F`) | `+0x1EA`: интерьер или `gridCells` | подключённый интерьер не обходится; что делает обход — не выяснено |
| Papyrus `FindRandomReference*` | `AE#13450`–`AE#13453` (клоны) | `+0x53`: интерьер → только он, иначе сетка + небо | «вокруг игрока» — допустимо |
| Диспетчеры, загрузка | `AE#13313`, `AE#13699`, `AE#13376` (флаг `bhkWorld +0xC5F9`) | ветка «интерьер / экстерьер» | интерьер не виден; поток не найден (нет прямых вызовов) |
| Карта | `AE#16335`, `AE#16344`, `AE#19144`, `AE#52971`, `AE#53144`, `AE#53160` | интерьер или сетка мира | только карта |
| Отладка и консоль | `AE#13340` (`SaveWorld`), `AE#13785` (`HotLoadPlugin`), `AE#15577`, `AE#15596`, `AE#15598`, `AE#15643` | — | не влияет |

Не проверено: потоки функций без прямых вызовов (`AE#13313`, `AE#13365`, `AE#13366`, `AE#13376`, `AE#13699`, `AE#13722`, `AE#26237`, `AE#26261`) — вызываются через таблицы.

## T42, пачка B. Кто спрашивает текущий мир `TES::AE_13314` (`TES+0x148`), 32 ID

Вопрос: сломается ли функция для ячейки другого мира (ворота: загружена ячейка мира 2, `TES::worldSpace` — мир 1).

**Вывод.** Причина поломок одна: ячейка, ссылка или блок LOD ищутся по `(x,y)` в карте ячеек мира игрока (`ws+0x80`), а ключи таблиц — `(x,y)` без мира. Лечится подстановкой мира самой ячейки (`cell+0x128`) или ссылки (`GetWorldSpace`): у большинства функций он под рукой. Сетка `TES` (`currentGridX/Y`) — сетка игрока; второму миру нужна своя сетка и свой обход (`AE#18234`, `AE#40436`).

| Вердикт | ID | Что делает и зачем мир |
|---|---|---|
| Ломается | `AE#18236` (сверено: `+0x99`, затем флаг `ws+0xA0 & 0x40`), `AE#18237`, `AE#18223` | постоянные ссылки ячейки: таблица `ws+0x250` по `(x,y)`, перенос в небесную ячейку мира игрока; в `AE#18223` (из `AE#18976`) подвижные ссылки сверяются с ячейкой по позиции в мире игрока |
| Ломается | `AE#19826` (сверено: `+0x47E` `interiorCell`, `+0x48C` мир) | применение отложенного перемещения ссылки: ячейка по позиции `AE#13717(pos, ws)` и `MoveRefToNewSpace(ref, 0, ws)` — ссылка из мира ворот уйдёт в ячейку мира игрока |
| Ломается | `AE#32057` (сверено: `+0x79`; задание `Job_Water_audio`) | звук воды: ячейка по позиции слушателя в мире игрока |
| Ломается | `AE#31846`, `AE#31847` | видимость подблоков LOD земли по загруженным ячейкам мира игрока |
| Ломается | `AE#13699`, `AE#13722`, `AE#40436`, `AE#19144` | обход ссылок в радиусе (`AE#20554`), создание ссылки, соседняя ячейка у границы (через сетку `TES`), туман карты |
| Зависит | `AE#13724`, `AE#13726` (мир параметром, ломается только вызов с `null`); `AE#31837`, `AE#31862` (запасной мир — владелец блока); `AE#32189` (своё поле мира); `AE#18234`, `AE#39946`, `AE#36575`, `AE#31816`, `AE#19456` | мир только как условие или есть свой |
| Безвредно | `AE#35639`, `AE#35651` (сохранение пишет и `TES::worldSpace`, и мир игрока), `AE#53093`, `AE#53144`, `AE#53150`, `AE#16335` (карта), `AE#15596`, `AE#15598`, `AE#23228` (отладка), `AE#43834`, `AE#43869` (взрывы в единственный корень LOD) | — |

Помощники мира (по `asm`, тела до 0x36 байт): `AE#20469` — LOD-данные мира с подъёмом к родителю, `AE#20470` — флаг `ws+0xA0` бит 0, `AE#20490` — родитель при флаге «use LOD data», `AE#20491` — родитель `ws+0x158`.

## T43, пачка 1. Построение пути через двери, 34 ID

**Главное — поправка к T43** (сверено: `AE#91750` вызывают только `BSPathBuilder::AE_91717+0x148` и `+0x23E`; `TeleportDoorSearch`-поиск `AE#30932` вызывают только `Pathing::AE_30656`–`AE_30659`): путь NPC строит `BSPathBuilder::Build` (`AE#91713`) → `AE#91717` (навмеш-поиск, узел на каждую дверь `AE#91750` → `AE#91674`). `TeleportDoorSearch` — грубый маршрут по дверям для оценки длины и первой двери (игрок `AE#40517`, карта, консоль, патруль `AE#28930`, квесты). Исправлено в `NPC_DOORS.md` и `results/T43.md`.

- **Что узел пути знает о двери:** `PathingDoor` (`AE#30809`): FormID `+0x10`, кэш handle `+0x14`, ленивые места `+0x18` (своя сторона, `AE#30820`) и `+0x48` (связанная дверь на её навмеше, `AE#30821`). Маркер `ExtraTeleport` в пути не используется.
- **Бесшовный перенос** в другую точку целевой ячейки пути не ломает (**предположение**): узлы за дверью — абсолютные места навмеша от связанной двери, не от позиции актёра. Поведение агента после скачка — пачка 2.
- **Обе стороны должны быть загружены:** без связанной двери по handle путь помечается неудачным (флаг `+0xA8`, строка «Found an invalid Teleport Door location»). Удерживаемому интерьеру это подходит.
- Состояние двери в пути — `AE#30816`–`AE#30819` (`GetOpenState` 1–4); штраф автодвери `AE#30824` (`TESObjectDOOR::AE_17918`).
- Не найдено: поток `Build` (цепочка рвётся на слоте vtable: `AE#90401+0x1F`, `AE#31483+0xA1`, `AE#91699+0xBF`).

## T43, пачка 2. Ходок пути high у двери и после скачка

Итог перенесён в `docs/NPC_DOORS.md`, раздел 9. Сверено: `AE#90769` зовёт только `AE#90711+0x544` (подход, `ApproachingDoor`), `AE#90770` — `AE#90711+0x613` и `+0x7C7` (сообщение узла, `ActivateDoor`, конец пути).

- Сообщения двери шлёт машина состояний ходока по параметру пути, а не по расстоянию; доставка — очередь контроллера `AE#41762` → разбор `AE#41717` (главный поток) → `AE#37892`.
- После скачка ходок не перепланирует: проекция только вперёд (`AE#91002`, порог 10 единиц). Повторный `ActivateDoor` после бесшовного переноса возможен (**предположение**) — гасить для двери-портала.
- Код «ушёл с пути» — `AE#90714` (3), `AE#90737` (2); `LoadedAreaMonitor` `AE#92466` — 3, если сегмент вне загруженной области. Потребитель кодов не найден.

## T43, пачка 3. Побочные эффекты переноса NPC, 24 ID

Вопрос: что из побочных эффектов переноса — состояние игры, которое должно случиться ровно один раз, и что звать бесшовному проходу кроме `AE#37313`.

**Ванильная дверь не зовёт `AE#37313`.** Ветка «активатор — не игрок» в `TESObjectDOOR::Activate`: точка назначения `AE#37438` (`+0x5AD`) → `SetPosition` и поворот (или `AE#11817`, если ячейка не загружена) → `AE#37716` (`+0x620`, «NPC идёт за игроком»; если да — `StopMovement`) → `MoveRefToNewSpace` (`+0x63A`). `AE#37313` — путь Papyrus `MoveTo` (`MoveTo_Impl` `AE#56626`: `StopInteractingQuick`, `AE#37313`, затем `AE#40512` на `+0x2B4`).

**Двойной `OnLocationChange`** (сверено: `AE#37313+0x16C…+0x1D7`): после `MoveRefToNewSpace` (`+0x167`) `AE#37313` шлёт событие сам, если мир до переноса ненулевой (`+0x16C`), мир после равен миру до (`+0x17D`/`+0x180`) и локация сменилась (`+0x18E`/`+0x191`). `MoveRefToNewSpace+0x8A2` шлёт его для актёра при смене локации. Значит, событие приходит дважды при переносе **внутри одного мира экстерьера** со сменой локации (улица ↔ улица, ворота в том же мире). Экстерьер ↔ интерьер, интерьер ↔ интерьер, разные миры — один раз (у интерьера мира нет).

| Что | ID | Вывод |
|---|---|---|
| Всё состояние переноса | `MoveRefToNewSpace` (`AE#19799`) | делает сам: `OnLocationChange`, `OnAttached/DetachedFromCell` (`AE#20043`, при `cellState == 7`), стартовая ячейка в `ExtraData 0x48` (`AE#11992`, идемпотентно), постоянные ссылки мира (`AE#20526`/`AE#20527`, только для постоянных ссылок при смене мира), Havok (очереди `AE#36932` `MoveHavok`, `AE#36921` `InitHavok`, `AE#36933` `DetachHavok`), подключение 3D (`AE#19411` → `AE#19413` → `ProcessLists` `AE#41343`) |
| Маркеры квестов | `AE#40512` (для NPC → `AE#40848`, пересчёт целей квестов с этим NPC; для игрока → `AE#40517`) | не обязателен, идемпотентен; ваниль зовёт после `ActivateRef` (`+0x702`) и `MoveTo`. Желательно один раз после переноса |
| Событие активации | `AE#20037` (источник `AE#20053`, из `ActivateRef+0x778`) | без `Activate` не нужно; подписчики не найдены |
| Не звать | `AE#20541`/`AE#20542` (небесная ячейка), `AE#35570` (для актёра недостижимо), `AE#37864` (маунты) | — |
| Очередь позиции | `AE#36949` (из `AE#37313+0x27E`) | выбор очереди по `ShouldUseTaskQueue` (`AE#39033`); на главном потоке не проверено |

**Вывод для бесшовного прохода NPC.** Лучше повторить путь ванильной двери (позиция и поворот через портал → при необходимости `StopMovement` → `MoveRefToNewSpace`), чем `AE#37313`: эффектов ровно столько же, сколько у ванили, и нет двойного `OnLocationChange` внутри одного мира. `AE#37313` добавляет `StopInteractingQuick`, курс, `StopMovement`/`EndInterruptPackage`. После переноса — `AE#40512`, по желанию.

## T44, пачка 3. Кто читает глобальные состояния вида

Вопрос: какие глобалы проход портала должен поставить перед собой и вернуть после — «вид», «кадр» или «мир».

| Глобал | Что это | Читатели | Класс | Проходу портала |
|---|---|---|---|---|
| `AE#414580` | указатель на `NiCamera` текущей отправки (сверено: `SubmitAccumulator+0x49`) | 19 мест, все — вид: расстояние до камеры `AE#105692`, SSAO (`AE#107245`, `AE#107256`), near/far ImageSpace (`AE#107276`–`AE#107278`, `AE#107285`, `AE#107287`), `BSEffectShader` (`AE#107524+0x1FD`, мягкие частицы), вода (`AE#107362+0x2C6`), DoF (`AE#107388`, `AE#107391`), объёмный свет (`AE#107379+0xD2`), `BSUtilityShader` (тени, `AE#107759`, `AE#107762`), хвост `RenderWorld` (`AE#107148+0x1B5`) | вид | ставить и **возвращать**: пишут его ещё `StartAccumulating` (`AE#106437`), `ShadowSceneNode::OnVisible` (`AE#106322`), `AE#106323`, `AE#105623`, `AE#105656`, `AE#107140`, `AE#107148` — внутри кадра он меняется |
| `AE#415299` | копия камеры мира | `AE#107753+0x7C` (проекция точки на экран) | вид | подменить, если проход вызывает `AE#107753` (**не проверено**) |
| `AE#411319`–`AE#411321` | снимок `AE#388909`–`AE#388911` (сверено: `Main::Draw+0x800…+0x828`) — по писателю `AE#77504+0x18` рядом с `State::SetCameraData` это, вероятно, смещение позиции камеры состояния графики (**предположение**) | только `BSMasterParticleSystem::AE_76893+0x44` (сверено) — сортировка частиц по расстоянию | вид, снимок раз за кадр | ставить на позицию камеры портала: иначе частицы интерьера сортируются от камеры игрока (кандидат для T17) |
| `AE#414568` | вероятно Z камеры, пишет `AE#36561+0x30E` раз за кадр | `BSTreeNode::OnVisible+0x23` (сверено) — скрыть дерево по высоте, при флаге `AE#390930` и флаге вида `+0x121` | вид | для портала улицы (изнутри наружу) — ставить; для интерьера деревьев нет |
| `AE#415422` | флаг, пишет `RenderWorld` | `FullScreenBlur` | кадр | не трогать |
| `AE#414566`, `AE#414567` | вероятно X/Y камеры | читателей нет | — | не трогать |

**Вывод.** Обязательный минимум для прохода «камера как параметр»: `AE#414580` ставить и возвращать (19 читателей через ImageSpace, SSAO, мягкие частицы, воду, тени). `AE#411319`–`AE#411321` и `AE#414568` — снимки позиции камеры игрока раз за кадр: для частиц и деревьев за порталом их тоже надо ставить. Читатели через копии указателя в регистрах и параметрах не искались.

## T44, пачка 2. Частицы для T17: границы, симуляция, LOD

Вопрос: почему дым пропадает в картинке портала, когда очаг не виден, и почему огонь меньше.

- **Граница и симуляция от видимости не зависят** (по коду): `worldBound` считает `BSGeometry::UpdateWorldBound` (`AE#70925`, слот `0x2F` не переопределён) в проходе обновления сцены; `modelBound` — `Do_UpdateSystem` (`AE#74498`) и `NiPSysBoundUpdateModifier` (`AE#74861`). Симуляцию ведёт `NiPSysUpdateCtlr::Update` (`AE#74716`) по времени, менеджер — `UpdateParticleSystemManagerJob` (`AE#19206`) → `AE#76601`. `OnVisible` (`AE#70926`) только отдаёт геометрию. Значит, «дым пропал» — не устаревшая граница от камеры игрока.
- **LOD частиц** — уже известное (FINDINGS, T13; `HooksPortal.cpp`, перенос расстояния через дверь): `BSPSysLODModifier::Update` (`AE#108480+0x23…+0x31`, сверено) берёт расстояние `+0x144` узла затухания, если не стоит бит 15 в `+0xF4`. Пишет `+0x144` только `AE#105506`; её зовут `BSFadeNode::OnVisible+0xDB`, `AE#105486+0x37`, `BSLeafAnimNode::OnVisible+0x79` (сверено) — расстояние от камеры, чей проход последним вызвал `OnVisible`.
- **Кандидаты на пропадание дыма** (**предположения**, проверять выключателями в T17):
  - отсечение пирамидой портала по границе системы (T44, `RENDER_VIEW_MAP.md`, 7);
  - сброс системы, если время уходит назад (`AE#74498`: частицы и `modelBound` обнуляются);
  - сортировка частиц от позиции камеры игрока (`AE#411319`–`AE#411321`, пачка 3).
- Что логировать в T17: `lastUpdate`, радиусы `modelBound`/`worldBound` дыма в проходе портала, `+0x144` узла затухания до и после прохода.

## Для куратора (кандидаты имён из пачек)

```
AE#13340  TES::SaveHavokWorldToFile  ; strcat ".hkx", caller ConsoleCommand::SaveWorld (medium)
AE#13450  TES::FindRandomRefInRadius  ; caller TES::AE_13423 <- Papyrus FindRandomReferenceOfAnyTypeInList; AE#13451..13453 are clones per callback type (medium)
AE#13722  TESDataHandler::PlaceAtLocation  ; wraps CreateReferenceAtLocation, land height, interiorCell as parent (low)
AE#13785  TES::ReloadPluginCells  ; caller ConsoleCommand::HotLoadPlugin (low)
AE#15643  TES::OutputCellTextureUse  ; caller ConsoleCommand::OutputTextureUseMap (medium)
AE#26237  Sky::UpdateInteriorMode  ; +0xB6 interiorCell -> Sky+0x1BC = 3 (medium)
AE#26261  TES::GetCurrentWeatherRegion  ; interior ? cell region : lastKnownWeatherRegion (medium)
AE#32057  WaterAudio::Update  ; tail target of Job_Water_audio, skipped in interior +0x95 (medium)
AE#36560  Main::UpdateWater  ; only caller Main::Draw+0x172, TESWaterSystem, interior check +0x55 (medium)
AE#53150  MapMenu::GetMapWorldSpace  ; TES::worldSpace, else player cachedWorldSpace, up the parent chain; 6 callers in MapMenu (medium)
AE#16335  LocalMapRenderer::Render  ; caller AE#52967 <- MapMenu vf_0x7, clears target, BSTreeManager (medium)
AE#35651  BGSSaveLoadGame::SavePlayerLocation  ; writes TES worldSpace, grid, player worldspace, position (medium)
AE#13726  TESDataHandler::AddReferenceToCellAt  ; cell by (x,y), TESObjectCELL::AE_19411 (medium)
AE#18236  TESWorldSpace::LoadPersistentCellRefs  ; table ws+0x250 by (x,y), PromoteReference into sky cell (medium)
AE#20469  TESWorldSpace::GetLODData  ; ws+0x90, up to parent by flag (medium)
AE#20491  TESWorldSpace::GetParentWorldSpace  ; ws+0x158 (medium)
AE#91717  BSPathBuilder::BuildSegments  ; navmesh search, door node per door via AE#91750 at +0x148/+0x23E (medium)
AE#91674  PathNode::InitDoorNode  ; position from PathingDoor forward location, message +0x30 (medium)
AE#30932  TeleportDoorSearch::Run  ; only callers Pathing::AE_30656..30659 (high)
AE#30933  TeleportDoorSearch::ExpandNode  ; vtable slot 1, doors of cell/world via AE#19071/AE#20535 (high)
AE#30809  PathingDoor::PathingDoor  ; ctor by FormID, handle cache +0x14, locations not computed (high)
AE#30820  PathingDoor::GetForwardLocation  ; door position, parentCell, world -> Pathing::AE_30640 (high)
AE#30821  PathingDoor::GetBackwardLocation  ; linked door via GetTeleportData -> its position (high)
AE#30825  PathingDoor::CreateLinkedPathingDoor  ; (high)
AE#30827  PathingDoor::GetDebugName  ; "<Unknown Door>" or name(formID) (high)
AE#20535  TESWorldSpace::CollectTeleportDoors  ; persistent refs +0x88 under lock, filter AE#19279 (high)
AE#19071  TESObjectCELL::CollectTeleportDoors  ; refs +0xB0 under lock +0x120 (high)
AE#30657  Pathing::GetDoorRouteLength  ; FLT_MAX on failure; callers AE#40518, Actor::AE_40445, BGSProcedurePatrol::AE_28930 (medium)
AE#30658  Pathing::FindDoorRoute  ; navmesh AE#90441, else TeleportDoorSearch (medium)
AE#414580  gCurrentSubmitCamera  ; NiCamera*, written by SubmitAccumulator+0x49 / StartAccumulating, 19 readers (high)
AE#415299  gWorldCameraCopy  ; written AE#36558+0x66, RenderWorld+0x74; read AE#107753+0x7C (medium)
AE#411319  gParticleSortCameraPos  ; x,y,z at AE#411319..411321, copied from AE#388909..388911 at Main::Draw+0x800; read BSMasterParticleSystem::AE_76893+0x44 (medium)
AE#105692  Renderer::DistanceToCurrentCamera  ; distance from point to AE#414580 position +0xA0 (medium)
AE#90711  MovementAgentPathFollowerStandard::UpdateStateMachine  ; planner slot 2, state +0x100, AE#90769 at +0x544, AE#90770 at +0x613/+0x7C7 (high)
AE#90714  MovementAgentPathFollowerStandard::CheckPathDeviation  ; code 3 beyond fMaxDistFromPathRadiusMult x path radius (high)
AE#90769  MovementAgentPathFollowerStandard::SendApproachMessage  ; action object +0x10 -> ApproachingDoor (medium)
AE#90770  MovementAgentPathFollowerStandard::SendNodeMessage  ; node +0x30 -> AE#41762 (high)
AE#90778  MovementAgentPathFollowerStandard::FindNextMessageNode  ; next node with +0x30 != 0 (high)
AE#91002  Path::ProjectPositionForward  ; forward scan from progress, 10-unit threshold (high)
AE#91964  MovementMessageActivateDoor::SendApproachingDoor  ; creates ApproachingDoor if distance > 0 (high)
AE#30824  PathingDoor::GetApproachDistance  ; AE#375551 if TESObjectDOOR::AE_17918 (medium)
AE#41717  MovementControllerNPC::DrainMessageQueues  ; AE#37892 per message, stops on process level change (high)
AE#41761  MovementControllerNPC::QueueMessageAt  ; IMovementMessageInterface slot 1 (high)
AE#41762  MovementControllerNPC::QueueMessage  ; IMovementMessageInterface slot 2 (high)
AE#41763  MovementControllerNPC::SendMessageNow  ; IMovementMessageInterface slot 3 (high)
AE#92466  MovementPathManagerAgentLoadedAreaMonitor::CheckPath  ; code 3 if path segment outside loaded area (medium)
AE#37716  Character::IsFollowingPlayerForDoor  ; package +0x24==2 with target == player, else process byte +0x13B; caller TESObjectDOOR::Activate+0x620 (medium)
AE#40512  PlayerCharacter::UpdateQuestTargetPaths  ; player -> AE#40517, NPC -> AE#40848 over TESQuestTarget; callers ActivateRef+0x702, MoveTo_Impl+0x2B4, Enable (medium)
AE#40517  PlayerCharacter::ComputeQuestPathToRef  ; Pathing::AE_30658 (medium)
AE#20526  TESWorldSpace::AddPersistentRef  ; creates ws+0x88 cell, AE#19411 (high)
AE#20527  TESWorldSpace::RemovePersistentRef  ; ws+0x88 -> TESObjectCELL::AE_19032; decompiler shows foreign code, function bounds to check (high)
AE#20528  TESWorldSpace::GetPersistentCell  ; returns ws+0x88 (high)
AE#11992  ExtraDataList::SetStartingWorldOrCell  ; ExtraData 0x48, guarded by AE#11993 (medium)
AE#11993  ExtraDataList::GetStartingWorldOrCell  ; ExtraData 0x48 +0x10 (medium)
AE#36932  TaskQueueInterface::QueueMoveHavok  ; task 0x31 (high)
AE#36921  TaskQueueInterface::QueueInitHavok  ; task 0x36 (high)
AE#36933  TaskQueueInterface::QueueDetachHavok  ; task 0x32 (high)
AE#36949  TaskQueueInterface::QueueActorSetPosition  ; task 0x20, position + heading, from AE#37313+0x27E (medium)
AE#35570  BGSSaveLoadGame::SaveRefOnUnload  ; "FORMS: Form ... is unloading" (medium)
AE#105506  BSFadeNode::UpdateLODDistance  ; writes +0x144 distance to camera param, callers BSFadeNode::OnVisible+0xDB, AE#105486+0x37, BSLeafAnimNode::OnVisible+0x79 (high)
AE#108480  BSPSysLODModifier::Update  ; slot 0x25 (NiPSysModifier::Update), reads fade node +0x144, writes system +0x180..+0x188 (high)
AE#74861  NiPSysBoundUpdateModifier::Update  ; slot 0x25, particle bound, mesh/strip branches AE#74864/AE#74865 (high)
AE#76601  BSParticleSystemManager::UpdateMasterSystem  ; by index +0x118, vfunc +0x160; caller UpdateParticleSystemManagerJob AE#19206 (high)
```
