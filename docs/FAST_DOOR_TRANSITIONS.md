# Nearly instant door transitions in Skyrim AE: measurements

This document provides the source data for a mod that shortens a loading-door transition from ~3.7 s to ~0.8 s. All figures were measured by the CellLoadResearch research SKSE plugin (versions 0.6.0–0.7.2) in October 2026.

**Conditions:**
- Skyrim Special Edition / Anniversary Edition, runtime **1.6.1170**, SKSE 2.2.6, CommonLibSSE-NG v13.1.0;
- location — Whiterun: Bannered Mare tavern door (`WhiterunBanneredMare`), Belethor's General Goods (`WhiterunBelethorsGeneralGoods`), main gates;
- ~30 transitions across five game launches.

Nothing was tested on SE 1.5.97 or VR.

---

## 1. Summary

| | Game values | With settings from section 3 |
|---|---|---|
| From pressing the door to the image | **3.6–3.8 s** | **0.78–0.87 s** |
| of which cell loading | 0.25–0.36 s | 0.02–0.11 s |
| of which black screen and fade-in | 2.8 s | 0.1 s |

Almost all transition time is not loading, but UI timers and physics warm-up. They are controlled by ordinary game settings (INI). If changed in memory through SKSE, the game picks up the new values immediately, without a restart.

The user visually verified the result:
- entry is nearly instant, the black screen is barely visible;
- items and NPCs behave normally, including on first entry into a cell;
- a ~0.65 s pause remains while the "door animation plays" (section 5).

---

## 2. Components of a door transition

Phases defined through `RE::MenuOpenCloseEvent` menu events and the `TESObjectDOOR::Activate` hook (`TESObjectDOOR` vtable, index `0x37`):

```
player pressed the door (TESObjectDOOR::Activate, called OFF the main thread)
  ── pause 0.645 s (house doors) / 0.507 s (city gates) ──
Fader Menu open                              fade to black begins
  ── fade to black = fNormalDoorFadeSecs (0.4 s) ──
Fader Menu close → Loading Menu open → Fader Menu open
  [one main-thread frame]
  PositionPlayerEvent kPre → cell loading → kPost
  → TES warms up physics: fWarmStartMaxTime (4 s of simulation = 241 Havok steps, 180–330 ms of real time)
  → (exit outside) wait for background IO task queue, up to ~60 ms
  → kFinish
Loading Menu close
  ── hold fMinSecondsForLoadFadeIn (1.5 s) + fade in fFadeToBlackFadeSeconds (~0.9 s) ──
Fader Menu close                             image fully visible
  (+0.25–0.27 s: autosave "on travel", frame hitches for 87–111 ms)
```

Measurement with game values (0.7.1, entering/exiting the Bannered Mare):

| Phase | ms |
|---|---|
| door to fade to black | 644–650 |
| fade to black | 407–409 |
| loading screen | 253–359 |
| fade-in | 2373–2376 |
| **total** | **3688–3782** |

---

## 3. Settings that shorten the transition

All settings except the last are in `RE::INISettingCollection` (`Skyrim.ini` file). The name uses the format `name:Section`.

| Setting | Game | Tested | What it does | Effect |
|---|---|---|---|---|
| `bPreloadLinkedInteriors:General` | 0 | **1** | Game preloads interiors behind loading doors near the player (`CellLoaderTask` with `interior=true`, on an IO thread) | Interior loading through a door 420–640 ms → **18–35 ms** |
| `fWarmStartMaxTime:HAVOK` | 4 | **0** | "Warm start": after moving the player, `TES` synchronously advances the new cell's physics world by this many simulation seconds. Warm-up does not end early | 241 steps, 180–330 ms → **1 step, 1.5–7 ms** |
| `fNormalDoorFadeSecs:General` | 0.4 | **0.05** | Fade-to-black duration after pressing an ordinary loading door | 407 ms → **50 ms** |
| `fAutoDoorFadeSecs:General` | 0.5 | 0.05 | Same for automatic doors | not measured |
| `fMinSecondsForLoadFadeIn:Interface` | 1.5 | **0** | Hold in darkness after the loading screen, before fade-in begins | fade-in 2.37 → 0.9 s |
| `fFadeToBlackFadeSeconds:Interface` | 1 | **0.05** | Duration of the fade-in itself | fade-in 0.9 → **0.05 s** |
| `fTeleportDoorActivateDelayTimer` (`GameSettingCollection`, no section) | 0.6 | 0 | — | **No effect:** pause before fade to black remained 0.645 s |

**Presets tested** (2–4 transitions each, `logs/run-0.7.1`, `run-0.7.2`):

| Preset | Total, ms | door→fade to black | fade to black | loading | fade-in |
|---|---|---|---|---|---|
| game values | 3688–3782 | 644–650 | 407–409 | 253–359 | 2373–2376 |
| warm-up 0 + door fade 0.05 | 3130–3229 | 643–645 | 50 | 25–158 | 2377–2412 |
| + `fMinSecondsForLoadFadeIn` 0 | 1632–1693 | 644–651 | 50–51 | 30–112 | 879–908 |
| + `fFadeToBlackFadeSeconds` 0.05 | **778–872** | 643–649 | 49–51 | 23–114 | 53–73 |

Entry takes 23–31 ms, exit outside — 105–114 ms: on exit, waiting for the IO queue remains (section 5).

### Side effects of warm-up 0
Warm-up lets items placed in the editor "settle" before the player sees them. Tested:
- repeat entries into the tavern — no anomalies;
- **first entry** into Belethor's shop: the game had preloaded the cell but had never advanced its physics world. Items on shelves and NPCs behaved normally.

Not tested:
- cells with corpses and ragdolls;
- hanging and suspended items;
- cold loading without preloading (`coc`, fast travel);
- exteriors with physics objects.

**Important:** every interior cell and every worldspace has its own physics world (`bhkWorld`/`hkpWorld`). Only the world of the player's current location steps. While the player is outside, interior physics is stopped, and vice versa. Warm-up "catches up" the frozen world on entry, so at 0 the world resumes from where it froze.

---

## 4. How to apply this in a mod

### Option A — SKSE plugin, values in memory (tested)
This is what CellLoadResearch did: values are written to `RE::Setting` objects after data loads; INI files are not modified.

```cpp
namespace
{
	void SetFloat(RE::INISettingCollection* a_ini, std::string_view a_name, float a_value)
	{
		if (auto* setting = a_ini->GetSetting(a_name)) {
			setting->data.f = a_value;
		}
	}

	void ApplyFastDoors()
	{
		auto* ini = RE::INISettingCollection::GetSingleton();
		if (!ini) {
			return;
		}
		if (auto* preload = ini->GetSetting("bPreloadLinkedInteriors:General")) {
			preload->data.b = true;
		}
		SetFloat(ini, "fWarmStartMaxTime:HAVOK", 0.0f);
		SetFloat(ini, "fNormalDoorFadeSecs:General", 0.05f);
		SetFloat(ini, "fAutoDoorFadeSecs:General", 0.05f);
		SetFloat(ini, "fMinSecondsForLoadFadeIn:Interface", 0.0f);
		SetFloat(ini, "fFadeToBlackFadeSeconds:Interface", 0.05f);
	}
}

// in the SKSE message handler:
// case SKSE::MessagingInterface::kDataLoaded: ApplyFastDoors(); break;
```

- `RE::INISettingCollection::GetSetting(std::string_view)` searches by the full `name:Section`.
- `GameSettingCollection::GetSetting` takes `const char*`, not `std::string_view`.
- In CellLoadResearch, values were changed during gameplay by a hotkey on the main thread and took effect from the next transition. `bPreloadLinkedInteriors` was enabled on the first player tick after data loaded.
- On 1.6.1170, the `kDataLoaded` message arrives **off the main thread**. Writing a `float` to a setting is safe, but if the mod does anything else, move that work to the main thread.
- Values are not stored in the save. **Not tested** whether they persist after loading another save or exiting to the main menu: in all tests they were set after loading. It is more reliable to set them on both `kDataLoaded` and `kPostLoadGame`.

### Option B — lines in `Skyrim.ini` (not tested)
The settings belong to the `Skyrim.ini` collection, so this should work the same way. **Not tested through the file.**

```ini
[General]
bPreloadLinkedInteriors=1
fNormalDoorFadeSecs=0.05
fAutoDoorFadeSecs=0.05

[HAVOK]
fWarmStartMaxTime=0

[Interface]
fMinSecondsForLoadFadeIn=0
fFadeToBlackFadeSeconds=0.05
```

### Mod recommendations
- Make values configurable, especially `fWarmStartMaxTime`: for example, 0.25–0.5 instead of 0 if items start "settling late". Cost — ~16 physics steps per 1/60 s of warm-up, at 0.7–1.4 ms per step.
- `bPreloadLinkedInteriors=1` is cheap: interiors load on a background IO thread. But the interior buffer has only `uInterior Cell Buffer = 3` slots, and preloading evicts old interiors from it.

---

## 5. What remains unresolved

1. **Pause of 0.645 s (house doors) / 0.507 s (gates) from `Activate` to fade to black.**
   - The user perceives it as the "door animation".
   - Duration is identical for different doors of the same type.
   - `fTeleportDoorActivateDelayTimer = 0` does not affect it; there is no other setting with this value in the dump.
   - The game continues running during this time (frames tick); the player's `queuedTargetLoc` is still empty (`isValid=false` on exit from `Activate`). It probably waits for the door animation or sound to finish.
   - Next step: find what opens `Fader Menu` after `Activate`, and what it waits for.
2. **Autosave "on travel".**
   - 0.25–0.27 s after every loading screen, a frame takes 87–111 ms: this is `kSaveGame`.
   - Can be disabled in game: "Settings → Gameplay → Save on Travel" (`bSaveOnTravel`, `SkyrimPrefs.ini`).
   - The mod can offer this to the player.
3. **Exit outside: up to ~60 ms waiting for the IO queue.**
   - After `kPost`, transition function `AE#40438` calls `AE#75708`, which waits in the kernel. This code is related to `IOManager`/`BSTaskManager`.
   - With warm-up 4, the queue had time to empty during the physics steps.
   - Which tasks are in the queue is unknown.
4. **Worldspace change** (Whiterun gates): cold load 2.15–2.48 s. With the settings from section 3, the player sees the loading-screen image (with text) for this time and then immediately arrives at the destination. The settings do not shorten this time: cells of the other world are not loaded, and when unloading a world the game deletes its cell objects.
5. **`coc` into an interior** reloads it every time (not from the buffer). This does not matter for a door transition.

---

## 6. Engine facts (AE 1.6.1170), useful for development

**Transition event order** (same in ~30 cases): `Loading Menu open → kPre → ActorCell leave → loading → ActorCell enter → ActorLocationChange → CellFullyLoaded (if the cell was not loaded) → kPost → kFinish → Loading Menu close`.
- `kPreUpdatePackages`/`kPostUpdatePackages` phases do not occur.
- Everything from `Loading Menu open` to `close` happens **within one frame** of the main thread.

**Functions (Address Library ID for AE):**

| ID | What it is |
|---|---|
| 13316 | `TES::SetCurrentCell` — called only when entering an interior |
| 13419 | `TES` as `BSTEventSink<PositionPlayerEvent>` (`TES+0x68`, vfunc 1). Starts warm-up on `kPost` |
| 13676 → 77921 | warm-up preparation and loop: step = `fMaxTime:HAVOK` (1/60), while sum < `fWarmStartMaxTime` |
| 77851 | `bhkWorld::vfunc[0x32](bhkWorld*, int)` — one world step; step comes from globals |
| 61410 | `hkpWorld::stepDeltaTime(float)` |
| 40438 | player transition function; calls `PositionPlayerEvent` dispatch (`AE#41051`) |
| 75708 | wait for background task queue (near `IOManager::vfunc[0x14]` = `AE#75710`) |
| 18541 | `ExteriorCellLoader::QueueCellLoad` |

**Threads:**
- **arrive off the main thread:** `TESObjectDOOR::Activate`, input events, exterior `TESCellFullyLoadedEvent`, SKSE `kDataLoaded` message;
- **game mutations** (teleport, UI, world changes) must be done on the main thread, for example in the `PlayerCharacter::Update` hook (`PlayerCharacter` vtable, index `0xAD`);
- **deferred work:** do not requeue a task from `SKSE::GetTaskInterface()->AddTask` within itself — SKSE runs the queue until it is empty, and the game hangs.

**Pitfalls:**
- Exterior cell objects (`TESObjectCELL`) are deleted when the worldspace unloads. Do not store pointers to them; store FormIDs or coordinates. Interiors are persistent forms.
- **F5/F9 are the game's quicksave and quickload.** Do not use them as hotkeys. F1–F4, F6–F8 are free.
- `<windows.h>` (through spdlog) defines the macros `MAX_PATH`, `min`, `max`, `CP_ACP`.

---

## 7. How to measure it yourself
- **Phases:** timestamps (`steady_clock`) in the `TESObjectDOOR::Activate` hook and in `BSTEventSink<MenuOpenCloseEvent>` for `Fader Menu` and `Loading Menu`. "Image visible" = `Fader Menu close` after `Loading Menu close`.
- **Menu events:** `Fader Menu` arrives from different threads, `Loading Menu` from the main thread.
- **If there is no fade-in at all** (for example, with `fMinSecondsForLoadFadeIn=0` and a short fade-in), `Fader Menu` may already be closed by the time the loading screen closes. Check `RE::UI::IsMenuOpen("Fader Menu")` when `Loading Menu` closes.
- **Physics steps:** detour on `hkpWorld::stepDeltaTime` (`AE#61410`), counter by world pointer.
- **Sources and raw data:**
  - code: `E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch` (`src/Events.cpp` — phases, `src/Overrides.cpp` — setting substitution);
  - traces: `logs/run-0.7.0` … `run-0.7.2`;
  - analysis: `docs/FINDINGS.md` (hypotheses H4 and H13).
