---
name: ghidra
description: Read the machine code of SkyrimSE.exe 1.6.1170 through the project's Ghidra base (MCP tools ghidra_*, CLI tools/ghidra/gq.py). Use when a task needs what a game function does, who calls it, which vtable slot it fills, where a field offset is read or written, or where a string is used. Covers target syntax, which tool to pick, the rule that pseudocode never goes into git or docs, and how to ask the base curator for names and types.
---

# Ghidra base of SkyrimSE.exe

Read-only base of `SkyrimSE.exe` 1.6.1170 with names from CommonLibSSE-NG, the curator list and Address Library. Details: `docs/GHIDRA.md`.

## Rules

- **Pseudocode, disassembly and the project are Bethesda code.** Never put them into git, `docs/`, results or commit messages: only conclusions with addresses (`AE#13316+0x65C calls AE#18976`) and short instruction quotes.
- The base is changed only by the curator (the T33/T35 chat, worktree `..\CLR-GHIDRA`). Need a name, a type, a comment or a deeper analysis — write a request for the curator and give it to the user: target, what you believe and the evidence. Your own conclusions go into your task result, not into the base.
- The server holds 0.4-1.2 GB. Before the user starts the game: `python tools/ghidra/gq.py stop`.

## Targets

`AE#13316`, `AE#13316+0x65C` (offset marks the line), `0x14019D490` (VA), `0x19D490` (RVA), `TES::SetCurrentCell`, `TES::SetCurrentCell+0x10`.

## Which tool

| Question | MCP tool | CLI (`python tools/ghidra/gq.py ...`) |
|---|---|---|
| What does the function do, what is at +0xOFF | `ghidra_decompile(targets, marks?)` | `decomp AE#id+0xOFF --out <scratchpad>` |
| Exact instructions, registers | `ghidra_asm` | `asm` |
| Who calls / reads it, which vtable slot | `ghidra_xrefs` | `xrefs` |
| Call tree up or down | `ghidra_callgraph(target, callers/callees, depth)` | `callgraph --callers/--callees` |
| Name, size, number of callers | `ghidra_info` | `info` |
| Find a function by name or a string | `ghidra_search(name_regex / string_regex)` | `search --name/--string` |
| Slots of a class: own, inherited, overridden | `ghidra_vtable(class)` | `vtable` |
| Who reads/writes `[reg+0x1D0]` | `ghidra_fieldrefs(offset, users_of / within / all)` | `fieldrefs` |
| Layout of a type | `ghidra_struct(name)` (NG classes, `<Class>_vftable`) | `struct` |

- Long answers are files: the tool prints the path and an excerpt; read more with Read (`offset`/`limit`). With the CLI, pass `--out` = your scratchpad.
- In the MCP tools the output goes to `%TEMP%\clr-ghidra\out\mcp-…` unless you pass `out`.
- Several calls may run in parallel: the server has 3 decompilers and caches pseudocode.

## Reading the answers

- Without the server: `%TEMP%\clr-ghidra\names-1.6.1170.tsv` maps `AE#id` to name and source (grep it).
- Name sources (plate comment `[clr] ng:` / `name from …`): NG, `curated`, Papyrus and command names are reliable; AddressLibraryDatabase names agree with NG in 90 % of shared IDs (the rest are synonyms); `Class::AE_<id>` means only the class is known; `vf_0xNN` = unnamed vtable slot; `AE_<id>` = unnamed. Names from Ghidra analysis (Function ID) are right for STL/CRT, sometimes false on Bethesda code; `FID_conflict:` is ambiguous. The source is in the header of the pseudocode file and in `ghidra_info`.
- Types (T36): NG classes of this build are in the base (`/NG/RE`), methods with NG names have a typed `this`, fields have NG names (`this->interiorCell`), and calls through a typed object read `this->vfptr->SetParentCell`. Field offsets are exact (MSVC layout of the AE build); `unkXXX` names are NG's own gaps.
- Calls on untyped pointers stay `(**(code **)(*p + 0x4c0))`: slot = offset / 8 (`0x4C0` → slot `0x98`); look the slot up with `ghidra_vtable`.
- `fieldrefs` matches `[reg + offset]` without types: candidates, confirm with pseudocode.
- `callgraph` callees follow direct calls only; indirect calls are counted. Callers include vftable slots that hold the function.
- Fallback without the server: `dumpbin /disasm` and `tools/name_functions.py` (`CLAUDE.md`, section 7).

## Delegating

For broad questions ("which of these 40 functions touch the player cell?") use the `re-reader` agent (Sonnet) or `re-reader-fast` (Haiku, mass labeling): they read the base and return conclusions with addresses, without long pseudocode.
