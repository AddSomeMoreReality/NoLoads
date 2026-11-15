# T15. Game and script reactions to player location changes

Branch `exp/T15-location-triggers`. Code: none, research only. Test run: none. Can start right away.

## User's decision (04.11.2026)

- The bard song breaking on entry is solved with a general rule, without FormIDs in code.
- A list of everything that triggers on a location change or player entry is needed, along with how each case detects it.
- Scripts are not modified (`CLAUDE.md`, 5.14, item 7). What changes is the trigger condition: a hook decides whether a reaction fires as usual or not.
- Example. The bard song must not restart when entering through a portal. A story quest that starts events on player entry must fire.
- Possibly, later all such reactions will have to be suppressed, and dynamically placed markers will become the triggers for scripts. Assess this direction here, but do not implement it.

## What is known

- **`BardSongStarter`** (`000BE159`, T10). Story Manager starts it on the `CLOC` event. Node `000D1EEB` checks the condition `GetEventData(HasKeyword, L2, LocTypeInn)`. The fragment calls `BardSongsScript.PlaySongRequest`, which after 1 s calls `StopAllSongs` and starts a new song.
- **Run 0.8.27** (`docs/FINDINGS.md`, "Run 0.8.27"). On every entry, with and without the portal, all 16 song scenes stop after +1.5 s and a new one starts after +2.2 s. Without the portal the song has already ended by then, so the break is not heard.
- **171 quests** with a `CLOC` start in `Skyrim.esm`, `Update.esm`, and the DLCs (T10, item 9): `dun*QST`, `WIChangeLocation*`, `Favor*`, `CW*`, DLC1 and DLC2 quests, `WITavern`, `DialogueWhiterunArgueScene`.
- **USSEP** changes `BardSongsScript` (T14). The rule must work both with and without it.
- **ESM, VMAD, and BSA parsing scripts** from T10 and T14 are in the coordinator's scratchpad: `...\scratchpad\T10\`, `...\scratchpad\T14\`. They can be used as a starting point.
- **Sources:**
  - Papyrus sources — `E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\Source\Scripts`;
  - `Skyrim.esm`, `Update.esm`, DLCs — also in `Data`.

## To do

1. **Mechanisms.** How the game and scripts learn about a player location or cell change:
   - **Story Manager:** `CLOC` and other events related to location and cell. Search by `ENAM` on `QUST` and by `SMBN`/`SMQN`/`SMEN` nodes;
   - **Papyrus events** from the sources: `OnLocationChange`, `OnCellLoad`, `OnCellAttach`/`OnCellDetach`, `OnLoad`/`OnUnload`, `OnTriggerEnter`/`OnTriggerLeave` (including triggers at doors);
   - **conditions** in packages, dialogues, and scenes (`CTDA`): `GetInCurrentLocation`, `IsInLocation`, `GetInCell`, `GetIsCurrentLocation` and similar;
   - **engine:** where the game determines the player's current location and dispatches the event. IDs and offsets — `tools/name_functions.py` and dumpbin (`CLAUDE.md`, section 7).
2. **Table** in a separate file `docs/LOCATION_TRIGGERS.md`: quest or script, mechanism, condition, what it does, category. Categories:
   - **ambient** (bards, greetings, vignettes, `WI*`): on entry through a working portal the reaction is unnecessary or harmful;
   - **story:** the reaction must fire;
   - **unclear.**
3. **General rule without FormIDs.** Find data features that separate the categories: quest type (`QUST` `DNAM`), quest and Story Manager node flags, presence of journal stages, location keywords, etc. Test the rule against the whole table and count its errors.
4. **Hook points:**
   - where the engine forms `CLOC` and hands it to the Story Manager;
   - where the node's quest is selected;
   - where `OnLocationChange` is dispatched.

   What can be substituted: the old location in the event, the condition result, or the quest start itself. How the hook knows that the player entered through a portal that was already showing this location. How the rule works with multiple portals (5.14, item 1).
5. **Marker triggers.** Assess the idea of "suppressing such reactions and placing markers": what it gives a world without loading screens, what it costs, what it risks.
6. **Any cell pairs** (5.14, item 2). City gates (exterior — exterior) also change the location: the rule must work there too.

## Constraints

- No code, build, or game launch. Game data is only read.
- Your own scripts go in your own scratchpad folder, not in the repository.
- Do not install pip packages or programs.

## Result

- `docs/tasks/results/T15.md`, no longer than 80 lines:
  - mechanisms;
  - proposed rule and its errors on the table;
  - hook points with IDs;
  - code plan for the next task;
  - assessment of the marker idea.
- `docs/LOCATION_TRIGGERS.md` — the table, length unrestricted.

## Conflicts

No code. New files belong to this task only.
