# How NPCs pass through load doors

Task T43, 08.11.2026. Research only: `SkyrimSE.exe` 1.6.1170 (Ghidra database, `docs/GHIDRA.md`) and old traces `logs/run-0.8.24`…`run-0.8.32` (the `stack` field of `hook.TESObjectDOOR.Activate.enter`, 474 records). No game code or data was changed.

Labels: **from code** — disassembly or pseudocode, references verified with `xrefs`/`asm`; **in game** — trace; **assumption** — conclusion without direct verification. Offsets are from `asm`. This is game behavior: control run 0.8.27, 318–460 s, F8 disabled (23 transitions not in high and one in high, the same stacks and the same 0.5 s fade).

## 1. Summary

- An NPC passing through a load door always uses `TESObjectDOOR::Activate` (`AE#17922`, slot `0x37` of the `TESObjectDOOR` vtable). It performs the transfer itself: `TESObjectREFR::SetPosition` (`AE#19790`) to the teleport marker position and `TESObjectREFR::MoveRefToNewSpace` (`AE#19799`). `MoveTo` and `PlayerCharacter::SetParentCell` are not involved (**from code**).
- There is one branch, based on processing level (`Character::AE_37965` = `process+0x137`, 0 — high):
  - **high**: activation animation, then a 0.5 s fade, then transfer and fade-in;
  - **not high** (middleHigh, middleLow, low): immediate transfer, without a fade (**from code**, **in game**).
- All of this runs **on `BSJobs` worker threads**: 465 of the 474 `Activate` calls with a stack were not on the main thread. The exception is 9 calls from `Actor::MoveToMiddleHigh` while demoting the actor (**in game**).
- The path through a door is a single path spanning both cells. The node at the door carries a `MovementMessageActivateDoor` message, followed by nodes on the other side. The path continues after the transfer (**from code**).

## 2. Path into another cell

### 2.1. Data

- **Door.** `ExtraTeleport` (`TESObjectREFR::GetTeleportData` `AE#20216`) stores a reference to the linked door (`+0`), the marker position (`+4`, `AE#12972`), and the marker rotation (`+0x10`, `AE#12973`).
  - The linked door's cell is taken from the door's `parentCell` (`AE#12974`).
  - The world is taken from the cell (`AE#12976`): only an exterior has one; for an interior it is `null`.
  - The arrival point is on the source door, not the linked door (**from code**).
- **An NPC's path is built by `BSPathBuilder`, not `TeleportDoorSearch`** (batch T43-1, verified by the coordinator on 09.11.2026). `BSPathBuilder::Build` (`AE#91713`) → `AE#91717`: navmesh search; for each door, node `AE#91717+0x148`/`+0x23E` → `AE#91750` → `AE#91674`. The node stores `PathingDoor` (FormID and cached handle). Positions are computed lazily: `AE#30820` — the door's location on its side, `AE#30821` — the linked door's location. There is no `ExtraTeleport` marker in the path nodes: the segment beyond the door starts at the linked door on its navmesh. If the linked door cannot be found by handle, the path is marked as failed ("Found an invalid Teleport Door location"). The thread running `Build` was not found (**open**).
- **Coarse route through doors** (`TeleportDoorSearch`; search — `AE#30932`, called only by `Pathing::AE_30656`–`AE_30659`): estimates length and the first door for the player, map, console, patrols, and quests; it is not the NPC path (**from code**, `xrefs`). `TeleportDoorSearch::vf_0x1` (`AE#30933`) iterates over the world's (`AE#20535`) or cell's (`AE#19071`) teleport doors. For each one it reads `ExtraTeleport`, the linked door's cell and world, and the marker position, then creates `PathingDoor` nodes (`AE#30809`; the door is stored as a FormID, `+0x14` is the cached handle, `PathingDoor::AE_30828`) (**from code**: call list; that `AE#20535`/`AE#19071` are door lists is an **assumption**).
  - Within a cell, the path follows the navmesh. `BSPathingLocation` is a position, navmesh, triangle, and `PathingCell` (cell or world with coordinates, `Pathing::GetPathingCell` `AE#30682`).
- **Path.** A 0xB0 object with 0x38-byte nodes: `BSPathingLocation` 0x30 and a message pointer at `+0x30`. The node count is at `+0x80` (`AE#92556`, `AE#37438`).
  - The node at the door is built by `AE#91673`/`AE#91696` with a `MovementMessageActivateDoor` message (constructor `AE#91958`, containing `PathingDoor`) (**from code**).
  - Quest objectives, the map, and the player search the same door graph: `AE#30931` is called by `PlayerCharacter::AE_40517`, `AE_23940`/`AE_23941`, `MapMenu` (**from code**: call tree).
- **Where the path is computed** — `BSPathBuilder` (above); the thread was not found because the call chain breaks at a vtable slot (**open**). By its name, the `MovementPathManagerAgentLoadedAreaMonitor` agent monitors the boundary of the loaded area (**assumption**).

### 2.2. What moves the actor along the path

`MovementControllerNPC` (`Actor+0x150`) holds path agents and the `IMovementMessageInterface` interface (table `[1]`, `+0x120`). Slots (**from code**):

| Slot | Function | What it does |
|---|---|---|
| 1 | `AE#41761` | queues a warp (`BSPathingLocation` and rotation, 0x40-byte record) at `+0x170` under a mutex |
| 2 | `AE#41762` | queues a message at `+0x158` |
| 3 | `AE#41763` | processes a message immediately: `AE#37892` and handler `+0xA0` |

- **Not high** — `MovementAgentPathFollowerVirtual::AE_92556` moves the actor along the path at `speed × dt`. For each node passed, it queues a warp to the node (slot 1). If the node has a message (`AE#91683`), it queues that as well (slot 2) (**from code**).
- **Queue processing:** `Job_Low_messages` (`AE#39071`, a `BSJobs` job) → `AE#41297` takes actors from queue `AE#404447` → `MovementControllerNPC::AE_41717` (**from code**, **in game**: 420 stacks).
  - Warps and messages are processed in path order. A warp becomes `MovementMessageWarpToLocation` (`AE#91979`).
  - Processing stops if `AE#37503` requests a different level or the actor is disabled or deleted (`flags & 0x820`) (**from code**, `AE#37503` at `AE#41717+0x22D`, `+0x2C8`).
- **Dispatcher** `AE#37892` finds a handler by the message type CRC in table `AE#403901+0x3B8` and calls it. The table is built by `AE#38103` (**from code**):

| Message | Handler | Purpose |
|---|---|---|
| `WarpToLocation` | `AE#37877` | `TESObjectREFR::AE_37313` to the node position; cell and world come from the node's `PathingCell` (`AE#30784`, `AE#30781`). A second call also transfers the rider |
| `ActivateDoor` | `AE#37878` | door transition, section 3 |
| `ApproachingDoor` | `AE#37889` | high: if `GetFadeState` = 0, `kOut` fade with no target (`AE#39454(…,0,0)`) and `approachingAutoTeleportDoor` = 1 |
| `WarpToMultiple` | `AE#37890` | series of `AE#37877` |
| 8 more (`Blocked`, `NewPath`, `PathComplete`, `PathFailed`, `Jump`…) | addresses `0x1406958E0`…`0x1406966C0` | unrelated to the door |

- **High.** The standard agent follows the path. How `ActivateDoor` gets from it to `AE#37878`, via slot 2 or 3, has not been traced (**open**, batch 2).

### 2.3. Path on the other side

- Nodes beyond the door are in the same chain. Their `PathingCell` belongs to the destination cell.
  - Not high: the next warp after the door itself transfers the actor to the node's cell: `AE#37313` → `MoveRefToNewSpace` (**from code**).
  - High: `Character::AE_37438` (`AE#17922+0x5AD`) gets the current path (`IMovementQueryPathingState`) and, if the actor has followers (`AE#37435`, `AE#37437`: sum of radii), shifts the arrival point along the path so they stand behind it (**from code**). Without followers, the point is the marker.
  - What the standard agent does after the position jump (projection onto the path or a new path) has not been traced (**assumption**: continues from the nearest point, batch 2).

## 3. Transition in high

Chain (**from code**, **in game**: 0.8.24, 0.8.27, 0.8.29, 0.8.31, 0.8.32):

1. Door node → `AE#37892` → `AE#37878`. High branch condition (`+0x15B`: `process+0x137` = 0, player not sleeping): the door has a teleport, the door is not automatic (`TESObjectDOOR::AE_17918`: `+0xB0` bit 1), and `AE#39255(process, actor, kActionActivate = 55, base, door)` (`+0x1AA`) started the animation action.
   - The door is then stored in `HighProcessData::lastExtDoorActivated` (`+0xD8`, `AE#39407`, `+0x1CD`), `doorActivated` (`+0x470`) = 0. Otherwise, `ActivateRef` is called immediately (`+0x1ED`), as outside high.
2. The door is activated by one of two paths:
   - `ActionActivateDoneHandler::Handle` (`AE#42866+0x89`, animation graph event) — 8 times in the traces;
   - if the action did not start (`high+0x455`), `AE#39637+0x1310`/`+0x2039` → `AE#39408` (package, `UpdateActor` `AE#41448` → `AE#41369` → `AE#37347`) — 15 times.
3. `TESObjectREFR::ActivateRef` (`AE#19796`) acquires the common recursive spin lock `AE#401080`, then sends `TESActivateEvent` (`+0x434`, `ScriptEventSourceHolder+0x58`, Papyrus `OnActivate`). It then calls `Activate` through the base object's vtable.
4. `AE#17922` for high with `fadeState` 0: opening sound (`+0x4B4`), then `AE#39454(process, actor, door, 1)` (`+0x4EB`): `fadeState` = `kTeleportOut` (4), `fadeTrigger` = door, `fadeAlpha` = 1. Returns 0: activation "failed", so `ActivateRef` does not call `AE#40512` or `AE#20037`.
5. Every frame `Actor::UpdateNonRenderSafe` (`AE#37360+0x98`, job `ProcessActorAnimSG` `AE#41446` → `AE#41381`) → `AE#39457`: alpha decreases by `dt / AE#370515`.
   - `AE#370515` is `fActorTeleportFadeSeconds`, based on the order of setting strings (**assumption**). The fade lasts 0.49–0.50 s in 22 of 22 pairs (**in game**).
6. At alpha ≤ 0, `AE#39457+0x1FB` calls `ActivateRef(fadeTrigger)` again. Now `fadeState` = 4: `ActivateRef+0x197` sets a flag, and **`TESActivateEvent` is not sent a second time** (bypass at `+0x37F`) (**from code**).
7. `AE#17922` takes the transfer branch (condition `+0x3CE`…`+0x3EF`: level ≠ high or `fadeState` = 4):
   - `AE#37438` (`+0x5AD`), `SetPosition` to the marker (`+0x5BA`);
   - if the destination cell is loaded (`TES::AE_13308(cell,0)`, `+0x5D1`): marker rotation (`+0x5EA`, `+0x5F5`), otherwise `ExtraBadPosition` (`AE#11817`, `+0x600`);
   - clear bit `0x2000` in `boolBits`; `StopMovement` if the package is player follow or `AE#39138` (`AE#37716`, `+0x620`);
   - `MoveRefToNewSpace(actor, cell, world)` (`+0x63A`). Returns 1: `ActivateRef` calls `Character::AE_37864` (`+0x6EA`, dismount), `AE#40512(player, actor)` (`+0x702`) (door graph path search `AE#40517` → `AE#30658`; **assumption**: quest objective update), and `Actor::DispelAlteredStates` (`+0x74A`).
8. `AE#39457+0x20C`: if `AE#37503(actor)` ≠ 0, `fadeState` = 0. Otherwise `kTeleportIn` (3) and fade-in on the other side.

The delay from `ActivateDone` or the package to the transfer is 0.49–0.50 s (**in game**). There is no player screen or fade: only the actor's alpha fades.

**Demotion with a door pending.** `Actor::MoveToMiddleHigh` (`AE#37624+0x135`), while the level is still high, calls `AE#39408` — on the main thread, stack `AE#41468` → `AE#41292` (cell detach). `Activate` starts the fade and returns 0, then `HighProcessData` is destroyed. In 9 of 9 cases (0.8.28–0.8.30), there is no subsequent transfer in the trace: the transition is apparently lost. There is no control run (**open**).

## 4. Transition outside high

- **middleHigh, middleLow, low, unloaded cell:** virtual path → warp to the door node → `ActivateDoor` → `AE#37878`. The high branch is not taken (`+0x15B`): immediately `ActivateRef` (`+0x1ED`, return `+0x1F2`) → `AE#17922` → step 7 without a fade. If `ActivateRef` fails — `Actor+0x204 |= 0x8000000` (**from code**).
  - **In game:** 420 stacks `AE#19796+0x6C0 < AE#37878+0x1F2 < AE#37892+0xA3 < AE#41717+0x215 < AE#41297+0x7B`, all off the main thread.
  - Doors throughout Skyrim (Winterhold, Temple of Kynareth); actor distance to door 37–799 units, median 85.
- **Destination cell not loaded** (`MoveRefToNewSpace`, **from code**):
  - 3D is detached (`AE#12358`, `+0x1D8`) or queued for detachment (`AE#36943`) if `TaskQueueInterface::ShouldUseTaskQueue`;
  - for a temporary (non-persistent) actor, the destination cell is stored, the reference is saved (`AE#35570`), and it is sent to `GarbageCollector` (`+0x82F`);
  - otherwise the actor is added to the cell (`AE#19411`) and to the level recalculation queue `AE#41343` (`ProcessLists+0x158`).
- **Warp into another cell** without a door also transfers: `AE#37877` → `AE#37313` → `MoveRefToNewSpace`. This is how interior NPCs "walk" 1–2 thousand units when the player exits (T11, FINDINGS "Run 0.8.25").

## 5. What the transfer does (`MoveRefToNewSpace`, `AE#19799`)

From code (offsets are call sites):
- `cell` is taken only for an interior (`cellFlags & 1`); for an exterior, the cell is derived from the world and position (`AE#13641`) or the sky cell is used (`AE#20543`).
- If the world changed: the actor is removed from the old world's persistent list (`AE#20527`) and added to the new one (`AE#20526`); `AE#11992` stores the source cell.
- `TESObjectCELL::AddReference` (`AE#19411`, `+0x2C4` or `+0x42D`): remove from the old cell (`AE#19032`), add to the new one, **`Actor::SetParentCell` (`AE#37304`, slot `0x98`)**. If the cell is attached — 3D into the graph, `MoveToHigh` (slot `0xF1`) or `MoveToMiddleHigh` (`0xF4`) for an actor without 3D.
- Havok: `MoveHavok`, `InitHavok`, or `DetachHavok`; with `ShouldUseTaskQueue` — through the task queue (`AE#36932`, `AE#36921`, `AE#36933`). Therefore the function is safe on a worker thread.
- `AddChange(8)` — transfer flag for saving.
- **Events** — only for an actor:
  - `TESActorLocationChangeEvent` (`AE#20039`, `+0x8A2`) — if `GetCurrentLocation` differs before and after. This is Papyrus `OnLocationChange`: according to T15 — 38 `NPC_` bindings, 5 `ACHR`, some of 101 quest aliases;
  - `TESMoveAttachDetachEvent` (`AE#20043`, `+0x986`) — if attachment of the old and new cells (`cellState` = 7) differs. This is `OnAttachedToCell` / `OnDetachedFromCell`.
- **What NPCs do not have:** `CLOC` (`AE#32288` is called only by `AE#40466+0x105`), `BGSActorCellEvent` (`AE#41034` — only `AE#40466+0x75`/`+0xC1`), `OnCellAttach`/`OnCellDetach` (these are cell events). `Actor::SetParentCell` sends no events (**from code**, `xrefs`).
- Around `MoveRefToNewSpace`, `AE#37313` adds rotation, the character controller's `SetPosition`, and heading (`+0x24D`…`+0x261`, or through queue `AE#36949`, `+0x27E`), and `StopMovement` when flagged. If the cell is not loaded, it sets `ExtraBadPosition` (`+0x159`).
  - If the world did not change but the location did, it sends `TESActorLocationChangeEvent` again (`+0x1D7`). Duplicate `OnLocationChange` is **confirmed** (`+0x16C…+0x1D7`, verified 09.11.2026): condition — world before transfer is non-null and equal to world after, location changed. That is, a transfer within one exterior world; exterior ↔ interior and different worlds — once (`results/T42-T44-batches.md`, "T43, batch 3").
  - `AE#37313` is the general actor transfer: it is called by `MoveTo_Impl+0x1C6` (Papyrus `MoveTo`), sandboxing, and path warps.

## 6. What a seamless transition needs

Goal: an NPC crosses the portal door plane — it is in another cell, without `Activate`, fading, or teleporting to the marker. Both sides are equivalent: interior ↔ street, interior ↔ interior, street ↔ street (another world).

**Minimum set** (modeled on the non-high branch in `AE#17922`, **from code**):
1. Position and rotation — portal transformation (near door frame → far door frame), not the `ExtraTeleport` marker. The marker is suitable as an anchor for the far-side frame: the game places the actor there during a normal transition.
2. **Vanilla door path** (clarified by batch 3, 09.11.2026): `SetPosition` (`AE#19790`) and rotation → `MoveRefToNewSpace` (`AE#19799`), as in `AE#17922+0x5BA…+0x63A`, without `StopMovement`. A vanilla door does not call `AE#37313`; `MoveRefToNewSpace` itself handles all transfer state (events, starting cell, persistent references, Havok, 3D attachment), with exactly the same effects as vanilla.
   - `AE#37313(actor, pos, rotZ, rotX, cell, world, stop=0)` (the Papyrus `MoveTo` path) adds `StopInteractingQuick`, heading, controller, and **a second `OnLocationChange` when transferring within one world** — unsuitable for gates in the same world.
   - `cell` — the far cell if it is an interior, otherwise `null`; `world` — the far cell's world (`null` for an interior).
   - After the transfer, optionally `AE#40512(player, npc)` — recalculate quest markers, as after `MoveTo`.
3. Only on the main thread, in the `Update` tick (`CLAUDE.md`, section 7). The game does the same on worker threads, so hooks on those threads only record the request.

**What must not trigger** (points — section 7):
- activation animation (`AE#39255` from `AE#37878+0x1AA`) and deferred door `+0xD8`;
- fade: `AE#39454` (`AE#17922+0x4EB`), `kOut` from `ApproachingDoor` (`AE#37889`), fade-in `kTeleportIn` (`AE#39457`);
- transfer to the linked door's marker (`AE#17922+0x5AD…+0x63A`), `ExtraBadPosition` (`+0x600`) if the far cell is held;
- `StopMovement` (`AE#37878+0x225`, `AE#17922+0x62C`).

**Continuing the path.**
- Far-side nodes are already in the path (section 2.3).
- Not high: the next warp from the queue will continue the path itself.
- High: the agent must accept the position jump. This is what the game does after the marker (**assumption**, verify with batch 2 or a run).
- Activation distance — 54–291 units in high and up to 799 outside high (**in game**). Thus `ActivateDoor` arrives before the plane. Either let the actor reach the plane (replace the door node or directly control `IMovementDirectControl`, table `[0x138]`), or transfer when the message arrives with an offset for the remaining path. The latter produces a jump visible through the portal (**open**).

**`ActorHold` (T11).**
- Detour `AE#37503` keeps high only for actors in held cells that are already high. An actor from the street entering a held interior becomes high according to game rules. If it was middleHigh, `MoveToHigh` promotes it by moving `AE#37618`; `write_call` `AE#37621+0x348` will skip that only for actors that were previously high (**from code**, T11).
- `AE#41717` aborts processing if the desired level changed. After transfer to another cell, the level may change and the rest of the queue will be lost (**assumption**).
- If the held interior is not attached (`cellState` < 6), `MoveRefToNewSpace` will not enable Havok and will queue the actor for level recalculation (`AE#41343`). Attached holding is needed (T20).

**Events — exactly once** (**from code**, section 5):
- NPC `OnLocationChange` — from `MoveRefToNewSpace` if the location changed. Do not suppress it (T30 policy).
- `OnAttachedToCell`/`OnDetachedFromCell` — only when attachment differs. With attached holding they will not fire, which is correct: the actor remained in the loaded area. In vanilla, an NPC leaving the loaded area would produce it.
- Door `OnActivate` — exactly one per transition in vanilla (step 3), even in high. A seamless transition without `ActivateRef` will not produce it. Whether to send it ourselves (`AE#20038` with `ScriptEventSourceHolder+0x58`, as in `AE#19796+0x434`) is the coordinator's decision (**open**). Door scripts usually check the player (**assumption**).
- Quest objective update `AE#40512` (if that is what it is) occurs in vanilla after successful activation. Without it, the compass marker behind an NPC may lag (**assumption**).
- `CLOC` and `BGSActorCellEvent` are not generated for NPCs in vanilla or by us.

## 7. Hook points (engine only, 5.14 item 7)

| # | Where | How | Thread | What it changes |
|---|---|---|---|---|
| P1 | `AE#37878` (`ActivateDoor` handler, pointer in wrapper `AE#38103`, no direct calls) | detour at entry | `BSJobs` workers (`Job_Low_messages`, actor jobs) | for a portal door: do not call `AE#39255`/`ActivateRef`, record request "actor X enters portal Y" and return 1 (processing continues). Catches both levels |
| P2 | `TESObjectDOOR::Activate`, slot `0x37` (`AE#17922`) | vfunc, already installed for tracing | any | NPC and portal door: return 0 without fading or transfer. Also catches package and script activations (`AE#39408`, `AE#42866`, Papyrus `Activate`) |
| P3 | `AE#17922+0x4EB` (`AE#39454`) and `+0x63A` (`MoveRefToNewSpace`) | `write_call` | any | finer-grained than P2: remove only the fade or only the transfer |
| P4 | `AE#37889` (`ApproachingDoor`) | detour | workers | do not fade before an automatic portal door |
| P5 | `MoveRefToNewSpace` `AE#19799` | detour, observation only | workers and main | all NPC cell changes: registry of "who entered the held cell", T40 probe |
| P6 | slots 2, 3 of `IMovementMessageInterface` (`AE#41762`, `AE#41763`) | vtable `MovementControllerNPC[1]` (`ActorHold` already has slot 1 there) | workers | remove a portal door's `ActivateDoor` before processing |
| P7 | custom plane-crossing detector | no hook, `Update` tick | main | any actor at a portal door (path, combat, push) → `AE#37313`. This is object passage from 5.14 item 2 |

Build order: P5 and counters (T40) → P1 + P7 → P2 as a safeguard for packages and scripts.

## 8. Open questions

1. Which thread runs `BSPathBuilder::Build` (`AE#91713`): the call chain breaks at a vtable slot.
2. How the standard high agent sends `ActivateDoor`/`ApproachingDoor` (slot 2 or 3, at what distance) and what it does after a position jump (batch 2).
3. Whether the transition is lost on demotion with a door pending (`AE#37624+0x135`) — a control run without the portal is needed.
4. ~~Duplicate `OnLocationChange` in `AE#37313`~~ — confirmed; solution — the vanilla door path (section 6).
5. Whether to send `OnActivate` and `AE#40512` during a seamless transition.
6. How to move the actor from the door node to the plane (section 6).

## 9. Path follower after a jump (batch T43-2, 09.11.2026)

- **`ActivateDoor` is sent by a path parameter, not distance:** the high follower (`MovementAgentPathFollowerStandard`, update `AE#90711`) advances `progress` (node number + fraction); at the approach point `AE#90711+0x544` → `AE#90769` queues `ApproachingDoor` (only for doors with nonzero `D`, `PathingDoor` slot `0xE` `AE#30824`), and at the door node `+0x613` → `AE#90770` queues the node message (`ActivateDoor`) to controller `AE#41762` (verified). The queue is processed by `AE#41717` → `AE#37892` → `AE#37878`.
- **The follower does not replan after a jump:** every tick it projects the position onto the path only forward from `progress` (`AE#91002`, threshold 10 units). If the projection lands on the door node or beyond, the door is considered reached, and `ActivateDoor` **may be sent again** after a seamless transfer (**assumption**, from code). The mod must suppress it for this door (hook `AE#37878`) or reset the follower (`AE#90716`).
- **Left the path:** `AE#90714` (code 3, radius `fMaxDistFromPathRadiusMult` × path radius), `AE#90737` (code 2, path reset). Who calls these slots and what the manager does with the codes was not found.
- **`MovementPathManagerAgentLoadedAreaMonitor`** (`AE#92466`): code 3 if a path segment is outside the loaded area. After entering a held cell, the path will probably survive (both sides loaded), otherwise it will be discarded (**assumption**).
- **`AE#41717` aborts the queue** if the actor's process level changed after a message or the actor is disabled (`+0x226…+0x247`): a transfer from the handler may lose the rest of this tick's queue.
- Verify in game: follower `progress` before and after transfer, duplicate `ActivateDoor`.
