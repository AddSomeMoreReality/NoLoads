# Ghidra: SkyrimSE.exe database and pseudocode tools

The database curator is a separate chat (T33, T35). Only the curator modifies the Ghidra project (names, types, comments) and repairs `tools/ghidra/`. Executors only read the database: MCP tools `ghidra_*` or `gq.py`. If you need a name, type, or function analysis, send a request to the curator through the user.

## Rules

- **The project, pseudocode, and C export are Bethesda code.** Do not put them in git or `docs/`: only conclusions, addresses, and short instruction quotations. Write `gq.py` output to your own scratchpad. `.gitignore` contains: `*.gpr`, `*.rep/`, `*.gbf`, `*.gzf`, `ghidra_out/`.
- Names from project documents are comments only (`[clr] docs AE#…`); they do not replace the NG name.
- The server uses 0.4–1.2 GB RAM (growing with `search` indexes and the number of analyzed functions) and exits automatically after 20 minutes of inactivity; `gq.py stop` releases it immediately.

## Locations

| What | Where |
|---|---|
| Ghidra 12.2, JDK 25 | `E:\Ghidra\ghidra_12.2_PUBLIC`; Java is selected in the Ghidra window and remembered in `%APPDATA%\ghidra\ghidra_12.2_PUBLIC\java_home.save` (`D:\Java`), `launch.properties` was not changed |
| PyGhidra | venv `E:\Ghidra\pyghidra-venv` (pyghidra 3.2.0, JPype 1.5.2 from `Ghidra\Features\PyGhidra\pypkg\dist`) |
| Main project (curator) | `E:\Games\Skyrim\ModCreation\My attempts\SkyrimSE-1.6.1170\` (1.5 GB), program `/SkyrimSE.exe` |
| Read-only copy (server) | `...\My attempts\SkyrimSE-1.6.1170-ro\` — created by `snapshot.py` |
| Server logs, names, reports | `%TEMP%\clr-ghidra\` (`server.log`, `mcp.log`, `names-1.6.1170.json`, `*.report.txt`) |
| Name table without the server | `%TEMP%\clr-ghidra\names-<version>.tsv`: `AE#id`, RVA, name, source — `grep "^AE#13316<TAB>"` or search by name |
| Third-party name sources | `%TEMP%\clr-ghidra\thirdparty\` (`git --depth 1` clones, not in git) |
| Pseudocode cache and search indexes | `%TEMP%\clr-ghidra\cache\<version>-<snapshot>\` (the snapshot is a hash of the copy's database files; a new copy means a new directory) |
| MCP tool output | `%TEMP%\clr-ghidra\out\mcp-<date>-<pid>\`, retained for 7 days |
| Paths and versions | `tools/ghidra/gconfig.py`, overridden by `CLR_GHIDRA_*` variables |

## For executors: MCP `ghidra` and `tools/ghidra/gq.py`

Targets: `AE#13316`, `AE#13316+0x65C`, `0x14019D490` (VA), `0x19D490` (RVA), name `TES::SetCurrentCell[+0x10]`.

| Question | MCP | CLI `python tools/ghidra/gq.py …` |
|---|---|---|
| Pseudocode, line with `+0x65C` marked `//>>>` | `ghidra_decompile(targets, marks?, addr_prefix?, max_lines?)` | `decomp AE#13316+0x65C [--addr] --out <sp>` |
| Disassembly with offsets and names | `ghidra_asm` | `asm AE#19130 --out <sp>` |
| Calls, vtable slots, data reads/writes | `ghidra_xrefs` | `xrefs AE#40466 --out <sp>` |
| Name, size, caller count | `ghidra_info` | `info AE#36629 AE#40411` |
| `[reg+0x1D0]` in functions that read the player | `ghidra_fieldrefs(offset, users_of / within / all, access)` | `fieldrefs 0x1D0 --users-of AE#403521 --out <sp>` |
| Symbols by name regex, strings, reference locations | `ghidra_search(name_regex / string_regex, kinds?)` | `search --name SetParentCell --string "^sLoad"` |
| Call tree upward or downward | `ghidra_callgraph(target, callers/callees, depth, max_nodes)` | `callgraph AE#13316 --callers --depth 2` |
| Class slots: own, inherited, overridden | `ghidra_vtable(class, offset?)` | `vtable PlayerCharacter` |
| Type layout, slot table | `ghidra_struct(name)` | `struct TES`, `struct PlayerCharacter_vftable` |
| Server status | `ghidra_status` | `status`, `stop` |

- **MCP** (`tools/ghidra/mcp_server.py`, stdio, stdlib): a short function is returned as text; a long one is returned as a file plus an excerpt of ±10 lines around marks with file line numbers (read the rest with Read using `offset`). The response reports time spent in Ghidra and waiting for the decompiler. The connection configuration is in git: `.mcp.json` (`ghidra` server), `.claude/skills/ghidra` (skill), `.claude/agents/` (`re-reader` on Sonnet, `re-reader-fast` on Haiku); see below.
- **CLI** uses the same server; `--out` specifies your own scratchpad.
- The first call starts the server (`gserver.py`): 7–8 s. Requests run in parallel; the pool has 3 decompilers (`CLR_GHIDRA_DECOMPILERS`), and pseudocode is cached in memory and on disk by function address and database snapshot.
- Timing (T35, warm server): MCP adds 20–30 ms; `decomp` takes 0.1–0.7 s, 0.02–0.05 s from cache; 48 uncached functions take 3.5 s sequentially and 1.5 s across 3 threads; `search` takes 6 s to build the first name or string index, then 0.1 s; `vtable PlayerCharacter` (14 tables) takes 0.2 s; `xrefs` up to 1 s; `fieldrefs --users-of AE#403521` takes 9 s.
- If the server belongs to older tools (protocol below `gq.PROTOCOL`), the new client stops it and starts it again.
- Pseudocode file header: address, all `AE#`, name, signature, curator comments (`[clr] ng: NG file`, `[clr] curated: …`, `[clr] docs …`), file lines with marks.
- `fieldrefs` searches for a `[reg + offset]` operand without regard to type — these are candidates; verify them using pseudocode. `R`/`W` means read/write, `A` means `lea`.
- `callgraph`: downward includes only direct calls and tail `jmp`, and counts indirect (vtable) calls; upward includes calls, `jmp`, and vftable slots containing the function. `search` does not find `AE_<id>` labels (use `AE#id` for those); it does not count references from exception tables (`.pdata`, `UNWIND_INFO`).
- `vtable` reads MSVC RTTI: `CompleteObjectLocator` → subobject offset, `ClassHierarchyDescriptor` → bases. A slot is compared with the base's primary table at the same offset: `inherited`, `override of …`, `new in …`.
- The name source is shown in `info` and the header. NG, `curated`, and `AE_<id>` names are reliable. Ghidra analysis names (66,723: Function ID, exception analysis) are correct for STL/CRT/XAudio2 but can be false for Bethesda code; `FID_conflict:…` indicates ambiguity.
- Unnamed functions are named `AE_<id>`.
- **NG types (T36):** classes for this build are in `/NG/RE` (5,433 `RE::` structures, offsets use the MSVC layout of the “AE only” build). Methods with NG names have typed `this`, named fields (`this->interiorCell`), and calls through a typed object appear as `this->vfptr->SetParentCell`. `unkXXX` fields are gaps in NG itself. A call through an untyped pointer remains `(**(code **)(*p + 0x4c0))`: slot = offset / 8; see `vtable`.

### MCP connection

Since 08.11.2026, `.mcp.json` and `.claude/` (skill, agents) are in git and apply in every chat opened at the repository root or a worktree. These files are the source of truth; there are no separate templates. `.claude/settings.local.json` is personal and is not committed to git.

- Claude Code asks for approval of the `ghidra` server on the first launch in a chat; `/mcp` shows the list and status. Only a new chat picks up a new or changed `.mcp.json`.
- Subagents receive `mcp__ghidra__*` tools only if the server is connected in the main chat. Without them, `re-reader*` agents cannot read anything (this happened on 07.11.2026): in that case, run `general-purpose` with `python tools/ghidra/gq.py` through Bash.
- For a single directory without git only: `claude mcp add --scope local ghidra -- python tools/ghidra/mcp_server.py`.
- Check: `python tools/ghidra/mcp_server.py --selftest` — starts the MCP server as a child process, invokes tools, and runs 3 decompilations in parallel.

### How to add a tool

1. Add a `Ghidra` class method in `gserver.py` that returns `dict(text=…)` (long text via `self.write_text(out, name, lines, limit)`), and a branch in `dispatch()`. Methods run in parallel and only read the program; shared indexes are built under `self.index_lock`, and the decompiler is accessed only through `decompiled_rows()`.
2. Add a subcommand in `gq.py` and an entry in `TOOLS` in `mcp_server.py` (name `ghidra_…`, description, JSON schema, handler; for plain text, `t_text("cmd", field="argument")`).
3. If the old server does not know the command, increment `PROTOCOL` in `gserver.py` and `gq.py`. If the pseudocode cache row format changes, increment `CACHE_FORMAT`.
4. Run `python tools/ghidra/gq.py stop`, then `--selftest` and invoke the new command.

## For the curator

Make changes only with scripts that can be repeated from scratch:
1. Analysis: `powershell -File tools\ghidra\analyze.ps1 -LogDir <scratchpad>` (Ghidra window closed; 12 min, 10 GB heap). Two `analyzeHeadless` invocations:
   - import with `-noanalysis` and `scripts\ClrAnalysisOptions.java`: compiler `visualstudio:unknown` (the PE loader leaves `unknown`, which prevents the MSVC RTTI and exception analyzers from running) and **Create Address Tables** disabled (with an 8 GB heap in the window, it crashed with `OutOfMemoryError`, 05.11.2026);
   - `-process` in a new process: analyzers are selected when opening the program, now with MSVC.
2. Names: `python tools/ghidra/collect_names.py` (5–20 s), then `E:\Ghidra\pyghidra-venv\Scripts\python.exe tools/ghidra/apply_names.py` (window closed). Repeating on a prepared database takes 30 s; on a fresh analysis, phase 0 creates about 12,600 functions and takes up to 1.5 h (each new function rebuilds neighboring bodies).
3. NG types (T36): `python tools/ghidra/collect_types.py` (MSVC, 30 s), then `E:\Ghidra\pyghidra-venv\Scripts\python.exe tools/ghidra/apply_types.py` (window closed; 35 s). Details below.
4. Read-only copy: `python tools/ghidra/snapshot.py` (stops the server, seconds). The old copy's pseudocode cache and indexes remain in `%TEMP%\clr-ghidra\cache\` under the old snapshot; they can be deleted.

- **Your own conclusions** (a name based on analysis) — add a line to `tools/ghidra/curated_names.txt` (in git), then run `apply_names.py --project-dir <project>-pre-types`, `apply_types.py --restore`, `snapshot.py`: the copy without types must know the new names, otherwise `--restore` will lose them. `apply_names.py` does not touch a manual name in the Ghidra window (`USER_DEFINED`), but `analyze.ps1` will erase it.
- Ghidra window: open the main project, save and close after changes, then perform step 4.
- **Checks (T34, read-only access to the main project, window closed):**
  - `check_base.py [--ref-id 403521]` (1–2 min): RIP references to an ID by bytes versus Ghidra references, `.pdata` starts without functions, code outside functions, `FUN_`, names by source, 500 most-called unnamed functions;
  - `check_docs.py`: assertions `AE#a+0xNN` → / call / `<` / from `AE#b` in `docs/*.md` against the instruction in the database. It does not edit documents; the list is `%TEMP%\clr-ghidra\check-docs-<version>.txt`.

## NG types (T36)

The compiler is the parser: NG headers are compiled by MSVC **for a single runtime** (`ENABLE_SKYRIM_AE` for 1.6.x, `ENABLE_SKYRIM_SE` for 1.5.97). NG then lays out classes exactly for that runtime (`TES` = 0x2C0 with fields that the SE+AE build hides behind `GetRuntimeData()`), and every `STATIC_ASSERT_SIZE` checks the size for that runtime. Flags and paths come from `build/.deps` of the plugin build in the main directory (build the plugin at least once); MSVC comes from `gconfig.VCVARS`.

- `collect_types.py` → `%TEMP%\clr-ghidra\types\<version>\`:
  - pass 1 — `/d1reportAllClassLayout`: sizes, bases, vftables with method names (16,087 classes, 3,005 tables);
  - pass 2 — a DLL with one function per `RE::` class (3,228) → `NG.pdb` with complete field types;
  - from headers (`collect_names.py` parser) — static methods (548) and singletons (80: which are pointers and which are objects).
- `apply_types.py` (main project; the first run copies it to `<project>-pre-types`, subsequent runs use `--restore`, because Ghidra does not delete `/NG` when types are already in use):
  1. Ghidra PDB loader, “types only”, layout with class hierarchy → `/NG`; the preferred root of the program's namespaces is `/NG/RE`, so the `TES` class finds the structure for `this`;
  2. unnamed bases → `super_<Base>`; NG placeholder `_padPlayerRuntimeData` (0x3E0, 0x808) → `PLAYER_RUNTIME_DATA` of the same size;
  3. `/NG/vftables/<Class>_vftable` (1,793 tables, 31,495 slots, names from the MSVC report); for classes with their own primary table (1,435), the primary-base chain is expanded into flat fields, and `vfptr` at +0 points to the class table;
  4. `__thiscall` — for functions in a class namespace with a structure in `/NG/RE`: virtual functions (6,910) and functions with an NG/curated name, except static ones (479);
  5. singletons — 79 (`TES` → `TES*`, `PlayerCharacter` → `PlayerCharacter*`, `BSTimer` — the object itself); `RenderTargetManager` is an object, contrary to NG (`SINGLETON_DEPTH_FIX`, `CLAUDE.md`, section 7);
  6. verification: all 5,433 `RE::` structures match MSVC sizes.
- The AE layout in NG is for 1.6.629–1.6.1170: `PlayerCharacter` lacks the 1.7.99 base (`BSTEventSink<BSSystemEvent>`), and `TES` lacks the 1.6.1130+ block (`AE_RUNTIME_DATA`). 1.7.x needs its own entry in `gconfig.VERSIONS` and verification.
- Not done: `SkyrimVM::_padVMRuntimeData` (0x554) does not equal `VM_RUNTIME_DATA` (0x558) — left as a placeholder; signatures from NG `decltype` (parameter types) are not applied.

## Names: sources and priority

`collect_names.py` parses `lib/commonlibsse-ng` in the main directory (NG v13.1.0, MIT), documents, and Address Library:

1. **curated** — `curated_names.txt`: names established by the curator; methods receive `this`.
   **reviewed** — `reviewed_names.txt`: names the curator verified against code (often proposed by `re-reader-fast`). Below curated, above NG and third-party tables; no `this` is assigned. Comment `[clr] reviewed: …` contains a short proof.
2. **ng-reloc** — `RELOCATION_ID(se, ae)` in NG functions → `Class::Method`; type `decltype(&X::Y)` yields `X::Y`. Data: `Class::singleton`, otherwise `Class::Method_variable`. `REL::ID` without an SE/AE pair is skipped.
3. **ng-vfunc** — virtual methods in NG headers (slot comment `// 98`, `// SE/AE 0x82`) through `VTABLE_*[0]`. The name comes from the most base class with the same function in that slot; functions shared by different methods (ICF) remain unnamed.
4. **ng-table** — `VTABLE_*`, `RTTI_*`, `NiRTTI_*`: checked against Ghidra RTTI; a label is applied only if Ghidra has none.
5. **AE_<id>** — a label at every Address Library address; an unnamed function receives the name `AE_<id>`.
6. **rtti-vslot** (T34, `rtti.py`): custom MSVC RTTI parsing in the `.exe` (8,558 vftables). An unnamed function in a slot → `vf_0xNN` in the namespace of the Ghidra `vftable` symbol for the most base class with the same function in that slot. `std::`, `Concurrency::`, and unparsed template classes are not named; a function in slots of several unrelated classes receives only a comment.
7. **papyrus** (`papyrus.py`): native-function registrations by two byte patterns (`lea r8,[class]; lea rdx,[name]; call` + handler in `[obj+0x50]` or in `r9`, then `call [rax+0xC0]`) → `Papyrus::<Class>::<Function>`.
8. **console** (`name_sources.py`): NG `SCRIPT_FUNCTION` tables (`AE#361120` — 0x2E0 script functions, `AE#365650` — 0x1B4 console functions, 0x50-byte entries) → `ScriptCommand::`, `ConsoleCommand::`, `Condition::` (condition function +0x40). A handler shared by several commands receives a comment.
9. **Third-party tables** (`thirdparty.py`, AE only; `collect_names.py --no-thirdparty` disables them, `--update-thirdparty` updates the clones). Only names and IDs are taken; code is not copied, and data stays outside git:

   | Source | License | What it provides | Match with NG |
   |---|---|---|---|
   | AddressLibraryDatabase (meh321), `skyrimae.rename` | no license file; README: shared name database for Address Library IDs | 42,180 IDs: 24,524 names, 17,656 “class only” (`sub`, `FuncN`, IDA placeholders) | 4,086 of 4,560 (90%); the rest are synonyms (`IsEqual`/`IsNotEqual`, `_Impl`) |
   | po3 CommonLibSSE, `dev-1.6.1170` branch | MIT | 1,066 IDs (`REL::ID(n)` = AE ID) | 1,019 of 1,049 (97%) |
   | TiltedEvolution, `POINTER_SKYRIMSE` | GPL-3.0 | 255 IDs with variable names | 14 of 106 — comment only |

   Priority: NG, curated, Papyrus, and commands above; po3 treated as NG; AddressLibraryDatabase above NG `Unk_` and `vf_0xNN`; “class only” → `Class::AE_<id>` for functions that no other source named. Comment `[clr] name from …` shows the source, and `[clr] elsewhere: …` shows differing names from other sources. A name from third-party tables places a function in a class but does not assign `this`: the method may be static.
10. **docs** — `` `Class::Method` `` immediately adjacent to `AE#id` in `docs/*.md` and `src/` comments → plate comment.

- **`Class::Method` strings** in data: if exactly one function uses the string, add comment `[clr] string names …`.
- **Phase 0** of `apply_names.py` creates missing functions: `.pdata` starts (except `UNW_FLAG_CHAININFO` chains), code addresses in data (8-byte pointers), targets of `lea reg,[rip+x]` in code (if Ghidra analyzed `LEA` there), Address Library IDs after `int3`/`ret`, and name addresses. A function previously created on raw bytes (1-byte body) is recreated.
- **Function ID:** any of our names replaces an analysis name (the old one remains a label). MFC names (`CWinApp`, `CMFC*`, `CTaskDialog`…) are false — the game does not use MFC: the function receives `AE_<id>`, and the MFC name becomes a label and comment. The following remain: exception funclets `Unwind@`/`Catch@` (from exception tables, accurate), CRT/`std`/`Concurrency`/XAudio2 (statically linked), `FID_conflict` (ambiguous).
- **RTTI comparison with NG** (`norm()`): differences in template spelling (`unsigned_char`/`uchar`, `class_`, `const`, `near`, `__ptr64`, anonymous namespaces, function-local classes) are not considered mismatches.

Counts and conflicts are in `%TEMP%\clr-ghidra\names-*.report.txt`, `apply-*.report.txt`; summary in `docs/tasks/results/T33.md`.

## Other versions (5.14, item 6)

`gconfig.VERSIONS` has one entry per build: `runtime` (`se`/`ae` — the half of `RELOCATION_ID` and the `SE_`/`AE_` label prefix), `.exe`, Address Library table (`version-1-5-97-0.bin` — format 1, `versionlib-*.bin` — format 2), project name. `analyze.ps1` accepts paths as parameters. Names from documents exist only for AE.
