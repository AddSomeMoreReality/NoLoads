# T17. Portal picture: distant haze, smoke without the hearth in frame, adaptation anchor

Stage 3. Branch `exp/T17-portal-picture`. Code: portal rendering (`PortalGrade.cpp`, `PortalImage.cpp`, `PortalEffects.cpp`, `PortalRender.cpp`). Run: yes. Leftovers of T12 and T13 after runs 0.8.29–0.8.30. Better before T27: T27 will rework the pass for any camera.

## What is known

- **Stage 2 (T12) matches the inside view up close** (user, 0.8.29–0.8.30): fire, light, brightness with `iQuadFogModel=1`. No brightness flicker, rim, or overexposure at the door. Since 0.8.31 model 1 is the default.
- **Interior fog comes from the door plane** (0.8.30): `max(z − g_eyeDistance, 0)` in the composite, the effect fog's near/far shifted by `g_eyeDistance`. The distant haze is gone, but not entirely.
- **Distant haze** (user, 0.8.30 a and b): the farther from the door, the brighter and more "washed out" the portal, while nearby walls are darker. Coordinator's measurement: the door was found in `main-N` by correlation with `graded-N`, fitting `main = α·graded + β` per channel (r² > 0.9 with the door fully in frame):

  | distance to door, units (≈ m) | α (measured) | α model 1 = w(1−f) | α model 2 | β (measured, R G B) | β fog per model, w·f·color |
  |---|---|---|---|---|---|
  | 992 (14) | 0.40 | 0.46 | 0.21 | 0.08 0.09 0.09 | ≈ 0.06 |
  | 1289 (18) | 0.37 | 0.43 | 0.18 | 0.22 0.23 0.22 | ≈ 0.07 |
  | 1497 (21) | 0.36 | 0.41 | 0.16 | 0.22 0.23 0.23 | ≈ 0.07 |
  | 2280 (33) | 0.30 | 0.34 | 0.11 | 0.32 0.34 0.32 | ≈ 0.08 |

  - α ≈ 0.88 × model 1, model 2 is wrong (twice as bright in the distance). This is already fixed.
  - **β grows with distance and is nearly gray**, 2–3 times the street fog (fog color is bluish, 0.13–0.20). Source unknown. Candidates:
    - the street composite's SSR: it was raining during the run, wet surfaces reflect more, and the SSR mask on the quad may be garbage;
    - effect shader fog with a different color or distance than `BSFogProperty`;
    - depth or normals the quad writes into the frame targets.
  - The measurement script (user has numpy 2.1): `C:\temp\TEMP\claude\e--Games-Skyrim-ModCreation-My-attempts-NoLoads-CellLoadResearch\743dbd9b-dbf3-4ab3-9d3b-95be6732c2fe\scratchpad\r029\fit.py`. Screenshots — `logs/run-0.8.30-a`, `logs/run-0.8.30-b`.
- **Smoke disappears when the hearth is not visible through the portal** (user, 0.8.30): shifting less than a meter, the smoke is either drawn in full or gone entirely. Hypothesis: the smoke particle system's bound sits at the emitter, and pass culling discards all of the smoke when the hearth is outside the visible part of the door. **A control is needed:** the same inside the tavern without the portal (`CLAUDE.md`, section 7: game behavior must be proven by a control).
- **Interior adaptation anchor** (`interiorAdapt`) — a snapshot of the last seconds before exiting, usually near the entrance (0.057–0.071, 0.27 at the hearth). It does not noticeably affect brightness under model 1 (0.8.30-b), but depends on where the player stood.
- **Not done from T13:** shimmer (group 5, `kRefraction`, the pass does not draw it), pass camera as the player's view through the door with an oblique near plane (T13, plan, item 4; intersects with T27).

- **Per code (T44, 09.11.2026, `docs/RENDER_VIEW_MAP.md`, 7):** candidates for the haze — the street shadow mask on interior shapes (`AE#107297+0x9B6…+0xA65`, `renderTargets[0x12]`), volumetric light (`AE#107023`, switch `AE#414913`), the quad's SSR; fog is taken from `SSN[State.sceneGraph]->fogProperty` (`AE#105637`). Smoke — the particle system bound when culling by the portal frustum. All are **hypotheses**, verify with switches.
- **Particles per code** (`results/T42-T44-batches.md`, "T44, batch 2" and "batch 3"): the bound and simulation do not depend on visibility. Candidates for smoke disappearing: culling by the portal frustum, system reset on time going backward (`AE#74498`), particle sorting from the player camera position (`AE#411319`–`AE#411321`, snapshot once per frame). Log `lastUpdate`, smoke bound radii in the pass, `+0x144` of the fade node before and after the pass. The pass must set and restore the dispatch camera `AE#414580`.

## To do

1. **Haze:** find what adds β. Compare clear weather and rain, F11 at 350, 700, 1500 units. Check SSR (mask on the quad, disabling SSR in memory), effect shader fog color and distance from the bytecode (`AE#107524`, the quad's shader № 746/2395 in T12). The fix — in the quad model (`iQuadFogModel`) or in what the quad writes into the targets.
2. **Smoke:** control inside, then the particle system bounds in the pass (`worldBound` after `UpdateSelectedDownwardPass`, `BSCullingProcess` culling). Fix under an INI switch.
3. **Adaptation anchor:** average brightness of the portal picture itself with GPU reduction and temporal smoothing, like the game (`eyeAdaptSpeed`). Without stalling the GPU.
4. **5.14:** the solutions must not read the global player camera (item 4) and must not interfere with mirrors (item 5).

## How to verify in the game

1. Clear weather and rain: F11 at the door, at 700 and 1500 units (≈ 10 and 20 m). Distant haze same as nearby walls.
2. Stand so the hearth is behind the door edge but the smoke is in the doorway: smoke visible, not flickering. Control inside.
3. Stand at the hearth and at the entrance, exit: portal brightness is the same.

## Conflicts

T27 (pass for any camera), T24 (effect light). With stage 2 — little: `PortalRender.cpp` touches T29.
