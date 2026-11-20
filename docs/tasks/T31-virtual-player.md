# T31. Virtual player beyond the portal (research)

Stage 2. Branch `exp/T31-virtual-player`, worktree `..\CLR-T31`. First research without code, then a prototype based on its results. Start **immediately after the 0.8.32 control run (T28)**. User proposal dated 05.11.2026. If the approach proves viable, it will replace some Stage 2 and 3 tasks (list below). Therefore, T22 awaits the T31 verdict.

## Idea (user)

Place not a virtual camera beyond the portal, but a **full virtual player**. Scripts and engine functions believe the player is actually standing there, without site-specific compatibility patches: `GetPlayer` and related functions are hooked.
- The real player sees the image from the virtual player's camera on the portal.
- Sounds, subtitles, and everything else the virtual player “perceives” are routed to the real player.
- The virtual player has the same attributes, skills, perks, equipment, factions, and so on as the real player.

## What is known

- **The player in the engine is a global variable, not a function.** `PlayerCharacter::GetSingleton()` reads `NiPointer<PlayerCharacter>` at `RELOCATION_ID(517014, 403521)`. This pointer has 2,843 references, and 149 locations read the position (`+0x54`) directly (`docs/DISTANCE_CHECKS.md`, sections 3, 6). Only Papyrus has a function: `Game.GetPlayer` is native, and the script then calls the engine on the same object.
- **Transfer is currently site-specific** (`DISTANCE_CHECKS.md`; `docs/FEATURES.md`, 5.13): world camera `AE#36609` within `Portal::ActorScope` (`thread_local`, while the thread updates an interior actor); `GetDistance`/squared distance; sound location `AE#68007`; subtitles `AE#52638+0x122`; removal of `kNotVisible`; controller LOD `AE#78274`; fading `AE#105486`; traversal `AE#37277`; `PromoteActors`; `ActorHold` holding.
- **Street and interior update simultaneously**: `ProcessLists` jobs and cell traversals run in parallel on several threads. There is one player pointer for everyone: it cannot be replaced globally for “our” NPCs (`DISTANCE_CHECKS.md`, section 6).
- **A player location change means exactly one `SetParentCell` per transition** (`CLAUDE.md`, section 7; T15). The virtual player's presence must not generate `CLOC`, `OnLocationChange`, or other Story Manager events. Otherwise, 148 story reactions will fire prematurely or twice.
- Tools: `tools/re/scan_player_access.py` (who reads the player and position), `tools/re/vcalls.py`, `vtab.py`, `riprefs.py`, `tools/name_functions.py --xref`; F3 captures (`AccessWatch`) from 0.8.24 — `logs/run-0.8.24/CellLoadResearch.access-*.txt`.

## Research

1. **Inventory of “player identity.”** How the engine and scripts recognize the player:
   - reads of `AE#403521`;
   - comparisons with the player: pointer, `formID == 0x14`, player handle, `IsPlayerRef`;
   - `PlayerCharacter` vfuncs;
   - Papyrus `Game.GetPlayer` and conditions `RunOn Player` / `GetIsReference Player`;
   - detection, dialogue (`Hello`, T30 greetings), sound listener `AE#67710`, `PlayerCamera` (`AE#50785`);
   - cell grid around the player, active Havok area, `ProcessLists` processing levels, `high` selection.
   Group by subsystem and state frequency from F3. Separately mark which of these must see the real player, which the virtual player, and which both.
2. **Options.** For each: feasibility, per-frame cost, threads, risks, which site-specific patches it removes, portability to other versions (5.14, item 6).
   - **A. Second `PlayerCharacter` object.** What the player constructor registers (singletons, event sources, camera, UI, `ProcessLists`). The expectation is infeasibility; verify and record why.
   - **B. Actor double.** An ordinary `Actor` with player data (race, level, values, skills, perks, equipment, factions, crimes), collision-free and invisible. Placed at the player's eye position transferred through the door. Far-side subsystems ask for the “player” and receive it. How to synchronize data. Where the game checks specifically for `PlayerCharacter`.
   - **C. Temporal substitution.** Far-side updating is allocated its own main-thread phase while game jobs are stopped (like the portal pass at the scene list boundary). During that phase, the real player's position, cell, and world are substituted, then restored. Player data remains the same, with no synchronization. Can actor and cell updates for one side be isolated into a separate phase (`ProcessLists`, `AE#13291`, `AE#13325`)? Which threads read the player during that time: sound, physics, Papyrus VM, loading.
   - **D. Site-specific transfer** (as now) — the comparison baseline: what remains (T22).
   - **E. Pointer substitution only for the far-side thread** (user hypothesis dated 05.11.2026: “cells connected through the portal are processed in a separate thread — replace the global variable only for that thread”). The report must answer directly: is the hypothesis correct, and if not, exactly why.
     - **Does such a thread exist?** According to `docs/ARCHITECTURE.md`, 3.5 (verify with a thread-ID trace): the interior Havok world step runs on the physics thread immediately after the player's world; cell traversals (`AE#13291`, `AE#13325`) and actor updates (`ProcessLists`) run in game jobs on shared threads, intermixed with the street. The far side currently has no separate thread, but option C could allocate one.
     - **Can the global variable be made thread-specific?** Game code reads `AE#403521` directly, without a function. Options: rewrite all RIP-relative reads of this address at startup (found by scanning `.text`, without version binding) to stubs that check a `thread_local` substitution; hardware read breakpoint only in the required threads (each thread has its own DR0–DR3 registers, and `AccessWatch` already does this) — estimate the cost of an exception per read with thousands of reads per frame.
     - **What the substitution points to.** An object that game code accepts as `PlayerCharacter` is needed: a second object (A) or its shadow copy. This is the option's main question.
3. **Routing to the real player.** Image — portal pass with the virtual player's camera (this is T27's “view”). Sound: one XAudio2 listener with location transfer (as now) versus a second listener and mixing. Subtitles, dialogue, NPC address through the door, detection and combat through the opening. What the virtual player should not “perceive”: collision, location entry, rewards, crimes?
4. **Conflicts with project rules.** Exactly one `SetParentCell` (T15/T16); many portals means many virtual players (5.14, item 1); any cell pairs (item 2); any camera (item 4); mirrors (item 5); game data unchanged (item 7); Engine Fixes (item 8).
5. **Verdict and plan.** Recommendation: which option (or combination) to adopt, what it covers, prototype success criterion. If a prototype is recommended — a brief for the next task with a first step verifiable in the game: for example, interior NPCs detect and greet the player through the door and simplify with distance, while their distance and camera hooks are disabled.

## What success would cover (coordinator's estimate, verify)

- **T22** (remaining distance checks) — entirely or almost entirely.
- **T21** (living street while the player is inside) — the street sees the virtual player outside.
- **T23** (sound through the opening) — partially: listener in place.
- **T27** (any camera) — pass camera = virtual player's camera.
- Parts of `PortalActors` and portal hooks (world camera, `GetDistance`, `kNotVisible`, LOD, fading, `PromoteActors`) would become unnecessary.

## Result

- `docs/tasks/results/T31.md` (≤ 80 lines): inventory, options table, verdict, prototype brief.
- Long inventory tables — new file `docs/VIRTUAL_PLAYER.md`.
- Analysis scripts — in the scratchpad. Game data is read-only.

## Conflicts

None: documents only. In content — T22 (awaiting verdict), T21, T23, T27.
