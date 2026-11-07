# Ghidra: база SkyrimSE.exe и инструменты псевдокода

Куратор базы — отдельный чат (T33). Только он меняет проект Ghidra (имена, типы, комментарии) и чинит `tools/ghidra/`. Исполнители базу только читают через `gq.py`. Нужно имя, тип или разбор функции — запрос куратору через пользователя.

## Правила

- **Проект, псевдокод и экспорт C — код Bethesda.** В git и в `docs/` не класть: только выводы, адреса и короткие цитаты инструкций. Вывод `gq.py` — в свой scratchpad. В `.gitignore`: `*.gpr`, `*.rep/`, `*.gbf`, `*.gzf`, `ghidra_out/`.
- Имена из документов проекта — только комментарии (`[clr] docs AE#…`), имя NG они не заменяют.
- Сервер держит около 0,7 ГБ ОЗУ и сам выходит через 20 мин простоя; `gq.py stop` освобождает сразу.

## Где что

| Что | Где |
|---|---|
| Ghidra 12.2, JDK 25 | `E:\Ghidra\ghidra_12.2_PUBLIC`; Java выбрана в окне Ghidra и запомнена в `%APPDATA%\ghidra\ghidra_12.2_PUBLIC\java_home.save` (`D:\Java`), `launch.properties` не менялся |
| PyGhidra | venv `E:\Ghidra\pyghidra-venv` (pyghidra 3.2.0, JPype 1.5.2 из `Ghidra\Features\PyGhidra\pypkg\dist`) |
| Основной проект (куратор) | `E:\Games\Skyrim\ModCreation\My attempts\SkyrimSE-1.6.1170\` (1,5 ГБ), программа `/SkyrimSE.exe` |
| Копия для чтения (сервер) | `...\My attempts\SkyrimSE-1.6.1170-ro\` — делает `snapshot.py` |
| Журналы сервера, имена, отчёты | `%TEMP%\clr-ghidra\` (`server.log`, `names-1.6.1170.json`, `*.report.txt`) |
| Пути и версии | `tools/ghidra/gconfig.py`, переопределяются переменными `CLR_GHIDRA_*` |

## Исполнителю: `tools/ghidra/gq.py`

Любой Python 3, без зависимостей. Цели: `AE#13316`, `AE#13316+0x65C`, `0x14019D490` (VA), `0x19D490` (RVA), имя `TES::SetCurrentCell`.

```
python tools/ghidra/gq.py decomp AE#13316+0x65C AE#40466 --out <scratchpad>   # псевдокод; строка с +0x65C помечена //>>>
python tools/ghidra/gq.py decomp AE#13316 --addr --out <scratchpad>           # у каждой строки смещение инструкции
python tools/ghidra/gq.py asm AE#19130 --out <scratchpad>                     # дизассемблер со смещениями и именами
python tools/ghidra/gq.py xrefs AE#40466 --out <scratchpad>                   # вызовы, слоты vtable, чтения/записи данных
python tools/ghidra/gq.py info AE#36629 AE#40411                              # имя, размер, число вызывающих
python tools/ghidra/gq.py fieldrefs 0x1D0 --users-of AE#403521 --out <sp>     # [reg+0x1D0] в функциях, читающих игрока
python tools/ghidra/gq.py fieldrefs 0x6AA --all --access W --out <sp>         # весь код по байтам, смещения >= 0x80
python tools/ghidra/gq.py status | stop
```

- Первый вызов поднимает сервер (`gserver.py`): около 8 с. Дальше `decomp` 0,1–0,7 с, `xrefs` до 1 с, `fieldrefs --users-of AE#403521` 9 с (1 254 функции), `--all` 0,7 с.
- Запросы нескольких чатов идут в очередь по одному; ответ печатает время в Ghidra и в очереди.
- Заголовок файла псевдокода: адрес, все `AE#`, имя, сигнатура, комментарии куратора (`[clr] ng: файл NG`, `[clr] curated: …`, `[clr] docs …`), строки файла с пометками.
- `fieldrefs` ищет операнд `[рег + смещение]` без учёта типа — это кандидаты, проверяй псевдокодом. `R`/`W` — чтение/запись, `A` — `lea`.
- Источник имени — в `info` и заголовке. Надёжны имена NG, `curated` и `AE_<id>`. Имена анализа Ghidra (66 723: Function ID, разбор исключений) верны для STL/CRT/XAudio2, но на коде Bethesda бывают ложными; `FID_conflict:…` — неоднозначные.
- Безымянные функции названы `AE_<id>`; вызовы через vtable в псевдокоде остаются `(**(code **)(*p + 0x1c0))`: слот = смещение / 8.

## Куратору

Правки — только скриптами, повторяемыми с нуля:
1. Анализ: `powershell -File tools\ghidra\analyze.ps1 -LogDir <scratchpad>` (окно Ghidra закрыто; 12 мин, куча 10 ГБ). Два вызова `analyzeHeadless`:
   - импорт `-noanalysis` с `scripts\ClrAnalysisOptions.java`: компилятор `visualstudio:unknown` (загрузчик PE оставляет `unknown`, и тогда анализаторы RTTI и исключений MSVC не запускаются) и выключенный **Create Address Tables** (в окне с кучей 8 ГБ он упал с `OutOfMemoryError`, 05.11.2026);
   - `-process` в новом процессе: набор анализаторов выбирается при открытии программы, уже с MSVC.
2. Имена: `python tools/ghidra/collect_names.py` (20 с), затем `E:\Ghidra\pyghidra-venv\Scripts\python.exe tools/ghidra/apply_names.py` (окно закрыто; 10–40 с).
3. Копия для чтения: `python tools/ghidra/snapshot.py` (останавливает сервер, секунды).

- **Свои выводы** (имя по разбору) — строкой в `tools/ghidra/curated_names.txt` (в git), затем шаги 2–3. Ручное имя в окне Ghidra (`USER_DEFINED`) `apply_names.py` не трогает, но `analyze.ps1` его сотрёт.
- Окно Ghidra: открыть основной проект, после правок сохранить и закрыть, затем шаг 3.

## Имена: источники и приоритет

`collect_names.py` разбирает `lib/commonlibsse-ng` основной папки (NG v13.1.0, MIT), документы и Address Library:

1. **curated** — `curated_names.txt`.
2. **ng-reloc** — `RELOCATION_ID(se, ae)` в функциях NG → `Класс::Метод`; тип `decltype(&X::Y)` даёт `X::Y`. Данные: `Класс::singleton`, иначе `Класс::Метод_переменная`. `REL::ID` без пары SE/AE пропускается.
3. **ng-vfunc** — виртуальные методы заголовков NG (комментарий слота `// 98`, `// SE/AE 0x82`) через `VTABLE_*[0]`. Имя — самого базового класса, у которого в слоте та же функция; общие для разных методов функции (ICF) без имени.
4. **ng-table** — `VTABLE_*`, `RTTI_*`, `NiRTTI_*`: сверяются с RTTI Ghidra, метка — только если у Ghidra её нет.
5. **AE_<id>** — метка на каждый адрес Address Library; функция без имени получает имя `AE_<id>`.
6. **docs** — `` `Класс::Метод` `` вплотную к `AE#id` в `docs/*.md` и комментариях `src/` → плашка-комментарий.

Счёт и конфликты — в отчётах `%TEMP%\clr-ghidra\names-*.report.txt`, `apply-*.report.txt`; сводка — `docs/tasks/results/T33.md`.

## Другие версии (5.14, п. 6)

`gconfig.VERSIONS` — запись на сборку: `runtime` (`se`/`ae` — половина `RELOCATION_ID` и префикс меток `SE_`/`AE_`), `.exe`, таблица Address Library (`version-1-5-97-0.bin` — формат 1, `versionlib-*.bin` — формат 2), имя проекта. `analyze.ps1` принимает пути параметрами. Имена из документов есть только для AE.
