# T36. NG types in the Ghidra base: structures, vtables, `this`, and singletons

Stage 2, auxiliary. Executor — the base curator. Branch `task/T36-ghidra-types`. Code: `tools/ghidra/collect_types.py`, `apply_types.py`. No run. After T35, before T38/T39: the player and cell-loading maps are read several times easier with fields instead of `*(param_1 + 0xc0)`.

## What there is

- NG headers (`lib/commonlibsse-ng`, v13.1.0): fields with offset comments (`/* 1114 */`, `// 0C0`), `static_assert(sizeof(X) == …)`, base classes. Layouts with SE/AE differences — `RUNTIME_DATA`, `GetAERuntimeData()`. For `TES`, fields beyond `0x128` are shifted in AE (`CLAUDE.md`, section 7).
- `collect_names.py` already parses NG scopes (classes, bases, virtual methods with slots). Ghidra's C parser cannot take NG's C++23 templates.

## To do

1. **Structures:** own NG parsing → JSON (class, size for AE, bases, fields: offset, type, name). The offset source is the comment. If there is none, compute it and verify against `static_assert`. Unknown types — `undefined`/byte array of the needed length.
   - First: `TES`, `TESForm` → `TESObjectREFR` → `Actor` → `Character` → `PlayerCharacter`, `TESObjectCELL`, `TESWorldSpace`, `NiAVObject`/`NiNode`, `ProcessLists`, `PlayerCamera`, `Main`, `BSTEventSource<T>`.
   - Then everything.
2. **Vtable:** slot structure from NG virtual methods (`SetParentCell` in slot `0x98`), a `vtable` field on the class. Then a call through a vtable in pseudocode gets a name.
3. **Application** (`apply_types.py`, PyGhidra, repeatable):
   - `/NG` category in the type manager;
   - `this` on methods with an NG/curated name (`__thiscall`, `Class*`);
   - singleton types (`PlayerCharacter::singleton` — `PlayerCharacter*`, `TES::singleton` — `TES*`, etc.);
   - signatures from NG `decltype` where they exist.
4. **Verification:**
   - pseudocode of `AE#13316` shows `TES` fields (`interiorCell` at `+0xC0`, `gridCells`, `interiorBuffer`);
   - `AE#40466` — `PlayerCharacter` fields;
   - sizes match `static_assert`.
   - List of classes where they diverge.
5. **Report:** number of structures and fields, methods with `this`, size discrepancies, time. Update `docs/GHIDRA.md`.

## Rules

Types are conclusions from NG (MIT); a JSON description may go into git if needed. Pseudocode — no. After applying — `snapshot.py`.

## Result

`docs/tasks/results/T36.md`.

## Conflicts

T34, T35 — one at a time in the curator's chat.
