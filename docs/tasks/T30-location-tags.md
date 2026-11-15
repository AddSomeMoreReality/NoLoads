# T30. Tags for location-change reactions: what to suppress on portal entry

Stage 2. Branch `exp/T30-location-tags`, worktree `..\CLR-T30` (submodule not needed). No code, no run. Can start immediately. The result is needed for the second T16 iteration. The first T16 iteration puts rule R8 into the game as is.

## Purpose

User decision dated 04.11.2026. Rule R8 (T15) divides quests into “suppress” and “keep” using a single attribute. Classification is needed: each location-change reaction has several tags, and the “suppress or not” decision is derived from those tags. The user's reference examples:
- `BardSongStarter` — a **technical** quest: on entry it stops the song the player already heard through the portal and starts a new one. Suppress it;
- a merchant's or innkeeper's greeting when entering a shop or tavern **repeats, but must remain**. R8 currently suppresses `WIGreeting` (the home owner greets the player) and `WITavern` (the tavern owner approaches and offers a seat): from the user's perspective, this is an R8 error;
- one-time scenes needed by an ordinary quest remain.

## What is known (T15)

- `docs/LOCATION_TRIGGERS.md`: section 4 (categories, R8, errors), section 9 (186 `CLOC` quests with conditions and attributes), section 10 (60 bound `OnLocationChange` scripts), section 11 (volumes at load doors). `docs/tasks/results/T15.md`.
- T15 analysis scripts (ESM, VMAD, conditions, rule, doors): `C:\temp\TEMP\claude\e--Games-Skyrim-ModCreation-My-attempts-NoLoads-CellLoadResearch\743dbd9b-dbf3-4ab3-9d3b-95be6732c2fe\scratchpad\T15\` (`common.py`, `a1_sm.py`, `a2_feat.py`, `a3_papyrus.py`, `a4_eval.py`, `labels.py`). Copy them into your scratchpad; do not put them in the repository.
- Script sources — `<Game>\Data\Source\Scripts`. Game: `E:\Games\Skyrim\The Elder Scrolls V - Skyrim`. Data is read-only (`CLAUDE.md`, 5.14, item 7).
- `OnLocationChange` must not be suppressed (49 of 60 scripts advance quests). Only Story Manager quest startup by `CLOC` can be filtered.

## Tasks

1. **Scope.** All player reactions to location changes: 186 `CLOC` quests and the scenes they start; 60 `OnLocationChange` scripts (tag for completeness; they must not be suppressed). Separately determine **how the vanilla merchant greeting works** when entering a shop: `CLOC` quest, `OnLocationChange`, or a conditional `Hello` line. If this is dialogue without `CLOC`, the T16 filter does not affect it: record that explicitly.
2. **Tag vocabulary.** Propose 6–10 tags. For each tag: definition, examples, data-based criterion. Starting point for refinement:
   - `once` — triggers once per playthrough (`RunOnce`, check of its own stage or a global);
   - `story` — journal, objectives, conditions on other quests' progress, needed for story progression;
   - `technical` — utility, without addressing the player (songs, music, ambient packages);
   - `restarts` — stops or restarts something already occurring at the location: scene, song, music, packages. The main harm on portal entry;
   - `greets` — reaction addressed to the arriving player: greeting, line, owner approaches;
   - `encounter` — random encounter, attack, NPC appearance;
   - `placeKind` / `placeSpecific` — condition on the kind of place (keyword) or on a specific place.
3. **Tagging.** For all 186 quests: manual tags (from scripts and scenes) and tags derived from data criteria. A “what is lost if suppressed” column. A discrepancy table, “manual ≠ data-derived.”
4. **What is available in the game.** For each criterion, state whether it can be read from game memory at `kDataLoaded` without FormIDs: `TESQuest::data` (type, flags), `objectives`, stages with journal, node and quest conditions, quest scenes, aliases, script names (through the VM, without bytecode analysis). If a criterion is unavailable in memory, propose a substitute or an EditorID exception in the INI. The mod must work with quests from other mods: a data-based decision is preferable to a list.
5. **Policy.** Rule “suppress ⇔ expression over tags” and its INI form for T16 (for example, `sSuppressIf=technical&restarts&!story&!greets`, `sForceKeep=`, `sForceSuppress=` by EditorID). Validate against the table: what is suppressed, what is not, errors compared with R8. User expectation: `BardSongStarter` is suppressed; `WIGreeting`, `WITavern`, and one-time scenes remain.
6. **5.14.** The policy does not divide transitions into “street/interior”: it works on entry and exit, for any cell pairs (item 2) and for multiple portals (item 1).

## Result

- `docs/LOCATION_TRIGGERS.md`: new “Tags and policy” section (vocabulary, criteria, policy, errors) and a tag column in the section 9 table. Put long tables at the end of the file.
- `docs/tasks/results/T30.md` following the `docs/tasks/README.md` template: policy outcome, what changes for T16 (which attributes to read at `kDataLoaded`, which INI keys), questions for the user — disputed quests, with a recommendation for each.

## Conflicts

Only `docs/LOCATION_TRIGGERS.md`. With T16 in content: the first T16 iteration builds the R8 cache, whose structure must accept tags without rework (this is in the T16 brief).
