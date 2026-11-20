# T37. Creation Kit: whether to analyze it and what it gives for game names

Stage 2, auxiliary, research. Branch `exp/T37-ck`, worktree `..\CLR-T37` (without the submodule). Code: scripts in `tools/ghidra/ck/`, if they pay off. No game run. **Does not modify the game base:** CK goes into a separate Ghidra project. Names for the game are handed to the curator as a candidate list.

## What there is

- `CreationKit.exe` 1.6.1378.1 in the game folder: a build newer than game 1.6.1170, the engine is shared, the editor on top.
- The T33 pipeline (`tools/ghidra/analyze.ps1`, path parameters) and the game base with NG names (`gq.py` / MCP from T35).
- Expected benefit:
  - check and message strings with function names and source paths;
  - RTTI;
  - more code of the same classes (`TESObjectCELL`, `TESForm`, cell loading).

## To do

1. **Reconnaissance, before analysis (one hour):** size, sections, RTTI, whether there is a PDB or debug strings. Count strings of the form `Class::Method` and `*.cpp` paths. If there are few and the RTTI is the same as the game's, little benefit: stop with a conclusion.
2. **Project** `My attempts\SkyrimCK-1.6.1378` via `analyze.ps1` with parameters (MSVC compiler, without Create Address Tables). Record time and memory.
   - **While the analysis runs, do not launch the game and do not run the curator's analysis:** up to 10 GB of RAM.
   - The user's consent for a new project outside the repository.
3. **Names inside CK:** functions from `Class::Method` strings (one function references the string), RTTI, vtable slots.
4. **CK → game matching:** Version Tracking (Ghidra 12.2). Correlators: exact bytes and instructions of the function, references to data and strings, references to functions, RTTI symbols. BSim — only if VT yields little.
   - **Evaluation on 20 known game functions** (NG/curated names, different subsystems): how many found, how many correct.
   - Keep in mind the build is newer: the code may have shifted and changed.
5. **Decision:** accuracy on the known ≥ 80 % and ≥ 500 candidates → candidate list to the curator:
   - `tools/ghidra/ck/candidates.tsv`: the game address and `AE#`, name, evidence, score; into git — only these fields;
   - a script, repeatable from scratch.
   - Otherwise — the conclusion "not worth it" with numbers.

## Rules

CK is a Bethesda program: the project and pseudocode do not go into git. Candidates — only addresses, names, and short evidence.

## Result

`docs/tasks/results/T37.md`:
- reconnaissance;
- analysis time;
- accuracy on 20 functions;
- number of candidates;
- decision;
- what was handed to the curator.

## Conflicts

None. Memory: not at the same time as T34–T36 (analysis) and as game runs.
