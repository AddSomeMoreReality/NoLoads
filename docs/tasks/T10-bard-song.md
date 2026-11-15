# T10. Bard song cuts off during transition

Branch `exp/T10-bard-song`. Code: instrumentation and a hook for the fix. A game run is required. Research (item 1, before coding) can start before T02.

## User constraint

Quests, scenes, scripts and forms must not be changed (`CLAUDE.md`, 5.14, item 7). The fix must only hook engine handlers. The replacement of the `BardSongStarter` fragment from 0.8.25 was removed: it did not help and violated the rule.

## What is known

- **Where.** Tavern `WhiterunBanneredMare [0001605E]`, bard Mikael `0001A671`.
- **Quests:**
  - `BardSongs` `00074A55` — songs with lyrics;
  - `BardSongsInstrumental` `0006E53F`;
  - `BardAudienceQuest` `001062A7`, audience scene `00108ED5`;
  - `BardSongStarter` `000BE159`. Story Manager starts it on the `CLOC` event (player location change). The fragment calls `BardSongsScript.PlaySongRequest`. After 1 s, `PlaySong(ChangeSettings=True)` calls `StopAllSongs()` and selects a new song.
- **Script sources:** `E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\Source\Scripts` (`BardSongsScript.psc`, `QF_BardSongsDrinkingSong01_000BE159.psc`).
- **Run 0.8.25** (`logs/run-0.8.25/`; `src/Probe.cpp` writes the `scene.change` record):
  - requested song `000D0843` (`BardSongs`) started at 182.6 s, player exited at 187.1, song stopped at 188.7. No new song started, so `PlaySongRequest` was not what stopped it;
  - instrumental scenes change on every transition. Exit at 142.4 → `00095889` started at 142.7, `00106007` stopped at 143.7. Exit at 242.1 → `00106007` started at 244.5, `00095889` stopped at 245.5. This is `BardSongStarter`;
  - the portal attaches the interior ~1.7 s after exit. Until then, interior NPCs are in middleHigh: `interior.sim` record, `action` = `promote`, `levelsBefore`.
- **Run 0.8.24** (`logs/run-0.8.24/`): on every transition the scene stopped after ~2.2 s and a new one started 1 s later.
- **Sound outside (0.8.25, 0.8.26).** `soundsCarried` in the status (every 10 s) increases by 170–430 only during the first ~20 s after an instrumental scene starts (`00106007`, `00095889`; in 0.8.26, `001069C2` — 55 in 3 s). Afterwards, with `playing=1`, it is 0–3. `00107511` in 0.8.26 produced no sound 12–22 s after starting. The instrument sound appears to be shorter than the scene or to have pauses: compare from inside without the portal.
- **Subtitles through the door** (hook `AE#52638 + 0x122`, `Portal::SpeakerThroughDoor`) have not been verified: `subtitlesCarried` = 0; only instrumental scenes played outside.

## Tasks

1. **Find what stops the scene.** Candidates:
   - a scene phase or action condition comparing the player and an NPC: `GetInSameCell`, `GetDistance`, `IsInLocation`, `GetInCurrentLocation`. Find the scene-condition evaluation function and record which condition failed;
   - `BardSongsScript`: `OnUpdate`, player-location checks;
   - the bard's package is interrupted while he spends 1.7 s in middleHigh.

   Tool — detour the `BGSScene` scene-stop function:
   - find the ID using `tools/name_functions.py` (strings, RTTI, callers) and dumpbin (`CLAUDE.md`, section 7);
   - record the stack as `ActorJumps::BeforeSetPosition` does in `src/ActorJumps.cpp`;
   - if called from Papyrus, record the Papyrus stack as well;
   - write a `scene.stop` trace record.
2. **Propose a hook-based fix** without modifying data. Directions:
   - a condition comparing the player with an NPC in the portal interior returns the through-door result. This is already done for `GetDistance` (`bDistanceThroughDoor`, 0.8.24);
   - filter the `CLOC` event for `BardSongStarter` if the bard in the new location is already singing. Assess the impact on other quests listening for `CLOC`.
3. **Verify subtitles:** a song with lyrics outside and tavern NPC dialogue (scene `WITavern` `000DEE96`).

## How to test in game

F8 enabled. Request a song, exit, listen outside and watch the subtitles, then enter again. Repeat with F8 disabled (control).

## Conflicts

Minor, in `Hooks.cpp`. With T11 if the fix touches the bard's process level.
