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
| Раскладка типа, таблица слотов | `ghidra_struct(name)` | `struct TES`, `struct PlayerCharacter_vftable` |
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
- Безымянные функции названы `AE_<id>`.
- **Типы NG (T36):** классы этой сборки лежат в `/NG/RE` (5 433 структуры `RE::`, смещения — раскладка MSVC сборки «только AE»). У методов с именами NG `this` типизирован, поля названы (`this->interiorCell`), вызов через типизированный объект — `this->vfptr->SetParentCell`. Поля `unkXXX` — пробелы самого NG. Через нетипизированный указатель вызов остаётся `(**(code **)(*p + 0x4c0))`: слот = смещение / 8, смотри `vtable`.

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
2. Имена: `python tools/ghidra/collect_names.py` (5–20 с), затем `E:\Ghidra\pyghidra-venv\Scripts\python.exe tools/ghidra/apply_names.py` (окно закрыто). Повтор на готовой базе — 30 с; на свежем анализе фаза 0 создаёт около 12 600 функций и идёт до 1,5 ч (каждая новая функция перестраивает соседние тела).
3. Типы NG (T36): `python tools/ghidra/collect_types.py` (MSVC, 30 с), затем `E:\Ghidra\pyghidra-venv\Scripts\python.exe tools/ghidra/apply_types.py` (окно закрыто; 35 с). Подробно — ниже.
4. Копия для чтения: `python tools/ghidra/snapshot.py` (останавливает сервер, секунды). Кэш псевдокода и индексы старой копии остаются в `%TEMP%\clr-ghidra\cache\` под старым снимком; их можно удалить.

- **Свои выводы** (имя по разбору) — строкой в `tools/ghidra/curated_names.txt` (в git), затем `apply_names.py --project-dir <проект>-pre-types`, `apply_types.py --restore`, `snapshot.py`: копия без типов должна знать новые имена, иначе `--restore` их потеряет. Ручное имя в окне Ghidra (`USER_DEFINED`) `apply_names.py` не трогает, но `analyze.ps1` его сотрёт.
- Окно Ghidra: открыть основной проект, после правок сохранить и закрыть, затем шаг 4.
- **Проверки (T34, только чтение основного проекта, окно закрыто):**
  - `check_base.py [--ref-id 403521]` (1–2 мин): RIP-ссылки на ID по байтам против ссылок Ghidra, начала `.pdata` без функций, код вне функций, `FUN_`, имена по источникам, 500 самых вызываемых функций без имени;
  - `check_docs.py`: утверждения `AE#a+0xNN` → / call / `<` / из `AE#b` в `docs/*.md` против инструкции в базе. Документы не правит, список — `%TEMP%\clr-ghidra\check-docs-<версия>.txt`.

## Типы NG (T36)

Компилятор — парсер: заголовки NG собираются MSVC **для одного runtime** (`ENABLE_SKYRIM_AE` для 1.6.x, `ENABLE_SKYRIM_SE` для 1.5.97). Тогда NG раскладывает классы ровно под него (`TES` = 0x2C0 с полями, которые сборка SE+AE прячет за `GetRuntimeData()`), и каждый `STATIC_ASSERT_SIZE` проверяет размер этого runtime. Флаги и пути — из `build/.deps` сборки плагина основной папки (собрать плагин хотя бы раз), MSVC — `gconfig.VCVARS`.

- `collect_types.py` → `%TEMP%\clr-ghidra\types\<версия>\`:
  - проход 1 — `/d1reportAllClassLayout`: размеры, базы, vftable с именами методов (16 087 классов, 3 005 таблиц);
  - проход 2 — DLL с функцией на каждый класс `RE::` (3 228) → `NG.pdb` с полными типами полей;
  - из заголовков (парсер `collect_names.py`) — статические методы (548) и синглтоны (80: где указатель, где сам объект).
- `apply_types.py` (основной проект; первый запуск копирует его в `<проект>-pre-types`, повтор — `--restore`, потому что Ghidra не удаляет `/NG`, когда типы уже используются):
  1. загрузчик PDB Ghidra, «только типы», раскладка с иерархией классов → `/NG`; предпочтительный корень пространств имён программы — `/NG/RE`, поэтому класс `TES` находит структуру для `this`;
  2. безымянные базы → `super_<База>`; заглушка NG `_padPlayerRuntimeData` (0x3E0, 0x808) → `PLAYER_RUNTIME_DATA` того же размера;
  3. `/NG/vftables/<Класс>_vftable` (1 793 таблицы, 31 495 слотов, имена — из отчёта MSVC); у классов со своей первичной таблицей (1 435) цепочка первичных баз развёрнута в плоские поля, и `vfptr` на +0 указывает на таблицу класса;
  4. `__thiscall` — функциям в пространстве имён класса со структурой в `/NG/RE`: виртуальным (6 910) и с именем NG/curated, кроме статических (479);
  5. синглтоны — 79 (`TES` → `TES*`, `PlayerCharacter` → `PlayerCharacter*`, `BSTimer` — сам объект); `RenderTargetManager` — объект, вопреки NG (`SINGLETON_DEPTH_FIX`, `CLAUDE.md`, раздел 7);
  6. сверка: все 5 433 структуры `RE::` равны размерам MSVC.
- Раскладка AE в NG — для 1.6.629–1.6.1170: у `PlayerCharacter` нет базы 1.7.99 (`BSTEventSink<BSSystemEvent>`), у `TES` — блок 1.6.1130+ (`AE_RUNTIME_DATA`). Для 1.7.x нужна своя запись в `gconfig.VERSIONS` и проверка.
- Не сделано: `SkyrimVM::_padVMRuntimeData` (0x554) не равна `VM_RUNTIME_DATA` (0x558) — оставлена заглушкой; сигнатуры из `decltype` NG (типы параметров) не применяются.

## Имена: источники и приоритет

`collect_names.py` разбирает `lib/commonlibsse-ng` основной папки (NG v13.1.0, MIT), документы и Address Library:

1. **curated** — `curated_names.txt`.
2. **ng-reloc** — `RELOCATION_ID(se, ae)` в функциях NG → `Класс::Метод`; тип `decltype(&X::Y)` даёт `X::Y`. Данные: `Класс::singleton`, иначе `Класс::Метод_переменная`. `REL::ID` без пары SE/AE пропускается.
3. **ng-vfunc** — виртуальные методы заголовков NG (комментарий слота `// 98`, `// SE/AE 0x82`) через `VTABLE_*[0]`. Имя — самого базового класса, у которого в слоте та же функция; общие для разных методов функции (ICF) без имени.
4. **ng-table** — `VTABLE_*`, `RTTI_*`, `NiRTTI_*`: сверяются с RTTI Ghidra, метка — только если у Ghidra её нет.
5. **AE_<id>** — метка на каждый адрес Address Library; функция без имени получает имя `AE_<id>`.
6. **rtti-vslot** (T34, `rtti.py`): свой разбор RTTI MSVC в `.exe` (8 558 vftable). Безымянная функция в слоте → `vf_0xNN` в пространстве имён символа `vftable` Ghidra того класса, который самый базовый с той же функцией в этом слоте. Классы `std::`, `Concurrency::` и неразобранные шаблоны не называются; функция в слотах нескольких несвязанных классов получает только комментарий.
7. **papyrus** (`papyrus.py`): регистрации нативных функций по двум шаблонам байт (`lea r8,[класс]; lea rdx,[имя]; call` + обработчик в `[obj+0x50]` или в `r9`, затем `call [rax+0xC0]`) → `Papyrus::<Класс>::<Функция>`.
8. **console** (`name_sources.py`): таблицы `SCRIPT_FUNCTION` NG (`AE#361120` — 0x2E0 скриптовых, `AE#365650` — 0x1B4 консольных, записи по 0x50) → `ScriptCommand::`, `ConsoleCommand::`, `Condition::` (функция условия +0x40). Общий для нескольких команд обработчик — комментарий.
9. **docs** — `` `Класс::Метод` `` вплотную к `AE#id` в `docs/*.md` и комментариях `src/` → плашка-комментарий.

- **Строки `Класс::Метод`** в данных: если строку использует ровно одна функция — комментарий `[clr] string names …`.
- **Фаза 0** `apply_names.py` создаёт пропущенные функции: начала `.pdata` (кроме цепочек `UNW_FLAG_CHAININFO`), адреса кода в данных (8-байтные указатели), цели `lea reg,[rip+x]` в коде (если Ghidra разобрала там `LEA`), ID Address Library после `int3`/`ret`, адреса имён. Функция, созданная раньше на сырых байтах (тело 1 байт), пересоздаётся.
- **Function ID:** любое наше имя заменяет имя анализа (старое остаётся меткой). Имена MFC (`CWinApp`, `CMFC*`, `CTaskDialog`…) ложны — игра не использует MFC: функция получает `AE_<id>`, имя MFC — метка и комментарий. Остаются: функлеты исключений `Unwind@`/`Catch@` (из таблиц исключений, точны), CRT/`std`/`Concurrency`/XAudio2 (статически влинкованы), `FID_conflict` (неоднозначны).
- **Сверка RTTI с NG** (`norm()`): написание шаблонов (`unsigned_char`/`uchar`, `class_`, `const`, `near`, `__ptr64`, анонимные пространства, локальные классы функций) не считается расхождением.

Счёт и конфликты — в отчётах `%TEMP%\clr-ghidra\names-*.report.txt`, `apply-*.report.txt`; сводка — `docs/tasks/results/T33.md`.

## Другие версии (5.14, п. 6)

`gconfig.VERSIONS` — запись на сборку: `runtime` (`se`/`ae` — половина `RELOCATION_ID` и префикс меток `SE_`/`AE_`), `.exe`, таблица Address Library (`version-1-5-97-0.bin` — формат 1, `versionlib-*.bin` — формат 2), имя проекта. `analyze.ps1` принимает пути параметрами. Имена из документов есть только для AE.
