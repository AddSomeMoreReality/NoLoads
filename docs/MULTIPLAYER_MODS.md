# Multiplayer mods: how they maintain multiple players in one world (T41)

Research without code, 08.11.2026. Brief: `docs/tasks/T41-multiplayer-mods.md`; result for the curator and coordinator: `docs/tasks/results/T41.md`.
Sources: source code on GitHub (TiltedEvolution `dev`, SkyMP `main`, snapshot 08.11.2026) and the Ghidra database (`AE#id`, names and callers only). GPL/AGPL source code **was not copied**: only facts about “which function is responsible for what” were taken, with file and line references. Labels: **from source** — read, not run in game; **from database** — verified using callers in Ghidra 1.6.1170; **not found** — searched for and not found.

## 1. Mods

| Mod | Repository | License | Game | Status |
|---|---|---|---|---|
| Skyrim Together Reborn (TiltedEvolution, “TE”) | [tiltedphoques/TiltedEvolution](https://github.com/tiltedphoques/TiltedEvolution) | GPL-3.0 (README) | SE; primary target 1.7.104, also 1.6.1170 (`AGENTS.md`), no VR; IDs are Address Library **AE**, no SE pairs | Active (push 06.11.2026, 73 open issues). Not an SKSE plugin: its own launcher `SkyrimTogether.exe`, SKSE loads afterward |
| SkyMP (Skyrim Multiplayer) | [skyrim-multiplayer/skymp](https://github.com/skyrim-multiplayer/skymp) | GPLv3 (client, SkyrimPlatform), AGPLv3 (`skymp5-server`) by subproject, `TERMS.md` | SE and AE: `Offsets.h` has `RELOCATION_ID(SE, AE)` pairs | Active (push 30.10.2026). Logic in TypeScript on top of SkyrimPlatform, server in C++ |
| Keizaal | [Keizaal/Keizaal](https://github.com/Keizaal/Keizaal) | CC BY-NC-SA 4.0 | SE, Wabbajack | **This is a modlist, not multiplayer.** No engine code. “Keizaal Online” appears only in third-party installers (`miir`, `skyrim-linux-bootstrap`); what server it uses was not determined |
| Skyrim Together (first version, Tilted Online) | no source code | closed | SE | Closed beta from 2019, predecessor of TE. Press coverage only; nothing to analyze |
| “Skyrim Online” / SkyOn | [Saint-Evil/SkyOn](https://github.com/Saint-Evil/SkyOn) (equivalence with “Skyrim Online” not confirmed) | GPL-3.0 | ScriptDragon, 2016 | 8 commits, “very raw”, README says nothing about players or cells. Not analyzed |
| Old SkyMP 0.9–3 | gitlab `pospelov/skymp2`, `skymp3`, `skymp2-cellhost` (`docs/docs_project_history.md`) | not checked | SE | Includes “cellhost” and “parallel cell processing on the server”. **Not reached**: source code is on gitlab |

## 2. Answers to the brief's questions

### 2.1 What remote players are in the engine

In all worlds, they are an **ordinary `Character` (NPC)**; each client has one real, local player. Nobody creates a second `PlayerCharacter` (confirms T31, `docs/VIRTUAL_PLAYER.md`).
- **TE:** `Actor::Create(TESNPC::Create(appearance buffer))` (`Games/Skyrim/Actor.cpp:208`, `Services/Generic/CharacterService.cpp:1637`): a fresh `TESNPC` base from serialized appearance. The `Character` itself is allocated by its own `Actor::New` with an extension tail and placed via `TESDataHandler::CreateReferenceAtLocation` (`AE#13723`, “SpawnNewREFR” in TE). Flags: do not save, `SetRemote(true)`, essential, no recovery after knockout, player faction `0xDB1` rank 1 (`Actor.cpp:317`). Stats arrive in full (`SetActorValues`), damage is calculated by the `Actor::DoDamage` hook (`AE#37335`). Equipment uses hooks into internal `ActorEquipManager` functions (section 3). Animations use the `ActorMediator::PerformAction` hook (`AE#38949`): game actions are not executed for remote copies; received actions are executed instead (`Games/Animation.cpp:23`).
- **SkyMP:** clone of vanilla NPC `0x10D13E` with “do nothing” AI packages (`0x654E2`), form with ID `0xFF000000+` (`PapyrusTESModPlatform.cpp:299`). Placement via `placeAtMe` at the player, then `setPosition`, `enable`, `resurrect` (`skymp5-client/src/view/formView.ts:170`, `spawnProcess.ts`). Appearance via custom natives `SetNpcRace/Sex/SkinColor/Tints`, equipment via `AddItemEx`, `UpdateEquipment`.
- Both use `TESObjectREFR::MoveTo_Impl` (`AE#56626`) for movement and transfer to a cell: TE `Games/Skyrim/TESObjectREFR.cpp:394`, SkyMP `MoveRefrToPosition`. **This is our hook** (`HooksTes.cpp:251`).

### 2.2 Uniqueness of the player

The player remains unique and real on their own client; “remote” players are adapted to what the engine already knows how to do with NPCs. There are few hooks, and all are targeted.
- **Combat and AI (TE):** the `ActorKnowledge::UpdateDetectionState` hook (`AE#42704`) cancels detection of “remote player → local player” (`Actor.cpp:1150`); `Actor::IsFleeing` always returns “no” for players (`:1283`). Everything else works through the player faction: NPCs see the copy as an ally. The `CombatController::UpdateTarget` (`#if 0`, `CombatController.cpp:80`) and AI package (`#define AI_SYNC 0`, `References.cpp:82`) hooks are **disabled**.
- **AI ownership:** an NPC copy on a non-owner has `SetRemote(true)`: the `Actor::Update` branch `AE#37356` returns 0 (no movement, only the controller step), `TESObjectREFR::SetPosition` (`AE#19790`) returns 1, and `Actor::ProcessTracking` (`AE#38009`) is skipped. **Our `SetPosition` hook matches** (`HooksTes.cpp:259`).
- **Dialogue and voice acting:** `MenuTopicManager::PlayDialogueOption` (`AE#35269`) sends the selected line; `Actor::SpeakSound` (`AE#37542`) sends the owner's lines; background lines come only from the NPC owner (`Actor.cpp:1208`).
- **Quests:** no Story Manager. `TESQuestStartStop/Stage` events are captured through `EventDispatcher` sinks (`AE#54425`/`AE#54522`), stages are delayed and applied to group members (`Services/Generic/QuestService.cpp`). The group leader is authoritative. For this, TE ships `SkyrimTogether.esp`, `SkyrimTogetherQuestPatches.esp`, and Papyrus scripts: **this modifies data, which our item 7 of 5.14 prohibits**.
- **Time and weather:** `AE#36291` (time step) is suppressed outside single-player mode; weather uses `Sky::SetWeather/ForceWeather/UpdateWeather` (`AE#26241/26243/26231`), with the leader authoritative.
- **Camera and input:** the camera is untouched. `PlayerControls::ProcessEvent` (`AE#42338`) receives an empty list while the overlay window is open; device polling is skipped when the window has no focus.
- **Crimes:** neither a hook nor code was found. Issue [#309](https://github.com/tiltedphoques/TiltedEvolution/issues/309): lawful theft by a group member gives the leader a 1-gold bounty.
- **SkyMP** takes a different approach: **it removes the vanilla world**. Every frame, `WorldCleanerService` disables and deletes one nearest actor within a radius of 8192 (`skymp5-client/src/services/services/worldCleanerService.ts`); fast travel, difficulty selection, and skill growth are disabled (`disable*Service.ts`). The server controls everything; vanilla AI and quests are unnecessary. Not applicable to us.

### 2.3 Cells

**Neither mod has its own cell loading:** each client loads the vanilla grid, and the mod only knows whose cell is where.
- **TE, client:** the player's cell is polled every frame using `TES::currentGridX/centerGridX` (`Services/Generic/DiscoveryService.cpp:60`, without a hook); it sends `EnterExteriorCell`, `EnterInteriorCell`, and `ShiftGridCell` with the grid cell list. Grid size is hardcoded: `m_gridsToLoad = 5` (20 for dragons), `encoding/Structs/GridCellCoords.h:10`. **If the player has a different `uGridsToLoad`, the calculation diverges.**
- **TE, server:** “in range” (`CellIdComponent::IsInRange`, `server/Components/CellIdComponent.h`): interior — same cell ID; exterior — same world and `|dx|,|dy| ≤ gridsToLoad/2`. The server does not simulate: it stores cells, owners, and “who sees whom”.
- **Remote player outside the grid:** there is **no actor**, only a record without 3D (`WaitingFor3D`). The actor is created only when the remote player's cell enters the local player's grid (`RunSpawnUpdates`, `CharacterService.cpp:2093`) and is placed in the local player's `parentCell`. The exception is transferring an actor to a cell that does not exist: calling `TESWorldSpace::LoadCell` (`AE#20460`, `CharacterService.cpp:1355`) creates the cell in place.
- **Do they retain cells beyond the grid:** **no**, neither TE nor SkyMP. This is the main capability they lack and we need (5.14, items 1–2).
- **Cell owner:** in TE, the **actor**, not the cell, is owned: one client runs an NPC's AI and physics and sends its state. When the owner leaves, the server takes the first client whose cell is “in range” (`server/Services/CharacterService.cpp:895`); if there is none, the actor is **removed from synchronization** (each client then has its own vanilla actor). Departed and declined clients are remembered so ownership does not bounce between clients without the cell.
- **Doors in TE:** nothing special. Each client has its own vanilla transition with a loading screen; `TESObjectREFR::ActivateRef` (`AE#19796`) sends the door state. The changelog includes “the door problem (again)” and “actor stuck on traveling through door” (`CHANGELOG.md`, 1.34.0).
- **SkyMP:** the server **does not load cells at all**; it parses `.esm/.esp` (`libespm`) and maintains a grid for each “world or cell” ID: 4096-unit cell (`MpObjectReference.cpp:151`), 3×3 neighbors (`WorldState.cpp:840`). The **server** activates a teleport door: it takes the destination from the REFR record, changes the actor's “world or cell”, and sends `TeleportMessage` to the client (`MpObjectReference.cpp:1466`). The client hides a remote player if their “world or cell” differs from its own (`formView.ts:66`). The AI host is the first client to request hosting; hosting switches after 2 s without updates (`ActionListener.cpp:653`); the others receive `isHostedByOther` and disable AI.

## 3. Engine hooks

All TE IDs are **AE**: `SetPosition` = `AE#19790` matches our `RELOCATION_ID(19363, 19790)`, and the database names fit semantically. TE has no SE pairs; SkyMP provides pairs (section 5). “Ours” means a hook exists in `src/Hooks*.cpp`. Lines refer to `Code/client/...` in TE.

| Group | `AE#id` Database name (if any) | Where in TE | Ours? |
|---|---|---|---|
| Cell, position | `19790` `TESObjectREFR::SetPosition` | `Games/Skyrim/Actor.cpp:1297` | **yes**, `HooksTes.cpp:259` |
| | `56626` `TESObjectREFR::MoveTo_Impl` (call, not hook) | `TESObjectREFR.cpp:394` | **yes**, `HooksTes.cpp:251` |
| | `20460` `TESWorldSpace::LoadCell` (call), `13718` (`GetCell(x,y,world)`, call), `13723` `TESDataHandler::CreateReferenceAtLocation` (call) | `TESWorldSpace.cpp:6`, `ModManager.cpp:68,76` | `20460` is called from our `CenterOnCell`; the rest are **new** |
| | `37313` (`AE_37313`, calls `MoveTo_Impl+0x1C6`), `19742` (`AE_19742`, called by `TES::AE_13355`, `TESObjectCELL::AE_19061`) | `Actor.cpp:1303,1304` | **new** (near our `MoveTo`/cell attachment) |
| Actor tick | `37356` `AE_37356` (calls `AE#37350+0xFB`, `Actor::Update` — `AE#37348`) | `Actor.cpp:1296` | **nearby**: our `37350` and `Actor::Update` vfunc `0xAD` |
| AI and combat | `42704` `ActorKnowledge::AE_42704` (calls `DoDetectionJob` `AE#42742`), `37577` (`IsFleeing`), `38009` `Actor::ProcessTracking`, `39643`, `39114`, `38959`, `33236` (disabled) | `Actor.cpp:1311,1317,1318`, `References.cpp:136` | **new** |
| Equipment | `38929/38934/38928/38933/38930/38935` — internals of `ActorEquipManager::EquipObject/UnequipObject/EquipSpell/Unequip…/EquipShout` (**from database**, called by wrappers `AE#38894/38901/38895/38897`) | `EquipManager.cpp:311` | new |
| Items, magic | `AE#37525` `Actor::AddObjectToContainer`, `37521` `Actor::PickUpObject`, `40454` `PlayerCharacter::DropObject`, `19708`, `19689`, `34144` `ActorMagicCaster::SpellCast`, `34526` `MagicTarget::AddTarget`, `44108` `Projectile::Launch` | `Actor.cpp`, `Magic/`, `Projectiles/Projectile.cpp:117` | new; `Projectile::Launch` will be useful for T25 |
| World | `36291` (time), `26241/26243/26231` `Sky::*Weather` | `TimeManager.cpp:25`, `Sky.cpp:91` | new |
| Activation and tick | `19796` `TESObjectREFR::ActivateRef`, `36564` `Main::Update`, `53926` `SkyrimVM::ProcessRegisteredUpdates` (TE client tick), `54425/54522` (event sinks) | `TESObjectREFR.cpp:1146`, `SkyrimVM64.cpp:42` | **nearby**: our door uses `TESObjectDOOR` vfunc `0x37` (`HooksCells.cpp:333`), tick via `PlayerCharacter::Update` |
| Input, rendering | `42338` `PlayerControls::ProcessEvent`, `68617`, `68545` `ControlMap::ToggleControls`, `77226` `Renderer::Init` | `PlayerControls.cpp:66` | new, needed by TE for the overlay |

Non-hooks: `AE#403567` (`ActorMediator`), `AE#400269` (`TESDataHandler::singleton`), `AE#400447` (`TimeData`). TE's “VM destructor” label at `AE#40412` is `PlayerCharacter::dtor_impl` in the database; the TE hook is empty (`SkyrimVM64.cpp:44`).

## 4. What is broken or unresolved and what risks it poses for us

- **[#610](https://github.com/tiltedphoques/TiltedEvolution/issues/610):** the player becomes invisible after a cell loads (gate → tavern → palace → gate). Consequence: the copy is tied to the grid, and the grid is rebuilt on transition. This threatens us when multiple portals update a cell's “owner”.
- **[#810](https://github.com/tiltedphoques/TiltedEvolution/issues/810):** when NPC ownership changes (`SetRemote(false)`), the engine enables AI, and NPCs loop on `Unequip`/`combatStanceStop`. Relevant to T11/T20: restoring interior NPC AI after a handoff must be done with clean state.
- **[#741](https://github.com/tiltedphoques/TiltedEvolution/issues/741), [#645](https://github.com/tiltedphoques/TiltedEvolution/issues/645):** enemies do not chase group members, and essential NPCs do not get up. AI does not always recognize a remote copy as the “player”. A direct analogue of our “far side sees the wrong player” (T32).
- **[#214](https://github.com/tiltedphoques/TiltedEvolution/issues/214), [#215](https://github.com/tiltedphoques/TiltedEvolution/issues/215), [#230](https://github.com/tiltedphoques/TiltedEvolution/issues/230):** an actor can be both local and remote; actors and objects do not appear for everyone. Owner desynchronization.
- **Hardcoded 5×5 grid** (`GridCellCoords.h:10`) and data modification (`.esp`, scripts, quest patches) are unsuitable for us: 5.14, item 7.
- **SkyMP, `ROADMAP.md`:** “NPC … should be able to walk outside players' field of view”, physics requires “host switching”, “Sounds and music” only within a cell, and “Linked Refs”/traps in dungeons are not ready. Sound through doors and NPCs outside the field of view are **unsolved in both**.
- **Threads:** TE (`AGENTS.md`): hooks often run on worker threads, with work queued to the main thread. Matches our `MainThread::Post` (7).

## 5. What is applicable to the portal

1. **No precedent for the main goal:** neither mod retains a cell beyond the vanilla grid, renders another cell, or runs AI without a player nearby. TE removes the actor from synchronization; SkyMP does not load cells.
2. **T32 (player location in a far-side scope):** confirms that there is no object to substitute for the player and that nobody has tried. A new idea: “who is looking at whom” points already exist as hooks: `ActorKnowledge::UpdateDetectionState` (`AE#42704`, called from `DoDetectionJob`, which is in the `BSJobs` pool per T31) — the place where “target = player” is compared with the owner. Suitable as a check for T22/T32; not tested.
3. **5.14, items 1–2 (multiple portals, any pairs):** TE's “in range” relation (interior — same ID, exterior — grid) can be extended with “cell ↔ cell through portal” edges. AI ownership should follow the “who is attached” cell rather than the actor.
4. **AI handoff:** lesson from #810 — reset the NPC's animation state when changing “owner”, rather than merely clearing the flag.
5. **5.14, item 3 (sound):** TE and SkyMP synchronize **events** (`SpeakSound` `AE#37542`), rather than mixing sound between cells. No ready-made solution exists.
6. **5.14, item 4 (any camera):** the camera is untouched in both; there is nothing to borrow.
7. **5.14, item 6 (SE 1.5.97):** SE/AE pairs from SkyMP `Offsets.h` (Main::Update `35565/36564`, `PlayerCharacter::DrawWeaponMagicHands` `40232/41235`, `Actor::DrawWeaponMagicHands` `36289/37279`, `NotifyAnimationGraph` `37020/38048`, `Actor::DoReset3D` `39181/40255`, `CreateReferenceAtLocation` `13625/13723`) — add to `PORTABILITY.md` for T26. In SkyMP, `13625` is the SE ID; `AE#13625` is a different function in the database.
8. **5.14, items 7–8:** TE modifies data (ESP, scripts) and breaks with other mods; SkyMP deletes vanilla actors: both approaches are ruled out. Compatibility with Engine Fixes was not checked in TE (not found in the sources).
