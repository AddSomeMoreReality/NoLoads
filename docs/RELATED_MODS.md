# Похожие моды и проекты

Поиск от 02.11.2026 по GitHub, Nexus и форумам. Цель: найти, кто ещё рисует вторую камеру, держит клетку без игрока или проводит звук между клетками.

**Что я смог прочитать.** GitHub-страницы и результаты поиска. Страницы Nexus отвечают 403, поэтому о модах Nexus я знаю только то, что попало в поисковую выдачу, а архивы не скачивал.

**Лицензии.** Код из GPL-проектов нельзя просто копировать в мод с другой лицензией. Идеи и факты о бинарнике брать можно. Правило для Realistic Reflections — в `CLAUDE.md`, 5.15.

## Что мне нужно от вас

Просмотрите раздел 1 и решите, что скачать. Положите архивы или исходники в `E:\Games\Skyrim\ModCreation\references\`, я разберу их так же, как Realistic Reflections. Самое полезное сейчас — **Audio Occlusion**: это готовый ответ на вопрос, как плагин находит источник и слушателя звука.

## 1. Стоит скачать

| Проект | Где | Зачем нам | Что нужно |
|---|---|---|---|
| Audio Occlusion | [Nexus SE 184748](https://www.nexusmods.com/skyrimspecialedition/mods/184748) | SKSE-плагин, который глушит голоса и звуки окружения за стенами. Решает нашу соседнюю задачу: звук через дверь, а не сквозь стены. В истории изменений есть работа с переходом через двери и границы клеток | Архив. Если в нём нет исходников, ссылка на них со страницы |
| Audio Occlusion - Universal Support | [Nexus SE 185674](https://www.nexusmods.com/skyrimspecialedition/mods/185674) | Форк с дифракцией через стены, полы и потолки. По описанию, на 1.5.97 работают только голоса, что полезно для переносимости | Архив |
| TrueThroughScope | [Nexus FO4 100201](https://www.nexusmods.com/fallout4/mods/100201) | Вторая камера в прицеле на том же движке, стоимость 25–45% FPS. На странице есть ссылка `[github]` | Ссылка на GitHub со страницы: найти её поиском я не смог |
| Fallout 4 Bodycam and double-rendered scopes | [Nexus FO4 110388](https://www.nexusmods.com/fallout4/mods/110388) | Второй рендер только в линзе, включается настройкой. Близко к нашему проходу | Архив или ссылка на исходники |
| Commonwealth Bodycam - F4SE | [Nexus FO4 110692](https://www.nexusmods.com/fallout4/mods/110692) | Вероятно, того же автора или того же приёма: отдельный вид с другой камеры. Не проверял | Посмотреть страницу |

## 2. Исходники открыты, могу разобрать сам

| Проект | Где | Лицензия | Зачем нам |
|---|---|---|---|
| Fo4VR True Scopes | [GitHub](https://github.com/rollingrock/Fo4VR-True-Scopes) | GPL-3.0, CommonLibF4 | Рендер в текстуру на родственном движке. Отключает себя при сбое с тремя повторами, как наш `portal.fault`. Обновляет линзу раз в N кадров, стоимость зависит от содержимого пирамиды: около 9 мс в плотной сцене |
| Community Shaders | [GitHub](https://github.com/community-shaders/skyrim-community-shaders) | GPL-3.0, проверить | Функция Skylighting рисует сцену в проход окклюзии осадков с накопителем и камерой. Это второй вид, сделанный на уже имеющемся проходе игры. PR #2971 упоминает `BSShaderAccumulator::RenderPersistentPassList`, отдельный список геометрии, который мы не разбирали |
| CommonLibSSE-NG | [v15.1.0](https://github.com/alandtse/CommonLibSSE-NG/releases/tag/v15.1.0) | MIT | У нас v13.1.0. В новых версиях названы поля `BSShaderAccumulator`, добавлены средства доступа к рендеру (#460), исправлены раскладки отсечения и порталов (#420). Обновление может убрать часть наших сдвигов |
| SeverActions | [GitHub](https://github.com/Severause/SeverActions/releases/tag/v0.90.1) | Не проверял | SKSE-плагин, водит NPC через клетки: пакеты каждый тик, снимок координат при выгрузке клетки, отключение столкновений NPC в пути. Прямо связано с Идолафом |
| NPC Pathing NG | [GitHub](https://github.com/ShugokiFable/Modern-NPC-Pathing/releases/tag/v2.5.0) | Не проверял | Обновляет логику по клеткам, полезно для уровней симуляции |
| SimplyKnock | [GitHub](https://github.com/bitnimble/SimplyKnock) | Не проверял | Простая работа со связанными дверями |
| SmoothCam, FreeCameraFramework | [SmoothCam](https://github.com/mwilsnd/SkyrimSE-SmoothCam), [FCF](https://github.com/staalo18/FreeCameraFramework) | Разные | Работа с `NiCamera`. Предупреждение из истории SmoothCam: правка всех `NiCamera` ломала отсечение и тени |

## 3. Подход другой, но полезно знать

| Проект | Подход | Что из этого следует |
|---|---|---|
| [No Loading Screen Breezehome](https://www.nexusmods.com/skyrim/mods/109103) | Интерьер перенесён в мировое пространство, один дом | Работает, но вручную. Наш 0.8.5 показал, почему не годится в общем случае: интерьер больше здания |
| Open Cities Skyrim | Города перестроены внутри основного мира | [По отзывам](https://stepmodifications.org/forum/topic/18862-open-cities-alternatives-no-gates/), несовместим почти со всем. Порталы как раз обходят эту проблему: данные игры не меняются |
| Starfield: Seamless City Interiors | [Копирует данные интерьера в мир](https://www.dsogaming.com/mods/starfield-just-got-a-must-have-seamless-city-interiors-mod/) | Тот же приём и те же конфликты |
| Starfield: Neon Core Disguised Seamless | [Скрывает загрузку приёмом лифтов Fallout 4](http://starfieldunity.com/mods/2450/neon-core-disguised-seamless-project) | Идея для шага 6: чем закрыть момент смены клетки |
| OpenMW | [Запрос #6602 «порталы»](https://gitlab.com/OpenMW/openmw/-/issues/6602) | Нигде в поиске не реализовано. Там же названы те же трудности: интерьеры больше зданий, нужна неевклидова связь. Состояние запроса не проверял |
| Oblivion Reloaded | [Nexus](https://nexusmods.com/oblivion/mods/51032) | Расширение рендера без правки exe, более старый движок. Прямой пользы не вижу |
| Skyrim Together Reborn, SkyMP | [TiltedEvolution](https://github.com/tiltedphoques/TiltedEvolution) | Копии игроков в чужой клетке. Как они живут с Havok, найти не удалось |
| Faster Loadscreens | [Nexus SE 182852](https://www.nexusmods.com/skyrimspecialedition/mods/182852) | Только ускоряет загрузку |

## 4. Вывод

- **Готового аналога нет.** Ни для Skyrim, ни для родственных движков поиск не нашёл живого вида в другую клетку через дверь. На Fallout 4 есть второй вид в прицелах, на Skyrim — Realistic Reflections.
- **Симуляция клетки без игрока** нигде публично не описана. Здесь мы, похоже, идём первыми.
- **Самая практичная пара источников сейчас:** Audio Occlusion для звука и Community Shaders для прохода со второй камерой.
