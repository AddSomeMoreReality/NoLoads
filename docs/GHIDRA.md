# Ghidra: база SkyrimSE.exe и инструменты псевдокода

Куратор базы — отдельный чат (T33, T35). Только он меняет проект Ghidra (имена, типы, комментарии) и чинит `tools/ghidra/`. Исполнители базу только читают: MCP-инструменты `ghidra_*` или `gq.py`. Нужно имя, тип или разбор функции — запрос куратору через пользователя.

## Правила

- **Проект, псевдокод и экспорт C — код Bethesda.** В git и в `docs/` не класть: только выводы, адреса и короткие цитаты инструкций. Вывод `gq.py` — в свой scratchpad. В `.gitignore`: `*.gpr`, `*.rep/`, `*.gbf`, `*.gzf`, `ghidra_out/`.
- Имена из документов проекта — только комментарии (`[clr] docs AE#…`), имя NG они не заменяют.
- Сервер держит 0,4–1,2 ГБ ОЗУ (растёт с индексами `search` и числом разобранных функций) и сам выходит через 20 мин простоя; `gq.py stop` освобождает сразу.

## Где что

| Что | Где |
|---|---|
| Ghidra 12.2, JDK 25 | `E:\Ghidra\ghidra_12.2_PUBLIC`; Java выбрана в окне Ghidra и запомнена в `%APPDATA%\ghidra\ghidra_12.2_PUBLIC\java_home.save` (`D:\Java`), `launch.properties` не менялся |
| PyGhidra | venv `E:\Ghidra\pyghidra-venv` (pyghidra 3.2.0, JPype 1.5.2 из `Ghidra\Features\PyGhidra\pypkg\dist`) |
| Основной проект (куратор) | `E:\Games\Skyrim\ModCreation\My attempts\SkyrimSE-1.6.1170\` (1,5 ГБ), программа `/SkyrimSE.exe` |
| Копия для чтения (сервер) | `...\My attempts\SkyrimSE-1.6.1170-ro\` — делает `snapshot.py` |
| Журналы сервера, имена, отчёты | `%TEMP%\clr-ghidra\` (`server.log`, `mcp.log`, `names-1.6.1170.json`, `*.report.txt`) |
| Кэш псевдокода и индексы поиска | `%TEMP%\clr-ghidra\cache\<версия>-<снимок>\` (снимок — хэш файлов базы копии; новая копия — новая папка) |
| Вывод MCP-инструментов | `%TEMP%\clr-ghidra\out\mcp-<дата>-<pid>\`, хранится 7 дней |
| Пути и версии | `tools/ghidra/gconfig.py`, переопределяются переменными `CLR_GHIDRA_*` |

## Исполнителю: MCP `ghidra` и `tools/ghidra/gq.py`

Цели: `AE#13316`, `AE#13316+0x65C`, `0x14019D490` (VA), `0x19D490` (RVA), имя `TES::SetCurrentCell[+0x10]`.

| Вопрос | MCP | CLI `python tools/ghidra/gq.py …` |
|---|---|---|
| Псевдокод, строка с `+0x65C` помечена `//>>>` | `ghidra_decompile(targets, marks?, addr_prefix?, max_lines?)` | `decomp AE#13316+0x65C [--addr] --out <sp>` |
| Дизассемблер со смещениями и именами | `ghidra_asm` | `asm AE#19130 --out <sp>` |
| Вызовы, слоты vtable, чтения/записи данных | `ghidra_xrefs` | `xrefs AE#40466 --out <sp>` |
| Имя, размер, число вызывающих | `ghidra_info` | `info AE#36629 AE#40411` |
| `[reg+0x1D0]` в функциях, читающих игрока | `ghidra_fieldrefs(offset, users_of / within / all, access)` | `fieldrefs 0x1D0 --users-of AE#403521 --out <sp>` |
| Символы по regex имени, строки, места ссылок | `ghidra_search(name_regex / string_regex, kinds?)` | `search --name SetParentCell --string "^sLoad"` |
| Дерево вызовов вверх или вниз | `ghidra_callgraph(target, callers/callees, depth, max_nodes)` | `callgraph AE#13316 --callers --depth 2` |
| Слоты класса: свой, унаследованный, переопределённый | `ghidra_vtable(class, offset?)` | `vtable PlayerCharacter` |
| Раскладка типа | `ghidra_struct(name)` — типы NG после T36 | `struct RTTICompleteObjectLocator` |
| Состояние сервера | `ghidra_status` | `status`, `stop` |

- **MCP** (`tools/ghidra/mcp_server.py`, stdio, stdlib): короткая функция возвращается текстом, длинная — файлом и выдержкой ±10 строк вокруг пометок с номерами строк файла (дочитывать Read с `offset`). Ответ печатает время в Ghidra и ожидание декомпилятора. Шаблоны подключения — `tools/ghidra/claude/` (`mcp.json`, навык `skills/ghidra`, агенты `re-reader` на Sonnet и `re-reader-fast` на Haiku); в `.mcp.json` и `.claude/` их ставит пользователь (см. ниже).
- **CLI** — тот же сервер; `--out` — свой scratchpad.
- Первый вызов поднимает сервер (`gserver.py`): 7–8 с. Запросы идут параллельно; декомпиляторов в пуле 3 (`CLR_GHIDRA_DECOMPILERS`), псевдокод кэшируется в памяти и на диске по адресу функции и снимку базы.
- Время (T35, тёплый сервер): MCP добавляет 20–30 мс; `decomp` 0,1–0,7 с, из кэша 0,02–0,05 с; 48 функций без кэша — 3,5 с по одной, 1,5 с в 3 потока; `search` — 6 с на первый индекс имён или строк, дальше 0,1 с; `vtable PlayerCharacter` (14 таблиц) — 0,2 с; `xrefs` до 1 с; `fieldrefs --users-of AE#403521` — 9 с.
- Сервер от старых инструментов (протокол ниже `gq.PROTOCOL`) новый клиент останавливает и поднимает заново.
- Заголовок файла псевдокода: адрес, все `AE#`, имя, сигнатура, комментарии куратора (`[clr] ng: файл NG`, `[clr] curated: …`, `[clr] docs …`), строки файла с пометками.
- `fieldrefs` ищет операнд `[рег + смещение]` без учёта типа — это кандидаты, проверяй псевдокодом. `R`/`W` — чтение/запись, `A` — `lea`.
- `callgraph`: вниз — только прямые вызовы и хвостовые `jmp`, косвенные (vtable) считаются; вверх — вызовы, `jmp` и слоты vftable, где лежит функция. `search` не ищет метки `AE_<id>` (для них есть `AE#id`); ссылки из таблиц исключений (`.pdata`, `UNWIND_INFO`) не считает.
- `vtable` читает RTTI MSVC: `CompleteObjectLocator` → смещение подобъекта, `ClassHierarchyDescriptor` → базы. Слот сравнивается с первичной таблицей базы на том же смещении: `inherited`, `override of …`, `new in …`.
- Источник имени — в `info` и заголовке. Надёжны имена NG, `curated` и `AE_<id>`. Имена анализа Ghidra (66 723: Function ID, разбор исключений) верны для STL/CRT/XAudio2, но на коде Bethesda бывают ложными; `FID_conflict:…` — неоднозначные.
- Безымянные функции названы `AE_<id>`; вызовы через vtable в псевдокоде остаются `(**(code **)(*p + 0x4c0))`: слот = смещение / 8, смотри `vtable`.

### Подключение MCP (пользователь)

Сейчас файлы не установлены: `.mcp.json` действует на все чаты проекта, ставит его пользователь.

- Только для одной папки, без git: `claude mcp add --scope local ghidra -- python tools/ghidra/mcp_server.py` в корне worktree.
- Для всех чатов (после слияния в общую ветку) — в корне репозитория:
  `Copy-Item tools\ghidra\claude\mcp.json .mcp.json; New-Item -ItemType Directory -Force .claude | Out-Null; Copy-Item -Recurse -Force tools\ghidra\claude\skills, tools\ghidra\claude\agents .claude\`
- Claude Code спросит одобрение сервера `ghidra` при первом запуске; список и состояние — `/mcp`. Новый `.mcp.json` подхватывает только новый чат.
- Проверка: `python tools/ghidra/mcp_server.py --selftest` — запускает MCP-сервер дочерним процессом, вызывает инструменты и 3 декомпиляции параллельно.

### Как добавить инструмент

1. Метод класса `Ghidra` в `gserver.py`, возвращающий `dict(text=…)` (длинный текст — `self.write_text(out, имя, строки, предел)`), и ветка в `dispatch()`. Методы работают параллельно и только читают программу; общие индексы строятся под `self.index_lock`, декомпилятор — только через `decompiled_rows()`.
2. Подкоманда в `gq.py` и запись в `TOOLS` в `mcp_server.py` (имя `ghidra_…`, описание, JSON-схема, обработчик; для простого текста — `t_text("cmd", поле="аргумент")`).
3. Если старый сервер команды не знает — поднять `PROTOCOL` в `gserver.py` и `gq.py`. Если меняется вид строк кэша псевдокода — `CACHE_FORMAT`.
4. `python tools/ghidra/gq.py stop`, затем `--selftest` и вызов новой команды.

## Куратору

Правки — только скриптами, повторяемыми с нуля:
1. Анализ: `powershell -File tools\ghidra\analyze.ps1 -LogDir <scratchpad>` (окно Ghidra закрыто; 12 мин, куча 10 ГБ). Два вызова `analyzeHeadless`:
   - импорт `-noanalysis` с `scripts\ClrAnalysisOptions.java`: компилятор `visualstudio:unknown` (загрузчик PE оставляет `unknown`, и тогда анализаторы RTTI и исключений MSVC не запускаются) и выключенный **Create Address Tables** (в окне с кучей 8 ГБ он упал с `OutOfMemoryError`, 05.11.2026);
   - `-process` в новом процессе: набор анализаторов выбирается при открытии программы, уже с MSVC.
2. Имена: `python tools/ghidra/collect_names.py` (20 с), затем `E:\Ghidra\pyghidra-venv\Scripts\python.exe tools/ghidra/apply_names.py` (окно закрыто; 10–40 с).
3. Копия для чтения: `python tools/ghidra/snapshot.py` (останавливает сервер, секунды). Кэш псевдокода и индексы старой копии остаются в `%TEMP%\clr-ghidra\cache\` под старым снимком; их можно удалить.

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
