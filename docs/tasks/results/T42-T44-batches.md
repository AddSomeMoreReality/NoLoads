# Batch reading from the T42–T44 lists

Batches from the T42, T43, and T44 results (`results/T42.md`, `T43.md`, `T44.md`). The batches were read by `re-reader` agents (Sonnet, medium); the coordinator spot-checked 2–3 claims of each batch against the database (marked "verified"). No pseudocode: conclusions, `AE#id`, and offsets from `asm` only.

## T42, batch A. Who reads `TES::interiorCell` (`TES+0xC0`), 31 IDs

Question: which decision depends on `interiorCell`, and will a function err for an interior that is attached but not current (a portal from the street).

False search matches: `AE#13450`–`AE#13453` `+0x72`/`+0x1ED` and `AE#52971+0x860` — these are `[rsp+0xC0]`/`[rbp+0xC0]`, not `TES`.

**Conclusion.** All decisions are binary: "player is in an interior" or "in an exterior". While the player is outside, `interiorCell == 0`, everything takes the exterior branch, and none of the 31 functions sees the attached interior: neither cell iterations, nor the map, nor water audio. For the reverse direction (player inside, the street visible through the door), `interiorCell` disables sky, water, and sunlight: for the view from inside out it will have to be substituted for the duration of the pass (5.14, item 2; T21, T44).

| Group | IDs | Reading and decision | For the portal |
|---|---|---|---|
| Sky | `AE#26237` (verified) | `+0xB6`: interior set → sky mode `Sky+0x1BC = 3` (disabled) | inside-out: sky disabled |
| Water | `AE#36560` (verified; only call — `Main::Draw+0x172`, `AE#36559`) | `+0x55`: interior without water flag → water update is skipped | inside-out: street water disabled |
| Sunlight | `AE#32189` (chain `Main::Draw` → `AE#36560` → `AE#32174`, per agent), `AE#39946` (from `PlayerCharacter::Update+0x548`, `AE#41381`; and from AI jobs via `AE#40522`) | `+0x98` / `+0x1E9`: no interior and world with flag `0x40` → sunlight and shadow logic | actors of the attached interior get the exterior branch (sun) — **assumption**, by structure |
| Mass visibility | `AE#13365` (verified), `AE#13366` | `+0xFF` / `+0x88`: flag for all grid cells, the interior (`AE#13673`/`AE#13674`), and the sky cell | the attached interior is not included in the toggle |
| References and unloading | `AE#19456` (in `TESObjectCELL::AE_19442`), `AE#19826` (reference removal, `ShadowSceneNode::RemoveLight`) | `+0xB4` / `+0x47E`: without interior — exterior branch (reference's world) | references of the attached interior take the exterior branch; protection — reference's world `null` |
| Reference creation | `AE#13722` (wrapper `CreateReferenceAtLocation`) | `+0x1F8` land height, `+0x23D` parent cell = `interiorCell` | created only in the current world |
| Water audio | `AE#32057` (tail of `Job_Water_audio`, task) | `+0x95`: work skipped in interior | the attached interior has no water audio |
| Weather | `AE#26261` | `+0xB`: interior → cell region, else `lastKnownWeatherRegion` | correct for the player on the street |
| Cell iteration with script | `AE#442673` (`Main::Update+0xA8F`) | `+0x1EA`: interior or `gridCells` | the attached interior is not iterated; what the iteration does — not determined |
| Papyrus `FindRandomReference*` | `AE#13450`–`AE#13453` (clones) | `+0x53`: interior → only it, else grid + sky | "around the player" — acceptable |
| Dispatchers, loading | `AE#13313`, `AE#13699`, `AE#13376` (flag `bhkWorld +0xC5F9`) | interior / exterior branch | interior not visible; thread not found (no direct calls) |
| Map | `AE#16335`, `AE#16344`, `AE#19144`, `AE#52971`, `AE#53144`, `AE#53160` | interior or world grid | map only |
| Debug and console | `AE#13340` (`SaveWorld`), `AE#13785` (`HotLoadPlugin`), `AE#15577`, `AE#15596`, `AE#15598`, `AE#15643` | — | no effect |

Not verified: threads of functions without direct calls (`AE#13313`, `AE#13365`, `AE#13366`, `AE#13376`, `AE#13699`, `AE#13722`, `AE#26237`, `AE#26261`) — they are called through tables.

## T42, batch B. Who asks for the current world `TES::AE_13314` (`TES+0x148`), 32 IDs

Question: will a function break for a cell of another world (gates: a cell of world 2 is loaded, `TES::worldSpace` is world 1).

**Conclusion.** There is a single cause of breakage: a cell, reference, or terrain LOD block is looked up by `(x,y)` in the player's world cell map (`ws+0x80`), while table keys are `(x,y)` without the world. Fixed by substituting the world of the cell itself (`cell+0x128`) or of the reference (`GetWorldSpace`): most functions have it at hand. The `TES` grid (`currentGridX/Y`) is the player's grid; the second world needs its own grid and its own iteration (`AE#18234`, `AE#40436`).

| Verdict | IDs | What it does and why it needs the world |
|---|---|---|
| Breaks | `AE#18236` (verified: `+0x99`, then flag `ws+0xA0 & 0x40`), `AE#18237`, `AE#18223` | persistent cell references: table `ws+0x250` by `(x,y)`, move into the player's world sky cell; in `AE#18223` (from `AE#18976`) mobile references are matched to a cell by position in the player's world |
| Breaks | `AE#19826` (verified: `+0x47E` `interiorCell`, `+0x48C` world) | applying deferred reference movement: cell by position `AE#13717(pos, ws)` and `MoveRefToNewSpace(ref, 0, ws)` — a reference from the gate world will end up in a cell of the player's world |
| Breaks | `AE#32057` (verified: `+0x79`; task `Job_Water_audio`) | water audio: cell by listener position in the player's world |
| Breaks | `AE#31846`, `AE#31847` | visibility of terrain LOD sub-blocks by the player's world loaded cells |
| Breaks | `AE#13699`, `AE#13722`, `AE#40436`, `AE#19144` | reference iteration in a radius (`AE#20554`), reference creation, adjacent cell at the boundary (via the `TES` grid), map fog |
| Depends | `AE#13724`, `AE#13726` (world as parameter, breaks only when called with `null`); `AE#31837`, `AE#31862` (fallback world — block owner); `AE#32189` (own world field); `AE#18234`, `AE#39946`, `AE#36575`, `AE#31816`, `AE#19456` | world only as a condition, or it has its own |
| Harmless | `AE#35639`, `AE#35651` (saving writes both `TES::worldSpace` and the player's world), `AE#53093`, `AE#53144`, `AE#53150`, `AE#16335` (map), `AE#15596`, `AE#15598`, `AE#23228` (debug), `AE#43834`, `AE#43869` (explosions into the single LOD root) | — |

World helpers (by `asm`, bodies up to 0x36 bytes): `AE#20469` — world LOD data with parent ascent, `AE#20470` — flag `ws+0xA0` bit 0, `AE#20490` — parent when "use LOD data" flag, `AE#20491` — parent `ws+0x158`.

## T43, batch 1. Path building through doors, 34 IDs

**Main point — a correction to T43** (verified: `AE#91750` is called only by `BSPathBuilder::AE_91717+0x148` and `+0x23E`; the `TeleportDoorSearch` search `AE#30932` is called only by `Pathing::AE_30656`–`AE_30659`): an NPC path is built by `BSPathBuilder::Build` (`AE#91713`) → `AE#91717` (navmesh search, one node per door `AE#91750` → `AE#91674`). `TeleportDoorSearch` is a coarse route through doors for estimating length and the first door (player `AE#40517`, map, console, patrol `AE#28930`, quests). Corrected in `NPC_DOORS.md` and `results/T43.md`.

- **What a path node knows about a door:** `PathingDoor` (`AE#30809`): FormID `+0x10`, cached handle `+0x14`, lazy locations `+0x18` (own side, `AE#30820`) and `+0x48` (linked door on its navmesh, `AE#30821`). The `ExtraTeleport` marker is not used in the path.
- **Seamless relocation** to another point of the target path cell does not break it (**assumption**): nodes past the door are absolute navmesh locations from the linked door, not from the actor's position. Agent behavior after the jump — batch 2.
- **Both sides must be loaded:** without the linked door by handle the path is marked failed (flag `+0xA8`, string "Found an invalid Teleport Door location"). This suits a held interior.
- Door state in the path — `AE#30816`–`AE#30819` (`GetOpenState` 1–4); the auto-door penalty `AE#30824` (`TESObjectDOOR::AE_17918`).
- Not found: the `Build` thread (the chain breaks at a vtable slot: `AE#90401+0x1F`, `AE#31483+0xA1`, `AE#91699+0xBF`).

## T43, batch 2. The path walker high at a door and after a jump

The result was moved to `docs/NPC_DOORS.md`, section 9. Verified: `AE#90769` is called only by `AE#90711+0x544` (approach, `ApproachingDoor`), `AE#90770` — by `AE#90711+0x613` and `+0x7C7` (node message, `ActivateDoor`, end of path).

- The walker state machine sends door messages by the path parameter, not by distance; delivery is the controller queue `AE#41762` → drain `AE#41717` (main thread) → `AE#37892`.
- After a jump the walker does not re-plan: projection only forward (`AE#91002`, threshold 10 units). A repeated `ActivateDoor` after a seamless relocation is possible (**assumption**) — suppress it for a portal door.
- The "off path" code — `AE#90714` (3), `AE#90737` (2); `LoadedAreaMonitor` `AE#92466` — 3 if the segment is outside the loaded area. No consumer of the codes was found.

## T43, batch 3. Side effects of moving an NPC, 24 IDs

Question: which side effects of a move are game state that must happen exactly once, and what should a seamless pass call besides `AE#37313`.

**A vanilla door does not call `AE#37313`.** The "activator is not the player" branch in `TESObjectDOOR::Activate`: destination point `AE#37438` (`+0x5AD`) → `SetPosition` and rotation (or `AE#11817` if the cell is not loaded) → `AE#37716` (`+0x620`, "NPC is following the player"; if yes — `StopMovement`) → `MoveRefToNewSpace` (`+0x63A`). `AE#37313` is the Papyrus path `MoveTo` (`MoveTo_Impl` `AE#56626`: `StopInteractingQuick`, `AE#37313`, then `AE#40512` at `+0x2B4`).

**Double `OnLocationChange`** (verified: `AE#37313+0x16C…+0x1D7`): after `MoveRefToNewSpace` (`+0x167`), `AE#37313` sends the event itself if the world before the move is non-null (`+0x16C`), the world after equals the world before (`+0x17D`/`+0x180`), and the location changed (`+0x18E`/`+0x191`). `MoveRefToNewSpace+0x8A2` sends it for an actor on a location change. So the event arrives twice on a move **within one exterior world** with a location change (street ↔ street, gates in the same world). Exterior ↔ interior, interior ↔ interior, different worlds — once (an interior has no world).

| What | IDs | Conclusion |
|---|---|---|
| All move state | `MoveRefToNewSpace` (`AE#19799`) | does itself: `OnLocationChange`, `OnAttached/DetachedFromCell` (`AE#20043`, at `cellState == 7`), starting cell in `ExtraData 0x48` (`AE#11992`, idempotent), world persistent references (`AE#20526`/`AE#20527`, only for persistent references on world change), Havok (queues `AE#36932` `MoveHavok`, `AE#36921` `InitHavok`, `AE#36933` `DetachHavok`), 3D attachment (`AE#19411` → `AE#19413` → `ProcessLists` `AE#41343`) |
| Quest markers | `AE#40512` (for NPC → `AE#40848`, recompute of quest targets with this NPC; for player → `AE#40517`) | not mandatory, idempotent; vanilla calls it after `ActivateRef` (`+0x702`) and `MoveTo`. Desirable once after the move |
| Activation event | `AE#20037` (source `AE#20053`, from `ActivateRef+0x778`) | not needed without `Activate`; no subscribers found |
| Do not call | `AE#20541`/`AE#20542` (sky cell), `AE#35570` (unreachable for an actor), `AE#37864` (mounts) | — |
| Position queue | `AE#36949` (from `AE#37313+0x27E`) | queue choice by `ShouldUseTaskQueue` (`AE#39033`); not verified on the main thread |

**Conclusion for a seamless NPC pass.** Better to repeat the vanilla door path (position and rotation through the portal → `StopMovement` if needed → `MoveRefToNewSpace`) than `AE#37313`: exactly as many effects as vanilla, and no double `OnLocationChange` within one world. `AE#37313` adds `StopInteractingQuick`, a wait, `StopMovement`/`EndInterruptPackage`. After the move — `AE#40512`, optionally.

## T44, batch 3. Who reads the global view-state globals

Question: which globals the portal pass must set before itself and restore after — "view", "frame", or "world".

| Global | What it is | Readers | Class | For the portal pass |
|---|---|---|---|---|
| `AE#414580` | pointer to the `NiCamera` of the current submission (verified: `SubmitAccumulator+0x49`) | 19 sites, all view: distance to camera `AE#105692`, SSAO (`AE#107245`, `AE#107256`), near/far ImageSpace (`AE#107276`–`AE#107278`, `AE#107285`, `AE#107287`), `BSEffectShader` (`AE#107524+0x1FD`, soft particles), water (`AE#107362+0x2C6`), DoF (`AE#107388`, `AE#107391`), volumetric light (`AE#107379+0xD2`), `BSUtilityShader` (shadows, `AE#107759`, `AE#107762`), tail of `RenderWorld` (`AE#107148+0x1B5`) | view | set and **restore**: it is also written by `StartAccumulating` (`AE#106437`), `ShadowSceneNode::OnVisible` (`AE#106322`), `AE#106323`, `AE#105623`, `AE#105656`, `AE#107140`, `AE#107148` — it changes within a frame |
| `AE#415299` | copy of the world camera | `AE#107753+0x7C` (projecting a point onto the screen) | view | substitute if the pass calls `AE#107753` (**not verified**) |
| `AE#411319`–`AE#411321` | snapshot of `AE#388909`–`AE#388911` (verified: `Main::Draw+0x800…+0x828`) — by the writer `AE#77504+0x18` next to `State::SetCameraData` this is probably the graphics state camera position offset (**assumption**) | only `BSMasterParticleSystem::AE_76893+0x44` (verified) — particle sorting by distance | view, snapshot once per frame | set to the portal camera position: otherwise interior particles are sorted from the player's camera (a candidate for T17) |
| `AE#414568` | probably camera Z, written by `AE#36561+0x30E` once per frame | `BSTreeNode::OnVisible+0x23` (verified) — hide a tree by height, with flag `AE#390930` and view flag `+0x121` | view | for a street portal (inside-out) — set; no trees in an interior |
| `AE#415422` | flag, written by `RenderWorld` | `FullScreenBlur` | frame | do not touch |
| `AE#414566`, `AE#414567` | probably camera X/Y | no readers | — | do not touch |

**Conclusion.** The mandatory minimum for a "camera as parameter" pass: `AE#414580` set and restore (19 readers via ImageSpace, SSAO, soft particles, water, shadows). `AE#411319`–`AE#411321` and `AE#414568` are per-frame snapshots of the player camera position: for particles and trees behind the portal they must be set too. Readers through pointer copies in registers and parameters were not searched.

## T44, batch 2. Particles for T17: bounds, simulation, LOD

Question: why does smoke disappear in the portal picture when the hearth is not visible, and why is the fire smaller.

- **Bounds and simulation do not depend on visibility** (by code): `worldBound` is computed by `BSGeometry::UpdateWorldBound` (`AE#70925`, slot `0x2F` not overridden) in the scene update pass; `modelBound` — by `Do_UpdateSystem` (`AE#74498`) and `NiPSysBoundUpdateModifier` (`AE#74861`). Simulation is driven by `NiPSysUpdateCtlr::Update` (`AE#74716`) by time, the manager — `UpdateParticleSystemManagerJob` (`AE#19206`) → `AE#76601`. `OnVisible` (`AE#70926`) only hands out geometry. So "smoke is gone" is not a stale bound from the player camera.
- **Particle LOD** — already known (FINDINGS, T13; `HooksPortal.cpp`, distance carry through the door): `BSPSysLODModifier::Update` (`AE#108480+0x23…+0x31`, verified) takes the distance `+0x144` of the fade node if bit 15 in `+0xF4` is not set. `+0x144` is written only by `AE#105506`; it is called by `BSFadeNode::OnVisible+0xDB`, `AE#105486+0x37`, `BSLeafAnimNode::OnVisible+0x79` (verified) — the distance from the camera whose pass last called `OnVisible`.
- **Candidates for the smoke disappearing** (**assumptions**, to test with toggles in T17):
  - portal frustum culling by the system bound (T44, `RENDER_VIEW_MAP.md`, 7);
  - system reset if time goes backwards (`AE#74498`: particles and `modelBound` are zeroed);
  - particle sorting from the player camera position (`AE#411319`–`AE#411321`, batch 3).
- What to log in T17: `lastUpdate`, the `modelBound`/`worldBound` radii of the smoke in the portal pass, the fade node's `+0x144` before and after the pass.

## For the curator (name candidates from the batches)

```
AE#13340  TES::SaveHavokWorldToFile  ; strcat ".hkx", caller ConsoleCommand::SaveWorld (medium)
AE#13450  TES::FindRandomRefInRadius  ; caller TES::AE_13423 <- Papyrus FindRandomReferenceOfAnyTypeInList; AE#13451..13453 are clones per callback type (medium)
AE#13722  TESDataHandler::PlaceAtLocation  ; wraps CreateReferenceAtLocation, land height, interiorCell as parent (low)
AE#13785  TES::ReloadPluginCells  ; caller ConsoleCommand::HotLoadPlugin (low)
AE#15643  TES::OutputCellTextureUse  ; caller ConsoleCommand::OutputTextureUseMap (medium)
AE#26237  Sky::UpdateInteriorMode  ; +0xB6 interiorCell -> Sky+0x1BC = 3 (medium)
AE#26261  TES::GetCurrentWeatherRegion  ; interior ? cell region : lastKnownWeatherRegion (medium)
AE#32057  WaterAudio::Update  ; tail target of Job_Water_audio, skipped in interior +0x95 (medium)
AE#36560  Main::UpdateWater  ; only caller Main::Draw+0x172, TESWaterSystem, interior check +0x55 (medium)
AE#53150  MapMenu::GetMapWorldSpace  ; TES::worldSpace, else player cachedWorldSpace, up the parent chain; 6 callers in MapMenu (medium)
AE#16335  LocalMapRenderer::Render  ; caller AE#52967 <- MapMenu vf_0x7, clears target, BSTreeManager (medium)
AE#35651  BGSSaveLoadGame::SavePlayerLocation  ; writes TES worldSpace, grid, player worldspace, position (medium)
AE#13726  TESDataHandler::AddReferenceToCellAt  ; cell by (x,y), TESObjectCELL::AE_19411 (medium)
AE#18236  TESWorldSpace::LoadPersistentCellRefs  ; table ws+0x250 by (x,y), PromoteReference into sky cell (medium)
AE#20469  TESWorldSpace::GetLODData  ; ws+0x90, up to parent by flag (medium)
AE#20491  TESWorldSpace::GetParentWorldSpace  ; ws+0x158 (medium)
AE#91717  BSPathBuilder::BuildSegments  ; navmesh search, door node per door via AE#91750 at +0x148/+0x23E (medium)
AE#91674  PathNode::InitDoorNode  ; position from PathingDoor forward location, message +0x30 (medium)
AE#30932  TeleportDoorSearch::Run  ; only callers Pathing::AE_30656..30659 (high)
AE#30933  TeleportDoorSearch::ExpandNode  ; vtable slot 1, doors of cell/world via AE#19071/AE#20535 (high)
AE#30809  PathingDoor::PathingDoor  ; ctor by FormID, handle cache +0x14, locations not computed (high)
AE#30820  PathingDoor::GetForwardLocation  ; door position, parentCell, world -> Pathing::AE_30640 (high)
AE#30821  PathingDoor::GetBackwardLocation  ; linked door via GetTeleportData -> its position (high)
AE#30825  PathingDoor::CreateLinkedPathingDoor  ; (high)
AE#30827  PathingDoor::GetDebugName  ; "<Unknown Door>" or name(formID) (high)
AE#20535  TESWorldSpace::CollectTeleportDoors  ; persistent refs +0x88 under lock, filter AE#19279 (high)
AE#19071  TESObjectCELL::CollectTeleportDoors  ; refs +0xB0 under lock +0x120 (high)
AE#30657  Pathing::GetDoorRouteLength  ; FLT_MAX on failure; callers AE#40518, Actor::AE_40445, BGSProcedurePatrol::AE_28930 (medium)
AE#30658  Pathing::FindDoorRoute  ; navmesh AE#90441, else TeleportDoorSearch (medium)
AE#414580  gCurrentSubmitCamera  ; NiCamera*, written by SubmitAccumulator+0x49 / StartAccumulating, 19 readers (high)
AE#415299  gWorldCameraCopy  ; written AE#36558+0x66, RenderWorld+0x74; read AE#107753+0x7C (medium)
AE#411319  gParticleSortCameraPos  ; x,y,z at AE#411319..411321, copied from AE#388909..388911 at Main::Draw+0x800; read BSMasterParticleSystem::AE_76893+0x44 (medium)
AE#105692  Renderer::DistanceToCurrentCamera  ; distance from point to AE#414580 position +0xA0 (medium)
AE#90711  MovementAgentPathFollowerStandard::UpdateStateMachine  ; planner slot 2, state +0x100, AE#90769 at +0x544, AE#90770 at +0x613/+0x7C7 (high)
AE#90714  MovementAgentPathFollowerStandard::CheckPathDeviation  ; code 3 beyond fMaxDistFromPathRadiusMult x path radius (high)
AE#90769  MovementAgentPathFollowerStandard::SendApproachMessage  ; action object +0x10 -> ApproachingDoor (medium)
AE#90770  MovementAgentPathFollowerStandard::SendNodeMessage  ; node +0x30 -> AE#41762 (high)
AE#90778  MovementAgentPathFollowerStandard::FindNextMessageNode  ; next node with +0x30 != 0 (high)
AE#91002  Path::ProjectPositionForward  ; forward scan from progress, 10-unit threshold (high)
AE#91964  MovementMessageActivateDoor::SendApproachingDoor  ; creates ApproachingDoor if distance > 0 (high)
AE#30824  PathingDoor::GetApproachDistance  ; AE#375551 if TESObjectDOOR::AE_17918 (medium)
AE#41717  MovementControllerNPC::DrainMessageQueues  ; AE#37892 per message, stops on process level change (high)
AE#41761  MovementControllerNPC::QueueMessageAt  ; IMovementMessageInterface slot 1 (high)
AE#41762  MovementControllerNPC::QueueMessage  ; IMovementMessageInterface slot 2 (high)
AE#41763  MovementControllerNPC::SendMessageNow  ; IMovementMessageInterface slot 3 (high)
AE#92466  MovementPathManagerAgentLoadedAreaMonitor::CheckPath  ; code 3 if path segment outside loaded area (medium)
AE#37716  Character::IsFollowingPlayerForDoor  ; package +0x24==2 with target == player, else process byte +0x13B; caller TESObjectDOOR::Activate+0x620 (medium)
AE#40512  PlayerCharacter::UpdateQuestTargetPaths  ; player -> AE#40517, NPC -> AE#40848 over TESQuestTarget; callers ActivateRef+0x702, MoveTo_Impl+0x2B4, Enable (medium)
AE#40517  PlayerCharacter::ComputeQuestPathToRef  ; Pathing::AE_30658 (medium)
AE#20526  TESWorldSpace::AddPersistentRef  ; creates ws+0x88 cell, AE#19411 (high)
AE#20527  TESWorldSpace::RemovePersistentRef  ; ws+0x88 -> TESObjectCELL::AE_19032; decompiler shows foreign code, function bounds to check (high)
AE#20528  TESWorldSpace::GetPersistentCell  ; returns ws+0x88 (high)
AE#11992  ExtraDataList::SetStartingWorldOrCell  ; ExtraData 0x48, guarded by AE#11993 (medium)
AE#11993  ExtraDataList::GetStartingWorldOrCell  ; ExtraData 0x48 +0x10 (medium)
AE#36932  TaskQueueInterface::QueueMoveHavok  ; task 0x31 (high)
AE#36921  TaskQueueInterface::QueueInitHavok  ; task 0x36 (high)
AE#36933  TaskQueueInterface::QueueDetachHavok  ; task 0x32 (high)
AE#36949  TaskQueueInterface::QueueActorSetPosition  ; task 0x20, position + heading, from AE#37313+0x27E (medium)
AE#35570  BGSSaveLoadGame::SaveRefOnUnload  ; "FORMS: Form ... is unloading" (medium)
AE#105506  BSFadeNode::UpdateLODDistance  ; writes +0x144 distance to camera param, callers BSFadeNode::OnVisible+0xDB, AE#105486+0x37, BSLeafAnimNode::OnVisible+0x79 (high)
AE#108480  BSPSysLODModifier::Update  ; slot 0x25 (NiPSysModifier::Update), reads fade node +0x144, writes system +0x180..+0x188 (high)
AE#74861  NiPSysBoundUpdateModifier::Update  ; slot 0x25, particle bound, mesh/strip branches AE#74864/AE#74865 (high)
AE#76601  BSParticleSystemManager::UpdateMasterSystem  ; by index +0x118, vfunc +0x160; caller UpdateParticleSystemManagerJob AE#19206 (high)
```
