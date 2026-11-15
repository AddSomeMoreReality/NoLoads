# Portability: everything that ties the mod to 1.6.1170

User's goal (01.11.2026): the mod works the same on all SE/AE versions, at least starting from 1.5.97. Hard-coded offsets, function numbers, and slots must be replaced with Address Library, NG fields, or verifiable signatures.

This file is a debt list. It exists so that experiments are not slowed down now and everything can be cleaned up later.

**Rules:**
- A new offset or AE number that cannot immediately be expressed portably is recorded here in the same commit.
- Completed items are removed from the tables and moved to section 5.
- First look for an NG field, then `RELOCATION_ID(se, ae)`, and only then a signature. A raw offset is the last resort.

## 1. Current state

- `Portal::Prepare` enables the portal only on 1.6.1170. On other versions the plugin loads, but the portal does not start and writes the reason.
- Call hooks (`WriteCall`) verify the bytes at the site and the `call` target. If they do not match, the hook is not installed, and `Hooks::Ready()` returns `false`.
- AE 1.6.x and 1.7.x use a single Address Library numbering space, but offsets inside functions may differ between them. This is stated in the sources of the Realistic Reflections - Mirrors mod; we have not verified it ourselves.

## 2. Offsets and constants

| What | Where | Value | Replace with |
|---|---|---|---|
| Ambient light | `PortalState.h`, `kAmbientOffset`, `kAmbientBytes` | `BSShaderManager::State + 0xC8`, `0x44` bytes | Named fields in NG, or saving through game functions |
| Particle emission remainder | `PortalEffects.cpp`, `Portal::BeforeEmission` | First field after `NiPSysModifier` (`+0x30`) | Describe `NiPSysEmitter` in NG; the class is not there |
| `lodDistance` site | `Hooks.cpp`, `InstallCalls` | `AE#37348 + 0x1F3` | Signature search inside the function |
| Fade site | `Hooks.cpp`, `InstallCalls` | `AE#19384 + 0xE4` | Same |
| Scene-list boundary | `Hooks.cpp`, `InstallCalls` | `AE#36559 + 0x17E` | Same. Per the mirrors-mod analysis the offset is the same on SE. Since 0.8.26 the only pass site: if the call is not found, there is no portal |
| `RenderPlayerView` call | `Hooks.cpp`, `InstallCalls` (0.8.25) | `AE#36558 + 0x11D` | Detour `AE#36559` (SE 35560) instead of call hook |
| Speaker site for subtitles | `Hooks.cpp`, `InstallCalls` (0.8.25) | `AE#52638 + 0x122`, "world position" layout `AE#19784` (point, `+0x10` space) | Signature inside the function; SE numbers `52638` and `19784` not found |
| Size of `AE#40845` | `Hooks.cpp`, `FromLinkedPreload` | `0x18E` | Size from `.pdata` or a signature |
| Accumulator type | `Portal.cpp`, `kAccumulatorType` | `0x63` | Verify on SE |
| Effect technique | `PortalRender.cpp`, `kEffectTechniqueBase`, `kSoftEffectBit` | `0x4000002C`, bit 18 | Verify on SE, move to NG |
| Light flags | `PortalLights.cpp`, `kLightInFrustum` | `0` | Verify on SE |
| Eye-adaptation targets (T12, 0.8.28) | `PortalGrade.cpp`, `FindAdaptTargets`, `AdaptFlag` | Render targets no more than 2 × 2; current = `base + 2 * flag`, flag `AE#415054` (`AE#107183 + 0x296`) | SE number of the flag; verify the HDR chain layout on SE and 1.7 |
| Render-target count (0.8.29) | `PortalGrade.cpp`, `RenderTargetCount` | 114 before 1.6.1130, 116 from it. In the FLAT build the NG `renderTargets` array has 125 elements (`kVRTOTAL`); the real targets are followed by `depthStencils`. The count is derived from the version-dependent offset of `GetDepthStencilData` | Do not iterate `renderTargets` via `std::size` (0.8.28 hung on element 122 = `depthStencils[1].depthSRV`) |
| BSFogProperty fields (T12, T13) | `PortalImage.cpp`, `FogJson` | Raw floats from `+0x58` to `+0x83` (in NG `unk58…unk80`) | Name them in NG from `AE#107524 + 0xE7…+0x181` |
| Depth mode | `PortalRender.cpp`, `kDepthTestWrite` | `3` | NG enum, if one appears |
| Pass reset | `PortalEffects.cpp`, `kRebuildPasses` | `0x7FFFFFFF` | Verify on SE |
| Renderer cells | `[Portal] iColorSlot`, `iDepthSlot` | `17`, `9` | Own choice of a free slot, or no slot substitution at all |
| Group index | `PortalRender.cpp`, `kRefractionNormalsGroup`, `kGeometryGroups` | `5`, `16` | Per the mirrors-mod analysis the same on SE and AE; VR has 19 groups |
| Unnamed fields | `PortalActors.cpp`, `PortalEffects.cpp` | `BSAnimationUpdateData::unk2A`, `unk2C`; `BSFadeNode::unk144`; `NiParticleSystem::unk180`, `unk188` | Name them in NG, verify on SE |
| Sound at the listener | `Hooks.cpp`, `SoundEmitterPosition::kAtListener` | Bit 10 of `BSGameSound::soundType` | Name it in NG, verify on SE |
| Snap-to-point relocation (0.8.27) | `ActorHold.cpp`, `MoveToHighSnap` | `AE#37621 + 0x348` (call to `AE#37618`) | Signature inside `MoveToHigh` (vfunc `0xF1`) or detour `AE#37618` with a caller check |
| Actor in the movement queue (0.8.27) | `ActorHold.cpp`, `kMessageInterfaceActor` | `IMovementMessageInterface + 0x98` = `MovementControllerNPC + 0x1B8` | Describe `MovementControllerNPC` in NG |
| Scene line started (0.8.27) | `Scenes.cpp`, `kLineSaid` | Bit `0x4000` of `BGSSceneAction::flags` | Name it in NG, verify on SE |

### Virtual function indices

In `Hooks.cpp` (`Hooks::Install`), and since 0.8.27 also `ActorHold.cpp` and `Scenes.cpp`. Values may differ for VR; SE needs verification.

| Class | Indices |
|---|---|
| `TESObjectDOOR` | `0x37` |
| `GridCellArray` | `1`–`8` |
| `CellLoaderTask` | `1`, `2`, `3`, `4`, `6` |
| `PlayerCharacter` | `0xAD` |
| `NiNode`, `BSFadeNode`, `BSMultiBoundNode`, `BSMultiBoundRoom`, `BSTriShape`, `BSDynamicTriShape`, `BSSubIndexTriShape` | `0x34` |
| `BSEffectShader` | `2` |
| `BSEffectShaderProperty` | `0x2A` |
| `Character` | `0x79`, `0xAD`; `0xF1`–`0xF4` (`ActorHold`) |
| `MovementControllerNPC`, table `[1]` (`IMovementMessageInterface`) | `1` (`ActorHold`) |
| `BGSSceneActionDialogue` | `0x11` (`Scenes`) |
| `HighActorCuller` | `1` |
| `BSXAudio2GameSound` | `0x0F` |
| `ImageSpaceEffectHDR` | `1` (T12, `AE#107183`) |

## 3. Address Library numbers

SE and AE are numbered separately. Without an SE number the code is written as `REL::ID(ae)`, and it cannot work on SE.

**SE number known, but the code still uses `REL::ID`.** Only a replacement with `RELOCATION_ID(se, ae)` is needed:

| AE | SE | What |
|---|---|---|
| 36559 | 35560 | `RenderPlayerView` |
| 69353 | 67992 | `JobList::Finish` |
| 106385 | 99741 | `ShadowSceneNode::SetPortalGraph` |

`RELOCATION_ID` is already in place for `AE#106600` (SE 99955), `AE#77463` (SE 75657, particle packer), and `AE#19823` (SE 19396, `GetDistance`).

**SE number unknown (43 items: 37 before 0.8.27, 5 from T11 in 0.8.27, 1 from T12 in 0.8.28):**

| Module | AE numbers |
|---|---|
| `Hooks.cpp` | 13291, 13325, 13370, 13419, 13434, 13721, 18547, 19130, 19384, 19784, 19824, 36558, 36609, 37277, 37348, 37350, 40845, 41466, 43690, 52638, 61410, 74651, 77851, 78274, 105486 |
| `InteriorSim.cpp` | 18890, 18900, 18976, 19046, 19063, 19130, 77905, 77906, 400333 |
| `Portal*.cpp` | 18966 and 105643 (`PortalImage`), 41496 (`PortalActors`), 106342 (`PortalLights`), 388630, 400333, 411270 (`PortalEffects`), 415054 (`PortalGrade`) |
| `Preload.cpp` | 18540 |
| `ActorHold.cpp` (0.8.27) | 37503 (detour), 37618, 37621 (call at `+0x348`) |
| `Scenes.cpp` (0.8.27) | 24140, 55321 (detour) |

**How to get the SE numbers.** `SkyrimSE.exe` 1.5.97 and `version-1-5-97-0.bin` are needed. The user does not have them on disk. Functions are then matched using the tools in `tools/`: via virtual-function tables, strings, and callers. Some of the numbers are in NG and in the sources of the mirrors mod: these are `RELOCATION_ID(se, ae)` pairs and `VariantID(se, ae, vr)`.

## 4. What is already known about other versions

From the sources of Realistic Reflections - Mirrors (facts about the binaries; code is not copied, see section 5.15 in `docs/FEATURES.md`):
- the mod works on SE 1.5.97, AE 1.6.1170, GOG 1.6.1179, AE 1.7.104, and VR 1.4.15;
- the window around the scene-list boundary sits at `RenderPlayerView + 0x16F` on SE, 1.6.1170, and 1.7.104, and at `+0x197` on VR;
- `BSShaderAccumulator`: `batchRenderer` at `+0x130`, and at `+0x158` on VR;
- `BSBatchRenderer::renderPass` stores `PassGroup` records directly in the array, although NG declares pointers.

Verified independently on 1.6.1170: `RendererShadowState` (`AE#388819`) + `0x35C` = `AE#388909`, `+0x380` = `AE#388915`, `+0x390` = `AE#388918`. The particle packer `AE#77463` reads exactly these fields.

## 5. Already removed

| What | Version | How |
|---|---|---|
| `bhkWorld + 0xC5F9` | 0.8.22 | NG field `bhkWorld::toggleCollision` |
| Manual call of `AE#18995` | 0.8.22 | `TESObjectCELL::GetbhkWorld()` |
| Camera fields of the packer | 0.8.23 | NG field addresses `posAdjust` and `cameraData`; no numeric offsets. The hook is a detour by ID, without an offset inside `AE#107637` |

## 6. Not version-dependent

- Empty. The replacement of the `BardSongStarter` quest fragment (0.8.25) was removed: the mod does not modify game data (`CLAUDE.md`, 5.14, item 7). Old entry: replacing `Scripts/QF_BardSongsDrinkingSong01_000BE159.pex`: Papyrus scripts are identical on SE and AE. If another mod replaces the same fragment, the one lower in the load order wins.
