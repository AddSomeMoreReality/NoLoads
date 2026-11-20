# T20. Entering the portal's interior without detaching it

Stage 2. Branch `exp/T20-attached-entry`, worktree `..\CLR-T20`. Code: `InteriorSim`, hooks (own ones in an own module, the T28 rule in `Hooks.h`). Run: yes. Start after T28 (splitting `Hooks.cpp`); T11 (`ActorHold`) is already merged. A seamless entry must call `PlayerCharacter::SetParentCell` for the player exactly once per transition (T15): it carries `CLOC`, `OnLocationChange`, and the T16 filter.

## What is known

- **Detach on entry.** The portal detaches the interior when the `Loading Menu` opens (`detach why=LoadingMenu.open` in the trace). After that the game attaches it again in `TES::SetCurrentCell` (`AE#13316` → `AE#18976` → `AE#19063`). The game's sequence — `docs/FINDINGS.md`, "Cell attach in the game (machine code)".
- **Run 0.8.27** (`docs/FINDINGS.md`, "Run 0.8.27"):
  - tavern NPCs are kept high (`ActorHold`), no jumps;
  - on entry the game tries to drop them precisely because of our detachment. The `actor.keptHigh` stack: `AE#37501 < AE#41468 < AE#41292 < AE#19130 < CellLoadResearch.dll`.
- **User (0.8.27):** on entry some NPCs change their movement direction. Their processing levels did not change at that moment. Candidates:
  - our interior detachment: the Havok world is inactive, NPC paths lose the cell;
  - restart of the bard and listener scenes (`00108ED5`) after `StopAllSongs` (this is T15);
  - quests on `CLOC` (`WITavern`).
- **Speed.** Entering an attached interior takes 70 ms (`docs/HANDOFF.md`).

- **Research of 09.11.2026:** `SetCurrentCell` extinguishes the street in five places (`docs/SINGLE_INSTANCE.md`, 5.1; FINDINGS "T42, T43, T44 research"); NPC passing through the door and the makeup of a seamless NPC transition — `docs/NPC_DOORS.md`.

## To do

1. **Package diagnostics** (user's decision of 04.11.2026). Record `actor.package`: change of the current package of an NPC in the held cell.
   - Fields: from → to (package FormID and procedure), 8 frames of stack, ms from the opening and closing of the loading screen.
   - Find the function through which the current package changes (`AIProcess`, actor package evaluation), via `tools/name_functions.py` and dumpbin.
   - If NPC path replanning is found cheaply, record it too.
2. **Entry without detachment.**
   - Can the interior be left attached on `LoadingMenu.open` when entering through the portal door, so that `SetCurrentCell` gets an already attached cell?
   - What do `AE#13316` and `AE#18976` do with a cell in `kAttached`: attach again, skip, or break?
   - Risks: autosave (detach on `kSaveGame` stays), portal graph (`AE#106385`), Havok world, `ActorHold` hold.
3. **Fix** under an INI switch. A diagnostics-only build can be handed over first if the research leaves questions.
4. **5.14.** The solution must not assume "a street outside, an interior inside" and a single portal.

## How to verify in the game

1. F8 enabled. Enter and exit 3 times, inside watch the NPCs walking through the hall.
2. Control: F8 disabled, the same.
3. In the trace compare `actor.package` on entry with and without the portal. Note whose package changes and where the stack comes from.

## Conflicts

`InteriorSim.cpp` (`Tick`, `DetachNow`, `Blocker`, T11 hold) and `Events.cpp` (`LoadingMenu`). With T15 — if the hook for location-change reactions lands in the same place.
