# Переносимость: всё, что привязывает мод к 1.6.1170

Цель пользователя (01.11.2026): мод работает одинаково на всех версиях SE/AE, минимум начиная с 1.5.97. Жёстко заданные сдвиги, номера функций и слоты нужно заменить на Address Library, поля NG или проверяемые сигнатуры.

Этот файл — список долгов. Он нужен, чтобы не замедлять эксперименты сейчас и подчистить всё потом.

**Правила:**
- Новый сдвиг или AE-номер, который нельзя сразу выразить переносимо, записывается сюда в том же коммите.
- Сделанное убирается из таблиц и переносится в раздел 5.
- Сначала ищем поле в NG, затем `RELOCATION_ID(se, ae)`, и только потом сигнатуру. Чистый сдвиг — последний вариант.

## 1. Текущее состояние

- `Portal::Prepare` разрешает портал только на 1.6.1170. На других версиях плагин загружается, но портал не запускается и пишет причину.
- Перехваты вызовов (`WriteCall`) проверяют байты места и цель `call`. При несовпадении перехват не ставится, а `Hooks::Ready()` возвращает `false`.
- AE 1.6.x и 1.7.x используют одно пространство номеров Address Library, но сдвиги внутри функций между ними могут отличаться. Так сказано в исходниках мода Realistic Reflections - Mirrors, сами мы это не проверяли.

## 2. Сдвиги и константы

| Что | Где | Значение | Чем заменить |
|---|---|---|---|
| Фоновый свет | `Portal.cpp`, `kAmbientOffset`, `kAmbientBytes` | `BSShaderManager::State + 0xC8`, `0x44` байта | Именованные поля в NG или сохранение через функции игры |
| Остаток выпуска частиц | `Portal::BeforeEmission` | Первое поле после `NiPSysModifier` (`+0x30`) | Описать `NiPSysEmitter` в NG, класса там нет |
| Место `lodDistance` | `Hooks.cpp`, `InstallCalls` | `AE#37348 + 0x1F3` | Поиск сигнатурой внутри функции |
| Место затухания | `Hooks.cpp`, `InstallCalls` | `AE#19384 + 0xE4` | То же |
| Граница списков сцены | `Hooks.cpp`, `InstallCalls` | `AE#36559 + 0x17E` | То же. По разбору мода зеркал смещение на SE такое же |
| Размер `AE#40845` | `Hooks.cpp`, `FromLinkedPreload` | `0x18E` | Размер из `.pdata` или сигнатура |
| Тип накопителя | `Portal.cpp`, `kAccumulatorType` | `0x63` | Проверить на SE |
| Техника эффектов | `Portal.cpp`, `kEffectTechniqueBase`, `kSoftEffectBit` | `0x4000002C`, бит 18 | Проверить на SE, вынести в NG |
| Флаги света | `Portal.cpp`, `kLightInFrustum`, `kLightOutOfFrustum` | `0`, `0xFF` | Проверить на SE |
| Режим глубины | `Portal.cpp`, `kDepthTestWrite` | `3` | Перечисление NG, если появится |
| Сброс проходов | `Portal.cpp`, `kRebuildPasses` | `0x7FFFFFFF` | Проверить на SE |
| Ячейки рендера | `[Portal] iColorSlot`, `iDepthSlot` | `17`, `9` | Свой выбор свободной ячейки или вообще без подмены ячеек |
| Индекс группы | `Portal.cpp`, `kRefractionNormalsGroup`, `kGeometryGroups` | `5`, `16` | В разборе мода зеркал так на SE и AE, на VR групп 19 |
| Безымянные поля | `Portal.cpp` | `BSAnimationUpdateData::unk2A`, `unk2C`; `BSFadeNode::unk144`; `NiParticleSystem::unk180`, `unk188` | Назвать в NG, сверить на SE |
| Звук у слушателя | `Hooks.cpp`, `SoundEmitterPosition::kAtListener` | Бит 10 в `BSGameSound::soundType` | Назвать в NG, сверить на SE |

### Индексы виртуальных функций

Все в `Hooks.cpp`, `Hooks::Install`. Для VR значения могут отличаться, для SE нужна сверка.

| Класс | Индексы |
|---|---|
| `TESObjectDOOR` | `0x37` |
| `GridCellArray` | `1`–`8` |
| `CellLoaderTask` | `1`, `2`, `3`, `4`, `6` |
| `PlayerCharacter` | `0xAD` |
| `NiNode`, `BSFadeNode`, `BSMultiBoundNode`, `BSMultiBoundRoom`, `BSTriShape`, `BSDynamicTriShape`, `BSSubIndexTriShape` | `0x34` |
| `BSEffectShader` | `2` |
| `BSEffectShaderProperty` | `0x2A` |
| `Character` | `0x79`, `0xAD` |
| `HighActorCuller` | `1` |
| `BSXAudio2GameSound` | `0x0F` |

## 3. Номера Address Library

SE и AE нумеруются отдельно. Без SE-номера код написан как `REL::ID(ae)`, и на SE он работать не может.

**SE-номер известен, код ещё пишет `REL::ID`.** Нужно только заменить на `RELOCATION_ID(se, ae)`:

| AE | SE | Что |
|---|---|---|
| 36559 | 35560 | `RenderPlayerView` |
| 69353 | 67992 | `JobList::Finish` |
| 106385 | 99741 | `ShadowSceneNode::SetPortalGraph` |
| 107142 | 100424 | `Main::RenderWorld` |

`RELOCATION_ID` уже стоит у `AE#106600` (SE 99955), `AE#77463` (SE 75657, упаковщик частиц) и `AE#19823` (SE 19396, `GetDistance`).

**SE-номер неизвестен (37 штук):**

| Модуль | AE-номера |
|---|---|
| `Hooks.cpp` | 13291, 13325, 13370, 13419, 13434, 13721, 18547, 19130, 19384, 19824, 36609, 37277, 37348, 37350, 40845, 41466, 43690, 61410, 74651, 77851, 78274, 105486 |
| `InteriorSim.cpp` | 18890, 18900, 18976, 19046, 19063, 19130, 77905, 77906, 400333 |
| `Portal.cpp` | 18966, 41496, 105643, 106342, 388630, 400333, 411270 |
| `Preload.cpp` | 18540 |

**Как получить SE-номера.** Нужны `SkyrimSE.exe` 1.5.97 и `version-1-5-97-0.bin`. На диске пользователя их нет. Дальше функции сопоставляются инструментами из `tools/`: по таблицам виртуальных функций, строкам и вызывающим. Часть номеров есть в NG и в исходниках мода зеркал: это пары `RELOCATION_ID(se, ae)` и `VariantID(se, ae, vr)`.

## 4. Что уже известно о других версиях

Из исходников Realistic Reflections - Mirrors (факты о бинарниках, код не копируется, см. раздел 5.15 в `CLAUDE.md`):
- мод работает на SE 1.5.97, AE 1.6.1170, GOG 1.6.1179, AE 1.7.104 и VR 1.4.15;
- окно вокруг границы списков сцены стоит на `RenderPlayerView + 0x16F` на SE, 1.6.1170 и 1.7.104, на VR на `+0x197`;
- `BSShaderAccumulator`: `batchRenderer` на `+0x130`, на VR на `+0x158`;
- `BSBatchRenderer::renderPass` хранит записи `PassGroup` прямо в массиве, хотя NG объявляет указатели.

Проверено на 1.6.1170 самостоятельно: `RendererShadowState` (`AE#388819`) + `0x35C` = `AE#388909`, `+0x380` = `AE#388915`, `+0x390` = `AE#388918`. Упаковщик частиц `AE#77463` читает именно эти поля.

## 5. Уже убрано

| Что | Версия | Как |
|---|---|---|
| `bhkWorld + 0xC5F9` | 0.8.22 | Поле NG `bhkWorld::toggleCollision` |
| Ручной вызов `AE#18995` | 0.8.22 | `TESObjectCELL::GetbhkWorld()` |
| Поля камеры упаковщика | 0.8.23 | Адреса полей NG `posAdjust` и `cameraData`, числовых сдвигов нет. Перехват — detour по ID, без смещения внутри `AE#107637` |
