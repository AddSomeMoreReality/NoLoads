# Engine state held "in a single instance" (T42)

SkyrimSE.exe 1.6.1170, Ghidra base (T33–T36), 2026-11-08. Continuation of `docs/CELL_LOADING_MAP.md`, section 11; player location (`currentLocation`, `cachedWorldSpace`, region, music, water) — `docs/PLAYER_MAP.md`, section 10, player pointer reads — T32. Rendering here covers only global states and their writers; what the draw pass reads is T44 (`RENDER_VIEW_MAP.md`).

Markings: **by code** — verified via `asm`/`xrefs`; **assumption** — a conclusion not verified in code or in the game. Threads: M — main, J — `BSJobs` jobs, IO — `BSTaskManagerThread`, R — render (T44).

Abbreviations for "what breaks the second instance": **I2** — a second interior (attached, not current), **G2** — a second grid of the same world, **W2** — the grid of another world (gates). Solution classes: **(a)** temporarily substituting the far side for the duration of a frame, **(b)** a per-portal copy of our own, **(c)** intercepting the readers, **(d)** an engine limitation: a different path is required.

## 1. In short

1. **The exterior Havok world is one per game, not per world.** `TESObjectCELL::GetbhkWorld` (`AE#18995`) returns `*AE#400440` for any exterior cell; it is created by `AE#19193` (from the `GridCellArray` constructor `AE#15952`), freed by `AE#19194`; there are no other writers of `AE#400440` **by code** (`xrefs`: 2 writes, 47 reads in 23 functions). Cells of two worlds (gates) will end up in a single `hkpWorld`. An interior holds its own world (`ExtraDataList::GetHavokWorld`) — there is no singleton here for many interiors.
2. **"Current location" is read not only by loaders.** `TES::interiorCell` (`+0xC0`) is read by 92 functions; among them the visibility check `TES::Pick` (`AE#13371`: interior or the center grid cell → `AE#18997`, 11 callers: `Actor::CalculateLOS`, `RequestLOS`, player LOS), `TES::GetCell(pos)` (`AE#13322`, 8), "position in the loaded area" `AE#13306` (5), the navmesh list `Pathing::GetAllLoadedNavmeshes1..5` (`AE#30633`…`AE#30629`, `Pathing` vtable slots 2–6: interior **or** `exteriorBuffer`) **by code**. For them, an attached but non-current interior does not exist.
3. **Globally substituting `TES` fields (class (a)) is only safe on the main thread or inside a render frame.** Readers of "current location" are also called from jobs (`Pick` via `AE#42744` ← `AE#39946` ← `ProcessActorAnimSG` `AE#41446`; cell animation jobs `Job_Cell_animations`/`Job_TES_animation`) **by code**; concurrency with the main thread is an **assumption** (T39, batch 3; T40 will verify). For AI, per-actor cell interception (c) is needed, not substitution.
4. **The renderer holds a single "location":** the flag `BSShaderManager::State.interior` (`+0x44`; written only by `AE#105631`, read by only 3 functions: `AE#107229`, `AE#106516`, `AE#106945`), the shader grid center `+0xA8…+0xBC`, `SSN` root slots 8/9, `portalGraph`, LOD roots, sky mode. All of them are set on transition and are suitable for substitution (a) for the duration of the portal pass.
5. **Gates are (d) for the current `TES` architecture:** one grid, one `worldSpace` (47 functions + 47 via `AE#13314`), one Havok world, one sky cell (updated only for the current world, `Main::Update+0x799`), the `ExteriorCellLoader` key `(x,y)` without a world. A second world can only be held outside `TES` — with a list of cells of our own, the way `InteriorSim` holds an interior.

## 2. How it was obtained

- Writers in `AE#13316`, `AE#13315`, `AE#13317`, `AE#13288`, `AE#40466` — via `asm`: all writes are to `[0x14…]` and `[reg+off]`.
- `TES` fields: 392 functions reading `TES::singleton` (`AE#403450`, 690 sites), and 103 methods of `TES::` (`this`); the register loaded from the singleton or the method's `RCX` was traced by `asm` (script in the scratchpad). The method is imprecise on branches and does not see a `TES*` passed as an argument; therefore helpers of `TES` are listed separately (section 4). The numbers below are a lower bound.
- Singletons `Sky` (58 functions), `TESWaterSystem`, `ExteriorCellLoader`, `BGSGrassManager` — by the same script; globals — via `xrefs`.
- Pseudocode analysis — for the helpers and the writers from the table; 30 unnamed readers of `interiorCell` and 32 callers of `AE#13314` were not read (batches in `results/T42.md`).

## 3. State table

### 3.1. Location and world (`TES`)

| State | Writers (by code) | Readers | What breaks the second instance | Class, scope |
|---|---|---|---|---|
| `interiorCell` `+0xC0` | `SetCurrentCell+0x466`, `AE#13317+0xC1`, `AE#13295+0xD6` (`DetachAll`), `AE#13280+0x323`, `AE#13753+0x90` (world reset), `AE#35652+0x1CF` (loading) — M | 92 functions: methods of `TES` (22), sky and sun (11: `Sky::UpdateWeather`, `Sun::Update`, `AE#26232`…), render (`DrawWorld_BuildSceneLists` `AE#36643`, `BSCullingProcess::AE_16336`, light `AE#17610`/`AE#17626`, `SceneGraph::GetFarDistance`), AI/navigation (`Pathing` ×5), physics (`Actor::Move+0xC97`, `bhkRigidBody::AE_32082`), quests (`TESQuest::AE_25063`), map (`AE#52971`, `AE#53144`, `AE#53160`), water (`AddWater`, `AE#32057`) | I2: for the 92 readers an attached interior is the "street"; G2/W2 — not applicable | (c) for AI and physics (via helpers, section 4); (a) inside a render frame; ~30 sites without helpers — batch A |
| `worldSpace` `+0x148` | `AE#13315+0x202`, `SetCurrentCell+0x21A` (branch `bPreemptivelyUnloadCells`), `AE#13428+0x19A`, `AE#13439+0x14D`, `AE#13280+0x27C`, `AE#13753+0x82`, `AE#13754+0x110`, **`MapMenu::AE_53094+0x147`** (zeroes it) — M | 47 functions directly + 47 via `TES::GetWorldSpace` (`AE#13314`, `mov rax,[rcx+0x148]; ret`): terrain, water, LOD, grass, sky (`AE#13326`), actor processing `Actor::TryChangeSkyCellActorsProcessLevel`, quests, impacts, map | W2: cells of world B are looked up and drawn through world A (`AE#13319` → `AE#13718(dh,x,y,worldSpace)`) | (d) as a singleton; (a) is possible at specific points on M — precedent: the map itself switches `worldSpace` and `TESWaterSystem.worldSpace` (`AE#53101`/`53102`/`53144`/`53147`) |
| `gridCells` `+0x78`, `currentGridX/Y` `+0xB0/+0xB4`, `currentQueued` `+0xB8/+0xBC`, `saveGrid` `+0xE0/+0xE4`, spare `+0x140/+0x144` | grid — `AE#13383+0x72` (creation); centers — `AE#13288`, `AE#13287`, `AE#13281` (reset to `0x7FFFFFFF`), `AE#13315`, `AE#13317`, `SetCurrentCell+0x4A2/+0x5C6`, `AE#13382`, `AE#35652`, `Main::AE_36601` — M | grid — 43, center — 33 functions: `TES` traversals, `TES::Pick`, `AE#13319`, `AE#13321`, `AE#13306`, quests `AE#25063`, water `BGSAutoWater`, LOD `AE#31835/31841` | G2: cells outside the grid are "not loaded" for `AE#13306`, `GetCell`, `AE#13319`; W2: one grid of one world | (d); a second grid is a list of attached cells of our own outside `GridCellArray` + (c) for helpers |
| `interiorBuffer` `+0xC8` (3), `exteriorBuffer` `+0xD0` (36) | `AE#13434`/`AE#13435`/`AE#13384`, on J — from `PostProcess` (T39) | 10 / 18 functions; `exteriorBuffer` — also the navmesh list `Pathing` | I2: more than 3 interiors displace each other (H4); W2: the buffer is shared across worlds | (c) exists: `Preload` (`AE#13370`, `AE#18547`, `AE#13721`); for N portals — multiple held cells (b) |
| (Permanent) sky cell of the world | attached by `AE#13315+0x1C6`, `AE#13317+0x480/+0x48A`; updated **only for the current world** `Main::Update+0x7A5` (`GetSkyCell`) → `AE#19012` | traversals `AE#13291`/`AE#13325` ("interior or grid + sky cell") | W2: permanent references of world B (city NPCs — **assumption**) are not attached and not updated | (b): attach and update world B's sky cell; 3 sites |
| `LoadedAreaBound` `+0x2B8` | `LoadedAreaBound::SetCurrentCell` (`AE#25946`, only from `AE#13288+0xC7`), `Clear` (`AE#13288+0x59`, `SetCurrentCell+0x687`, `AE#13381+0x113`) | 7 functions; outside `TES` — `AE#52964`/`52969`/`52980` (map, from `AE#53093`) | G2/W2: one area around the player; what it limits in Havok is an **assumption** | (b) or not needed for a "window without physics"; 1 writer |
| `navMeshInfoMap` `+0x2B0`, `placeableWaterCount` `+0x2A8`, `deadCount` | `AE#13403/13404`, `AE#13416/13417` | — | not singletons by nature (shared registries) | — |
| `sky` `+0x100` (`Sky` pointer) | `AE#13280+0x16B` (once) | 30 functions | see 3.4 | — |

### 3.2. Physics

| State | Writers | Readers | Breaks | Class, scope |
|---|---|---|---|---|
| Exterior Havok world `AE#400440` (`NiPointer<bhkWorld>*`) | `AE#19193+0x111`, `AE#19194+0xB2` | `GetbhkWorld` (`AE#18995`, 99 callers), directly — 23 functions (`Main::Update`, `AE#36565` Job_IO, `AE#13288`, `AE#13430`, `AE#18976`, `AE#18996`, `AE#18997`, `AE#19130`, `UpdateExteriorWorldJob` `AE#18870` and others) — M and J | W2: bodies of two worlds in one `hkpWorld`; city world and Tamriel coordinates overlap — **assumption** | (b)+(c): a second `bhkWorld` (like `AE#19195`) + intercepting `AE#18995` and the 23 functions; or (d): world B without physics |
| `bhkWorld.toggleCollision` `+0xC5F9` | `SetCurrentCell+0x21` (=0 for exterior), `AE#13317+0x30` (=1), `AE#18976+0xCE` (when attaching any cell), `AE#19130+0x19D` (=0 for interior), `AE#13375/13376` (`ToggleCollision`), `AE#19195`, `AE#19197` | only `bhkWorld::Unk_32` (`AE#77851`, already hooked in `HooksInteriorSim.cpp`) | I2: each world has its own — not a singleton; T21: entering turns off the street world | (c) exists |
| `TES::Pick` (`AE#13371`) | — | 11: actor and player LOS, `AE#42744` (from `AE#39946`: `PlayerCharacter::Update` M, `ProcessActorAnimSG` J), ash | I2: an NPC ray in an attached interior goes through the street world (`+0xC`, `+0x27`) | (c): 1 function, pick the cell by actor |

### 3.3. "Current location" helpers and loading

| State | Writers | Readers | Breaks | Class, scope |
|---|---|---|---|---|
| `ExteriorCellLoader` key `(x,y)` without a world | `QueueCellLoad` `AE#18541` (T39) | `AE#18543`…`AE#18547` | W2: grids of two worlds with the same `(x,y)` will merge | (d): load world B synchronously (`AE#13718`/`TESWorldSpace::LoadCell` `AE#20460`) or intercept the key |
| "cell process level" cache `AE#400339`/`AE#358794` in `AE#13321` | `AE#13321+0xBA/+0xF1`, reset `AE#13407` | 20 callers: attachment `AE#19063`, `AE#19411`, `Enable`, `Resurrect`, `UpdateAnimation`, quests | I2: interior is always "center" (1) — correct; W2: world B cells are measured from grid A's center | (c): 1 function |
| Encounter zone: level (`zone+0x44`) | `BGSEncounterZone::AE_18199` from `SetCurrentCell+0x288`, `AE#13315+0x215` | NPC level of the zone | I2: an interior attached outside `SetCurrentCell` does not get a level — **assumption** that it is not set otherwise | (b): call on attachment; 1 site |
| Quest aliases | — | `TESQuest::AE_25063+0x14A6/+0x14C7` (`interiorCell` or `gridCells`) | I2/W2: far-side references are not found (T38) | (c) or accept |

### 3.4. Sky, weather, water, grass (writers; player location — `PLAYER_MAP.md`, 10)

| State | Writers | Breaks | Class |
|---|---|---|---|
| `Sky.mode` `+0x1BC`, `extLightingOverride` `+0x28`, `flags` `+0x1DC` | `TES::AE_13326` (`SetCurrentCell+0x6B5`, `ToggleSky`): mode 1/2/3 by cell flags, `+0x28` = `worldSpace+0x160`; `Sky::AE_26222` (6 callers, incl. `AE#13317+0x67E`) | I2/W2: one sky and lighting per frame | (a) in the portal pass (T44) |
| `Sky.currentClimate` `+0x40`, weather `+0x48…+0x68` | `Sky::SetClimate` (`AE#26239`) from `SetParentCell+0x306` (world climate: `AE#20468`), `AE#13698`, `ReloadCurrentClimate`; weather — `Sky::UpdateWeather`/`SetWeather` | W2: world B weather = weather of A | (d) for simultaneity; accept one weather |
| Grass on/off | `GrassManager::SetGrassEnabled` (`AE#15378`) only from `SetParentCell+0x348` and `ToggleGrass` | W2: grass by the player world's flag | (a) or accept |
| `TESWaterSystem.worldSpace` `+0xA8`, `playerUnderwater` `+0xB8` | `+0xA8` — only the map (`AE#53101/53102/53144/53147`); `+0xB8` — `AE#36560+0x84` and the map | water — by camera location (T44) | — |
| Acoustics: `bhkWorld+0xC5C8` (`BGSAcousticSpaceListener`), flag `+0x18` | `=1` — `SetParentCell+0x258` for the player's world; `=0` — `AE#19130+0x1BE` on interior detach | I2: the acoustics listener is enabled only in the player's world — affects sound through the door (5.14, item 3; T23) — **assumption** about its purpose | (b) |

### 3.5. Render: global states and writers (reads — T44)

| State | Writers (by code) |
|---|---|
| `BSShaderManager::State.interior` `+0x44` and derived `AE#415263`/`AE#415264` | `AE#105631` ← `SetCurrentCell+0x46D` (1), `AE#13317+0x1E` (0), `MapMenu` `AE#53101+0xC8`, `AE#53102+0x393`; derived — also `AE#105624+0xBC3` |
| `State.oldGridArrayCenter`/`gridArrayCenter`/`gridArrayLerpStart` `+0xA8…+0xB8` | `AE#13288` (`+0x646…+0x6DB`, `+0xDE2…+0xE0A`), `AE#13317+0x70A…+0x73A`, `AE#13287+0x14F` |
| `State.loadedRange` `+0x34`, `cachedPlayerBound` `+0x12C` | `AE#105650+0x37`; `Main::AE_36561+0xAE` |
| `SSN` root slots 8/9 (world nodes `+0x148`/`+0x150`) | `TESWorldSpace::AE_20516` ← `AE#13317+0x1C5`; `AE_20517` ← `SetCurrentCell+0x54C` (calls `[vtbl+0x1E0]`/`[+0x1C0]` on `State.shadowSceneNode[0]`) |
| `SSN.portalGraph` `+0x228` | `SetPortalGraph` (`AE#106385`) ← `SetCurrentCell+0x37C`, `AE#13317+0x78`, `AE#18976+0x140`, `AE#19170+0x6D`, `AE#20515+0x43`; **inside `+0x50D` — `RemoveAllLights`** |
| Hiding LOD roots (bit 0 of `+0xF4` on `lodObjectRoot`, `lodLandRoot`, `treeNode` `BGSTerrainManager`) | `AE#31813` (show) ← `AE#13317+0x1B4/+0x3F9`; `AE#31814` (hide) ← `SetCurrentCell+0x53B` |
| `AE#414593`, `AE#414596`, `AE#414592`, `AE#400335` (terrain/water flags and counters) | `AE#13288+0x32/+0x6E3/+0x6EA/+0xDBD`, `AE#13317+0x6E3…+0x760`; `AE#414593` — also `BSLightingShader::SetupGeometry+0x278`; purpose — T44 |
| Light: `ShadowSceneNode::RemoveAllLights`, `AE#106355` | `AE#13317+0x34C`, `+0x5F9`; `SetPortalGraph+0x50D` |

## 4. `TES` helpers — interception points instead of substitution

| Helper | What it does (by code) | Callers | For I2 / W2 |
|---|---|---:|---|
| `AE#13314` `GetWorldSpace` | `worldSpace` | 47 | W2: (a) or batch B |
| `AE#13282` | `interiorCell`, otherwise the center grid cell (`AE#13319`) | 6 (`Sky::Update`, `AddDecal`, `AE#30452`, `AE#30495`, `AE#32054`, `AE#30577`) | the camera's cell, not the player's |
| `AE#13319` | cell `(x,y)` from the grid or from `TESDataHandler` by `worldSpace` | 11 functions | W2 |
| `AE#13322` `GetCell(pos)` | `interiorCell`, otherwise `AE#13319`, only `kAttaching`/`kAttached` | 8 (weapons, explosions, impacts, fire) | I2: hits and explosions in an attached interior look up the street cell |
| `AE#13306` | for interior — always "yes", otherwise — position in the grid | 5 (`Actor::AE_37618`, `Character::AE_37352/37353`, `AE#19413`, `AE#22511`) | I2/G2 |
| `AE#13371` `Pick` | ray in the world of `interiorCell` or the grid center | 11 (`Actor::CalculateLOS`, `RequestLOS`, player LOS `AE#40520`, `AE#42744`) | I2: LOS |
| `AE#13321` | level 1/3 by distance from the grid center, interior — 1 | 20 | W2 |
| `AE#13308` | "cell loaded/attached" only by `cellState` | 61 | no singleton — works for I2 |
| `Pathing` vtable 2–6 (`AE#30633`…) | navmeshes of `interiorCell` or `exteriorBuffer` | slots | I2: navmesh of an attached interior is not visible |

Intercepting these 8 functions by the actor's cell (or by the cell passed in the portal frame via `thread_local`) covers the bulk of I2 for AI and physics without global substitution.

## 5. Scenarios

### 5.1. Multiple interiors from the street (player outside, N interiors attached by `InteriorSim`)

In order of removal:
1. **Interior buffer (3):** multiple held cells instead of one (b) in the already existing `Preload` hooks.
2. **`portalGraph` and light:** one graph per `SSN`, and `SetPortalGraph` removes all light — graph per portal (b), setting only for the duration of the portal pass (a, R); the `AE#106385` hook already exists.
3. **"Current location" helpers** (section 4): `Pick`, `GetCell`, `AE#13306`, `Pathing` — (c) by the actor's cell; no global substitution on J.
4. **Traversals `AE#13291`/`AE#13325`/`AE#13388`/`AE#13415`:** hooks `13291`/`13325` exist — extend them to N.
5. **Zone level** `AE#18199` on attachment (b).
6. **Portal pass rendering:** `interior=1`, sky mode by cell, without slots 8/9 and LOD — (a) on R around the pass (T44).
7. Acoustics and sound — T23.

The Havok world, `toggleCollision`, and `interiorCell` do not need to change for this scenario.

### 5.2. Gates (exterior A ↔ exterior of another world B, both grids alive)

1. **Loading B outside `TES`:** the `ExteriorCellLoader` key without a world (d) → synchronous loading of world B cells or intercepting the key; the list of attached world B cells is our own (b).
2. **Havok:** one exterior world per game (d). Stage 1 — B without physics ("window"); stage 2 — a second `bhkWorld` + intercepting `GetbhkWorld` and the 23 functions (b+c).
3. **World B sky cell:** attach and update every frame (b), otherwise world B permanent references do not live.
4. **Helpers** `AE#13321`, `AE#13319`, `AE#13314`, `Pick`, `GetCell` for world B cells (c).
5. **Rendering B:** slots 8/9 and world B's shader grid center, LOD — `BGSTerrainManager` has one set of roots (d for world B's distance; not needed inside walls), sky mode — (a) on R.
6. **Transition:** `AE#13315` does `DetachAll` of the grid (`+0x224`) and resets the centers (`+0x233…+0x265`) — for a seamless transition (c): switch `worldSpace` and the grid without detaching A; `SetParentCell` exactly once (climate, grass, region — T15/T38).

### 5.3. Interior ↔ interior (player in A, portal into B)

1. **Transition:** `SetCurrentCell` detaches the old interior (`+0x3A1` `AE#19130(old,1)`) and resets the graph (`+0x37C`) — skip through the existing `AE#13316` detour (c).
2. **Portal graph** per portal (b), as in 5.1.
3. **Helpers** (`Pick`, `GetCell`, `Pathing`) for B actors while the current one is A (c).
4. **Sky and light:** mode by cell B during the portal pass (a).
5. Interior buffer.

The simplest of the three: both Havok worlds are their own, the street grid is not involved.

## 6. Summary: what to do

- **T29 (portal registry).** A portal gets its own: held far-side cell(s), portal graph, render-state snapshot for the pass (`interior`, sky mode, slots 8/9, LOD, shader grid center), a "zone initialized" flag. Shared hooks: `Preload` for multiple cells; new (c) ones — `TES::Pick`, `TES::GetCell`, `AE#13306`, `Pathing` 2–6 — decide by the actor's cell, not by the global. Substituting `TES` fields — only on M or R, never on J.
- **T21 (live street from inside, second portal on an inner door).** Entering an interior kills the street in five places in `SetCurrentCell`: `toggleCollision=0` (`+0x21`), `AE#19130(gridCell,0)` (`+0x50E`), `AE#31814` (`+0x53B`), `AE#20517` (`+0x54C`), `AE#105631(1)` (`+0x46D`); traversals `AE#13291`/`AE#13325` stop seeing the grid. For a live street — skip the `+0x49C…+0x54C` block in the detour (c), rendering the street from the interior is (a) on R. The second portal on an inner door — scenario 5.3.
- **Gates.** The scope is an order of magnitude larger: the `TES` singletons (grid, world, loader key, Havok world) can only be worked around with a list of world B cells of our own. Proposed path — in stages: first B as a "window" without physics and without NPCs (loading outside `TES`, render (a)), then the sky cell and helpers, and a second `bhkWorld` last.
