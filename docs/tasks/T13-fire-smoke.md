# T13. Fire is smaller and smoke is absent through the portal

Branch `exp/T13-fire-smoke`. Code: portal particles. A game run is required. Log analysis (item 1) can be done before T02.

## What is known

- **Run 0.8.25, according to the user.** After F2 the color matched (this is T12), but from inside the hearth fire is larger and smoke is visible above it. There is no smoke through the portal.
- **Already done for fire** (`docs/FEATURES.md`, 5.12):
  - interior controllers are updated (`iControllerUpdate`);
  - fade and camera distance are carried through the door (`AE#19384 + 0xE4`). From these, `BSPSysLODModifier` (`AE#108480`) sets particle emission `+0x180` and size `+0x188`;
  - particle budget is removed (`AE#74651`);
  - particle quads face the view (`AE#77463`);
  - master particle systems (`BSMasterParticleSystem`) are drawn in a second traversal.

  Soft particles have no effect (run 0.8.24).
- **Data in the portal status** (`interior.sim` `status`, `portal` field):
  - `watchedEffects`: `visits`, `passCalls`, `noPasses`, `fadeDistance`, `emitScale`, `sizeScale`;
  - `hiddenEffects` — interior effects omitted from rendering, with reasons;
  - `fires` — flame particles: size, radius, opacity;
  - `masters`.

  The inside probe (`probe`) has the same `fires` and `masters` for comparison.
- **Logs:** `logs/run-0.8.25/`, `logs/run-0.8.24/`.

## Tasks

1. **Compare the logs** for `fires`, `masters` and `watchedEffects` inside and through the portal: particle count, size, `emitScale`, `sizeScale`.
2. **Find the hearth smoke:** which node or emitter, which NIF, system type. Whether it appears in `hiddenEffects`, and why.
3. **If the data is insufficient**, add diagnostics (trace records only) and provide a build for a game run.
4. **Fix the cause.**

Hypothesis to test: the smoke may be a system with a different LOD modifier or part of a master particle system. It may also be drawn in a group that the portal pass does not render: `SubmitAccumulator` does not draw group 5 (Grep `Прогон 0.8.21` in `docs/FINDINGS.md`).

## Conflicts

Portal rendering — with T12. After T02 these are separate files.
