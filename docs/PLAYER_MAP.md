# Player map in code (T38)

A static map of everything in `SkyrimSE.exe` 1.6.1170 that is tied specifically to the player: the `PlayerCharacter` class, its fields, globals, and subsystems. Brief — `docs/tasks/T38-player-map.md`; results and lists for T40 — `docs/tasks/results/T38.md`. Foundation — `docs/VIRTUAL_PLAYER.md` (T31) and the T32 survey.

Notes: **by code** — Ghidra database (T35, T36), not verified in game; **in game** — trace or F3 stack from previous runs; **assumption** — verified by nothing. Subsystems in section 9 are grouped by class names in the database and by nearby IDs — this is a heuristic, not proof.

## 1. Summary

- **The player's identity — four paths, not three:** the pointer `AE#403521` (2 838 references in the database), a second pointer `AE#401069` (549, almost only `cmp`, T32), the handle `AE#403520` (111), and FormID `0x14` (2 places). A fifth path is virtual: `PlayerCharacter` overrides `ActorValueOwner::GetIsPlayerOwner` (`AE#40691`, always "yes").
- **Class:** 14 vtables by RTTI. In the main one (`AE#208040`, 303 slots) the player has 112 own slots: 107 overrides of `Character` and 5 new ones (`0x12A`–`0x12E`). The secondary vtables add 14 more own slots, 4 of which are handlers for menu, menu-mode, input, and stats events.
- **Only 13 overrides read `AE#403521` themselves, 3 read the handle.** The rest work through `this` and the `PLAYER_RUNTIME_DATA` block (`+0x3E0`, `0x808` bytes).
- **Where the player is — by code, one field for everything:** `currentLocation` (`+0xAD0`) is written only by `SetParentCell` (`AE#40466+0x19B`), the constructor, and the loading screen (`AE#51929`, temporarily). `TESObjectREFR::GetCurrentLocation` (`AE#19812`) returns this field for `formID == 0x14` and the cell's location for everything else.
- **Frame order, by code:** `MainLoop` (`AE#36550`) → `Main::Update` (`AE#36564`): `+0x6E` the player teleport queue and Story Manager (`AE#40438`), `+0x2A7` player update (`AE#36581`). `AE#36581` calls `PlayerCharacter::Update` (vfunc `0xAD`) on the main thread; the only other call of that slot in code is the `UpdateActor` task for actors from a list (section 6). Then the exterior cell-boundary crossing and the `TES` grid shift by player position.
- **Player animation and movement are separate `BSJobs` pool jobs:** `Job_Player_animation` (`AE#39055`) → `AE#41371` → vfunc `0x7D`; `UpdatePlayerMovementJob` (`AE#41445`) next to `UpdateActorMovementJob` (`AE#41444`). Both movement jobs call `AE#37350`, which compares the actor against `[AE#403521]` and passes the flag into `bhkCharacterController::TryMoveTo`.
- **NPC processing level around the player = attached cells:** the `TES` grid shifts by player position (`AE#36581+0x201` → `TES::AE_13288`); when a cell is attached, each reference receives vfunc `0x91` `DoMoveToHigh` (`AE#19063+0x2C0` → `AE#19280+0xCF`). There is no separate "high process" radius around the player on this path.
- **One cell and one world for everything:** the player's `SetParentCell`, besides `CLOC` and `OnLocationChange`, changes the climate (`Sky::SetClimate`), grass, camera, the Havok water world, map markers (`AE#40671`), and sends `BGSActorCellEvent` (region, music, water, HUD).
- **Subsystems:** most sites — actor (637), interface (473), AI (258), forms and activation (208). The second pointer is read by game logic, never by the interface (by the section 9 heuristic).

## 2. How the code finds the player

| Path | References | Form | Who writes it | Note |
|---|---:|---|---|---|
| Pointer `AE#403521` (`PlayerCharacter::singleton`) | 2 838 in 1 289 functions (database; T31 — 2 843 by `dumpbin`) | 2 604 `mov`, 223 `cmp`, 13 `cmov` (T31) | `AE#36629+0xAB` (creation), `AE#13754+0x358` | By code |
| Second pointer `AE#401069` (`gPlayerCharacter3` in the database) | 549 | 515 `cmp`, 33 `mov` (T32) | Constructor `AE#40411+0xFC7`, destructor `AE#40412+0xE3` | By code. The interface never reads it (section 9) |
| Handle `AE#403520` (misnamed `gBarterMenu_TargetRefHandle` in the database) | 111 | 80 `mov`, 28 `cmp`, 4 `lea` (T31) | `AE#36629+0xFB` | By code |
| `formID == 0x14` | 2 | `cmp [r+14h],14h` | — | `GetCurrentLocation` `AE#19812`, `SetStartingPosition` `AE#19668`: both read the player's `currentLocation` |
| vfunc `ActorValueOwner` slot 8 | — | call through `this+0xB8` | — | `AE#40691` returns 1, `Character` has `AE#27283`. The number of calls was not counted |
| `PlayerCharacter` overrides | 126 slots | vtable | — | Section 4. There are no vtable type checks outside the constructor (T31) |
| Papyrus `Game.GetPlayer` | 1 | `mov rax,[AE#403521]` | — | `AE#55469` |

The player handle is also stored in fields of other objects (combat targets, packages, dialogue, camera `+0x3C`) — such places are invisible to the T32 survey.

## 3. The class: 14 vtables

| vtable | Subobject | Slots | Player's own slots (not destructor) |
|---|---|---:|---|
| `AE#208040` | `+0x0` main (`Character`) | 303 | 107 overrides + 5 new, section 4 |
| `AE#208042` | `+0x20` `BSHandleRefObject` | 2 | — |
| `AE#208044` | `+0x30` `BSTEventSink<BSAnimationGraphEvent>` | 2 | — |
| `AE#208046` | `+0x38` `IAnimationGraphManagerHolder` | 19 | 4: `0x4` `AE#40755` (graph nodes, `grabData`), `0x5` `AE#40753` (own graph), `0x7` `AE#40756` (graph variables), `0xB` `AE#40754` (after manager creation) |
| `AE#208048` | `+0xA0` `MagicTarget` | 12 | 2: `0x4` `IsInvulnerable` `AE#40504` (plus `Actor::IsGhost`), `0x8` `EffectAdded` `AE#40762` (HUD message) |
| `AE#208050` | `+0xB8` `ActorValueOwner` | 9 | 1: `0x8` `GetIsPlayerOwner` `AE#40691` = 1 |
| `AE#208052` | `+0xC0` `ActorState` | 22 | 3: `0x4` `AE#40743`, `0x14` `DoSetSitSleepState` `AE#40735`, `0x15` `AE#40752` |
| `AE#208054`, `AE#208056`, `AE#208058` | `+0xD0`, `+0xD8`, `+0xE0` | 2 each | — |
| `AE#208060` | `+0x2B8` `BSTEventSink<MenuOpenCloseEvent>` | 2 | `Handle` `AE#40740`: controls, camera auto-look, follower command |
| `AE#208062` | `+0x2C0` `BSTEventSink<MenuModeChangeEvent>` | 2 | `Handle` `AE#40741` |
| `AE#208064` | `+0x2C8` `BSTEventSink<UserEventEnabledEvent>` | 2 | `Handle` `AE#40739`: exit sneak if disabled |
| `AE#208066` | `+0x2D0` `BSTEventSink<TESTrackedStatsEvent>` | 2 | `Handle` `AE#40742`: empty |

Event sources without vtables: `BGSActorCellEvent` `+0x2D8`, `BGSActorDeathEvent` `+0x330`, `PositionPlayerEvent` `+0x388`.

## 4. Overrides of the main vtable

`→` — calls the `Actor`/`Character` version; **bold** — reads `AE#403521` itself (13 functions); `[h]` — reads the handle `AE#403520`.

| Group | Slots and functions | How the player differs |
|---|---|---|
| Saving | `0xE` `AE#40538` `SaveGame`, `0xF` `AE#40540` `LoadGame`, `0x10` `AE#40541`, **`0x11` `AE#40542` `FinishLoadGame`**, `0x12` `AE#40543` `Revert`, `0x12A` `AE#40539` (tone masks) | The whole player block: perks, crimes, quests, targets, map, telemetry. `FinishLoadGame` calls `AE#40671` (map markers) and `AE#40450` |
| Frame | **`0xAD` `AE#40447` `Update`** `[h]` | Main thread only (section 6). → `Actor::Update` twice (`+0xA7F`, `+0xE25`), plus crosshair, grab, sitting and camera, HUD, jail, sleep, greeting, `AE#40469` (location and cell), `AE#40607`, `AE#40556`, `AE#40818` |
| Cell | **`0x98` `AE#40466` `SetParentCell`** | Section 5.2 |
| Synchronization | `0x46` `AE#41059` `QCanUpdateSync` | "No" while `playerFlags.isLoading` |
| Persistence | `0x47` `AE#40710` | Always "no" |
| 3D | `0x6A` `AE#40458` `Load3D`, `0x6C` `AE#40465` `Set3D`, `0x6F` `AE#40464` `Get3D1`, `0x70` `AE#40462` `Get3D2`, `0x8D` `AE#40463` `GetCurrent3D`, `0x71` `AE#41023` `Is3rdPersonVisible` | Two skeletons: `firstPerson3D` (`+0x8F8`) and third person, chosen by `playerFlags.isInThirdPersonMode`. `Load3D` attaches the first-person skeleton to the camera, `Set3D` — sound and shadows |
| Biped | `0x7E` `AE#40474`, `0x7F` `AE#40472`, `0x80` `AE#40473`, `0x82` `AE#40477` | Large biped (`largeBiped`) for first person |
| Animation | `0x72` `AE#40523`, `0x79` `AE#40758`, `0x7D` `AE#40521` `UpdateAnimation` | `0x7D` calls the `Job_Player_animation` task (section 6) |
| Light | **`0x55` `AE#40570` `UpdateRefLight`** | First- and third-person light (`+0x9A0`, `+0x9A8`) |
| Gaze and location | `0x5B` `AE#40664` `GetLookingAtLocation`, `0xC2` `AE#40505` `GetEyeVector`, `0xA4` `AE#40528` `GetHeading`, `0x11F` `AE#40751` `UpdateActor3DPosition`, `0x86` `AE#40471` `GetLinearVelocity` | Eye and gaze — from `PlayerCamera` (first-person node), heading — plus `sitHeadingDelta`, velocity — time-based cache |
| Movement | `0x11A` `AE#40451`, `0x109` `AE#40734`, `0xAF` `AE#41165` (empty), `0xB0` `AE#41058` (empty), `0x11D` `AE#40761`, `0x126` `AE#41146` (actor collisions — none) | Player movement controller, no AI |
| Water | `0x9C` `AE#442664` `ProcessInWater` | Camera target and underwater effect |
| Starting location | `0xF0` `AE#40441` | Empty |
| Items | **`0xCB` `AE#40454` `DropObject`**, `0xCC` `AE#40533` `PickUpObject`, `0xA3` `AE#40595`, `0xB2` `AE#40478`, `0xB3` `AE#41145`, `0xB4` `AE#40479`, `0x121` `AE#40708`, `0xCD` `AE#41253`, `0xCE` `AE#41254`, `0xD2` `AE#41243`, **`0x105` `AE#41258`**, `0xA0` `AE#41259`, `0xA2` `AE#40738` | Dropped-item list, ownership, theft, ammo HUD, god mode |
| Crimes | `0xB5`–`0xB8` `AE#40638`–`AE#40641`, `0x12B` `AE#40642`, `0x12C` `AE#40643`, `0x12D` `AE#40653`, **`0xB9` `AE#40655` `GoToPrison`** `[h]`, **`0xBA` `AE#40657` `ServePrisonTime`**, **`0xBB` `AE#40659` `PayFine`** | The bounty table by faction (`crimeGoldMap`) exists only on the player. Jail and fine generate Story Manager events (`AE#32310`, `AE#32308`) |
| Vampire and cannibal | `0xBC`–`0xC1` `AE#40429`, `AE#41129`, `AE#40430`, `AE#41142`, `AE#40431`, `AE#40432` | Player flags, "feed only once" package |
| Perks | `0xFA`–`0xFE`, `0x101` `AE#40680`–`AE#40684`, `AE#40686` | Own perk and standing-stone arrays |
| Skills and values | `0xF7` `AE#40488` `AddSkillExperience`, `0x118` `AE#40490` `InitValues`, `0x127` `AE#40688` | `PlayerSkills` (`+0x9B8`) |
| Shouts | `0xCF` `AE#40629` `AddShout`, `0xD0` `AE#40630`, `0x112` `AE#40627` | `AddShout` generates a Story Manager event (`AE#32299`); shout level — via `PlayerControls` (`AE#42344`) |
| Combat | `0xD4` `AE#41009`, `0xD5` `AE#41130` (own `combatGroup` `+0x9D8`), `0xD6` **`AE#41241` `CheckValidTarget`**, `0xE3` `AE#41024` `IsInCombat` (flag), `0xE4` `AE#41166` (empty), `0xE5` `AE#41147`, `0x11B` `AE#41167` (empty), `0xEF` `AE#40663`, `0xF5`, `0xF6` `AE#40531`, `AE#40532`, `0x102`, `0x103` `AE#40706`, `AE#40707`, `0xA6` `AE#41235` | No combat AI: combat update is empty, combat is a flag |
| Dialogue and packages | `0xD8` `AE#41017` (no), `0xDA` `AE#41005` (empty), `0xDC` `AE#41018` (no), **`0xDE` `AE#40487`**, `0x119` `AE#40759` | The player does not start dialogue like an NPC |
| Fading | `0xE0` `AE#40453` `UpdateAlpha` (lower bound), `0x11C` `AE#41168` (empty) | The player does not fade out with distance |
| Death | `0x10E` `AE#40733` `KillImpl`, `0xAB` `AE#40491` `Resurrect` `[h]` | Resurrection recreates `AIProcess` and puts the player back into `ProcessLists` (`AE#41291`), camera |
| Misc | **`0x117` `AE#40687`** (3D, third-person view), `0x113` `AE#41131`, `0x114` `AE#41076` (horse), `0xF9` `AE#40679` (factions considering `assumedIdentity`), `0x10F` `AE#40690`, `0x110` `AE#40483`, `0xC3` `AE#40468`, `0xC6` `AE#41006`, `0xC9` `AE#40480`, `0x129` `AE#41144`, `0x12E` `AE#40757`, `0x12D` | Minor flags and constant returns |

## 5. Player methods outside the vtable

### 5.1 List

| Function | What it does | Who calls it, thread |
|---|---|---|
| `AE#36581` (`Main::AE_36581` in the database) | Player update: vfunc `0xAD`; if the position left an exterior cell — new cell (`AE#13717`), position cache (`AE#40730`), `TES` grid (`AE#13288`), move to cell (`TESObjectCELL::AE_19411` → vfunc `0x98`) | `Main::Update+0x2A7`, main thread, **by code** |
| `AE#40438` | Executes the teleport from `queuedTargetLoc` (`+0x648`, 8 reads, 2 writes): `PositionPlayerEvent` (`+0x55E`, `+0x889`, `+0x8E1`), `AE#40744`, `TES::AE_13291`, saving; at the end drains the Story Manager queue (`+0x839` → `AE#32484`) | `Main::Update+0x6E`, `CenterOnCell_Impl` `AE#40437`, main thread, **by code** |
| `AE#40445` | Fast travel: `PositionPlayerEvent` (`+0x254`, `+0x414`), vfunc `0x98` (`+0x882`) | Not analyzed |
| `AE#40744`, `AE#40745` | Teleport steps; `AE#40744` writes `exteriorPosition` (`+0x638`) | `AE#40438`, `AE#40445`, `AE#35652` |
| `AE#40661` | Jail or escape, writes `queuedTargetLoc` | `AE#40438+0x9A`, `ServePrisonTime` |
| `AE#40469` | Places the player (and horse) at a point; on cell change — vfunc `0x98` | `Update+0x6FE`, `+0xF2C`; `AE#40562` |
| `AE#40671` | On world change — `cachedWorldSpace` (`+0x630`) and map markers (`+0x500`), under its own lock | `SetParentCell`, `FinishLoadGame` |
| `AE#40730` | Copy of player position into `AE#404241`…`AE#404243` | `AE#36581`; read by `AE#40807`, `AE#40817`, `AE#40818` (radius 700 from this point) |
| `AE#41371` | Player animation: vfunc `0x7D` | `Job_Player_animation` `AE#39055` |
| `AE#37350` | Actor movement; `+0x195` `cmp actor,[AE#403521]`, flag → `TryMoveTo` `AE#78260` | `UpdatePlayerMovementJob` `AE#41445`, `UpdateActorMovementJob` `AE#41444` |
| `AE#40522`, `AE#39814` | Part of player AI after the task list | `Job_Non_render_safe_AI` `AE#39089+0x19B`, `+0x1AE` |
| `AE#51929` | Loading screen: temporarily substitutes the player's `currentLocation` with the target's location (`+0xCA`), then restores it (`+0x325`) | `LoadingMenu::ProcessMessage` `AE#51924` |
| `AE#40437` `CenterOnCell_Impl`, `AE#40413` `IsSleeping`, `AE#40660` `SetEscaping`, `AE#40621` `UpdateCrosshairs`, `AE#40557` `DestroyMouseSprings` | NG names | — |

### 5.2 The player's `SetParentCell` (`AE#40466`, vfunc `0x98`)

By code, in order: `BGSActorCellEvent` "left" and "arrived" (`AE#41034`, `+0x67`, `+0x7C`) → if the cell's location differs: the `CLOC` event (`AE#32288`, `AddEvent_Impl`), `TESActorLocationChangeEvent` (`AE#20039`), writing `currentLocation` (`+0x19B`) → `Actor::SetParentCell` → Havok world flag, `MoveHavok` → `PlayerCamera::AE_50833` → `AE#13062` → `Sky::SetClimate` → `GrassManager::SetGrassEnabled` → `AE#40671` → world-boundary check (`lastKnownGoodPosition`, `playerFlags`). Everything except `Actor::SetParentCell` is global and designed for a single player cell.

Who calls vfunc `0x98` for the player: `TESObjectCELL::AE_19411+0x251` (from `AE#36581`), `AE#40469+0x261`, `AE#40445+0x882`. For any references, possibly including the player: `MoveRefToNewSpace` `AE#19799`, loading `AE#35717` (not verified). Doors — via `AE#40438` (T39).

## 6. Threads and order

| What | How it is called | Thread | Note |
|---|---|---|---|
| `PlayerCharacter::Update` (`0xAD`) | `AE#36550` → `AE#36564+0x2A7` → `AE#36581+0xAD`. Two other calls of slot `0xAD` in code: `AE#41369+0x2A6` (the `UpdateActor` task `AE#41448`, actor by handle from a list) and `FlameProjectile` `AE#44122` (a different class) | Main | By code. That the player is not in the `AE#41369` list is an assumption (otherwise `Update` would run twice). The project's hook of this slot runs on the main thread (in game) |
| Teleport queue and Story Manager | `AE#36564+0x6E` → `AE#40438` → `+0x839` `AE#32484` | Main | By code. Settles question 2 in `LOCATION_TRIGGERS.md` |
| `TES` grid, attachment, `DoMoveToHigh` | `AE#36581+0x201` → `AE#13288` → `AE#19063` → `AE#19280+0xCF` | Main | By code |
| Player animation | `Job_Player_animation` `AE#39055` → `AE#41371` → `0x7D` | `BSJobs` task | By code; that the jobs run on six worker threads — in game (T31) |
| Player movement | `UpdatePlayerMovementJob` `AE#41445` → `AE#37350` | `BSJobs` task | By code |
| Player AI after the tasks | `Job_Non_render_safe_AI` `AE#39089` → `AE#40522` | `BSJobs` task | By code |
| `Job_Wait_Player_Update_1` `AE#39049` | Waits for tasks 0 and 1 | Task | The "player first, then actors" order is an assumption |

## 7. `PlayerCharacter` fields (AE, 0xBE8)

Layout — `ghidra_struct PlayerCharacter` (T36). Up to `+0x2B8` — `Character`; `+0x2B8`–`+0x2D0` — four sinks; `+0x2D8`, `+0x330`, `+0x388` — three event sources; `+0x3E0` — `PLAYER_RUNTIME_DATA`.

| Field | Offset | Writers | Readers | Value for portals |
|---|---|---|---|---|
| `parentCell` | `+0x60` | `Actor::SetParentCell` | 73 sites in 61 functions (T31) | "Where the player is" |
| position, angle | `+0x54`, `+0x48` | movement, teleport | 57 sites; "position only" — 90 (T31) | — |
| `currentLocation` | `+0xAD0` | `AE#40466+0x19B`, `AE#40411+0xE39`, `AE#51929+0xCA`/`+0x325` | `AE#19812`, `AE#19668`, `AE#40466`, `AE#40660` | Quest and scene conditions, Story Manager |
| `cachedWorldSpace` | `+0x630` | `AE#40671` | map `AE#53111`, `AE#53121`, `AE#53150`, `AE#53154`, `AE#53155`, `AE#22836` | World map |
| `exteriorPosition` | `+0x638` | `AE#40744` | `MapCamera` `AE#53021` | Position on the map from an interior |
| `queuedTargetLoc` | `+0x648` | `AE#40438`, `AE#40661` | `AE#40438` | Teleport |
| `lastKnownGoodPosition` | `+0x480` | not found | `AE#40466`; candidates `AE#19411`, `AE#40554` | World boundary |
| `currentMapMarkers` | `+0x500` | `AE#40671` | map | — |
| `firstPerson3D`, `eyeHeight` | `+0x8F8`, `+0x900` | `Load3D`, `Set3D` | 3D overrides | — |
| `PlayerSkills*` | `+0x9B8` | constructor | 23 functions: HUD, menus, level, jail | — |
| `combatGroup` | `+0x9D8` | `AE#41130` | `AE#41009`, `AE#41147` | — |
| `crimeGoldMap` | `+0x3E8` | `AE#40638`, `AE#40640`, `AE#40653` | `AE#40641`–`AE#40643` | — |
| `playerFlags` | `+0xBE0` | `ScriptCommand::SetShowQuestItems` and others | 29 functions: doors `AE#17924`, jail, pickpocket, `AE#40445` | `isLoading`, `isInThirdPersonMode`, `isInCombat` |

## 8. Player globals and singletons

| Global | `AE#` | Relation to the player |
|---|---|---|
| `PlayerCharacter::singleton`, second pointer, handle | `403521`, `401069`, `403520` | Section 2 |
| `PlayerCamera::singleton` | `400802` (218 references) | Target (`+0x3C`) — player handle, set by the player constructor (T32). One camera per game |
| `PlayerControls::singleton`, `MenuControls::singleton` | `400864`, `401263` | Player input only |
| `ProcessLists::singleton` | `400315` | The player is added by the constructor (twice, `AE#41291`) and by `Resurrect` |
| `PlayerRegionState::singleton` | `400477` | Subscribed to the player's `BGSActorCellEvent`; read by `Sky::UpdateWeather` `AE#26231` |
| `CrosshairPickData::singleton`, `MenuTopicManager::singleton` | `401585`, `401099` | Player crosshair and dialogue |
| Position for iterating actors | `404241`…`404243` | Section 5.1, `AE#40730` |
| Events | `BGSActorCellEvent` `+0x2D8`: sent only by `SetParentCell`; listened by `AE#36629` (region, music, water), `PlayerParentCellListener` `AE#32154`, `HUDMenu` `AE#51612`, `StatsMenu` `AE#52509`. `PositionPlayerEvent` `+0x388`: sent by `AE#40438`, `AE#40445` | |

## 9. Subsystems

Heuristic by database names (see introduction). Numbers: sites of `AE#403521` / functions; second pointer; handle.

| Subsystem | Pointer | `401069` | Handle | How it finds out and what it does | What assumes one player in one place |
|---|---:|---:|---:|---|---|
| Actor, player | 637 / 266 | 252 | 18 | Comparison (theft `AE#37422`, death `AE#37896`, movement `AE#37507`, `AE#37350`), argument | Player movement and Havok have their own tasks |
| Interface, HUD | 473 / 180 | 0 | 28 | Pointer only: fields, skills, compass, enemy health | Compass and map — from the player's location (`AE#51728`, `AE#53111`) |
| AI, detection | 258 / 108 | 32 | 11 | Argument to `GetDistance` (`AE#39637` and neighbors), target handle | Distance to the player; processing level — by the grid around the player |
| Forms, activation, doors | 208 / 92 | 55 | 7 | Activation (`AE#24715`, `AE#17922`, `AE#17924`), theft | Doors read `playerFlags` |
| Input | 165 / 58 | 15 | 7 | Handlers (`AE#42371`, `AE#42423`) | One player for input |
| Papyrus | 164 / 133 | 29 | 0 | `Game.GetPlayer`, comparisons | — |
| Main loop | 117 / 28 | 2 | 2 | `AE#36601`, `AE#36581`, `AE#36629` | `TES` grid — by player position |
| Magic, effects | 108 / 54 | 56 | 3 | Comparing target and caster | Iteration around the player's point |
| Quests, Story Manager | 95 / 67 | 3 | 0 | Conditions (`GetVATSValue` `AE#21392`), events | `currentLocation` and `CLOC` |
| Camera | 71 / 34 | 1 | 8 | Fields, target handle | One `PlayerCamera`, target — the player (5.14, item 4) |
| Combat | 68 / 35 | 28 | 11 | Target handle, comparison | — |
| Cells, world, weather | 48 / 27 | 2 | 1 | Position | Climate, grass, region, water — by the player's cell |
| Dialogue | 13 / 12 | 1 | 2 | Target handle, `MenuTopicManager` | Subtitles — by distance to the player (`AE#52638`) |
| Sound | 11 / 8 | 0 | 1 | — | Music — `BGSActorCellEvent`; listener — the camera (assumption) |
| Items, render, physics, crimes | 121 / 57 | 28 | 4 | Argument, fields | Bounty table — player only |
| Saving, misc | 280 / 130 | 45 | 8 | — | — |

## 10. What holds "one player in one cell" (5.14, items 1, 2, 4)

| Place | Why it blocks many portals and any cell pairs | Note |
|---|---|---|
| `currentLocation` `+0xAD0` and `GetCurrentLocation` `AE#19812` | One player location for all conditions and scenes. Spoofing the "location" (T32, D′) does not change it: the field is neither position nor cell | By code |
| `SetParentCell` `AE#40466` | Besides location — climate, grass, camera, Havok, map markers, region, music, water. One call per transition (T15) enables everything at once | By code |
| `TES` grid and attachment | Center — player position (`AE#36581` → `AE#13288`); attached cells give NPCs high level | By code |
| `PlayerCamera` `AE#400802` | One camera, target — the player. The portal pass takes its own camera (T27) | By code |
| Position `AE#404241`–`43` | Another copy of the player's location for iterating actors around them | By code |
| `queuedTargetLoc`, `AE#40438` | One teleport in the queue, and in the same call — the Story Manager drain | By code |
| `cachedWorldSpace`, `exteriorPosition` | One world map and one position on it | By code |
| `PlayerRegionState`, `BGSPlayerMusicChanger`, `TESWaterSystem` | Listen to the player's `BGSActorCellEvent` only | By code (T32) |
| Loading screen `AE#51929` | Picks the picture by substituting `currentLocation`: a precedent of "temporary substitution" in the game itself | By code |

## 11. How this was obtained

- `ghidra_vtable PlayerCharacter` and `Character`, slot by slot (T38 scratchpad: `diffvt.py`); `ghidra_struct`; `ghidra_fieldrefs` on fields (`users_of AE#403521`); `ghidra_xrefs` of the three globals; call trees.
- Analysis of 125 overrides — summary over pseudocode (`summ.py`: calls, fields of the player block), selective reading.
- Subsystems — `classify.py`: class from the function name in the database or the nearest by ID (±40).
