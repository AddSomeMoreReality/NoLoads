# T38. Player map in code: the `PlayerCharacter` class and everything specific to the player

Stage 2, research. Branch `exp/T38-player-map`, worktree `..\CLR-T38` (without the submodule). Code: none, scripts in the scratchpad. Run: optional, with the T40 build. Reads the base via T35 (MCP or `gq.py`), better after T36 (types). Based on `docs/VIRTUAL_PLAYER.md`, `results/T31.md`.

## Why

5.14, items 1–2 and T32: the far side of the portal needs a "player" on its side, but the engine knows only one. A complete map of what in the code is tied specifically to the player is needed.
- Which functions are player-only (`PlayerCharacter` overrides, `== player` checks).
- Which fields and globals are his.
- Which subsystems ask for the player, and how: pointer, handle, FormID `0x14`.

## Separation from T32

T32 (in progress) counts reads of the pointer in the game and disassembles the constructor `AE#40411`. **T38 is a static map**, T32 gets it as support. Do not duplicate the census of 2 843 places: take it from T31/T32 and group by functions and subsystems.

## To do

1. **Class:**
   - all 14 `PlayerCharacter` vtables (the main `AE#208040` and the secondary ones), slot → function;
   - own function or inherited from `Character`/`Actor`/`TESObjectREFR` (compare slots);
   - player overrides — the main list: what each does differently, 1–2 lines.
2. **Methods not from vtable:** functions whose `this` is the player. Sources: NG; functions called with `PlayerCharacter::singleton` as the first argument; functions reading fields `≥ 0x2B8` (`fieldrefs` on `PlayerCharacter` fields).
3. **Fields:**
   - `PlayerCharacter` layout for AE (NG + T36);
   - for important fields (cell, position, `currentLocation`, `PlayerSkills`, telemetry, flags) — who reads and writes.
4. **Player globals and singletons:** `AE#403521`, handle `AE#403520`, `PlayerCamera`, `PlayerControls`, `MenuControls`, player event sources (`BGSActorCellEvent`, `PositionPlayerEvent`, …), "player by FormID 0x14".
5. **Subsystems:**
   - group the places that access the player: combat, AI and packages, dialogue, quests and Story Manager, crimes, map, interface, camera, input, saving, sound, cell loading;
   - for each — how it finds the player and what it does with him.
6. **Name candidates** for the curator (address, name, evidence).
7. **Optionally — T40 build run:** which overrides and player functions run per frame on the street, in the interior, and with the portal.

## Result

- `docs/PLAYER_MAP.md` (≤ 250 lines, tables with `AE#`, no pseudocode).
- `docs/tasks/results/T38.md`: the main things for T32 and 5.14, name candidates for the curator.

## Conflicts

T32 — by meaning (arrange through the coordinator). Files — new.
