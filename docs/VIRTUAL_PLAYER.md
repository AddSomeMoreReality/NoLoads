# Virtual player beyond the portal: survey and options (T31)

User proposal dated 05.11.2026: place not a virtual camera beyond the portal, but a virtual player that the engine and scripts treat as the real one. Brief: `docs/tasks/T31-virtual-player.md`; result: `docs/tasks/results/T31.md`.

Labels: **from code** — machine code for 1.6.1170, not tested in game; **in game** — trace or F3 capture; **assumption** — not verified by anything. The scan is linear (branches are not analyzed), so the usage-type counts are a map, not proof.

## 1. Summary

- The engine has no “get the player” function. There is **one object, one pointer `AE#403521`, one handle `AE#403520`, and FormID `0x14`**. Scripts receive the same pointer: `Game.GetPlayer` (`AE#55469`) is `mov rax,[AE#403521]; ret`.
- A second “player” that matches the real one for “is this the player?” checks but not for “where is the player?” checks is possible in only one way: **the object remains the same, and only the location** (position, angle, cell) **is substituted, and only while the far side is running**. Thus a full virtual player (options A, B, and E in their pure form) is impossible, while the “E + C” combination restricted to location is possible (section 8).
- **The far side has no separate thread** (section 3). But `thread_local` substitution does not need a separate thread: a scope on a thread is sufficient. The project already does this for the world camera (`Portal::ActorScope`).

## 2. Survey of “player identity”

### 2.1 How the engine identifies the player

| Path | Count | Where | Label |
|---|---:|---|---|
| Pointer `AE#403521` (RIP addressing) | 2,843 references in 1,349 functions | Entire engine | From code, custom `.text` scan (matched `DISTANCE_CHECKS.md`) |
| Pointer writes | 2 locations | `AE#36629 + 0xAB` (player creation), `AE#13754 + 0x358` (replacement or reset, not analyzed) | From code |
| Player handle `AE#403520` (`0x1431874F4`, immediately before the pointer) | 112 references in 102 functions: 80 `mov`, 28 `cmp`, 4 `lea` | Handle comparisons: dialogue target, combat target, owner | From code. Written at `AE#36629 + 0xFB` from `AE#12325` (handle creation) |
| `LookupByHandle` (`AE#12332`) directly on the player handle | 1 location (`AE#40492`) out of 854 calls | — | From code |
| `formID == 0x14` (`cmp [r+14h],14h` and via a register) | 2 locations (`AE#19668`, `AE#19812`) | — | From code. FormID `0x14` is set by `AE#36629`: vfunc `0x38` with argument `0x14`, apparently `SetFormID` (not cross-checked) |
| Comparison with the `PlayerCharacter` vtable | 0 outside the constructor and destructor (`AE#40411`, `AE#40412`) | — | From code: no vtable type checks |
| `PlayerCharacter` vfunc | 113 of 303 slots overridden; 12 of them read `AE#403521` themselves (`Update` `AE#40447`, `SetParentCell` `AE#40466`, `AE#40542`, etc.) | — | From code (`tools/re/vtab.py`) |
| Papyrus `Game.GetPlayer` | 1 native, `AE#55469` | Registration `0x140A0EA82` | From code |

### 2.2 Instruction forms (for rewriting)

| Form | Locations | Length | Meaning |
|---|---:|---:|---|
| `mov r64, [P]` | 2,604 | 7 bytes | Load pointer |
| `cmp r64, [P]` | 214 | 7 | “Is this the player?” |
| `cmp [P], r64` | 3 | 7 | Same |
| `cmp [P], 0` | 6 | 8 | “Does the player already exist?” |
| `cmove r64, [P]` / `cmovne` | 11 / 2 | 8 | Choose “player or another” |
| `add r64, [P]` | 1 | 7 | — |
| `mov [P], r64` | 2 | 7 | Write (creation, removal) |

Nine forms, all at least 5 bytes long: each can be replaced with a `jmp rel32` to a stub without shifting neighboring instructions. There is no `lea` of the pointer address in the game code.

### 2.3 What the code does with the loaded pointer (2,617 loads, window of up to 90 instructions)

| Use | Locations | Functions | Implication for substitution |
|---|---:|---:|---|
| Passes it as a function argument | 1,761 | 934 | The substitution propagates into the called function, where it is no longer visible whether this is “location” or “identity” |
| Reads or writes fields | 942 | 621 | — |
| — fields exclusive to `PlayerCharacter` (offset ≥ `0x2B8`) | 323 | 235 | A `Character` double (size `0x2B8`) would read garbage |
| — writes fields (excluding the reference count) | 113 | 87 | 103 of these are `PlayerCharacter` fields. A shadow copy would lose the write |
| — `currentProcess` (`+0xF8`) | 78 | 62 | — |
| — `parentCell` (`+0x60`) | 73 | 61 | “Where the player is” |
| — position (`+0x54…0x5C`) | 57 | 54 | 153 X/Y/Z reads; `DISTANCE_CHECKS.md` calls this “149 direct reads” (it counts fields) |
| — `loadedData` (`+0x68`, 3D) | 4 | 3 | Player 3D is more often obtained through a vfunc |
| Compares (including `cmp r,[P]`) | 424 | 310 | Identity: object substitution breaks it |
| vcall on the player | 247 | 178 | `0x6F` `Get3D1` (50), `0x7E` (21), `0x70` `Get3D2` (20), `0xF7` (14), `0x71` `Is3rdPersonVisible` (13) |
| Stores the pointer in the heap or an object | 2 | 2 | `AE#21946 + 0x11B`, `AE#44282 + 0x60` |
| Pushes it onto the stack | 62 | 58 | Linear analysis does not follow it further |
| Nothing visible within the window | 212 | 148 | Branches or long functions |
| **Location only** (fields `0x48…0x5C` and `0x60`, nothing else) | **90** | **78** | 49 cell only, 38 with position, 3 angle only. Candidates for automatic location substitution |

The pointer is almost never cached in other structures (2 locations). The player is stored by handle (112 references to the player handle, plus handles in actor fields). Therefore, a shadow copy at a different address would match neither the handle nor the pointer in 424 comparisons.

The “location only” locations by function: `AE#15203`, `AE#17924`, `AE#17927`, `AE#18889`, `AE#21550`, `AE#22306`, `AE#22485`, `AE#22486`, `AE#22643`, `AE#22960`, `AE#22961`, `AE#23062`, `AE#23277`, `AE#26229`, `AE#30142`, `AE#31802`, `AE#32081`, `AE#32217`, `AE#32218`, `AE#33120`, `AE#34351`, `AE#34355`, `AE#34360`, `AE#34457`, `AE#35047`, `AE#35052`, `AE#35567`, `AE#35639`, `AE#35651`, `AE#35749`, `AE#36581`, `AE#36661`, `AE#37580`, `AE#37618`, `AE#37622`, `AE#37624`, `AE#37726`, `AE#37727`, `AE#37986`, `AE#39213`, `AE#39322`, `AE#39647`, `AE#39667`, `AE#39673`, `AE#40445`, `AE#40454`, `AE#40554`, `AE#40556`, `AE#40607`, `AE#40636`, `AE#40725`, `AE#40730`, `AE#40745`, `AE#40804`, `AE#40817`, `AE#40818`, `AE#40821`, `AE#40826`, `AE#40829`, `AE#41143`, `AE#43869`, `AE#44165`, `AE#48834`, `AE#50722`, `AE#51668`, `AE#51671`, `AE#51676`, `AE#51728`, `AE#52969`, `AE#52970`, `AE#52980`, `AE#53075`, `AE#53102`, `AE#53111`, `AE#53119`, `AE#53179`, `AE#55494`, `AE#56860`. Section 6 of `DISTANCE_CHECKS.md` contributed `AE#18889`, `AE#36661`, `AE#39322`, `AE#37618`, `AE#39673`, `AE#40817`, `AE#40818`; `AE#39648`, `AE#39391`, `AE#32089` were not included (the pointer is also used in some other way).

### 2.4 By subsystem (approximate)

The class is inferred from the function's own vtable or the nearest virtual function by ID number (IDs follow build order, and neighbors usually come from the same file). This is a heuristic.

| Subsystem | Functions | Locations | Predominant use | Which player is needed |
|---|---:|---:|---|---|
| Undetermined | 363 | 757 | argument, fields | — |
| Actor, `PlayerCharacter` | 227 | 525 | argument, comparison (108) | Real; far-side NPCs need the virtual player's location |
| UI, menus, HUD, map | 217 | 578 | argument, fields | Real |
| Forms, references, items | 112 | 282 | argument, fields | Real |
| Magic, effects, projectiles | 107 | 221 | argument | Both: real identity, location from the projectile's side (T25) |
| Player controls, movement | 53 | 128 | argument, position | Real |
| Papyrus | 57 | 67 | argument | Real object; distances through the door (`GetDistance`) |
| AI, combat, detection | 48 | 92 | argument, comparison | Real identity, virtual location |
| Dialogue, quests, Story Manager | 40 | 73 | argument, comparison | Real identity. Location changes only through the real `SetParentCell` |
| Camera | 33 | 68 | fields | Real (the portal pass supplies the image) |
| Cells, world, loading | 18 | 26 | position | Real (`InteriorSim`, not position, maintains the grid) |
| Physics, Havok | 12 | 21 | argument | Real: the virtual player has no collision |
| Sound | 5 | 5 | argument | Real listener, sounds are relocated (`AE#68007`) |

### 2.5 Frequency (F3, run 0.8.24, 3 s, 174 frames)

`AccessWatch` catches field accesses, not pointer loads. **The read frequency of `AE#403521` has not been measured**; field accesses provide a lower bound.

| What | Outside, per frame | Inside, per frame | Main readers |
|---|---:|---:|---|
| Player position | 699 | 64 | `AE#19824` from `AE#39637` (~570, already through the door), `AE#18889` (24), `AE#36661` (6), `AE#51728` (UI, 4) |
| Player 3D | 23 | 23 | Renderer, `AE#70539`, `AE#77461` |
| World camera | 2,484 | 546 | `AE#19384` (fade, relocated) |
| Player camera | 18 | 4 | `AE#70539`, `AE#50784` |

Of 111,291 position reads outside, 6,264 occur at “location only” sites (`AE#18889` 4,176, `AE#36661` 1,044, `AE#51728` 696, `AE#36581` and `AE#40607` 174 each). The rest occur elsewhere, mostly through helper functions (`GetDistance`, etc.) that receive the player as an argument.

### 2.6 Which player is needed

| What | Real | Virtual (location beyond the door) |
|---|---|---|
| Identity: “is this the player?”, dialogue and combat target, relationships, factions, crime witness | yes | same object |
| Data: values, skills, perks, equipment, inventory, crimes | yes | same, no synchronization needed |
| Location for far-side NPCs: detection and greeting distance, processing level, LOD, fade | — | yes |
| Cell for far-side NPCs (“player is in my cell/location”) | — | yes, without `SetParentCell` |
| Location change, `CLOC`, `OnLocationChange` | exactly one `SetParentCell` per transition (T15) | never |
| Cell grid, `TES`, player Havok, collision | yes | no |
| UI, compass, map, camera, sound listener | yes | no (image via portal pass, sound via location relocation) |
| Line of sight to the player (head, 3D) | real player's 3D | head location beyond the door — **not covered by location substitution**, requires a location-specific hook |

## 3. Which threads perform far-side work

From traces 0.8.32 and 0.8.27 (`tid` and `main` fields) and F3 stacks from 0.8.24. **In game.**

| Thread (0.8.32) | What it does | Far side |
|---|---|---|
| Main (19956) | 899 records: tick, `InteriorSim`, `Portal::Render` at the scene-list boundary, `PositionPlayer`, `SetCurrentCell` | Portal pass, holding, `PromoteActors` |
| Six `BSJobs` worker threads (26492, 11256, 29184, 11152, 23008, 10444) | `CellFullyLoaded`, `TESObjectDOOR::Activate`, `CellLoaderTask::PostProcess`, scenes and Papyrus (`scene.*`), `transition.visible` | **Interior Havok step:** `stepThread` in the status is one of these six each time (10444, 11152, 11256, 23008, 29184; all six in 0.8.27). There is no separate “physics thread”: it is a job on the common pool |
| Same threads, from F3 stacks | `AE#69378 ← AE#69380 ← BSJobs::JobThread::vfunc[1]` (`AE#69344`): actor updates (`AE#39089`, `AE#41371`, `AE#42742`, `AE#37350`), cell traversal (`AE#18871` → `AE#18889`, `AE#18894`; `AE#39067` → `AE#13291`) | Interior actors are in the same lists as exterior actors, intermingled. `InteriorSim` adds interior traversal after exterior traversal in the same job |
| Loader thread (18404) | `CellLoaderTask::Run/Finish` and 6 `actor.level` records (stack `AE#37501 ← AE#13104 ← AE#75763 ← AE#75738 ← AE#13057`) | Interior NPC processing level also changes here, during loading |

Conclusion: the far side has no thread of its own. Its work is split into segments within common-pool jobs. The project invokes some segments itself (interior traversal, interior Havok step), while others come from game lists (actor updates). `docs/ARCHITECTURE.md`, 3.5: the “Physics” row is inaccurate; this is a `BSJobs` worker thread.

## 4. Options

| | Feasibility | Cost per frame | Threads | Main risks | What it removes from per-site relocation | Game versions |
|---|---|---|---|---|---|---|
| **A. Second `PlayerCharacter`** | No | — | — | The engine does not see the second one: 2,843 pointer reads, handle, FormID `0x14`. 12 overridden vfuncs read the global pointer themselves, so the second object's methods operate on the first. Creation (`AE#36629`: allocation of `0xBE8` bytes, constructor `AE#40411`) sets the pointer, handle, and FormID `0x14`; a second object with the same ID would replace the player in the form map | Nothing; without pointer substitution, reduces to B + E | — |
| **B. `Actor` double** | No as a substitution target; useless as an ordinary actor | Data synchronization every frame | Main | 323 locations read fields beyond `Character`, 249 vcalls, including slots exclusive to `PlayerCharacter` (`0x12B`, `0x12C`, crimes). 424 comparisons and handles do not recognize the double. Synchronization of values, perks, inventory, factions, crimes | Nothing without pointer substitution | `PlayerCharacter` layout changes in 1.6.629 and 1.7.99 |
| **C. Time-based substitution** | Partial. Requires a separate phase in which the game is not running, with far-side actor updates inside it | Sequential NPC updates on the main thread (estimated 0.05–0.2 ms per NPC, assumption) | Main, jobs stopped | Actor updating is split into 6+ job types (`AE#39089`, `AE#41371`, `AE#42742`, `AE#37350`, `AE#41466`, traversal `AE#18871`). They would have to be removed from game lists and invoked manually by phase. Readers on other threads during the phase (Papyrus, loading, Havok) — not tested. Writing the `parentCell` field bypassing `SetParentCell` | Everything about player location, including helper functions — but only for work included in the phase | Job-phase list is version-specific |
| **D. Per-site relocation** (current) | Yes, works | Low | Wherever needed | Each site needs a separate hook; the list is incomplete (`DISTANCE_CHECKS.md`, 9) | — (baseline) | Detours by ID, 2 sites by offset |
| **E. Per-thread pointer substitution** | Mechanics yes, substitution target no | Rewritten reads: 4 dependent loads + `pushfq/popfq`, about 20–40 cycles per read (assumption); thousands of reads per frame — less than 0.05 ms | Any, by scope | No second object (A, B). A shadow copy breaks identity (424 comparisons, handle) and loses writes (113 sites) | Nothing in its pure form | Sites found by scanning `.text` for the Address Library address — version-independent |
| **E′ = E + C, location only** (recommendation) | Yes, from code | Same as E, but 90 sites are rewritten (or all 2,843 for the census) | Any, by far-side scope | Site classification is linear: each “location only” site must be checked with a decoder at startup. Coverage depends on scopes: a reader outside a scope (for example, `AE#36661` in `RenderPlayerView`) will not be substituted | Direct location reads within the scope. Helper functions (`GetDistance`), camera, `kNotVisible`, LOD, and fade remain | Version-independent except for the decoder |

**Hardware breakpoints instead of rewriting.** DR0–DR3 registers are per-thread, but the far side does not live on its own thread. Therefore, all worker threads would have to be armed, and an exception would be paid for every player read in the game, not only beyond the door. A data breakpoint triggers after the instruction: the handler must decode it and fix the register (for `cmp`, the flags). The F3 capture sustained about 3,200 triggers per frame at 58 frames per second (in game), but pointer loads outnumber position reads (not measured). Suitable for diagnostics, not permanent operation: rewriting is thousands of times cheaper.

## 5. User hypothesis (option E)

“Cells beyond the portal are processed on a separate thread — substitute the global variable only for that thread.”

1. **There is no separate thread** (section 3). The interior Havok step, interior cell traversal, and interior NPC updates run in common-pool `BSJobs` jobs on the same six threads as the exterior. NPC updates are intermingled with exterior NPCs in the same lists.
2. **A global variable cannot be made thread-specific without rewriting code.** The engine reads it directly; there is no function. All 2,843 reads (9 forms) can be rewritten to stubs; hardware breakpoints are too expensive.
3. **The main objection is what it should point to.** There is no second `PlayerCharacter` (A). A shadow copy breaks identity: 424 comparisons and the player handle do not recognize it, 113 sites write fields, and those writes go to the copy.

What can be salvaged:
- **`thread_local` works even without a separate thread:** a scope is needed for the duration of far-side work on any thread. The project already establishes scopes (`Portal::ActorScope`) or can establish them (its own invocation of interior traversal and the Havok step).
- **Substitute the location, not the object.** The object and pointer remain real, while the 90 sites that obtain only the player's position, angle, and cell receive a view of the location beyond the door within the scope. This is the “virtual player”: the same identity and data, a different location.
- **Rewriting all 2,843 sites with counters** provides a complete census: which player reads occur within a far-side scope and how often. Nobody knows this now; F3 sees only 4 addresses.

## 6. Reduction to the real player

- **Image** — a portal pass using a camera aimed through the door (`AimCamera`). The “virtual player's camera” is the same camera; there is nothing new here. T27 (pass camera and target as parameters) remains its own task.
- **Sound** — one XAudio2 listener and location relocation (`AE#68007`), as now. A second listener is unnecessary: there is one player, and they should hear at their own location. An opening instead of walls is T23; a virtual player does not help here.
- **Subtitles** — relocate the speaker (`AE#52638 + 0x122`), as now.
- **Detection, greeting, combat through the opening.** Distance — `GetDistance` through the door (already implemented), direct location reads — E′. Line of sight to the player's head goes through 3D (`Get3D1`, 50 vcall sites) and the NPC cell's Havok world; location substitution does not provide it. A separate hook is needed (T22).
- **What the virtual player should not experience:** collisions (it has no body), location entry (no `SetParentCell`), rewards and experience (they go to the real player, who is the only one). A crime witnessed through the door is attributed to the real player — this is correct.

## 7. Conflicts with project rules

| Rule | A, B | C | E′ |
|---|---|---|---|
| Exactly one `SetParentCell` (T15, T16) | A double in another cell generates its own events if placed there according to the rules | The `parentCell` field is written directly; if even one path calls `SetParentCell` — `CLOC` | The cell is substituted only for reads within the scope; `SetParentCell` is not called |
| Multiple portals (5.14, item 1) | N doubles, N synchronizations | N sequential phases | Each scope carries its portal: one location view per scope |
| Any cell pairs (item 2) | — | — | Symmetric: an exterior scope has a location view “outside the door” |
| Any camera (item 4), mirrors (item 5) | Not affected | Not affected | Not affected |
| Game data is not modified (item 7) | A double is a new form in the world | No data edits | No data edits; game code is changed at 90 (or 2,843) sites — hooking, but on a mass scale |
| Engine Fixes (item 8) | — | — | Before replacement, check site bytes; skip sites occupied by another hook and log them |
| Game versions (item 6) | Different `PlayerCharacter` layout | Job-phase list | Sites are found by pointer address (`RELOCATION_ID(517014, 403521)`); classification requires a decoder (Zydis) or a per-version table |

## 8. Recommendation

1. **Do not build a full virtual player (A, B, E with an object).** It runs into the single player identity: one pointer, one handle, FormID `0x14`.
2. **D remains the foundation.** There is no point waiting for T22: helper functions, 3D and line of sight, camera, `kNotVisible`, LOD, and fade depend on the camera or on passing the player as an argument, not on reading its location.
3. **E′ — a two-step prototype, with no behavior change in the first step.**
   - Step 1, census: all 2,843 sites → stubs with “inside/outside far-side scope” counters. This gives T22 an exact list of what the far side actually requests from the player.
   - Step 2, location view: within the scope, the 90 “location only” sites read the view block (position and angle through the door, cell beyond the door). `GetDistance` through the door takes its location from the same block.
4. **Do not implement C separately.** Its idea (same object, different location) is included in E′, but without a global phase and without moving actor updates out of game jobs.

What this covers (the coordinator's estimate in the brief was too high):

| Task | Covered? |
|---|---|
| T22 | Partially: census tool and common mechanism for direct location reads. Helper functions, 3D, and line of sight remain |
| T21 | No. The mechanism is symmetric and will be useful for the exterior, but T21 is exterior holding and rendering |
| T23 | No: the listener is already in place; the opening is a separate task |
| T27 | No: the pass camera is the “virtual player's” camera; parameters and the texture pool are separate work |
| `PortalActors` hooks | Removes none: world camera, `kNotVisible`, LOD, and fade depend on the camera; `GetDistance` depends on the argument |

## 9. How the numbers were obtained

Scripts in the T31 scratchpad (not in the repository):
- `scan_player_ptr.py` — census of references to `AE#403521` using the full disassembly (`dumpbin /disasm:nobytes`, 7.2 million instructions): forms, usage in a linear window, fields, vcalls, arguments, stores.
- `scan_ident.py` — handle `AE#403520`, `formID == 0x14`, `LookupByHandle`, `PlayerCharacter` vtable.
- `group.py` — subsystems by vtable and neighboring IDs.
- `threads.py` — trace records by `tid` and `main`.
