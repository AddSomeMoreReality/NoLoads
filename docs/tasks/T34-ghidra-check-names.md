# T34. Ghidra base: checking discrepancies and naming anonymous functions

Stage 2, auxiliary. **Executor — the base curator** (only he modifies the Ghidra project). Branch `task/T34-ghidra-names`, curator's worktree. Code: `tools/ghidra/` (`collect_names.py`, `apply_names.py`, new collectors), `curated_names.txt`, `docs/GHIDRA.md`. No game run. Based on `docs/tasks/results/T33.md`, `docs/GHIDRA.md`.

## What is known (T33)

- The MSVC RTTI and exception analyzers **already work**: `analyze.ps1` sets the compiler to `visualstudio:unknown`. There are 173 124 functions; 97 474 of them have only the name `AE_<id>`, and 66 723 have a Ghidra analysis name (Function ID, exception parsing).
- Discrepancies found:
  - references to `AE#403521` in Ghidra: 2 829, in the T31 census: 2 843 (14 places Ghidra did not disassemble);
  - 36 references from code outside functions;
  - 6 anonymous functions (`FUN_`);
  - 23 NG conflicts (one ID for two wrappers), 15 ICF functions without a name;
  - RTTI vs NG: 184 vtables and 176 RTTI differ only in template spelling, the comparison is incomplete;
  - the first analysis produced false Function ID names (`Concurrency::…::scalar_deleting_destructor` on Bethesda destructors).

## To do

1. **Discrepancies.**
   - 14 missed places: the census (`tools/re/scan_player_access.py`) vs `gq.py xrefs AE#403521`. For each — why it was missed; disassemble and create the function if `.pdata` confirms it.
   - 36 references outside functions and 6 `FUN_` — same treatment.
   - Bring RTTI normalization to 0 false discrepancies (`uchar`, `class_`/`struct_`, `near`). List the real discrepancies.
2. **Document claims.** Script: verify every `AE#a+0xNN` appearing next to `` `call AE#b` `` / `AE#b` in `docs/*.md` against the instruction in the base. Produce a list of mismatches (file:line); do not edit the documents — the list is for the coordinator.
3. **False Function ID names.** A function from a vtable slot of a Bethesda class, or with an ID known to NG, cannot be an STL/CRT function. Find such names and rename them by slot. Record the rule in `apply_names.py`. Outcome — how many analysis names remain and why they can be trusted.
4. **New sources of names** (each — a collector in `tools/ghidra/`, repeatable from scratch, with a count in the report):
   - **vtable slots** of all RTTI classes: an anonymous function in a slot gets `Class::vf_0xNN` (the class — the most base one that has the same function in the slot);
   - **native Papyrus functions**: registration by class-name and function-name strings → `Papyrus::<Class>::<Function>`;
   - **console commands**: the `SCRIPT_FUNCTION` table (NG has the structure and the table ID) → `ConsoleCommand::<Name>` for handlers;
   - **strings of the form `Class::Method`** (error messages, checks) referenced by exactly one function — candidates, comment only;
   - **third-party ID → name tables**: SKSE64 2.2.6 sources (addresses for 1.6.1170), TiltedEvolution (Skyrim Together Reborn), CommonLibSSE forks (po3, alandtse), the meh321 database. License first: a name is a fact, do not copy code. Record the source and license in `docs/GHIDRA.md`. Comment only, until confirmed by a second source;
   - T37 (Creation Kit) and T41 (multiplayer mods) candidates, when they are ready.
5. **Report:** names by source, how many `AE_<id>` remain among the most-called functions (by number of callers: top-500).

## Rules

- Do not put pseudocode and the project into git (`docs/GHIDRA.md`). Into git go the collectors, `curated_names.txt`, and conclusions.
- Every step is a script, repeatable from scratch: after `analyze.ps1` → `collect_names.py` → `apply_names.py`, the base must come out the same.
- After edits — `snapshot.py`; while it runs, the query server is down.

## Result

`docs/tasks/results/T34.md`:
- discrepancies with causes;
- document mismatches (file:line);
- names by source, before and after;
- licenses of third-party sources;
- what remains anonymous among the important functions.

## Conflicts

T35, T36 — same `tools/ghidra/`, do them one at a time in the curator's chat. Does not edit documents.
