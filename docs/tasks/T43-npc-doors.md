# T43. How NPCs pass through doors with loading

Stage 2, research. No code: the chat works in the main folder on `feature/interior-portal` and writes only new files. Test run: none (optional — with the T40 build). Model — Opus 5.5, effort — high; mass reading — `re-reader` agents (Sonnet, medium) and `re-reader-fast` (Haiku, low). Database — MCP `ghidra_*` or `tools/ghidra/gq.py` (`ghidra` skill, `docs/GHIDRA.md`). Old traces — `logs/run-*`, summary — `tools/run_digest.py`.

## Why

Step 6 of the plan (`docs/HANDOFF.md`, section 4) and 5.14, item 2: NPCs pass through the portal the same way the player does — both directions, no teleport. Currently an NPC uses the door as in vanilla: walks up, activates the door, and appears at the door on the other side.

T20 (player entry) and T21 (a living street) depend on how the game moves NPCs between cells. This path's code has not been read yet.

## What is known (in-game)

- `TESObjectDOOR::Activate` (`AE#17922`) arrives **off the main thread**: 44 calls from 6 worker threads, 39 — NPCs in unloaded cells across Skyrim (`docs/FINDINGS.md`, H1).
- An NPC in middleHigh activates the door from afar: Idolaf — from 660 units (`Character::AE_37878` → `TESObjectREFR::ActivateRef` `AE#19796`).
- Not in high, the game leads the NPC along the path in large steps: `AE#37892+0xA3` → `AE#37877+0xDA` → `TESObjectREFR::AE_37313+0xA0` → `SetPosition`.
- On rising to high the NPC "catches up" to the place: `Actor::MoveToHigh` (`AE#37621+0x34D`) → `AE#37618+0x9DC` → `Actor::SetPosition` (`AE#37309`).
- Jumps of 23–27 thousand units — NPCs that passed loading doors (FINDINGS, "Run 0.8.26" and "Run 0.8.27").
- `ActorHold` (T11) keeps the portal's NPC in high (detour on `Character::AE_37503`). This removed repositioning on player transitions, but not the NPC passing through a door.
- Paths: `Pathing::GetPathingCell` (`AE#30682`), `PathingDoor` (`AE#30811`…), `Pathing::GetPathingDoorFromCollision` (`AE#30691`), `ExtraTeleport::GetLinkedDoor` (`AE#20301`), `TESObjectREFR::GetTeleportData` (`AE#20216`).

## To do

1. **Path into another cell.**
   - How the AI builds a path through a loading door: what data it takes (the linked door, the teleport marker, navmesh triangles at the door), where it stores the path, who computes it and on which thread.
   - What happens to the path on the other side.
2. **Passage in high.** The chain from approaching the door to appearing on the other side:
   - who calls `Activate` and why not on the main thread;
   - how the NPC is moved: `MoveTo`, the base `TESObjectREFR::SetParentCell` (`AE#19818`), or its own;
   - delays and the fade-out;
   - events: the actor's cell and location change, `OnCellAttach`/`OnCellDetach`, Story Manager.
3. **Passage outside high** (middleHigh, low, unloaded cell): the chains above, branching by process level, what happens when the destination cell is unloaded.
4. **What a seamless passage needs.** When an NPC crosses the plane of a door-portal, it must change cells without `Activate` and a teleport. Break down:
   - the minimal set of calls;
   - what must not fire (the move to the linked door's marker);
   - how the path continues in the new cell;
   - how to combine this with `ActorHold`;
   - which script events must fire exactly once.

   Both directions: interior → street, street → interior, interior ↔ interior. Mark every item **from code** or **assumption**.
5. **Hook points** in the engine only (5.14, item 7: quests, packages, and forms are not edited): function, location, what the hook changes, thread. Candidates for the T40 build.
6. **Name candidates** to the curator — only for functions from the findings.

## Rules

- Do not put Bethesda pseudocode into files or answers: only findings, `AE#id`, offsets. The database is read-only.
- Verify "there is no call" and "player-only" claims with `xrefs`/`asm`; take offsets from `asm`.
- "This is game behavior" is proven by a control run (`CLAUDE.md`, section 7). In old traces, first look for a control without the portal (F8 disabled).
- Package and scene behavior is often defined by scripts: sources are in `<Game>\Data\Source\Scripts`, read-only.

## Result

- `docs/NPC_DOORS.md` (≤ 200 lines): chains for high and non-high, threads, seamless passage composition, hook points.
- `docs/tasks/results/T43.md` per the README template:
  - the main points for T20, T21, and step 6;
  - list of IDs for T40;
  - a "For the curator" section.

## Conflicts

None: new files. Related in scope — T20 (`InteriorSim`, entry), T21, T11 (`ActorHold`): the coordinator moves the findings into their briefs.
