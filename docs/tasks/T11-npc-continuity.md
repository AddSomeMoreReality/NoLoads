# T11. NPCs jump during transition (Saadia, Idolaf)

Branch `exp/T11-npc-continuity`. Code: `InteriorSim`, hooks. A game run is required. Research (item 1) can start before T02.

## What is known

Data from run 0.8.25 (`logs/run-0.8.25/`): the `actor.jump` record, written by `src/ActorJumps.cpp`, with the `SetPosition` (`AE#19790`) stack.

- **Player exits the tavern.**
  - The portal attaches the interior and raises its NPCs to high after ~1.7 s. In `src/InteriorSim.cpp`, this is `kSettleUs` = 1.5 s after the loading screen, then `MoveToHigh`: `promote` record, `levelsBefore` = 1.
  - Until then, interior NPCs are in middleHigh, and the game moves them along their paths in large steps: `AE#37892+0xa3` → `AE#37877+0xda` → `AE#37313+0xa0` → `SetPosition`.
  - Saadia `000D7505`: 570 + 1413 units in two frames (242.5 s), 458 + 1407 (270.4 s). The user saw her disappear and then found her standing in the kitchen after entering.
- **Player enters.**
  - NPCs in the cell the player enters “catch up” in high: `AE#37621+0x34d` → `AE#37618+0x9dc` → `AE#37309+0x31`.
  - Olava `0001A699` — 1187 units (120.5 s), Saadia — 346 (154.4 s), Idolaf `0001A689` — 1648 (120.9 s, immediately after the door `AE#19796`).
  - The same happens to guards outside when the player exits (74.7 s, 225.8 s).
- **Jumps through doors (`tools/run_digest.py`, 0.8.25 and 0.8.26).** `kind=cell` jumps of 23–27 thousand units are NPCs passing through a loading door (`AE#19796`): from middleHigh via `AE#37878 < AE#37892 < AE#41717`, from high via `AE#39457 < AE#37360 < AE#41381`.
  - Almost all are ordinary city activity. A guard and Nelkir enter Dragonsreach ~24 s after loading the save in both runs: in 0.8.25 this coincided with the player entering the tavern; in 0.8.26 it happened beforehand. Olfrid enters House Battle-Born, Ahlam enters the Temple of Kynareth, Olava exits the tavern.
  - Only Idolaf concerns the tavern: he enters 0.25–0.7 s after the player (0.8.25 — 120.8 s, 0.8.26 — 60.8 s), exits 0.4 s after the player (0.8.26, 98.3 s), and enters again while the portal is active, this time from high (108.8 s).
- **Run 0.8.24.** Idolaf receives bard-audience package `00108ED6` and passes through the door instantly, using the path for an NPC not in high: `AE#41297` → `AE#41717` → `AE#37892` → `AE#37878` → `AE#19796`. `AE#37892` appears both in this path and in the large steps.
- **Portal attachment of the interior** — `docs/FEATURES.md`, 5.11 (“Attachment”). The portal detaches the interior when `Loading Menu` opens.

## Tasks

1. **Research, no code.** What `AE#37618`/`AE#37621` do (catch-up: on player cell change? on entering high?) and what `AE#37877`/`AE#37892` do (middleHigh path movement). Conditions, flags, and time over which catch-up occurs. Tools: `tools/name_functions.py`, dumpbin (`CLAUDE.md`, section 7).
2. **Fix:**
   - **exit:** portal-interior NPCs must not drop out of high. Options:
     - raise them as soon as the loading screen closes, without `kSettleUs`, if the interior is already loaded;
     - prevent the game from lowering them: hook the transition to middleHigh for portal-interior actors;
   - **entry:** do not catch up those already in high through the portal (hook `AE#37618` for them).

   Do not change NPC packages or data (`CLAUDE.md`, 5.14, item 7).
3. **Control without the portal** (F8 disabled): check whether the same jumps occur. Rule — `CLAUDE.md`, section 7.

## How to test in game

F8 enabled. Wait until Saadia walks from the kitchen to the counter. Exit and watch through the portal, then enter again. Repeat 2–3 times. Then repeat with F8 disabled.

## Conflicts

`InteriorSim.cpp`: attachment and raising NPCs. Future T20 (entry without detaching the interior) comes after this task.
