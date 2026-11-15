# T38, T39. Bulk reading by lists — batch results

Research coordinator, 08.11.2026. The batches are from `results/T38.md` and `results/T39.md` ("Batches for bulk reading"). Read by the `re-reader` agent (Sonnet 5.5 / medium) against the Ghidra base; the coordinator spot-checked (marked "verified"). Conclusions and addresses only, no pseudocode. Name candidates are at the end of the file, for the curator.

## T38, batch 10. Camera and input (24 IDs)

Question: what the camera takes from the player (5.14, item 4 — any camera).

| ID | Name | What it does; what it takes from the player |
|---|---|---|
| `AE#50724` | `FirstPersonState::Update` | first-person view frame; position, sitting, combat, head node — all from `PlayerCharacter::singleton`; `+0x684` → `AE#50832` |
| `AE#33120` | `DragonCameraState::UpdateRotation` | camera rotation on a dragon; player position, `PlayerCamera::SetCameraTarget(player)` |
| `AE#50832` | `PlayerCamera::CheckCameraCollision` | shortens the camera by the target cell's Havok world (`target+0x60` → `GetbhkWorld`); the target is by handle `this+0x3C`, the player only in states 0, `0xB`, VATS |
| `AE#50738` | `FirstPersonState::AE_50738` | player head-turn graph variable; a single call from `AE#50724+0x2B1` |
| `AE#50725` | `FirstPersonState::GetRotation` | rotation matrix: camera node + player rotation + `sittingRotation` |
| `AE#50833` | `PlayerCamera::AE_50833` | creates the camera collision sphere (`this+0x128`) and puts it into the target cell's `bhkWorld`; does not read the player singleton. **Only call — `SetParentCell+0x271`** (verified) |
| `AE#50785` | `PlayerCamera::AE_50785` | focus point: target position by handle (`+0x54`) + `AE#37477` height or the state node; cache `this+0x144`, lock `+0x134` |
| `AE#50794`, `AE#50800`, `AE#50799`, `AE#50792` | `PlayerCamera` | sitting rotation, look input, node offset, state pointer reset; do not read the player |
| `AE#50781` | `PlayerCamera::ctor` | writes the singleton, 13 states, target = empty |
| `AE#50838` | `PlayerCamera::SetCameraTarget` | `this+0x3C` = reference handle |
| `AE#50830`, `AE#50831` | `PlayerCamera` | create / destroy the collision phantom `this+0x120` |
| `AE#36661` | — | not a camera: collects references within a radius of the player position into a global array |
| `AE#42371`, `AE#42350`, `AE#42344` | `PlayerControls` | player movement flags, action by target list, handler flag |
| `AE#42423`, `AE#42420` | `JumpHandler`, `ActivateHandler::ProcessButton` | jump, activation — all on the player |
| `AE#39762` | `RunActionAnimationLoadedCallback::vf_0x1` | not a camera: action after animation load, branch for the player under AI |
| `AE#53021` | `MapCamera::AE_53021` | map focus: in an interior — the player's `exteriorPosition`, outside — his position |
| `AE#21550` | `Condition::GetVatsTargetHeight` | VATS target height relative to the player |

**Conclusion for 5.14, item 4.** The game needs a foreign camera only for collision and the `PlayerCamera` states. The camera does not read the cell or the world directly: it takes the target's `parentCell` (`+0x60`) and its `bhkWorld`. The camera pose comes from the states (`FirstPersonState` etc.) through the `PlayerCamera` and player singletons. A portal camera needs its own pose (`NiCamera` node, matrix), not these states. Camera collision is a single one bound to one `bhkWorld`, moved only in `SetParentCell` (`AE#50833`). It is not needed for clean rendering with a given pose. Who builds the view matrix and picks cells for culling is outside the batch (render, T27).

## T38, batch 3. AI and detection (29 IDs)

Question: where AI takes the player's location, where his identity, and what radius. Identity everywhere — comparison with `PlayerCharacter::singleton`; the handle and 0x14 do not appear in the batch.

**Distance to the player — two paths:**
- **`TESObjectREFR::GetDistanceBetweenSquared` (`AE#19824`)** calls `SameWorldspace` (`AE#19494`, `+0x47`, verified) and returns `FLT_MAX` if the worlds differ; with an interior the same cell is required. Through it go `AE#39632` (greeting, radius `AE#370892`), `AE#39637` (AI high: package, combat, dialogue), `AE#39639`, `AE#39648` (travel packages: 800), `AE#39652` (alert, `AE#371146`), `AE#41334` (crime witness), `AE#41348` (350), `AE#41402` (`AreHostileActorsNear`, `AE#375160/163/166`), `AE#41403`, `AE#41442` (`CommentOnPlayerAction`: 350, 400). **Our detour of `AE#19824` (`HooksPortal.cpp:375`, 0.8.24) already carries this path across the door.**
- **`DoDetectionJob` (`AE#42742`, `BSJobs` jobs) — its own path:** `AE#19784` (position + "interior cell or world" key) for the observer (`+0x4E0`) and the player (`+0x4F0`), `AE#18517` on `+0x4FD` (`FLT_MAX` for different keys); same for observer↔target (`+0x513`, `+0x520`) (verified). Per the agent, beyond 8192 or in another cell — `CalculateDetection` is skipped, state "not detected". **Our plugin only replaces `AE#19784` in subtitles (`AE#52638+0x122`)** — NPCs behind the portal do not detect the player. Task for T22/T32.

**Direct reads of the player location** (`data.location` `+0x54`, `parentCell` `+0x60`): `AE#39322` (dialogue avoidance sphere), `AE#39391` (can the NPC start a conversation: water depth by `parentCell`), `AE#39673` (reaction to the player: target — player position), `AE#37618` (free spot nearby: the player is added to the obstacles separately from `highActorHandles`, radius 256), `AE#40818`/`AE#40821`/`AE#40826` (player travel tracker, 700, 6144), `AE#40829` (actors around the player, `AE#381586`).

**Identity only:** `AE#39644` (package procedures; **if the player is a package participant, writes his real position** `SetPosition`/`StopMoving` — the location substitution must not redirect these writes), `AE#39651`, `AE#39675`, `AE#40816`, `AE#40725` (mount), `AE#40522`, `AE#39814` (`Job_Non_render_safe_AI`, main thread), `AE#41384` (neither location nor identity).

**`AE#41369` (`UpdateActor` job, vfunc `0xAD` on `+0x2A6`):** there is no explicit player check; the task list is filled by `AE#41367` from `ProcessLists::highActorHandles`. Per the agent, the player is not in this list: `AE#37618` adds him to the obstacles manually, and the player's AI goes its own way (`PlayerCharacter::Update+0x1479` → `AE#40748` → `AE#39108` → `AE#39637`). Medium confidence; T40 will verify (open question T38 about the double `Update`). NPC AI runs on job threads, player AI on the main thread.

**Conclusion for T22, T32 (player location substitution within the far-side frame):** cover `AE#19824` (done), the `AE#19784`/`AE#18517` pair in `DoDetectionJob` (not), direct reads of `data.location`/`parentCell` from the list above (not). The frame is `thread_local`: detection and NPC AI run in jobs. Not read: `RequestDetectionLevel`, `Character::AE_37762`, `AE_37533`, `GetLineOfSightImplementation`, `AE_40493`, `AE_37250`; radius values from settings.

## T38, batch 4. Actor code: "if this is the player" branches (25 IDs)

Question: what the comparison with the player changes and what breaks (a) for NPCs behind the portal, (b) for the player on the far side of the portal. Comparisons — pointers `AE#403521` and `AE#401069`; the handle and 0x14 do not appear (except `formID == 7` — the player's base NPC in `AE#24669`). The agent matched branch offsets by the order of reading the pseudocode — approximate.

| ID | What it does; the player branch |
|---|---|
| `AE#37348` | `Actor::Update`: **`+0x1D0` `cmp this, player`** → for a non-player `+0x1E4` player camera position (`AE#50785`) → `+0x1F3` `AE#78274` writes the distance to the camera into the controller `+0x29C` (verified). **Already hooked:** `CharControllerLodDistance` (`HooksPortal.cpp:383`, 0.8.17). Also: the player sneaking is passed to followers, drowning only for the player |
| `AE#37347` | NPC AI per frame: followers, crimes, player sleep changes `dt`; player → `AE#39108`, others → `AE#39109` |
| `AE#37356`, `AE#37507` | movement from animation (for any actor, from `AE#37350+0xFB`); `Actor::Move`: jump and fall — for the player via `PlayerControls`, for NPCs via `BGSActionData` |
| `AE#37896` | `Actor::KillImpl`: a child dies only if the player; killing a follower is credited to the player; **`+0x1106` — NPC↔player distance when `gSceneComplexCounter != 0`** |
| `AE#37422`, `AE#37425`, `AE#37426`, `AE#19827`, `AE#14864` | theft, assault, murder (`crimeCounts`, bounty, `RequestDetectionLevel(witness, player)`); "is activation a crime" (owner of the door's destination cell); harvesting |
| `AE#17922` | `TESObjectDOOR::Activate`: player — lock, jail, fade via `AE#14284`; **an NPC with a teleport door is moved immediately** (`MoveRefToNewSpace`, without fading, `+0x558`) |
| `AE#17924`, `AE#17927` | after the fade: sets the player's teleport (`AE#40442`, callback `AE#17927`); after the teleport: position and rotation correction, terrain or cell, sound |
| `AE#19796`, `AE#17887`, `AE#24715` | `ActivateRef`, container, NPC: the player opens a menu, an NPC acts immediately |
| `AE#38001`, `AE#38002`, `AE#38805`, `AE#34145`, `AE#24669` | equipment (HUD messages only to the player), shout, spell check, race change |
| `AE#38586` | hit application: sneak attack, critical hit, stagger, kill cam |
| `AE#18102`, `AE#34452` | weapon fire (for the player — a ray and his 3D node); spell projectile launch: **launch cell — the shooter's `parentCell`** |
| `AE#40814` | the player on a flying mount attacks a target |

**Conclusion.** (a) NPCs behind the portal: the distance to the camera in the controller (`AE#78274`) is already moved; remaining are the distance in `KillImpl` and `RequestDetectionLevel` of crime witnesses (see batch 3 — `DoDetectionJob`). An NPC activating a real teleport door is moved without fading via `MoveRefToNewSpace` — a portal that replaces a door needs its own path for NPCs. NPC projectiles stay in their cell. (b) The player on the far side: the door, crimes, and shots take the player's `parentCell`, so the cell change must go only through `SetParentCell` (T15).

## T38, batch 6. Player handle `AE#403520` (30 IDs)

Question: what the player handle is compared with. Almost everywhere — "actor is the player"; location is not taken through a handle comparison.

| What the handle serves | IDs |
|---|---|
| dialogue target (`AIProcess+0x114`): close dialogue, turn to the player; in the NPC↔NPC branch — distance | `AE#40092` |
| combat target (`actor+0x104`, verified: a single access) — count the player's enemies, add to the player's combat group | `AE#41261` |
| the player's combat groups, packages 0x16/0x18 | `AE#40590` |
| escort: player in the target list → path flag | `AE#28453` |
| projectile shooter: kill cam, hit effect level, sound listener | `AE#44122`, `AE#44213`, `AE#44166` (reads the listener position `AE#67710`), `AE#44005` (throw source) |
| end of controlling a mount or dialogue actor: the player's and the actor's positions and world | `AE#40808` |
| 3D queue: `QueuedPlayer` receives the player handle | `AE#13057`, `AE#13112` |
| interface: pause menu and tween menu (synchronized animation `AE#32808`), map (player marker, map target), quest markers (the player is not a target) | `AE#35734`, `AE#52714`, `AE#52718`, `AE#52719`, `AE#52971`, `AE#53074`, `AE#53077`, `AE#53087`, `AE#53178` |
| misc: extra data 0x1D, `BipedAnim`, effect shader on the player's 3D, `ProcessLists` records, control lock, handle substitution | `AE#11648`, `AE#15692`, `AE#34935`, `AE#37681`, `AE#41349`, `AE#41642`, `AE#43573`, `AE#43577`, `AE#51116` |
| not a comparison: a "not zero" check before polling input | `AE#39039` |

**Conclusion.** The handle is pure identity; the location substitution does not touch it. For the portal, the important ones are the dialogue target, combat target, combat group, and escort — they all take the player via the singleton, not via the cell.

## T38, batch 5. Interface and map (25 IDs)

Question: which player location the interface and the map take. `currentLocation` (`+0xAD0`) and `exteriorPosition` (`+0x638`) are not read by any function in the batch (`fieldrefs`, per the agent). Location is taken by four paths: position `data.location`, "is in interior" `AE#19815` (bit 0 of `parentCell+0x40`, verified), `GetWorldSpace(player)`, `cachedWorldSpace` (`+0x630`).

| ID | What it does; the player location |
|---|---|
| `AE#51612` | `HUDMenu::ProcessMessage`: compass (`AE#51744`, `AE#51728`); "in interior" via `AE#19815` |
| `AE#51728`, `AE#51744`, `AE#51676`, `AE#51671` | compass marker (direction and distance from the player position); enemies on the compass — **only from the player's world** (`AE#51744`, the only call from `HUDMenu+0x16A9`, verified); on-screen quest markers; enemy health bar — all by position |
| `AE#53111` | world map every frame: markers, "You are here" hidden if the player is in an interior (`AE#19815`) or the world has no parent |
| `AE#53121`, `AE#53150`, `AE#53154`, `AE#53155` | "no-map location"; choosing the world for the map (`TES::worldSpace`, otherwise `cachedWorldSpace`, to the top parent); show / restore terrain on map open and close |
| `AE#52409`, `AE#52345`, `AE#52536`, `AE#52335`, `AE#51141`, `AE#51126`, `AE#50951`, `AE#51543`, `AE#51548`, `AE#51571`, `AE#51577`, `AE#51578`, `AE#51583`, `AE#51584` | race, stats, container, barter, favorites, gift menus — identity only |
| `AE#47314` | `Actor::UpdateNavPos` — not interface, does not take the player location |

**Conclusion.** The interface does not hold onto `currentLocation`. The compass and the map compute coordinates in the player's world: interior actors behind the portal will not appear on the compass or will end up in a foreign coordinate system (interior and street coordinates are unrelated). The map follows the player's `parentCell`: a seamless transition that leaves `parentCell` on the street will leave the map a street one; gates to another world require reconciling `cachedWorldSpace` and `TES::worldSpace`.

## T38, batch 7. The second player pointer `AE#401069` (28 IDs)

**Who writes** (verified): `AE#401069` (`gPlayerCharacter3`) — only the player constructor `AE#40411+0xFC7` (`= this`, the last write before return) and `dtor_impl` `AE#40412+0xE3` (`= 0`); the other 547 references are reads. `AE#403521` is written by `AE#36629+0xAB` (constructor result) and cleared by the `TES` reset `AE#13754+0x358` (`= 0`). **During gameplay both pointers are equal**; they diverge only in the creation menu (after the constructor, before `AE#36629+0xAB`) and on game reset (`AE#403521` is nulled earlier, `AE#401069` — in the destructor). Player substitution must account for **both** pointers.

**Why the code takes the second pointer:** in all read places — only identity comparison (`actor == player` / `!=`); player methods are called via `AE#403521` (e.g. `AE#12789+0x56`). Probably different translation units; the reason is not proven.

| ID | What it does; the comparison |
|---|---|
| `AE#38928`–`AE#38930`, `AE#38933`–`AE#38935` | internal `ActorEquipManager` functions (equip/unequip spell, item, shout; Skyrim Together hooks them) |
| `AE#39897`, `AE#39912`, `AE#39923` | equipment: player camera, `AE#20159`, vampire feeding |
| `AE#28644` | follow package: player in combat → combat group |
| `AE#44725`, `AE#44015`, `AE#44014` | combat and hits: damage multiplier on the bleeding-out player (`fCombatPlayerBleedoutHealthDamageMult`) |
| `AE#42824` | right-hand weapon button handler |
| `AE#16059`, `AE#16019`, `AE#16020`, `AE#16117`, `AE#15678` | inventory: ownership, theft, gold |
| `AE#32782`, `AE#34407`, `AE#34658`, `AE#34914` | synchronized animation (VATS), spell cast, model effect and shader position (first person) |
| `AE#54768`, `AE#56098` | Papyrus `StartCombat`, `RemoveItem` (VM thread) |
| `AE#36991`, `AE#26366`, `AE#12789` | `BSTaskPool` dispatcher; perk entry point formula; writing a reference to a save |

## T38, batch 8. Quests, Story Manager, scenes, dialogue (27 IDs)

Question: what assumes a single location or cell of the player.

| ID | Name | What it does; the single-location assumption |
|---|---|---|
| `AE#40466` (context) | `PlayerCharacter::SetParentCell` | **the only one that reads and writes the player's `currentLocation`.** `CLOC` (`AE#32288` at `+0x105` → `AddEvent_Impl` `+0x11C`) and `OnLocationChange` (`AE#20039` at `+0x154`) — only if `GetLocation(new) != currentLocation` (`+0xEB…+0xF5`, verified); the `BGSActorCellEvent` pair (`AE#41034`: `+0x75` — leaving the old one, `+0xC1` — entering the new one, verified) — on every cell change |
| `AE#32288` | — | writes the `CLOC` event record (0x18 bytes: handle, old and new location); gets the locations as arguments; the only call — `SetParentCell+0x105` (verified) |
| `AE#41034` | — | `BSTEventSource<BGSActorCellEvent>::SendEvent`: type 1 — leaving, 0 — entering |
| `AE#20039` | — | `SendEvent` of the `ScriptEventSourceHolder+0x108` source: (player, old, new location) = `OnLocationChange`. 4 more callers (`AE#37313`, `MoveRefToNewSpace`, `TESWorldSpace::AE_20541/20542`) not analyzed |
| `AE#32154` | `PlayerParentCellListener::vf_0x1` | the player's `BGSActorCellEvent` receiver: on entry writes the cell's water height (`AE#19001`, `cell+0x78`) into a **single global** `AE#379980` (verified: listener vtable slot 1). A portal without `SetParentCell` leaves it stale; who reads the global — not found |
| `AE#25063` | `TESQuest::AE_25063` | filling quest aliases: searches in `TES::interiorCell` if it exists, otherwise in `gridCells` — i.e. in the cells held by `TES`, not by the player's location. With several live cells it sees only what `TES` holds |
| `AE#25046` | `TESQuest::AE_25046` | quest reset/start; from `BeginStartUpQuest` `AE#32485`, `LoadGame` |
| `AE#28386`, `AE#28374` | dialogue activation | Story Manager activation event with the **target's location**, not the player's |
| `AE#52638` | — | distance to the player for tracked HUD references; computes a single coordinate space without a cell check |
| `AE#39250` | `AIProcess` (release) | if this is the player's process — a special branch |
| `AE#21392`, `AE#25260`, `AE#23933`, `AE#23940`, `AE#23941`, `AE#28352`, `AE#25609`, `AE#24234`, `AE#24244`, `AE#21422`, `AE#32160`, `AE#56856`, `AE#56858`, `AE#56853`, `AE#32299`, `AE#32308`, `AE#32310` | conditions, quest objectives, scenes, voice, event records | no cell assumptions except the player singleton |

**Conclusion for T16, T20.** Everything that depends on the player's location comes from a single place — `SetParentCell`; the events receive the old and new locations as arguments. A transition between cells of one location does not produce `CLOC` and `OnLocationChange`, but produces the `BGSActorCellEvent` pair. A seamless transition without `SetParentCell` will leave `currentLocation`, the water global `AE#379980`, climate, grass, and camera collision (`AE#50833`) stale. Quest aliases are searched in the cells of `TES` — with many portals this is a question for T29 and T21.

## T38, batch 1. Unnamed overrides and `Update` steps (34 IDs)

Question: what is different for the player and whether it concerns his location. Slots — the main vtable `AE#208040`, otherwise `@0x38` (`IAnimationGraphManagerHolder`), `@0xA0` (`MagicTarget`), `@0xC0` (`ActorState`).

**Overrides** (do not write the player location):

| ID | Slot | What it does |
|---|---|---|
| `AE#41259` | `0xA0` | projectile launch angles: first person — with the INI arrow-tilt correction |
| `AE#40738`, `AE#40479` | `0xA2`, `0xB4` | same for first- and third-person 3D (first-person biped, `TaskQueueInterface`) |
| `AE#41145`, `AE#41006`, `AE#41242`, `AE#40757`, `AE#41144` | `0xB3`, `0xC6`, `0xD1`, `0x12E`, `0x129` | constants and pass-throughs (no equip animation in the queue; 2; false; false; `Is3rdPersonVisible`) |
| `AE#40480` | `0xC9` | reset of player flags, then `Actor::Unk_C9` |
| `AE#41258` | `0x105` | return ammunition to the hand |
| `AE#40687` | `0x117` | first-person offset → world (reads the position) |
| `AE#40539` | `0x12A` (new) | game save + tint masks |
| `AE#40753`, `AE#40755`, `AE#40756`, `AE#40754` | `@0x38` 5, 4, 7, `0xB` | the player's animation graph: creation (0x250 bytes), nodes, first-person variables, finalization |
| `AE#40762` | `@0xA0` 8 | `EffectAdded`: HUD message about a new effect |
| `AE#40743`, `AE#40735`, `AE#40752` | `@0xC0` 4, `0x14`, `0x15` | angle without tilt; `DoSetSitSleepState`; weapon drawn → `PlayerCamera` |
| `AE#41147` | `0xE5` | `StopCombat`: remove the player from the combat group |

**`PlayerCharacter::Update` steps (`AE#40447`):**

| ID | Where | What it does; the player location |
|---|---|---|
| `AE#40470` | `+0xF36`, `FinishLoadGame+0x106`, `AE#40562+0x123` (verified) | **writes the location cache:** `lastKnownGoodPosition` (`+0x480`) and the exterior world or interior cell into `lastKnownGoodLocation` (`+0x990`, verified via `fieldrefs`); every frame |
| `AE#40750` | `+0xF7B` | opening of the player's world map markers within a radius, `LocationDiscovery` event (world and position) |
| `AE#40450` | `AE#36581+0x60` (verified), `FinishLoadGame+0xA1` | a variant of the player update for a special camera state; `BGSTerrainManager::Update` for the player's world |
| `AE#40748` | `+0x1479` | the "under AI or dead" branch; at the end `AE#19165(parentCell, player)` |
| `AE#40556`, `AE#40568`, `AE#40588`, `AE#40423` | `+0xC15`, `+0xC1D`, `+0x332`, `+0x28D` | grabbed object (`bhkWorld` of the player's cell), thrown objects, controller support, rays from the player position to fading nodes |
| `AE#40449` | `+0xA87`, `+0xF65` | `SetJobTaskEvent(2)`, `PlayerCamera::Update`, sound listener from the camera (`AE#67708`/`AE#67709` not fully read — important for T23) |
| `AE#40747`, `AE#40736`, `AE#40604`, `AE#40605` | `+0x1EB`, `+0xD4`, 14 callers | timers and looking at the sun; speed history; the player's action list for NPC barks |

**Conclusion.** Of the 34 functions, only `AE#40470` writes the player location — the "last known good place" cache (the player returns from the world border by it, `AE#40469`). None writes `parentCell` or `currentLocation`. The cache is built every frame from `parentCell` and the position: a transition without changing `parentCell` will leave the old cell in it.

## T38, batch 2. Teleport and transition (31 IDs)

Question: the order of the transition and who writes `queuedTargetLoc` (`player+0x648…+0x68A`: world `+0x648`, cell `+0x650`, position `+0x658`, rotation `+0x664`, flags `+0x688…+0x68A`).

**Order in `AE#40438`** (main thread, `Main::Update+0x6E`): the jail branch (`AE#40656` — to jail, `AE#40661` — to freedom) → `AE#36604` (preparation: re-entry guard, `TES`, `ProcessLists`, VM, player perks; 11 callers) → `AE#51628` (HUD message) → door with loaded 3D: `AE#40445` (followers, mount, `AE#40745`, `MoveRefToNewSpace`), otherwise `AE#40744` (cell) or `AE#40745` (world + position → cell → `AE#40744`) → `AE#32491` (start-up quests) → `PositionPlayerEvent` (`AE#41051`, values 0/3/4, source `player+0x388`) → loop `AE#41313` until `AE#41316` → closing `AE#13363(0,…)` → `AE#13388` (bringing cells to state 7) → reset of `queuedTargetLoc`.

**Inside `AE#40744`** (verified): the base `TESObjectREFR::SetParentCell` (`AE#19818`) is called **directly** (at `+0x18D` — with an empty cell); between them `SetCurrentCell` (`+0x14E`) or `AE#13315` + `AE#13317`; then `AE#19411` (`+0x19B`, add the reference to the cell), inside `+0x251` — slot `0x98` of the reference, for the player this is `PlayerCharacter::SetParentCell` with `CLOC`. The batch agent claimed there is no `0x98` call — **incorrect**.

**Who writes `queuedTargetLoc`:** `AE#40442` (common path), `CenterOnCell_Impl` (`AE#40437`: `+0x144`, `+0x259`, then calls `AE#40438` itself), `AE#40469+0xB3` (return to the last "good" place at the world border), `AE#40656+0x12C`, `AE#40661+0x107`, `AE#40438` itself (`+0x5F3`, `+0x98B`); reset — `AE#40732` and the constructor (`AE#40411+0x4D3`). Callers of `AE#40442`: door `AE#17924+0x1B1`, `MoveTo_Impl` `AE#56626+0x18B`, fast travel (`AE#22612`, Papyrus `AE#55457`, map `AE#53102`), `AE#22279`, `AE#55427`, game start `AE#36549`, `AE#36642`.

| ID | What it does |
|---|---|
| `AE#40807`, `AE#40806` | flying mount: moving the marker `AE#371018` and the mount |
| `AE#41346` | `ProcessLists`: the player's followers go with him (inferred, not fully read) |
| `AE#41317`, `AE#41316`, `AE#41313` | double buffer of nearby actors; "processing busy" flag; forced actor update |
| `AE#32216`, `AE#32487` | enabling the water system; processing of `BGSStoryTeller` quests after loading |
| `AE#13425` | whether all grid cells around (x, y) are loaded |
| `AE#40517` | path to the player's map marker |
| `AE#40562` | world border check → `AE#40469(…,0)` |
| `AE#35652`, `AE#35651`, `AE#35639` | loading and saving the player location in a save (`AE#35652` calls `AE#40744`/`AE#40745`) |
| `AE#13754`, `AE#36601`, `AE#36629` | `TES` reset; full game reset; player creation (0xBE8, FormID 0x14, handle) |

## T38, batch 9. Cells, world, weather, region, music, water (31 IDs)

Question: what treats the player's cell as "the world".

| Node | IDs | A single player point? |
|---|---|---|
| Grid and world | `AE#13288` (grid shift from position; from `AE#36581` with the player position), `AE#13363` (loading screen; on closing `BGSTerrainManager::Update` with the player position), `AE#31802` (`BGSTerrainManager::Update`: one active LOD manager, the player's `parentCell`) | yes: one grid, one world, one LOD |
| Weather and region | `AE#26231` (`Sky::UpdateWeather`: the `TES` interior region or `PlayerRegionState::lastKnownWeatherRegion`), `AE#41262` (`PlayerRegionState` constructor, `BGSActorCellEvent` listener), `AE#41265` (point in regions; from `PlayerCharacter::Update+0xEED`, `AE#40562`), `AE#41266`, `AE#41272` | yes: one weather, one region |
| Water | `AE#32164`, `AE#32179` (`AddWater`), `AE#32217` (`AddRipple`), `AE#32218`, `AE#32089` (updating all `BGSWaterUpdateI` with the player position; the only call — `TES::AE_13291+0x247`, verified) | yes |
| Light | `AE#18889` (after attaching a cell: light within a radius of the player) | yes |
| Music | `AE#40177` (`BGSPlayerMusicChanger` bound to the player), `AE#35180`, `AE#35122`, `AE#35181` (`AddEventSink`) | yes |
| Cell mechanics | `AE#19063` (attach; state 7), `AE#19280` (on a reference: vfunc `0x91` `DoMoveToHigh`), `AE#19411` (add a reference to a cell), `AE#19165` (move a reference's 3D into the cell graph, with `BSPortal` rooms) | **no**: the cell is an argument |
| Misc | `AE#13328` (`coc` transition: `CenterOnCell_Impl` `+0x96`, waiting for IO `+0xA7`, verified), `AE#17967` (`AutoDoorFadeCallback`: door → the player's `forceActivateRef`), `AE#13729`/`AE#13730` (explosion, projectile at a point), `AE#13763`, `AE#13771`, `AE#13062`, `AE#40201`, `AE#32224` | — |

**Conclusion for 5.14, items 1–2.** Cell mechanics (attach, references, 3D) works off the cell argument and is suitable for many live cells. "World" — grid, LOD, weather, region, water, light, music — is bound to the player position or to `TES::interiorCell`/`worldSpace`. A second live exterior cell will be left without the water and weather of its world; an interior behind a portal does not suffer from this.

## T39, batch 1. Save loading and start (30 IDs)

Question: which thread the save loading runs on (where the `DetachAll` from `AE#13295+0x64` off the main thread comes from, 44 cases in traces) and in what order the cell work happens.

**The thread is the main one; there is no switch to another thread in the code.** The chain (the first and last links verified): `Main::Update+0x9A` → `AE#35772` (save/load request queue) → `AE#442580+0x465` → `AE#35600+0x34F` → `AE#35641+0x7E` → `AE#35642+0x46` → `AE#35652` → `AE#13295` at `+0x67A`. `AE#35772` also calls the loading screen `AE#13363+0x3FA` — on the caller's thread. The only path to `AE#13295` outside the main thread is **game start**: `InitTESThread` (`AE#36644`) → `Main::AE_36553+0x341` → `AE#13753+0x6F` → `AE#13295(0,0)`, once. The 44 cases are not explained by this. **Hypothesis:** in old traces the "main thread" was identified incorrectly (e.g. by the `kDataLoaded` thread, which is not the main thread on 1.6.1170). Check — `GetCurrentThreadId` in the `PlayerCharacter::Update`, `AE#35772`, `AE#13295` hooks (T40).

**Loading order** (`AE#35600`): preparation `AE#35779` (VM freeze, canceling loads, sound, screen `AE#13363(1,…)`) → `IOManager::AE_75712` → global data table 1 → `AE#35652`: world `AE#13315`, grid shift `AE#13288`, `AE#40744` (or `AE#40745`/`AE#13317`), then `AE#13295(1,0)` (`DetachAll` only if there is an `interiorCell`) → the change form loop, tables 2 and 3 → `AE#35567` (form finalization, synchronous cell loading `TES::AE_13387`, waiting for IO) → `AE#35780` (`GridCellArray::AE_15960`, `BGSTerrainManager::Update`, close the screen `AE#13363(0,…)`, VM).

**`AE#13295(TES, a, b)`:** grid vfunc 1 of `TES+0x78` (`DetachAll`, return `+0x64`) — always with `a = 0`, with `a = 1` only if there is an `interiorCell` (`+0x4E`); then buffer work (`AE#13439`, `AE#13436`); with `(0,0)` it nulls `interiorCell`. Callers: `AE#36601` (game reset), `AE#13753`, `AE#13754`, `AE#40745`, `AE#35652`, `AE#40446`.

| ID | What it does |
|---|---|
| `AE#36544`, `AE#36548`, `AE#36549`, `AE#36641`, `AE#36642` | start: frame loop with `Main::Update`; initialization and the `TESInitThread` thread; the start cell from the command line, placing the player (`AE#40442` + `AE#40438`) |
| `AE#36601`, `AE#13280`, `AE#13627`, `AE#13754` | full game reset (`AE#13295(0,0)` `+0x1E1`, `AE#13754` `+0x514`, new player `AE#36629`); destruction of `TES` |
| `AE#35599`, `AE#35638`, `AE#35639`, `AE#40562`, `AE#40469` | saving: the player location in the save (world, grid, cell, position), world border check before saving |
| `AE#36581`, `AE#36582` | per-frame player update (cell exit → `AE#13717`, `AE#13288`); a variant for menu mode |
| `AE#13327`, `AE#13328`, `AE#13616` | console cell check `RunCellTest` |
| `AE#55427`, `AE#22279` | Papyrus `LatentPlayerMover`; moving a reference or the player to a reference with an offset (the player — via `AE#40442`) |

## T39, batch 2. Attach and events (33 IDs)

Question: who sends `CellFullyLoaded` and `AttachDetach`, and what the `bool` of the primitives means.

**Three different events:**
- **`CellAttachDetachEvent{cell, 0…3}`** (own source `AE#400441`, dispatch `AE#19361`): `AE#18976+0x69` (0, before attach) and `+0x329` (1, after); `AE#19130+0x91` (2, before detach) and `+0x77C` (3, after). Main thread: `Main::Update+0x825` → `TES::AE_13288` → `AE#18976`/`AE#19130`, also `SetCurrentCell`, `AE#13315`, `AE#13317`. The names 0…3 — by call order.
- **`TESCellAttachDetachEvent(ref, attached)`** (`ScriptEventSourceHolder+0x1B8`, dispatch `AE#19362`): for each reference from `AE#19187+0x32E` (1) and `AE#19188` (0), inside `AE#18976`/`AE#19130` — main thread.
- **`TESCellFullyLoadedEvent`** (`+0x210`, dispatch `AE#13664`): `AE#18895+0x55E` (cell update when the counters `runtime+0x16C`, `+0x170`, `+0x17C` are 0), `TES::AE_13381+0x345` (after save loading), a dead copy `AE#19146`. **`AE#18895` is called by `UpdateManagedNodesJob` (`AE#18871+0x18E`, a job) and `TES::AE_13291+0x23A`** (verified). Hence the exterior `FullyLoaded` off the main thread (in game: 1 624 of 1 676), while the current interior — via `AE#13291` on the main thread. Which thread runs the job list `gJobList_TES_CellAnimations` (`Job_Cell_animations` `AE#39067`) was not traced.

**The meaning of `bool`** (from branches, not verified in game):
- `AE#19063(cell, b)` — `b` = at the end put the cell's collision (`CellMopp`) into the Havok world (`AE#18996`). 1 — from `AE#13285`, `AE#13286`, `SetCurrentCell`; 0 — `AE#13426`, the sky cell.
- `AE#19130(cell, b)` — `b` = tear down the collision (`CellMopp`, `bhkWorld`, `UnloadCellMopp`, water off). `SetCurrentCell` and `AE#13426` give 0 for the grid — **the exterior collision stays**.
- `AE#19059(cell, b)` — 1 create the dynamic light of all the cell's lamps, 0 remove from `ShadowSceneNode`.

| ID | What it does |
|---|---|
| `AE#18976` / `AE#19063` / `AE#19130` | attach, part 1 (event 0, state 6, node to parent, references, event 1) / part 2 (node update, land, clearing fading, water on, state 7) / detach (only from 6 or 7; state 5 → 4, `+0x46 = 1`) |
| `AE#19411` / `AE#19032` | add a reference to a cell (first `AE#19032` from the old one, `+0x188`) / remove a reference |
| `AE#15962`, `AE#15959` | `GridCellArray`: attach cell (x, y) via `AE#18976`; collision of all cells |
| `AE#13427`, `AE#13426`, `AE#13285`, `AE#13286`, `AE#13287` | `TES`: load and attach a grid cell; attach/detach the whole grid; attach immediately; the grid center and its loading |
| `AE#13367`, `AE#13437`, `AE#13433` | buffers: sorting the exterior one by distance from the center; whether a cell is in the exterior / interior buffer |
| `AE#13308` | "cell attached": states 6–7 → 1, 3–5 → `!strict`; 61 callers |
| `AE#13718`, `AE#13710`, `AE#20460`, `AE#19112` | find or create an exterior cell; `TESWorldSpace::LoadCell` (also from `CellLoaderTask::Run+0x3E`, IO); reading cell data from the plugin (state 2) |
| `AE#19113`, `AE#19056`, `AE#18980`, `AE#18981` | unload: cell data; 3D and portal graph; collision; reset to state 0 (from `AE#13721`) |
| `AE#19167`, `AE#18979`, `AE#19145`, `AE#19153` | the cell's portal graph (for the exterior — of the world); the root node; whether the reference lock is taken; the counter until `FullyLoaded` |
| `AE#19055`, `AE#19028`, `AE#19046`, `AE#41292`, `AE#13440`, `AE#19178` | cell water on/off; clear reference fading; cell actors into `ProcessLists`; terrain shader flags 5×5; not fully read |

**Conclusion for T20, T21.** Detaching the grid when entering an interior leaves its collision in the Havok world. A cell's light is turned on and off by a single function `AE#19059`, water — `AE#19055`. These are ready-made levers for "holding" a cell behind the portal. The exterior `FullyLoaded` arrives from a job — event handlers in the plugin must not touch the game (`CLAUDE.md`, section 7).

## T39, batch 4. Main-thread transition helpers (38 IDs)

Question: which parts of the transition a seamless transition needs (keep, skip, replace) and what is designed for a single instance.

| ID | What it does | Seamless |
|---|---|---|
| `AE#13363`, `AE#52852`, `AE#35407`, `AE#36555` | loading screen; `LoadingMenu` message; screen mode for `JobListManager`; rendering a frame under the screen | skip (`AE#35407` would stall the frame) |
| `AE#41294`, `AE#41313`–`AE#41316`, `AE#41406` | `ProcessLists`: temporary moves, the package location queue (`AE#41316` — are there unfinished), animation and synchronization queues, high data release | skip: `Main::Update` calls them every frame anyway (`AE#41313` from `+0x7D6`, verified) |
| `AE#32484`, `AE#32487`, `AE#32491` | `BGSStoryTeller`: quest queue; quest lists (also in `Main::Update+0x524`); start-up quests once | skip |
| `AE#13425` | whether the whole target grid is already loaded → screen flag | replace |
| `AE#13388`, `AE#13415`, `AE#19118` | clear reference fading in loaded cells (`TES` / exterior buffer) | only for the new cell, if "appearing" is noticeable |
| `AE#13398`, `AE#53142` | one-time model preload; sky and map overlay | skip / once per world |
| `AE#13326` | sky and lighting mode by cell type (one sky, one world) | replace: by the camera's cell |
| `AE#13291` | animation of loaded cells (interior or grid + sky), every frame from `Job_Cell_animations` | skip |
| `AE#105631`, `AE#105654` | **global flag `BSShaderManager::State.interior`**; empty stub | replace: one flag is wrong for an interior and a street in one frame |
| `AE#106335` | `ShadowSceneNode`: draining light and object queues (light is checked against `portalGraph` `SSN+0x228`) | keep, once after attaching |
| `AE#106355`, `AE#106459` | `SSN`: update or remove all light; reset `lastRenderPassState` for all geometry of the subtree (not an `SSN` method) | replace (will remove light of other cells); the second — only for stale passes (T14) |
| `AE#31813`, `AE#31814` | show / **hide** LOD roots (global) | do not call `AE#31814`: it hides the distance for the inside-out view |
| `AE#77905`/`AE#77906`, `AE#77908`/`AE#77909` | `bhkWorld`: deferred work and batched body add | keep in pairs around the attach with collision |
| `AE#20514`, `AE#20548` | world: terrain manager, `MultiBoundNode` and `Portal-shared` nodes → `SSN` slots 8 and 9; height data | keep, once per world |
| `AE#20516`, `AE#20517`, `AE#20463` | **slots 8, 9 of the root `SSN` — one pair per active world** (verified); remove them; free world data | replace / do not call for the visible world |
| `AE#40745`, `AE#40445` | moving the player to a world (detaches the old cell: `AE#19032`, `AE#13298`, `AE#13295`); fast travel to a reference | replace (take only the cell lookup) / not for a door |

**Conclusion — the minimum for a seamless transition** (from code, not verified in game): no screen and its waits; do not detach the old cell (`AE#13295`, `AE#13298`, `AE#13317`, `AE#40745`, `AE#20463`, `AE#20517`, `AE#31814`, removing all light); for the new cell — `AE#20514`/`AE#20548` once per world, `bhkWorld` pairs around the attach, `AE#106335` after; the player's `SetParentCell` exactly once (via `AE#19411`). **Singletons that need replacement for many portals and gates:** `portalGraph` is one per `SSN` (`SetPortalGraph` `AE#106385`), slots 8/9 of the `SSN` for one world, the `interior` shader flag, the sky mode, LOD roots.

## T39, batch 3. IO, models, jobs (32 IDs)

Question: do `Job_IO` jobs (`PostProcess` → `AE#13434` → `AE#13721`) run concurrently with the main thread's `Update`.

**`Main::Update` frame (`AE#36564`, verified via `asm`):** player `+0x2A7` → inline IO step `+0x5AC…+0x5D3` (`bhkWorld` `AE#77905`/`AE#77908`, `AE#75705`; per the agent — only when the game is paused, the branch not verified) → grid shift `AE#13288` `+0x825` → `SetJobTaskEvent(1…4)` `+0x82F…+0x859` (`AE#69514`; decals `AE#15207` between them) → camera, world root → `WaitForJobTask(0,1)` `+0x8B3` (`AE#69516`, only if `AE#35406(1)`). `AE#36565` (`Job_IO`) repeats the inline IO step.

**Verdict — a hypothesis, not proven:** `Job_IO` is started by stage events and runs concurrently with the main thread at least between `+0x82F` and `+0x8B3`; which stage it stands in was not found in the code (`Job_IO` `AE#39084` exists only in the data table `0x142011bf0`, with no code references). T40 will answer: counting calls of `AE#13721`/`AE#13434` by threads and frame phases. Until then, our `AE#13721` detour must not rely on the main thread (`CLAUDE.md`, section 7).

| ID | What it does; thread |
|---|---|
| `AE#75651` | `BSTaskManagerThread::vf_0x1` — the IO thread loop: semaphore, task via `AE#75682` |
| `AE#75636` | `BSTaskManager::vf_0x13` — running a task: state 4 → `Run` → 5 (IO thread) |
| `AE#75709`, `AE#75643`, `AE#75644`, `AE#75646` | `IOManager`: processing tasks up to priority; pausing and resuming IO threads; task count |
| `AE#75686`, `AE#75676`, `AE#75696`, `AE#75693` | lock-free queues of `IOManager` (search, task selection, cleanup) |
| `AE#18549` | wait until all cell loading tasks are done (spins `AE#75708`, main thread) |
| `AE#18550` | loading a cell by (world, x, y): `LoadCell`, `AE#18978`, land vertices (probably the body of `CellLoaderTask::Run`, IO) |
| `AE#18546`, `AE#18547`, `AE#18543`–`AE#18545`, `AE#18554`, `AE#18559` | cell loader maps under a write lock; key `(x,y)` |
| `AE#13005`, `AE#13355`, `AE#13023`, `AE#13015` | model finalization: `AttachDistant3DTask` → `TES::AE_13355` (reference 3D), tree, `QueuedReference` (thread not traced) |
| `AE#441574`, `AE#441575`, `AE#19232` | iterating a cell's references under the lock `cell+0x120` |
| `AE#63973`, `AE#63974` | animation file manager: processing and draining the queue (main thread, `AE#40438+0x8A1/+0x8C3`, `AE#35567`) |
| `AE#15319`, `AE#15328` | `QueuedPromoteReferencesTask` → promoting references to permanent (IO) |
| `AE#36565`, `AE#39084` | the IO step with `bhkWorld`; `Job_IO` on the `BSJobs` worker threads |

## For the curator (name candidates from the batches)

Format `tools/ghidra/reviewed_names.txt`. Other candidates are in `results/T38.md` (11), `T39.md` (45), `T41.md` (22).

```
AE#50833  PlayerCamera::MoveCollisionBodyToCellWorld  ; only caller PlayerCharacter::SetParentCell+0x271; creates sphere body this+0x128, adds to bhkWorld of target parentCell (target+0x60) (medium)
AE#50785  PlayerCamera::GetFocusPosition  ; target by handle this+0x3C, position +0x54 + AE#37477 height, cache this+0x144, lock +0x134, 11 callers (medium)
AE#50830  PlayerCamera::CreateCollisionPhantom  ; recreates this+0x120 with radius param (medium)
AE#50831  PlayerCamera::DestroyCollisionPhantom  ; frees this+0x120, nulls it (medium)
AE#50738  FirstPersonState::UpdateHeadTwist  ; only caller FirstPersonState::Update+0x2B1, graph variable from player rotation X (medium)
AE#53021  MapCamera::GetFocusPosition  ; interior -> player exteriorPosition, exterior -> player location via GetWorldSpace (medium)
AE#32288  BGSLocationChangeEvent::ctor  ; only caller PlayerCharacter::SetParentCell+0x105, before AddEvent_Impl(AE#380069 CLOC) at +0x11C; record handle+old+new location (medium)
AE#41034  BSTEventSource<BGSActorCellEvent>::SendEvent  ; only callers SetParentCell+0x75 (leave, type 1) and +0xC1 (enter, type 0) (medium)
AE#20039  BSTEventSource<TESActorLocationChangeEvent>::SendEvent  ; SetParentCell+0x154, source ScriptEventSourceHolder+0x108, event (actor, old, new location) (medium)
AE#19001  TESObjectCELL::GetWaterHeight  ; float at cell+0x78 with worldspace/land fallback; caller PlayerParentCellListener AE#32154 (medium)
AE#25063  TESQuest::FillAlias  ; resolves aliases, scans TES interiorCell or gridCells; from TESQuest::AE_25048+0x1A8 (medium)
AE#23933  TESQuest::SetObjectiveState  ; callers Papyrus/ScriptCommand SetObjective* (medium)
AE#39250  AIProcess::Free  ; frees high/middleHigh/middleLow; callers Actor::Predestroy, PlayerCharacter::Resurrect, dtor_impl (medium)
AE#40656  PlayerCharacter::QueueJailTeleport  ; copies prison marker teleport into queuedTargetLoc +0x648 at +0x12C, sets +0x68A (medium)
AE#40661  PlayerCharacter::QueueJailReleaseTeleport  ; release marker into queuedTargetLoc at +0x107, clears prison faction +0x720 (medium)
AE#40732  PlayerCharacter::ClearQueuedTargetLoc  ; resets +0x648..+0x68A (medium)
AE#41051  BSTEventSource<PositionPlayerEvent>::SendEvent  ; source player+0x388, values 0/3/4 from AE#40438 (medium)
AE#13425  TES::AreGridCellsLoaded  ; checks grid around (x,y) of worldspace, returns bool; from AE#40438 (medium)
AE#40469  PlayerCharacter::ReturnToLastGoodLocation  ; mode 1 writes queuedTargetLoc at +0xB3, mode 0 moves directly; from AE#40562, PlayerCharacter::Update (medium)
AE#40562  PlayerCharacter::CheckBorderRegion  ; outside playable region -> HUD message + AE#40469(...,0) (medium)
AE#36549  Main::StartGameEnterCell  ; AE#40442 at +0x1B7 then AE#40438 (medium)
AE#35652  BGSLoadGameBuffer::LoadPlayerLocation  ; reads worldspace/cell/pos, calls AE#40744/AE#40745 (medium)
AE#36629  PlayerCharacter::CreateSingleton  ; alloc 0xBE8, ctor AE#40411, formID 0x14, handle AE#403520 (high)
AE#36601  Main::ResetGameState  ; unloads cells, AE#13754, recreates player via AE#36629 (medium)
AE#41262  PlayerRegionState::ctor  ; sets singleton, registers BGSActorCellEvent sink via AE#35181 (high)
AE#35181  BSTEventSource::AddEventSink  ; mutex, no duplicates, pending list (high)
AE#35180  BGSPlayerMusicChanger::GetSingleton  ; static init of AE#402995, 5 callers (high)
AE#13288  TES::UpdateGrid  ; grid shift from position, QueueCellLoad per row/col; Main::Update+0x825, AE#36581+0x201 with player position (medium)
AE#13363  TES::SetLoadScreen  ; begin/end loading screen, end path BGSTerrainManager::Update with player position; 13 callers (medium)
AE#13328  TES::MoveToCell  ; CenterOnCell_Impl at +0x96, AE#75708 at +0xA7 (medium)
AE#19063  TESObjectCELL::Attach  ; per-ref AE#19280, state +0x44 = 7 at +0x33C (medium)
AE#19411  TESObjectCELL::AddReference  ; ref lists, vfunc 0x98 SetParentCell on the ref at +0x251; 25 callers (medium)
AE#19165  TESObjectCELL::ReparentRef3D  ; moves ref 3D into the cell scene graph, BSPortal rooms (medium)
AE#32089  BGSWaterUpdateI::UpdateAll  ; only caller TES::AE_13291+0x247, player position (medium)
AE#75651  BSTaskManagerThread::ThreadProc  ; vf_0x1: semaphore +0x50, stop event +0x58, task via AE#75682 (high)
AE#75636  BSTaskManager::RunTask  ; vf_0x13: state 4, task Run, state 5, AE#75634 (high)
AE#36565  Main::IOStep  ; bhkWorld lock, AE#75705, AE#77909; same sequence inline at Main::Update+0x5AC; only caller Job_IO AE#39084 (high)
AE#75643  IOManager::PauseThreads  ; ref-counted, mutex, builds thread list (medium)
AE#75644  IOManager::ResumeThreads  ; drains via AE#75686, SetEvent + ReleaseSemaphore per thread (medium)
AE#18549  ExteriorCellLoader::WaitForAll  ; loops AE#75708 until pending == done (medium)
AE#40753  PlayerCharacter::ConstructAnimationGraph  ; own override in @0x38 (IAnimationGraphManagerHolder) slot 5, current class prefix wrong (high)
AE#40754  PlayerCharacter::PostCreateAnimationGraphManager  ; @0x38 slot 0xB override (high)
AE#40762  PlayerCharacter::EffectAdded  ; @0xA0 (MagicTarget) slot 8 override, HUD message (high)
AE#40735  PlayerCharacter::DoSetSitSleepState  ; @0xC0 (ActorState) slot 0x14 override (high)
AE#40755  PlayerCharacter::PopulateGraphNodesToTarget  ; @0x38 slot 4; shares vf_0x4 name with AE#40743 (medium)
AE#40470  PlayerCharacter::SaveLastKnownGoodPlace  ; writes +0x480 position and +0x990 worldspace/cell; Update+0xF36, FinishLoadGame+0x106, AE#40562+0x123 (high)
AE#40750  PlayerCharacter::UpdateMapMarkerDiscovery  ; Update+0xF7B, time-sliced currentMapMarkers, LocationDiscovery event (high)
AE#40556  PlayerCharacter::UpdateGrabbedObject  ; Update+0xC15, mouse spring, parentCell bhkWorld, calls DestroyMouseSprings AE#40557 (high)
AE#40604  PlayerCharacter::AddPlayerActionObject  ; inserts into runtime.playerActionObjects, 14 callers (medium)
AE#40605  PlayerCharacter::RemovePlayerActionObject  ; removes from playerActionObjects (medium)
AE#40449  PlayerCharacter::UpdateCameraAndAudioListener  ; SetJobTaskEvent(2), PlayerCamera::Update, audio listener (medium)
AE#19361  BSTEventSource<CellAttachDetachEvent>::SendEvent  ; source AE#400441+8; AE#18976+0x69/+0x329 (0,1), AE#19130+0x91/+0x77C (2,3) (high)
AE#13664  BSTEventSource<TESCellFullyLoadedEvent>::SendEvent  ; holder +0x210; AE#18895+0x55E, TES::AE_13381+0x345 (medium)
AE#19362  BSTEventSource<TESCellAttachDetachEvent>::SendEvent  ; holder +0x1B8; from AE#19187 / AE#19188 (medium)
AE#18976  TESObjectCELL::Attach  ; event 0, state 6, parent vfunc 0x1A8, per-ref AE#19187, event 1 (high)
AE#19130  TESObjectCELL::Detach  ; only from state 6/7, event 2, state 5 -> 4, objRoot DetachChild2, event 3; bool = collision teardown (high)
AE#18895  TESObjectCELL::Update  ; per-frame cell update; sends FullyLoaded at +0x55E; callers UpdateManagedNodesJob AE#18871+0x18E, TES::AE_13291+0x23A (medium)
AE#19059  TESObjectCELL::SetLightsActive  ; 1 GenDynamic all light refs, 0 ShadowSceneNode::RemoveLight; SetCurrentCell+0x76D, AE#13317 (medium)
AE#19055  TESObjectCELL::SetWaterDisabled  ; water lists +0xD8/+0xF0/+0x108 (medium)
AE#19032  TESObjectCELL::RemoveReference  ; first step of AE#19411 at +0x188 (medium)
AE#19145  TESObjectCELL::IsReferenceListLocked  ; cell+0x124 != 0 (high)
AE#19167  TESObjectCELL::GetPortalGraph  ; runtime[0], exterior -> worldspace +0x130 (medium)
AE#18979  TESObjectCELL::GetCell3D  ; runtime+8 root node, 39 callers (medium)
AE#15962  GridCellArray::AttachCell  ; cell at (x,y) -> AE#18976 (high)
AE#13427  TES::LoadAndAttachGridCell  ; LoadCell or create, put into grid, AttachCell, land, water (medium)
AE#13426  TES::AttachDetachAllGridCells  ; bool 0 attach state-4 cells, 1 detach state-7 cells; bhkWorld paused (medium)
AE#13437  TES::IsInExteriorBuffer  ; linear search exteriorBuffer +0xD0 (high)
AE#13433  TES::IsInInteriorBuffer  ; linear search interiorBuffer +0xC8 (high)
AE#13308  TES::IsCellAttached  ; state 6/7 -> 1, 3..5 -> !strict; 61 callers (medium)
AE#13710  TESDataHandler::CreateExteriorCell  ; alloc 0x148, coordinates, AE#20507 register (medium)
AE#13718  TESDataHandler::GetOrCreateExteriorCell  ; AE#13641 lookup, else AE#13710 (medium)
AE#19113  TESObjectCELL::UnloadCellData  ; from AE#13721+0x17F (medium)
AE#19056  TESObjectCELL::Unload3D  ; from AE#13721+0xF2, drops portal graph (medium)
AE#19112  TESObjectCELL::LoadCellData  ; reads cell forms from plugin, state 2; TESWorldSpace::LoadCell+0x1BB (medium)
AE#19784  TESObjectREFR::GetWorldLocationWithCell  ; xyz + cell key (interior cell or worldspace); DoDetectionJob+0x4E0/+0x4F0, AE#52638+0x122 (medium)
AE#18517  SameCellDistanceSquared  ; FLT_MAX if cell keys differ; DoDetectionJob+0x4FD/+0x520 (medium)
AE#39632  AIProcess::UpdateGreeting  ; dist to player <= AE#370892, RequestDetectionLevel, LOS; AE#39637+0x518/+0x742 (medium)
AE#41334  ProcessLists::FindWitnessForCrime  ; nearest actor to player passing faction test; callers TrespassAlarm, SendAssaultAlarm (medium)
AE#41403  ProcessLists::CompareByDistanceToPlayer  ; sort callback, two GetDistanceBetweenSquared (medium)
AE#37618  Actor::FindFreePositionAvoidingActors  ; obstacles = highActorHandles + player added separately, radius 256 (medium)
AE#40821  PlayerCharacter::ResetTravelTracker  ; stores player xyz into AE#404241..AE#404243 (medium)
AE#39814  AIProcess::FlushEquipQueue  ; Job_Non_render_safe_AI+0x1AE, ActorEquipManager (medium)
AE#106335  ShadowSceneNode::FlushQueues  ; under +0x1A8/+0x1F8 locks drains light/object queues, light owner vs portalGraph +0x228 (medium)
AE#106459  BSShaderProperty_InvalidateRenderPassStates  ; not an SSN method: walks any subtree (AE#106477, AE#106468), lastRenderPassState +0x34 = 0x7FFFFFFF (medium)
AE#105631  BSShaderManager::SetInterior  ; writes State.interior and derived AE#415263/AE#415264 (medium)
AE#13425  TES::IsTargetGridLoaded  ; target grid cells loaded and in gridCells; from AE#40438 (medium)
AE#13326  TES::UpdateSkyForCell  ; sky mode 1/2/3 and lighting override from cell type and worldspace lighting template (medium)
AE#20514  TESWorldSpace::EnsureTerrainAndMultiBoundNodes  ; terrain manager +0x130, nodes +0x148/+0x150 to SSN slots 8/9 (medium)
AE#20516  TESWorldSpace::AttachNodesToSSN  ; SSN[0] vfunc +0x1E0 slot 8 = +0x148, slot 9 = +0x150 (medium)
AE#20517  TESWorldSpace::DetachNodesFromSSN  ; DetachChild2 slots 8/9 (medium)
AE#20548  TESWorldSpace::LoadMaxHeightData  ; MHDT into +0x350, IOManager lock off IO thread (high)
AE#31813  BGSTerrainManager::ShowLODRoots  ; clears hidden bit on LOD object/land/tree roots, AttachWater(1) (medium)
AE#31814  BGSTerrainManager::HideLODRoots  ; sets hidden bit, AttachWater(0) (medium)
AE#41313  ProcessLists::ProcessInitPackageLocations  ; initPackageLocationsQueue -> AE#404443; Main::Update+0x7D6 (medium)
AE#40745  PlayerCharacter::MoveToExteriorPosition  ; finds/loads cell, detaches old (AE#19032, AE#13298, AE#13295), then AE#40744 (medium)
AE#40445  PlayerCharacter::FastTravelToRef  ; only caller AE#40438+0x5D9, then AE#40745 (medium)
AE#52852  LoadingMenu::PostMessage  ; UIMessageQueue::AddMessage(loadingMenu, 1 show / 0 update) (medium)
AE#19815  TESObjectREFR::IsInInterior  ; parentCell+0x40 bit 0, fallback vfunc +0x4B8 and worldspace +0x128 (high)
AE#20491  TESWorldSpace::GetParentWorldSpace  ; returns +0x158 (medium)
AE#13314  TES::GetWorldSpace  ; returns TES+0x148 (medium)
AE#53150  MapMenu::GetMapWorldSpace  ; TES::worldSpace or player cachedWorldSpace +0x630, walks to top parent (high)
AE#53111  MapMenu::UpdateMarkers  ; per-frame markers, YouAreHere hidden in interior (medium)
AE#53154  MapMenu::ShowMapTerrain  ; map open, terrain managers of the player worldspace chain (medium)
AE#53155  MapMenu::HideMapTerrain  ; map close, reverse of AE#53154 (medium)
AE#51728  Compass::AddMarker  ; bearing/distance to player, HUD+0x14BC count, limit 0x30 (medium)
AE#51744  Compass::UpdateEnemyMarkers  ; only caller HUDMenu::ProcessMessage+0x16A9; same worldspace as player (medium)
AE#50951  BarterMenu::TransactItem  ; buy/sell one item, speech XP, gold (medium)
AE#51543  FavoritesMenu::EquipHotkeyItem  ; equips favorite in hotkey slot (medium)
AE#13112  QueuedPlayer::ctor  ; stores player handle as task ref; from AE#13057 (high)
AE#13057  TESObjectREFR::Queue3DLoad  ; creates QueuedPlayer/QueuedTree/generic task, 21 callers (medium)
AE#28453  BGSProcedureEscort::Update  ; player in target handle set -> path flag 0x80; from vf_0xC AE#28449 (medium)
AE#40092  AIProcess::UpdateDialogueTarget  ; dialogue target AIProcess+0x114 vs player handle (medium)
AE#41261  PlayerCharacter::CountActorsInCombatWithPlayer  ; actor+0x104 currentCombatTarget vs player handle (medium)
AE#53077  IsValidNonPlayerHandle  ; handle == player -> false (medium)
AE#52714  UI::OpenTweenMenu  ; blocked during synchronized animation AE#32808 (medium)
AE#52719  UI::CanOpenTweenMenu  ; same checks as AE#52714 (medium)
AE#52971  MapMenu::BuildMarkerList  ; player marker entry type 3 (medium)
AE#53178  MapMenu::OpenAtRef  ; falls back to player handle (medium)
AE#37425  Actor::CommitAssaultCrime  ; crimeCounts[3], witnesses, RequestDetectionLevel(witness, player) (medium)
AE#37426  Actor::CommitMurderCrime  ; crimeCounts[4], from KillImmediate AE#37735 (medium)
AE#17924  TESObjectDOOR::TeleportPlayerAfterFade  ; AE#40442 at +0x1B1 with post callback AE#17968/AE#17927; from HandleOpen AE#17926, NormalDoorFadeCallback (medium)
AE#17927  TESObjectDOOR::OnPlayerTeleported  ; post-teleport fix of player position, terrain/cell, sound (medium)
AE#14864  TESFlora::HarvestImpl  ; from TESFlora::Activate AE#17369, TESObjectTREE::Activate AE#18042 (medium)
AE#34452  MagicCaster::LaunchSpellProjectile  ; launch cell = caster parentCell (medium)
AE#37356  Actor::ApplyAnimationMotion  ; any actor, from AE#37350+0xFB; current PlayerCharacter:: prefix is wrong (medium)
AE#78274  bhkCharRigidBodyController::SetDistanceToCamera  ; Actor::Update+0x1F3 writes max(0, dist(camera, bound) - radius) to +0x29C (medium)
AE#40814  PlayerCharacter::MountAttackTarget  ; player implicit, only caller AE#40719; current TESObjectREFR:: prefix wrong (low)
AE#38586  Actor::ApplyHit  ; target Actor + HitData; sneak/critical, stagger, kill cam (medium)
AE#35772  BGSSaveLoadManager::ProcessRequests  ; request queue, load 0xD0000010 -> AE#442580; Main::Update+0x9A, AE#13363+0x3FA (medium)
AE#442580  BGSSaveLoadManager::LoadGame  ; header, missing content checks, AE#35600/AE#35765/AE#35780 (medium)
AE#35600  BGSSaveLoadGame::LoadGame_Impl  ; "***Loading game", global data tables, change forms, AE#35567 (medium)
AE#35641  Win32FileType::LoadGlobalData  ; reads type ids, calls AE#35642 (medium)
AE#35642  LoadGlobalDataType  ; case 1 AE#35652, case 2 TES::AE_13380 (medium)
AE#35567  BGSSaveLoadGame::PostLoadInitForms  ; form vfuncs 0x80/0x88, TES::AE_13387, IO waits (medium)
AE#35779  BGSSaveLoadManager::PreLoadGameSetup  ; VM freeze, CancelMasterFileLoads, KillAll audio, loading screen (medium)
AE#35780  BGSSaveLoadManager::PostLoadGameSetup  ; ResumeMasterFileLoads, AE#15960, BGSTerrainManager::Update, hide loading screen (medium)
AE#13295  TES::DetachAndUnloadCells  ; gridCells vfunc 1 DetachAll at +0x61, buffers, (0,0) nulls interiorCell (medium)
AE#13280  TES::Destroy  ; body of TES::~TES, calls AE#13754 at +0x4CF (high)
AE#36544  Main::Run  ; init, start, loop with Main::Update, reset via AE#36601 (medium)
AE#13327  TES::RunCellTest  ; ConsoleCommand::RunCellTest+0x59 (high)
AE#55427  LatentPlayerMover::Start  ; mode 0 CenterOnCell_Impl, mode 1 AE#40442 (medium)
```
