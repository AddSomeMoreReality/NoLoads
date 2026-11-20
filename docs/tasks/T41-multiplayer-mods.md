# T41. Multiplayer mods: how they keep several players in one world

Stage 2, research without code. Branch `exp/T41-multiplayer`, worktree `..\CLR-T41` (without the submodule). No run. Web and sources. Reads the Ghidra base only (T35), to compare their hooks with our names.

## Why

T31: the engine has a single player identity, a second `PlayerCharacter` is infeasible, the escape is substituting the player position. Multiplayer mods solved a similar task — several "players" in one world, each with their own loaded cells. We need to know how they did it and where they hit the wall. This supports T32 (the far side frame), 5.14, item 1 (many portals) and item 2 (cells held beyond the grid).

## Mods

Skyrim Together Reborn (TiltedEvolution), SkyMP (Skyrim Multiplayer), Keizaal (a server based on STR), and the early ones (Skyrim Online, the first version of Skyrim Together). For each — repository, license, game version, state.

## Questions

1. **Other players:** what are they in the engine — NPCs (`Character` with a copied base form?), a second `PlayerCharacter`, something custom. How they get equipment, animations, attributes.
2. **Player uniqueness:** what they do with the places that know the one player: AI and combat targeting the player; dialogue; quests and Story Manager; crimes; camera; input. Do they hook, substitute, or leave them.
3. **Cells:**
   - each client loads its own grid;
   - what happens with another player in an unloaded cell or interior;
   - do they keep cells loaded beyond the grid and how;
   - who is the "owner" of a cell (who runs the NPC AI and physics);
   - how door transitions work for different players.
4. **Hooks:**
   - the list of engine functions they hook (from their sources: Address Library ID or offsets) → our names via the base;
   - which coincide with ours (`Hooks*.cpp`), which are new to us.
5. **What is broken or unsolved** (bugs, limitations from documentation, issues): which of this threatens us.

## Rules

Do not copy code: GPL/AGPL. Ideas and facts (which function is responsible for what) — allowed, with a source. ID → name pairs — to the curator as candidates with source and license.

## Result

- `docs/MULTIPLAYER_MODS.md` (≤ 150 lines): mod table, answers to the questions, what applies to the portal.
- `docs/tasks/results/T41.md`: name candidates for the curator, conclusions for T32 and 5.14.

## Conflicts

None.
