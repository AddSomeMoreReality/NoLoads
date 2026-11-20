# T33. Ghidra database for SkyrimSE.exe: names and tools for executors

Stage 2, supporting. Branch `task/T33-ghidra`, worktree `..\CLR-T33` (submodule not needed: read NG headers from the main folder, `lib\commonlibsse-ng`). Code: only our scripts in `tools/ghidra/` and `docs/GHIDRA.md`. No in-game run. User decision dated 05.11.2026: move from `dumpbin` to Ghidra pseudocode. After the task, the chat remains the **database curator**: the only one who changes the Ghidra project (names, types, comments), answers requests about game functions, and fixes the tools. Executors only read the database.

## Available resources

- **Ghidra 12.2** — `E:\Ghidra\ghidra_12.2_PUBLIC` (PyGhidra and Version Tracking included). User variable `GHIDRA_MAXMEM=8G`. 15.8 GB RAM.
- **Java.** JDK 25 (Oracle) — `D:\Java`. Java 8 (`Oracle\Java\java8path`) comes first in PATH; `JAVA_HOME` is not set. Ghidra 12.2 sets `-XX:+UseCompactObjectHeaders` (`support\launch.properties`), which requires JDK 24+.
- **Python** 3.12.8.
- **Game:** `E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe`, 1.6.1170.0; Address Library — `Data\SKSE\Plugins\versionlib-1-6-1170-0.bin`. `CreationKit.exe` in the same location — version 1.6.1378.1, a newer build than the game.
- **No Ghidra project exists yet.**
- **How code is analyzed now:** `tools/name_functions.py` (ID ↔ RVA, `.pdata`, RTTI, `--xref`), `tools/re/*.py`, `dumpbin` (`CLAUDE.md`, section 7). Name sources: `lib/commonlibsse-ng` (`RELOCATION_ID(se, ae)`, `REL::ID`, `VTABLE_*`, `RTTI_*`, offset files), project documents where `AE#nnnn` and a name appear together (`ENGINE_MAP.md`, `FINDINGS.md`, `PORTABILITY.md`, `DISTANCE_CHECKS*.md`, `LOCATION_TRIGGERS.md`, `VIRTUAL_PLAYER.md`).

## Tasks

1. **Java for Ghidra.** `JAVA_HOME_OVERRIDE=D:\Java` in `support\launch.properties` (affects only Ghidra). Ask the user first. Verification — launch `support\analyzeHeadless.bat` without arguments.
2. **Project** `E:\Ghidra\projects\SkyrimSE-1.6.1170`, outside the repository. Import `SkyrimSE.exe` and run headless auto-analysis (`analyzeHeadless … -import …`) in the background, with the log in the scratchpad. Analysis takes hours: warn the user not to launch the game during that time. Record analysis settings and duration in `docs/GHIDRA.md`.
3. **Names** (script, repeatable from scratch):
   - every function in Address Library gets the label `AE_<id>`; an unnamed function gets the name `AE_<id>`;
   - NG names: `Class::Method` from `RELOCATION_ID`/`REL::ID` in NG sources, `VTABLE_*`, `RTTI_*`, globals (singletons; for example, `AE#403521` is the player pointer, `AE#403520` is the player handle);
   - names from project documents go into a comment with the source; they do not replace the NG name;
   - Ghidra recovers RTTI itself: compare `PlayerCharacter` with NG `VTABLE_PlayerCharacter`;
   - report: how many functions were named from each source, name conflicts.
4. **Tools for executors** (`tools/ghidra/`, our code, in git):
   - pseudocode by `AE#id`, `AE#id+off`, or address → file in the caller's scratchpad; multiple functions per call;
   - who calls a function, who references a global, and where possible who reads a class field at an offset;
   - headless operation. Measure the time for one call. If JVM startup and program opening take longer than 30–60 s — a long-running process (PyGhidra) with a simple client, or batch requests;
   - check whether two requests can run simultaneously (project locking). If not — a queue or a read-only project copy.
5. **Verification against known functions** — pseudocode must match the conclusions in the documents:
   - `AE#13316` (calls `AE#13363`, `AE#18976`, `AE#19063` at `+0x65C`, `+0x66B`, `+0x6AA`; T20);
   - `AE#40466` (`+0x105`, `+0x11c`, `+0x154`; T15, T16);
   - `AE#19130`;
   - `AE#36629` (player creation: 0xBE8 bytes, constructor `AE#40411`; T31).
6. **`docs/GHIDRA.md`** (≤ 80 lines): project location, how to recreate it, how to use the tools, rules. Propose edits to `CLAUDE.md` and `docs/tasks/README.md` in the result — the coordinator applies them.
7. **Optional, second step — Creation Kit.** Add `CreationKit.exe` to the same project. Version Tracking and assertion strings with function names provide name candidates for the game. First evaluate on 20 known functions: how many match. The CK build is newer than the game; this must be taken into account.

## Rules

- **The Ghidra project, pseudocode exports, and C exports are Bethesda code.** Do not put them in git or `docs/`: only conclusions, addresses, and short instruction quotations. If anything could enter the tree, add it to `.gitignore`.
- Do not modify the game folder or launch the game.
- Third-party name databases only with a clear license and source; record the source in `docs/GHIDRA.md`.
- 5.14, item 6: equivalent projects will later be needed for SE 1.5.97 and 1.7.x (T26) if the user provides their `.exe`. The name script takes a version parameter and its own Address Library table.
- Do not read large documents in full (task README rule 1). Cyrillic breaks in the Bash console: write long output to a file and read it with Read.

## Result

`docs/tasks/results/T33.md`: what was done, analysis duration, name counts by source, tool call duration, results of verification against known functions, what to add to `CLAUDE.md` (section 7, “Game machine code”) and to the task README.

## Conflicts

None: new files. In content — all tasks analyzing machine code (T32, T22, T20).
