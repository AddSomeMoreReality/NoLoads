# T36. Типы NG в базе Ghidra: структуры, vtable, `this` и синглтоны

Этап 2, вспомогательная. Исполнитель — куратор базы. Ветка `task/T36-ghidra-types`. Код: `tools/ghidra/collect_types.py`, `apply_types.py`. Прогона нет. После T35, до T38/T39: карты игрока и загрузки ячеек читаются в разы легче с полями вместо `*(param_1 + 0xc0)`.

## Что есть

- Заголовки NG (`lib/commonlibsse-ng`, v13.1.0): поля с комментариями смещений (`/* 1114 */`, `// 0C0`), `static_assert(sizeof(X) == …)`, базовые классы. Раскладки с отличиями SE/AE — `RUNTIME_DATA`, `GetAERuntimeData()`. У `TES` поля дальше `0x128` в AE сдвинуты (`CLAUDE.md`, раздел 7).
- `collect_names.py` уже разбирает области видимости NG (классы, базы, виртуальные методы со слотами). Парсер C в Ghidra шаблоны C++23 NG не возьмёт.

## Сделать

1. **Структуры:** свой разбор NG → JSON (класс, размер для AE, базы, поля: смещение, тип, имя). Источник смещения — комментарий. Если его нет, вычислить и сверить с `static_assert`. Неизвестные типы — `undefined`/массив байт нужной длины.
   - Первыми: `TES`, `TESForm` → `TESObjectREFR` → `Actor` → `Character` → `PlayerCharacter`, `TESObjectCELL`, `TESWorldSpace`, `NiAVObject`/`NiNode`, `ProcessLists`, `PlayerCamera`, `Main`, `BSTEventSource<T>`.
   - Потом все.
2. **Vtable:** структура слотов по виртуальным методам NG (`SetParentCell` в слоте `0x98`), поле `vtable` у класса. Тогда вызов через vtable в псевдокоде получает имя.
3. **Применение** (`apply_types.py`, PyGhidra, повторяемо):
   - категория `/NG` в менеджере типов;
   - `this` у методов с именем NG/curated (`__thiscall`, `Class*`);
   - типы синглтонов (`PlayerCharacter::singleton` — `PlayerCharacter*`, `TES::singleton` — `TES*` и т. д.);
   - сигнатуры из `decltype` NG там, где они есть.
4. **Проверка:**
   - псевдокод `AE#13316` показывает поля `TES` (`interiorCell` на `+0xC0`, `gridCells`, `interiorBuffer`);
   - `AE#40466` — поля `PlayerCharacter`;
   - размеры совпадают с `static_assert`.
   - Список классов, где расходятся.
5. **Отчёт:** число структур и полей, методы с `this`, расхождения размеров, время. Обновить `docs/GHIDRA.md`.

## Правила

Типы — выводы из NG (MIT), в git можно JSON-описание, если нужно. Псевдокод — нет. После применения — `snapshot.py`.

## Результат

`docs/tasks/results/T36.md`.

## Конфликты

T34, T35 — по очереди в чате куратора.
