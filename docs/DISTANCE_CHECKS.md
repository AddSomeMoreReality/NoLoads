# Distance and visibility checks: what the game measures from the player and camera

User request of 02.11.2026: find all checks of distance to the player, even those unrelated to the portal at present, replace them with a check from the portal's virtual camera, and add hooks that record every access.

The full automatically generated list is in `docs/DISTANCE_CHECKS_APPENDIX.md`. This document analyzes what matters, what has already been transformed, and what remains.

Labels: **in code** — found in the 1.6.1170 machine code, not tested in the game; **in game** — confirmed by a trace or the user; **hypothesis** — not verified by anything.

## 1. Main conclusion

Most "distance problems" turned out to be **visibility** problems. Interior NPCs are not far away because they are 27,000 units away, but because the main camera does not see them. Based on this, the game disables everything that cannot be noticed. The portal shows them through another camera, and the disabled behavior becomes noticeable: hence both walking on air and NPCs below the floor.

| Mechanism | What it reads | What it broke | Status |
|---|---|---|---|
| `kNotVisible` mark | Main camera | NPC height, `kFarAway`, animation | Cleared, 0.8.24 |
| World camera `AE#36609` | Street camera | Fade, update-path selection, movement | Replaced within actor update scope, 0.8.24 |
| `GetDistance` and its square | Different worlds produce `FLT_MAX` | 108 call sites | Through the door, 0.8.24 |
| Direct read of `player + 0x54` | Player position | Part of AI logic | Not transformed, section 6 |

The transformation is enabled by `[Portal] bActorsSeen`, `bVirtualCamera`, `bDistanceThroughDoor`, `bSoundThroughDoor`, all defaulting to 1. Each can be disabled separately.

## 2. How this was investigated

| Method | What it provides | What it misses |
|---|---|---|
| `tools/re/scan_player_access.py` | Code that itself loads the player or camera and reads the position, as well as functions to which the player is passed as an argument. 59 direct readers, 89 indirect readers, 46 helper functions | Player obtained from a field of another object. Linear scan; branches are not analyzed |
| `tools/re/scan_distance_settings.py` | 639 settings with distance, radius, or range in the name; reader functions found for 517 | Numbers embedded in code and settings with other names |
| `tools/re/scanflag.py` | All readers of the flag bit through a register | Reads through a saved copy of the flags |
| `src/AccessWatch.*` (F3) | Every instruction that actually touched a position, with callers, in the live game | Code that did not execute during capture and threads created after startup |

Static analysis shows what exists in the game at all. F3 capture shows what actually runs and how often. Together they cover each other's weaknesses.

**`AccessWatch`.** CPU hardware breakpoints (registers DR0–DR3) are set on four addresses: player position, player 3D, world camera, player camera. A breakpoint triggers on both reads and writes. The report is `logs/CellLoadResearch.access-N.txt` and an `access.watch` record in the trace.

## 3. Position sources

| What | Source | Number of locations |
|---|---|---|
| Player position | `AE#403521` (player pointer), field `+0x54` | 2843 references to the pointer, 149 direct position reads |
| World camera | `AE#36609`: `[AE#403513] + 0x128` | Section 5 |
| Player camera | `AE#50785` from `PlayerCamera` (`AE#400802`) | `AE#20710`, `AE#37348`, `AE#39102`, `AE#40449`, `AE#40620`, `AE#40636` |
| Sound listener | `AE#67710`, read by `AE#68007` | Sound, section 7 |

## 4. `kNotVisible`: twelve readers

Set by `HighActorCuller` (`AE#40335`) using the main camera frustum. Cleared only by `BSFadeNodeCuller` (`AE#107507`) and only for nodes in the main camera scene. Interior 3D is outside the scene, so the mark is set once and never cleared. **In code.**

| Function | What it is | What happens with `kNotVisible` | For portal NPCs |
|---|---|---|---|
| `AE#37992` | `Actor` vfunc `0x11C`, from `AE#37350` | Sets `kFarAway` on the controller | The flag means "not visible", not "far away". This explains an old mystery: it was also set on street NPCs 300–900 units away |
| `AE#41375`, `AE#41447` | Post-animation jobs | Skip `AE#43690` and `AE#37480` | **Cause of the height issue**, see below |
| `AE#38054` | `Actor` vfunc `0x79` | Animation update without bone movement | Bypassed since 0.8.13; now cleared at the root as well |
| `AE#41381` | Actor update, `AE#40522`, `AE#41446` | Gets visibility differently, through `AE#36618` | Cause not analyzed |
| `AE#36661` | From `AE#36561`, `AE#36590` | Skips the check with the player position | Not analyzed |
| `AE#41246` | From `AE#41233` | Skips the distance-to-player check | Not analyzed |
| `AE#76553`, `AE#71084`, `AE#108600` | Culling processes | Skip the node | Portal context sets `ignorePreprocess` |
| `AE#107089` | `BSParticleShaderGeometryEmitter` | Skips the emitter | Not analyzed |
| `AE#40335` | `HighActorCuller` itself | Does not check a second time | A hook clears the mark after it |

**Why NPCs walk on air.** `AE#43691` accepts a new position from the controller, but keeps the old Z for the actor from the Foot IK base (`AE#401101`, `BGSFootIkRaycastInterfaceDB`). Foot IK feedback `AE#43690` subsequently drives Z. It is called only by `AE#41375` and `AE#41447`, which skip `kNotVisible`. The result: X and Y follow the controller, while Z stays fixed.

Evidence from the 0.8.23 trace, height difference between controller and actor (norm 64):

| NPC | t | Actor Z | Controller Z | Difference |
|---|---|---:|---:|---:|
| Синмир | 58.9 | 87.8 | 126.7 | 38.9 |
| Олава | 182.4 | 32.6 | 134.7 | 102.1 |
| Идолаф | 182.4 | 51.4 | 94.8 | 43.4 |

The controller is at the correct height everywhere. As soon as the NPC stops, the difference becomes 64 again. **In game**; the causal link to `kNotVisible` is still **in code**. Check: `footFeedback` in the status is greater than zero, and the difference is around 64 while walking as well.

This is unnoticeable in normal play: Z freezes only for someone nobody sees.

## 5. World camera `AE#36609`

Detour: while a thread updates an interior actor (`Portal::ActorScope`), the function returns the portal camera. It is positioned at the player's eye transformed through the door, even when the door is off-screen (`CarryCamera`). The scope is installed on `Character::Update`, `AE#37350`, and `ModifyAnimationUpdateData`.

| Reader | What it does | Status |
|---|---|---|
| `AE#19384` | Fade of animated references | Transformed since 0.8.19 by a separate hook, outside actor scope |
| `AE#38054` | Animation update data | Within scope |
| `AE#37507` via `AE#105490` | "Visible" flag for the controller (`moveData + 0x20`). Only an optimization for stationary actors | Within scope |
| `AE#37277` via `AE#105490` | Reduced update | Bypassed since 0.8.14 |
| `AE#37286`, `AE#37361` | Actor update, not analyzed | Within scope |
| `AE#37690` | Hit on an actor, called from `AE#36991`, `AE#38586` | Only if it runs within scope |
| `AE#38308` | Navmesh movement, threshold `fVisibleNavmeshMoveDist` 4096 and `fNotVisibleNavmeshMoveDist` 2048 | Disabled in normal play: `bUseNavMeshForMovement=0` in `Skyrim_Default.ini` |
| `AE#43851`, `AE#44213` | Explosions and projectiles | Outside scope, needed for step 5 |
| `AE#32179` | Sky | Not needed |

## 6. Distance to the player

**`AE#19823` and `AE#19824`** — `TESObjectREFR::GetDistance` and its square, 10 and 98 call sites. For references in different worlds, both return `FLT_MAX`. Now, between the player and an interior reference, they calculate distance through the door. **Risk:** logic that previously saw "infinitely far away" now sees the player nearby. For example, an NPC near the door may speak to the player through the portal. If behavior is strange, use `bDistanceThroughDoor=0`.

Readers to which the player is passed explicitly: `AE#25063`, `AE#37284`, `AE#37627`, `AE#37896`, `AE#38009`, `AE#39219`, `AE#39632`, `AE#39637`, `AE#39648`, `AE#39652`, `AE#40435`, `AE#40847`, `AE#41334`, `AE#41348`, `AE#41402`, `AE#41403`, `AE#41442`.

**Direct reads not yet transformed.** Groups from the appendix:

| Group | Functions | Needed by the portal |
|---|---|---|
| AI and packages | `AE#39322` (six reads, from `AE#37800`–`AE#37805`), `AE#39648`, `AE#39391`, `AE#39673`, `AE#37618` | Yes, analyze using F3 data |
| Cells | `AE#18889`, `AE#32089` | Check: updates the player's interior |
| Actor enumeration around a point | `AE#41470`–`AE#41474` (magic, effects), `AE#40807`, `AE#40817`, `AE#40818` | Step 5 |
| Projectiles and explosions | `AE#43869`, `AE#44217` | Step 5 |
| Camera and crosshair | `AE#50722`, `AE#50724`, `AE#33120`, `AE#21550` (VATS) | No |
| Interface | `AE#47314`, `AE#51671`, `AE#51676`, `AE#51728`, `AE#51744`, `AE#53111` (compass, map, enemy health) | No |
| Magic effects | `AE#34351`, `AE#34355` (`GuideEffect`, Clairvoyance) | No |
| Fast travel | `AE#40445` | No |
| Sky, weather | `AE#32164`, `AE#32179`, `AE#32217`, `AE#32218` | No |
| Effects near the player | `AE#15203`, `AE#36216`, `AE#36661`, `AE#17927`, `AE#22643`, `AE#35639`, `AE#35651`, `AE#36581`, `AE#40725`, `AE#40821`, `AE#40826`, `AE#40829`, `AE#55494`, `AE#56860` | Not analyzed |

The player position cannot be replaced globally: street NPCs read it concurrently, and the pointer is an ordinary variable, not a function. Therefore, such locations are transformed one at a time.

## 7. Sound

Transformed at a single point: `BSXAudio2GameSound::SetEmitterPositionImpl` (vfunc `0x0F`, `AE#68007`). The final position of every sound arrives here, and the function itself calculates `listenerDistance`. Sound within the interior bounds is transformed into street coordinates. Music, 2D sounds, and sounds without a position are untouched. **In code**, not tested in the game.

Limitations:
- sound passes through the tavern walls, not only through the door: attenuation is based only on distance;
- the game may decline to start a sound even earlier, for example if the check is in AI logic;
- the bounds are the interior sphere: in a world where interior coordinates overlap the street, a street sound may be transformed by mistake. In Whiterun, 27,000 units separate them.

Sound settings: `fMaxHighlightRadius:Audio` 250 (`AE#35124`, `AE#35127`), `fNonHighlightSpeechAtten:Audio` 12, `fDeathSoundMaxDistance` 1000 (`AE#37896`).

Run 0.8.24: the transformation works. The bard song was audible outside (`carried` on lines in the probe for 25–38 s each), but only from the beginning of a new song. Why the song stops and starts over on every door transition is covered in `docs/FINDINGS.md`, run 0.8.24.

**Subtitles (0.8.25).** They are displayed by the subtitle manager:
- every frame, `AE#52638` writes the squared distance to `SubtitleInfo + 0x18`: `AE#18517` from the speaker's "world position" to the player position. World position (a point and a space: interior cell or world) is supplied by `AE#19784`;
- `AE#52637` shows only subtitles closer than `fMaxSubtitleDistance:Interface` (1250).

A speaker in the interior is in another space, so their subtitles were not visible outdoors. The `AE#19784` call for the speaker (`AE#52638 + 0x122`) is hooked: a speaker in the portal interior is transformed through the door into the player's space (`[Portal] bSubtitlesThroughDoor`).

## 8. F3 capture, run 0.8.24

Two 3 s captures: outside at the door with the portal (`logs/run-0.8.24/CellLoadResearch.access-1.txt`, 561,000 accesses) and inside (`access-2.txt`, 111,000). 174 frames in 3 s.

**Player position outside**, most frequent readers:

| Accesses | Where | What it is |
|---:|---|---|
| 64,469 + 35,857 | `AE#19824` (our detour) from `AE#39637 + 0x119` and `AE#39632` | Squared distance to the player in full NPC update (`AE#37347` → `AE#39108` → `AE#39637`): about 570 per frame |
| 10,017 | Our `DistanceThroughDoor` | Same call flow |
| 4,176 | `AE#18889 + 0x1BB` | Per-frame update of the attached interior (24 per frame) |
| 1,044 | `AE#36661 + 0xFE` | One of the `kNotVisible` readers (section 4) |
| 656 | `AE#19784` from `AE#42742` | World position for actor-list processing |
| 376 | `AE#19823` from `AE#21405` | `GetDistance` from a script function |

The remaining locations ran once per frame (174) or less often. Of the 149 direct reads in section 6, about 30 triggered in 3 s.

**World camera outside:** 364,793 accesses from `AE#19384 + 0xAC` (fade of animated references, already transformed in 0.8.19), 16,878 from `AE#31754` (sky), 8,874 from `AE#105490` (`BSFadeNode` fade distance), 4,524 from `AE#38308` (navmesh, disabled in `Skyrim.ini`).

Conclusion: there are few frequent uncovered checks. The main one is `AE#39637` with about 570 calls per frame, and it already goes through `GetDistance`. The next candidates are `AE#18889` and `AE#36661`.

## 9. Next steps

1. Using F3 data, analyze `AE#18889 + 0x1BB` and `AE#36661 + 0xFE`: what they do with the player position.
2. Analyze `AE#41381`, `AE#41246`, `AE#37286`, `AE#37361`.
3. Replace "always visible" with a portal-camera frustum check once class separation exists: currently interior NPCs are considered visible even if the door is off-screen.
