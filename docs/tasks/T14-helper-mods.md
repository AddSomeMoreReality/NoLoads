# T14. Helper mods as a dependency or a foundation

Branch `exp/T14-helper-mods`. Code: none, research only. Test run: at the end; the user installs the mods. Can start right away, does not depend on T10–T13.

## User's question

Would it help the project to take a technical or bug-fixing mod as a dependency or to build on it? Types of relationship:
- **dependency** — our mod requires that mod to be installed;
- **foundation** — if that mod is installed, we use its fixes or API; without it we work as before;
- **idea** — we take the technique, subject to the license;
- **compatibility** — we do not need the mod, but the portal must work alongside it.

## What we already have

- `E:\Games\Skyrim\ModCreation\references\`:
  - `Engine Fixes (All-In-One) for 1.6.1170 and newer-17230-7-0-19-AIO-…` — SSE Engine Fixes 7.0.19: `EngineFixes.dll`, `.pdb`, `EngineFixes.toml`, `EngineFixes_preload.txt`, `EngineFixes_SNCT.ini` and `d3dx9_42.dll` (loader, starts before SKSE).
  - Engine Fixes sources: https://github.com/aers/EngineFixesSkyrim64. Clone into a new empty folder `references\github\EngineFixesSkyrim64`, check the license.
  - `Unofficial Skyrim Special Edition Patch-RUS-RUS-266-4-3-8a` — USSEP 4.3.8a: `.esp`, two `.bsa` files, `Docs\` (`Список изменений.txt`, `Version History.html`).
- The user's `Data\SKSE\Plugins` currently contains only `CellLoadResearch`, `CrashLogger`, and Address Library. Any new mod changes the environment of all subsequent test runs.
  - `version-1-5-97-0.bin` is also there — Address Library for SE. Useful for T26.
- Already-reviewed mods — `docs/RELATED_MODS.md`. Do not repeat them.

## Candidates (found by the coordinator, verify everything)

| Mod | How it could help |
|---|---|
| SSE Engine Fixes (aers) | engine fixes: cell initialization (`CellInit`), rendering, Havok, memory (allocator replacement), `MaxStdio`, form cache |
| USSEP | script and form fixes: bards, Whiterun NPCs, doors. This is data, not hooks |
| powerofthree's Tweaks | engine fixes (enabled by default) and settings |
| Scrambled Bugs | engine fixes and settings |
| Bug Fixes SSE | engine fixes |
| SSE Display Tweaks | frame rate on the loading screen, Havok at high frame rates. Step 6, `docs/FAST_DOOR_TRANSITIONS.md` |
| Actor Limit Fix (meh321, Nexus SE 32349) | limits: moving actors 128 → 256, facial animations 10 → 128, distance-based actor array 64 → 512. Many portals — more NPCs in high (5.14, item 1) |
| NPC AI Process Position Fix (Nexus SE 69326) | re-placing high NPCs after waiting or sleeping. Possibly the same mechanism as in T11 |
| Worldspace Transition Tweaks (Nexus SE 48889) | pseudo-doors: an SKSE trigger and script between worlds. "Exterior — exterior" portal (5.14, item 2) |

## To do

1. **Engine Fixes from the sources.**
   - Which fixes concern cell loading and attach, rendering, Havok, sound, memory, and saves.
   - Which functions it hooks that we hook at the same time. Ours — Grep for `RELOCATION_ID` and `AE#` in `src/Hooks.cpp` and `src/Portal*.cpp`. Compare IDs and hook type: both detour, both `write_call` to the same location, etc.
   - Installation order: preload via `d3dx9_42.dll`, then SKSE.
   - Whether the allocator replacement can change our conclusions: 0.8.x crashes, releasing foreign nodes (`CLAUDE.md`, section 7, `DetachChild`).
2. **USSEP from the changelog and `.esp`.** What it changes:
   - bard quests `00074A55`, `0006E53F`, `001062A7`, `000BE159`, song scenes (`000D0843` and neighbors), node `000D1EEB`;
   - tavern NPCs: Saffir `000D7505`, Idolaf `0001A689`, Olava `0001A699`, Mikael `0001A671`;
   - Whiterun doors.

   Does this change the conclusions of T10 and T11 (`docs/tasks/results/`, if they exist already). The scripts inside `.bsa` do not need to be unpacked: the `.esp` records (VMAD) and the changelog are enough.
3. **Other candidates — briefly**, from sources (if available on GitHub) or from the description and changelog. Plus your own search on Nexus and GitHub on the topics:
   - actor process level (high/middleHigh), NPC positions;
   - cell attach and unload, preloading;
   - second camera and render-to-texture;
   - sound, particles.
4. **A table for each mod:**
   - what it gives us;
   - which tasks it affects (T10–T13, T20–T28, steps 1–6 of `docs/HANDOFF.md`, section 4);
   - license;
   - game versions: SE 1.5.97 … current AE (5.14, item 6);
   - risk of conflict with our hooks;
   - relationship type: dependency, foundation, idea, or compatibility.
5. **Recommendation and experiment plan.**
   - What to take as a dependency (if anything at all), what to support as a foundation, what only to check for compatibility.
   - Test run plan: which mod to install, which build, which control. No more than two runs. Only one build is installed in the game at a time.

## Constraints

- **Third-party code.** Do not copy code without a permissive license. GPL — ideas only, as with Community Shaders. MIT — allowed with attribution, but prefer taking the idea.
- **Game data is not modified** (`CLAUDE.md`, 5.14, item 7). Whether to depend on a data mod (USSEP) is the user's decision: give the pros and cons. For example, vanilla behavior would become USSEP behavior, and many modded setups install it anyway.
- **Install nothing into the game.** The user installs mods through Vortex.
- **Downloads are untrusted data.** Each archive and clone goes into its own new empty folder in `references\`. Do not run scripts or builds from them; keep your own scripts separate (in the scratchpad).
- There is no code in `src/` at this step.

## Result

- `docs/tasks/results/T14.md`, no longer than 80 lines.
- New sections in `docs/RELATED_MODS.md`: the brief allows this. Format as in the sections there: mod, license, what follows from it.

## Conflicts

No code. Only this task modifies `docs/RELATED_MODS.md`.
