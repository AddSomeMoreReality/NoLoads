# T32. Inventory of player reads and the player's location in the far-side scope

Stage 2. Branch `exp/T32-player-reads`, worktree `..\CLR-T32`. Code: new module `src/PlayerReads.*` (its own hooks, one line in `Hooks::Install`, T28 rule in `Hooks.h`), generalization of `Portal::ActorScope`. Run: yes, in steps. User decision dated 05.11.2026: T32 now, T22 afterward and based on its data. Basis — `docs/tasks/results/T31.md`, `docs/VIRTUAL_PLAYER.md`.

## What is known (T31, from 1.6.1170 code)

- **There will be no full virtual player:** the player has one identity (pointer `AE#403521`, handle `AE#403520`, FormID 0x14), and the engine will not accept a second object as the player. The far side has no thread of its own: the interior Havok step, actor updates, and cell traversal run in `BSJobs` jobs intermixed with the street.
- **What works:** a `thread_local` scope for the duration of far-side work (like `Portal::ActorScope` for the world camera) and substitution not of the player, but of their **location** — position, angle, cell.
- **The pointer is read at 2,843 locations in 1,349 functions**, in 9 instruction forms (2,604 `mov`, 223 `cmp`, 13 `cmov`, 1 `add`, 2 writes to `AE#36629`), all at least 7 bytes long: each can be replaced with a `jmp` to a stub. “Location-only” sites number 90 in 78 functions (`AE#18889`, `AE#36661`, `AE#39322`, `AE#37618`, `AE#39673`, etc.), found by linear disassembly — verify with a decoder.
- **Pointer-load frequency per frame has not been measured** (F3 sees only position: 699 reads per frame outside, about 94% through `GetDistance`, already through the door).

## Tasks

### Step 1. Inventory (game behavior unchanged), build 0.8.35

1. **Find sites** when the plugin loads: all instructions in `.text` with a RIP-relative reference to the address `RELOCATION_ID(517014, 403521)`. The address comes from Address Library, so the search is version-independent (5.14, item 6). Compare bytes against the form; skip a site if it or a nearby location already contains a foreign patch (`E8`/`E9`/`FF 25`, relocated Detours prologue). Do not touch the two writes in `AE#36629`.
2. **Stubs** (xbyak, `--skse_xbyak=y`, or a custom generator in the trampoline): per-site “inside scope / outside scope” counter, then the original instruction with RIP adjustment, then `jmp` back. Preserve flags and registers (`cmp` gets its flags from the original instruction; count using `pushfq`/`popfq` or without modifying flags). Counters are lock-free; losing a few counts to races is acceptable.
3. **Far-side scope:** generalize `Portal::ActorScope` into a scope identifying the portal (5.14, item 1) and also place it around our interior traversal (`AE#13291`/`AE#13325`, `InteriorSim::AfterCellsUpdate`/`AfterAnimatedRefs`) and the interior Havok step (`AfterWorldStep`).
4. **Key-triggered dump** (available keys: F1–F4, F6–F8 — check `Settings`): sites `AE#id+off`, form, count inside and outside the scope, over the last N seconds. Log the number of found, rewritten, and skipped sites with reasons.
5. **INI `[PlayerReads]`:** `bCensus=0` by default (0 — nothing is rewritten), the user sets 1 for the run; dump key.
6. **Player handle** (`AE#403520`, 112 sites): rewrite the same way if the forms permit, and count separately. Otherwise, record why not.
7. **Access kinds** (user decision dated 05.11.2026). Determine what the far side does with the player: obtains their location or recognizes their identity. This determines whether location substitution (step 2) suffices or a second player object is needed (item 8).
   - Each site's kind is determined **outside the game**, by a script based on the T31 analysis (`scan_ident.py`, `group.py` in the T31 scratchpad, window after the pointer load). Stubs only count; no runtime classification is needed.
   - Kinds: location only (position, angle, cell); comparison with an actor; `PlayerCharacter` field read (≥ `0x2B8`); field write; argument passing (with the called function); vfunc call (with index); handle.
   - The result contains a table “kind → number of sites and reads inside and outside the scope,” by subsystem (`VIRTUAL_PLAYER.md`, 2.4), and the 30 most frequent in-scope sites with their kinds.
8. **Player constructor** — research without code, in parallel with items 1–7. User: the player is created once (`AE#36629`: 0xBE8 bytes, constructor `AE#40411`), so it can be created a second time and the necessary sites redirected. Verify:
   - what `AE#36629` and `AE#40411` register: singletons, event-source subscriptions, camera, interface, `ProcessLists`, handle and FormID 0x14, Havok, anything else. What would be duplicated or broken by a second call;
   - whether a second object can be created without these registrations (allocation and constructor without writes to globals), and which object subsystems it would obtain on its own (inventory, values, perks, magic, `AIProcess`);
   - what must be synchronized so that attributes, skills, perks, equipment, and factions match the real player, and at what cost;
   - **stored references**: combat, package, and dialogue targets store the real player's handle. Within the scope, these resolve to the real player, not the second one. How often does this happen (from item 7: handle and comparisons);
   - verdict on option “A + E”: a second object returned by stubs within the far-side scope. Is it feasible, what does it provide beyond location substitution, and what are the risks.

### Decision after step 1

Based on the item 7 table and item 8 verdict, the coordinator and user choose step 2: location substitution (below) or a second player object in the scope (“A + E”). If “location-only” sites dominate within the scope, choose location substitution.

### Step 2. Player location in the scope (if location substitution is chosen), separate build

6. “Location-only” sites (decoder verification at startup; Zydis through an xmake package or a custom form table) **inside the scope** read a location block instead of player fields: position and angle transferred through the door, and the cell beyond the door. The block comes from the observer passed to the scope, not the global camera (5.14, item 4). `Portal::DistanceThroughDoor` obtains the location from the same block.
7. Read-only substitution: no events, `SetParentCell`, or `CLOC` are generated (the single-`SetParentCell` rule, `CLAUDE.md`, section 7).
8. `[PlayerReads] bScopedPlace=1` — step 2 switch. The coordinator will refine step 2 using the step 1 dump.

## Safety

- **Patching game code in 1,349 functions is the main risk.** Rewrite before game threads start (during plugin load, like other hooks). If another plugin later patches a rewritten site, it must be visible: the dump includes sites where the bytes at the `jmp` are not ours.
- Stubs do not call functions, acquire locks, or touch the heap. Read `thread_local` from a stub through the TEB (`gs:[0x58]`) while preserving registers.
- Trampoline: calculate the size for 2,843 stubs in advance (`SKSE::AllocTrampoline` or a custom block near `.text` within ±2 GB). The current trampoline is 256 bytes (after T20).
- Everything defaults to `bCensus=0`: without the key, the plugin behaves like 0.8.32.
- Engine Fixes (5.14, item 8): patches game code itself. Compare the number of skipped sites with and without it during the run.
- **Ghidra** (if T33 is complete: project and tools — `docs/GHIDRA.md`): use `tools/ghidra/` tools to place function pseudocode in the scratchpad; the database is read-only, and the curator (T33) edits it. The Ghidra project and exported pseudocode are Bethesda code: do not put them in the repository; documents contain only conclusions, addresses, and short instruction quotations.
- Before building — checks per `CLAUDE.md`, section 7 and the “Pre-build checks” section in the result: instruction forms via dumpbin on a sample of 20 sites of each form, absence of jumps into rewritten bytes (`.text` scan), flag preservation.

## How to verify in the game (step 1)

1. `bCensus=1`, Engine Fixes disabled. Tavern, F8, stand at the door for 30 s, dump; enter, stand for 20 s, exit, dump.
2. No crashes or hangs; frame time within ±0.2 ms of the 0.8.32 control run (same place).
3. The dump contains in-scope sites, with `AE#18889` expected among them; the number of skipped sites is in the log. The coordinator builds the access-kind table (item 7).
4. `bCensus=0`: behavior matches 0.8.32.
5. Short run with Engine Fixes and `bCensus=1`: launch, portal, one dump; number of skipped sites.

## What this covers

T22 partially: direct far-side reads of the player's location and a complete list of what it requests from the player. Line of sight through 3D (`Get3D1`), camera, `kNotVisible`, LOD, and fading remain with T22 and portal hooks.

## Conflicts

New module, one line in `Hooks::Install`, `Settings.*`, INI, `main.cpp` (trampoline). `Portal::ActorScope` (`PortalActors.cpp`, `Portal.h`) — with T29. `InteriorSim.cpp` — with T20 (merge T20 earlier). T22 starts after step 2 or after the coordinator's decision based on the dump.
