# Similar mods and projects

First search — 02.11.2026 on GitHub, Nexus, and forums. The question was: who else renders a second camera, holds a cell without the player, or carries sound between cells. Second pass the same day — reviewing the sources the user downloaded and those that could be cloned from GitHub.

**Licenses.** Code from GPL projects cannot be copied into a mod with a different license. From projects without a license also cannot: only ideas and facts about the game binary may be taken. Code from MIT projects may be taken with attribution. The rule for Realistic Reflections is in `docs/FEATURES.md`, 5.15.

## What changed after the first search

- **Audio Occlusion - Universal Support** was removed by the author.
- **TrueThroughScope**: sources on [GitHub](https://github.com/ss7332337/True-Through-Scope), MIT license.
- The user downloaded to `E:\Games\Skyrim\ModCreation\references\`:
  - Audio Occlusion, with sources;
  - Bodycam FO4, DLL only;
  - Commonwealth Bodycam, sources;
  - TrueThroughScope, DLL.
- Cloned into `E:\Games\Skyrim\ModCreation\references\github\`:
  - True-Through-Scope, STB_ProjectileControl, hdtSMP64, Fo4VR-True-Scopes;
  - SeverActions, Modern-NPC-Pathing, SimplyKnock;
  - SkyrimSE-SmoothCam, FreeCameraFramework, community-shaders.

## User's decision on approaches

- **Moving the interior into the exterior cell** — a bad option. A non-Euclidean link is needed, i.e. a portal. Breezehome without loading, Open Cities, and Seamless City Interiors fall into the same category.
- **Hiding the load with elevators and airlocks** (like Neon Core in Starfield) does not fit. Skyrim has none, and the transition between cells must be invisible to the eye.

## 1. Review

### Audio Occlusion (SKSE, sources in the archive, no license — ideas only)

Muffles voices and sounds behind walls.

**How it works:**
- **Call interception.** Scans `.text` and places `write_call<5>` on all direct calls of sound functions: assembly, position, node tracking, start, stop (`AE#67666`, `67631`, `67636`, `67616`, `67619`). The dialogue replica constructor (`AE#35220`) is intercepted separately. The mod does not patch function prologues.
- **XAudio2.** Intercepts `CreateSourceVoice` and adds a filter flag. Then on a specific voice it sets volume and a low-pass filter (`SetFilterParameters`). Listener and source positions are taken from `X3DAudioCalculate`.
- **Occlusion.** Rays via `hkpWorld::CastRay` without actors. If the direct path is blocked, it looks for a detour to the left, right, and above, and takes the width of the passage into account.

**What we take from this:**
- Our sound through the door currently passes through the walls of the building. AO has a ready-made scheme: muffle the sound and cut high frequencies when the path from source to listener is blocked. For a portal the path is "source → door → listener".
- A speaker's voice "follows" their node, so the voice position is recomputed every frame. Our hook in `SetEmitterPositionImpl` already catches this: the bard's song was audible outside.
- **Compatibility.** AO will cast rays from the player to the displaced point. The ray will hit the walls of the building, and the sound will be muffled. This is close to the truth, but the sound does not account for whether the doorway is open.

### True Through Scope (Fallout 4, MIT)

A scope through which a second camera sees.

**How it works:**
- The second view is drawn after the main pass and before ImageSpace (hooking `Render_PreUI`), directly into the game's render target: first a copy of the first frame, then a second pass, then merging (`RenderTargetMerger`).
- For TAA a motion-vector mask is written (`ApplyMaskToMVCS.hlsl`).
- Lighting for the second view is saved and substituted (`LightBackupSystem`), culling is its own (`ScopeCulling`).

**What we take from this:**
- Confirms our pass placement: after the world, before ImageSpace.
- Explains the fire. The second view's image goes through the main frame's color correction: eye adaptation, bloom, saturation. For a scope this is correct; a portal into different lighting has none. The portal image needs its own tone mapping (`docs/FINDINGS.md`, run 0.8.24).
- The motion-vector mask is a second way to get rid of TAA ghosting. We solved it differently in 0.8.21.

### Commonwealth Bodycam (Fallout 4, sources)

This is not a second view, but an effect over the frame via the ReShade API.

Useful detail: D3D11 state is saved and restored wholesale via `ID3D11Device1::CreateDeviceContextState` and `SwapDeviceContextState`. This is more reliable than our manual saving around the pass and will come in handy when there are many portals.

### Bodycam FO4 (DLL only)

A scope with zoom via a second render only inside the lens. No sources, nothing to review.

### STB Projectile Control (SE/AE, GPL-3)

Allows controlling an arrow or spell in flight. Will be useful for step 5: projectiles through the door.

- `ArrowProjectile` and `MissileProjectile::UpdateImpl` (vfunc) end with `Projectile::Move`. It takes velocity via virtual `GetLinearVelocity`, multiplies by the frame step, and moves the reference. So the door crossing can be caught in `UpdateImpl`, and the reference moved and velocity rotated right there.
- Slot `0xC3` of `MissileProjectile` (`CanReorient`) decides whether the model takes the reference's angles. For an arrow it returns 1, for a spell 0.
- `ProcessImpacts` is called every frame. A hit is detected by the `kProcessedImpacts` flag.
- A projectile outside the loaded area hits nothing. Inside an interior it will need the interior's Havok world.
- The free camera is done via `FreeCameraState::GetRotation` and `GetTranslation`. This will be useful for the "any camera" requirement (5.14).

### HDT-SMP (aers/hdtSMP64, MIT, 2020)

Hair and clothing physics on Bullet, in its own world, per skeletons.

- A skeleton is considered in the scene if its node has three levels of parents. Portal interior NPCs' 3D hangs under our node outside the scene, but it does have three parents. So physics should run for them.
- **To check:** the modern fork FSMP (Faster HDT-SMP) disables physics by distance to the camera. For it, interior NPCs stand 27,000 units away, and hair may not move through the portal. Relevant if the user runs it.

### SeverActions

The repository has only Papyrus scripts and a ready DLL, no native code. "Off-screen" travels are done in Papyrus. Lagging followers are teleported and walked through doors by the mod. This is the ordinary approach, no good for a portal.

### NPC Pathing NG (Modern-NPC-Pathing, GPL-3)

Finds stuck NPCs: a package tells them to walk, but the position does not change. First it tries to jump over the obstacle, in the worst case teleports.

**Compatibility:** an interior NPC behind the portal may look stuck to it. If the user installs this mod, watch the `actor.jump` records.

### SimplyKnock (2018)

Knocking on locked doors: the door is used as an activator. Unrelated to portals.

### SmoothCam, FreeCameraFramework

Both move `PlayerCamera::cameraRoot` and `NiCamera` and recompute the matrix (`UpdateWorldToScreenMtx`).
- The portal takes the eye from the world camera (`AE#36609`), not from the player's position, so it should work with them.
- FCF provides a camera path API. It is convenient for testing "any camera" (5.14).

### Community Shaders (GPL-3)

- `package/Shaders/ISHDR.hlsl` — the reconstructed tone-mapping shader. It shows what the outdoor ImageSpace does to the portal image.
- Skylighting draws the precipitation occlusion pass several times per frame from different directions (call from `AE#36559 + 0x3BF`). It also enlarges the accumulator's alpha grouping pool (`BSShaderAccumulator` vfunc `0x28`, `StartGroupingAlphas`). For many portals this matters: this pool can run out.
- **Compatibility.** Deferred, Light Limit Fix, Linear Lighting, Upscaling, and Post Processing change both lighting and tone mapping. The user does not have Community Shaders installed: `Data\SKSE\Plugins` has only our plugin and Crash Logger. Runs are on the game's clean renderer.

### Fo4VR True Scopes (GPL-3)

Already covered in the first search: on failure it disables itself after three retries, updates the lens once per N frames. Nothing new beyond that.

## 2. A different approach, but good to know

| Project | Approach | What follows from it |
|---|---|---|
| [No Loading Screen Breezehome](https://www.nexusmods.com/skyrim/mods/109103) | Interior moved into the worldspace, a single house | Rejected (see the user's decision). Our 0.8.5 showed the interior is larger than the building |
| Open Cities Skyrim | Cities rebuilt inside the main world | [Per reviews](https://stepmodifications.org/forum/topic/18862-open-cities-alternatives-no-gates/), incompatible with almost everything. A portal does not change game data |
| Starfield: Seamless City Interiors | [Copies interior data into the world](https://www.dsogaming.com/mods/starfield-just-got-a-must-have-seamless-city-interiors-mod/) | The same technique and the same conflicts |
| Starfield: Neon Core Disguised Seamless | [Hides the load with elevators](http://starfieldunity.com/mods/2450/neon-core-disguised-seamless-project) | Rejected: Skyrim has no elevators and airlocks |
| OpenMW | [Issue #6602 "portals"](https://gitlab.com/OpenMW/openmw/-/issues/6602) | Not implemented anywhere. The same difficulties are named: interiors larger than buildings, a non-Euclidean link needed |
| Oblivion Reloaded | [Nexus](https://nexusmods.com/oblivion/mods/51032) | Renderer extension without exe patching, old engine |
| Skyrim Together Reborn, SkyMP | [TiltedEvolution](https://github.com/tiltedphoques/TiltedEvolution) | Copies of players in another's cell. How they live with Havok, not determined |
| Faster Loadscreens | [Nexus SE 182852](https://www.nexusmods.com/skyrimspecialedition/mods/182852) | Only speeds up loading |
| CommonLibSSE-NG | [v15.1.0](https://github.com/alandtse/CommonLibSSE-NG/releases/tag/v15.1.0), MIT | We are on v13.1.0. Updating may remove some of our offsets (`BSShaderAccumulator` fields, render access) |

## 3. Conclusion

- **There is no ready-made portal analogue.** No one has done a second view into another cell through a door with a living interior.
- **What is useful right now:**
  - True Through Scope and the Community Shaders shader — for the fire: the portal image needs its own color correction;
  - Audio Occlusion — for sound through the door, not through walls;
  - STB Projectile Control — for step 5, projectiles through the door;
  - `SwapDeviceContextState` from Commonwealth Bodycam — for reliable D3D11 state saving when there are many portals.
- **Compatibility to check:** Audio Occlusion (rays to displaced points), FSMP (physics by camera distance), NPC Pathing NG (teleporting the "stuck").

## 4. Technical and bug-fixing mods (T14, 04.11.2026)

Question: whether to take the mod as a dependency, rely on it, or only verify compatibility. Details and the run plan — `docs/tasks/results/T14.md`. Engine Fixes clone — `references\github\EngineFixesSkyrim64`, reviewed tag `7.0.19` (`94ceec1`): this is the AIO version the user has.

### SSE Engine Fixes (aers, MIT)

**How it works.** On SE and on 1.6.1170 (7.0.19) `d3dx9_42.dll` calls the `Initialize` export before SKSE. Memory replacement, patches, and almost all fixes are installed in it. At `kDataLoaded` four more are added: save screenshots, mouse, tree reflections, parallax. Its trampoline is its own. Versions: 1.5.97 (separate Part 1 and Part 2 files), 1.6.1170 (7.0.19 AIO), 1.7.x (7.0.20 and newer, `master` built for 1.7.104).

**Overlaps with us.** I compared all `RELOCATION_ID`, `REL::ID`, and ID tables in the Engine Fixes `src/` with the IDs in our code (63 IDs) and with `AE#` mentions (117 IDs).
- One match: `AE#106342`. We call it in `PortalLights.cpp` (shape collection under a light), and Engine Fixes places a `write_branch` at `+0x16` adding a `nullptr` check (`ShadowSceneNodeNullPtrCrash`). Our call only becomes safer from this.
- Our detours, `write_call` (5 places), and vfuncs do not overlap with its patches. Its vfuncs: `Actor`/`Character`/`PlayerCharacter` `0x101`, `EnchantmentItem`, `TESObjectBOOK`, `BGSMusicType[1]` and others; our `Character` slots — `0x79` and `0xAD`.
- It does not touch the T10 and T11 functions: `AE#24140`, `24235`, `24236`, `37501`, `37503`, `37618`, `37621`, `55321`. Processing levels (`ProcessLists`, `MoveToHigh`) are untouched as well.
- Nearby, but in different functions: `CellInit` (`AE#18905+0x114`, cell location before initialization), `ESLCELLLoadBug` (`AE#18885`, `18886`), `SavedHavokDataLoadInit` (`AE#19535+0x348`), `CalendarSkipping` (calls in `AE#13328`, `36564`, `40445`), `TorchLandscape` (`AE#17610+0x530`, `AddLight`), `LipSync` (`AE#16267`).

**What changes our assumptions.**
- **`bOverrideRenderPassCache=true` by default.** `BSRenderPass::Allocate` and `Deallocate` (`AE#107497`, `107498`) are replaced with `malloc` and `free` via tbbmalloc, and the pass pool (`AE#107500`–`107502`) is disabled. The words "passes come from a shared pool" (`CLAUDE.md`, section 7, and the comment in `PortalRender.cpp`) are wrong with Engine Fixes. `DrainPasses` is still needed: the pass lists on a property remain shared, but the looping as in 0.8.21 will manifest differently, through reuse of freed memory. The pool no longer runs out, and for many portals this is a plus.
- **`BSLightingShaderPropertyShadowMap`.** The game reuses one property pass for all shadow maps of a light, and Engine Fixes considers this a bug. It creates up to four passes per property, by shadow map index (`AE#107602`, `106517+0x295`, `106526`, `106500`, `106534`). This is the same trouble we have with the second view ("one list per property"), and a ready-made MIT technique.
- **Memory manager replacement, scrap heap, Havok memory, and Scaleform.** A freed block is reused differently, so crashes from "foreign" freeing (0.8.17, `DetachChild`) will be elsewhere and with a different stack. Conclusions proven by trace and disassembly remain valid. Crash Logger stacks from builds with and without Engine Fixes must not be compared.
- **`SafeExit`.** On game exit `TerminateProcess` is called, destructors do not run. The trace loses no more than 250 ms (`kFlushInterval`).
- **`SaveScreenshots`.** Patches the save screenshot render (`AE#105674`). Check that the screenshot is normal when a portal is in the frame.

**Relationship type: compatibility, reliance — as occasion.** No dependency needed: it provides nothing that is costly for us to do ourselves. But the mod is installed for almost everyone, so the portal must work together with it. MIT techniques (passes per view index, `malloc` for passes) may be taken with attribution.

### USSEP 4.3.8a (Arthmoor et al., data mod, own license: we take nothing)

Master files — `Skyrim.esm`, `Update.esm`, three DLCs, `ccBGSSSE001-Fish`, `ccQDRSSE001-SurvivalMode`, `ccBGSSSE037-Curios`, `ccBGSSSE025-AdvDSGS`, and `_ResourcePack.esl`; the user has all of them. The version for 1.5.97 is a different, old one.

**What it changes for bards.** I compared the `.esp` records with `Skyrim.esm` and `Update.esm` by subrecords and VMAD, and the sources from `.bsa` — with `Data\Source\Scripts`.
- `BardSongs` (`00074A55`): property `TalsgarTheWanderer` added to VMAD. Fragments are the same.
- `BardSongStarter` (`000BE159`): alias `Lurbuk` flags `0` → `0x202` (Optional), otherwise the quest will not start after Lurbuk's death. DNAM differs only in form version.
- `0006E53F`, `001062A7`, scene `000D0843`, node `000D1EEB` unchanged. USSEP does not touch Whiterun's song scenes: edits only in the Bards College and Solitude scenes.
- **`BardSongsScript.pex` replaced.**
  - Checks `None`, `IsDead`, `IsDisabled`, and `Is3dLoaded` moved to the start of `PlaySong` and `Bard2PlaySong`. In vanilla, `Bard2PlaySong` did not check for 3D.
  - In vanilla `StopInnMusic` always calls `MUSTavernSilence.Add()`. The bug is that the `HasKeyword(LocTypeInn)` check stands on its own line without `if`. In USSEP music is muted only if the player is in the tavern. It is called by song scenes with vocals (`SF_BardSongsBallad01*`, `SF_BardSongsDrinkingSong01*`).
- `SF_BardSongsInstrumentalFlute_0006E5AE.pex`: `PlayIdle` only when 3D is loaded.

**Tavern NPCs.** Saadia, Olava, and Mikael unchanged. `IdolafBattleBornREF` (`0001A689`) only has `XLRL` added. Per the USSEP changelog it changes packages: the tavern keeper's preferred speed removed (`WhiterunInnkeeperPackage`), Saadia gets her own marker at Heimskr's and 4 hours of preaching, Mikael's and Saadia's beds, interrupt flags for Jonna.

**Whiterun doors.** 14 of 236 overridden door references, mostly `XLRL`, `XTEL`, `XNDP`, and `DATA` (door location and target). Among them four `WhiterunWorld` doors, Breezehome, Arcadia's Cauldron, House of Clan Battle-Born, Ysolda's house, and the Temple of Kynareth. The street door of the Bannered Mare (`0001A6F4`) is also changed: X shifted by 1 unit (25726.07 → 25725.07) and `XLRL` added. XTEL and the interior door `00016072` unchanged. For portal geometry the shift is negligible, but door coordinates in traces with USSEP will differ.

**What follows from this.**
- The T10 mechanism (scene lines without high, `AE#24236`) USSEP does not change. The portal bard has 3D, so the new checks pass. In the window without 3D, both vanilla and USSEP will show `Playing=0` and silence.
- **Difference for T23.** With USSEP outside music is not muted when a song starts inside. In vanilla `MUSTavernSilence` fires for the player on the street too.
- Tavern NPC schedules differ with USSEP. Control runs T10 and T11 must not be mixed with runs with USSEP.
- **Relationship type: compatibility.** A dependency contradicts 5.14, item 7 (a data mod), and 1.5.97 needs a different version. The user decides.

### Remaining candidates (by descriptions and sources on GitHub)

| Mod | License, sources | Versions | What for us | Relationship |
|---|---|---|---|---|
| powerofthree's Tweaks (Nexus SE 51073) | GPL-3 (`LICENSE` file; README says MIT), [GitHub](https://github.com/powerof3/po3-Tweaks) | SE, AE, VR | Load Door Activate Prompts (`AE#17923+0x140/+0x168`, `write_thunk_call`); `DistantRefLoadCrash` (`asm_replace` SE `18642`); `EffectShaderZBuffer` (AE data `360087`). Does not overlap with our IDs | compatibility, ideas |
| Scrambled Bugs (KernalsEgg, SE 43532) | sources not found | AE from 1.6.629, on 1.6.1170 reviews are mixed | game fixes (perks, enchantments, arrows); nothing on cells and NPCs | compatibility |
| Bug Fixes SSE (meh321, SE 33261) | closed | SE, AE from 1.6.629 | fix list not checked: Nexus returns 403 | compatibility |
| Actor Limit Fix (meh321, SE 32349) | closed; VR port without license | SE, AE from 1.6.629, per reviews works on 1.6.1170 | changes immediate limit values (128 → N). For 5.14, item 1 (many portals — many high), unrelated to T11 | compatibility, reliance later |
| NPC AI Process Position Fix NG (SE 69326) | GPL-3, [doodlum/MaxsuAIProcessFix-ng](https://github.com/doodlum/MaxsuAIProcessFix-ng) | SE, AE, VR one DLL | no hooks: `TESMoveAttachDetachEvent` events and wait or sleep ≥ 2 h → `MoveToPackageLocation` and `MoveToRef` for high actors. Does not touch the T11 mechanism (door crossing) | compatibility (waiting at the portal) |
| SSE Display Tweaks (SE 34705) | MIT per Nexus, unconfirmed | SE, AE | loading screen framerate, Havok at high framerate. Concerns step 6 and our `AE#61410`, `77851` via the Havok step | compatibility |
| Worldspace Transition Tweaks (SE 48889) | data (`.esp` + Papyrus) | SE, AE | trigger → coordinate recalculation → teleport with loading screen. The idea of coordinate recalculation between worlds for an "exterior — exterior" portal | idea |
