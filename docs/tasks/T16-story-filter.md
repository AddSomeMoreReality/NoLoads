# T16. Reactions to portal entry: Story Manager filter by tags

Stage 2. Branch `task/T16-story-filter`, worktree `..\CLR-T16`. Code: new `src/StoryFilter.*` (own hooks, one line in `Hooks::Install`, T28 rule in `Hooks.h`), `Settings`, INI. Test run: yes. Start after T28. Run 0.8.30 confirmed: on entry the song breaks after 1–2 s.

**User's decisions.**
- 04.11.2026: first see R8 in-game, then a tag-based policy (T30).
- 05.11.2026: the T30 policy is accepted (`docs/tasks/results/T30.md`, `docs/LOCATION_TRIGGERS.md`, 4.5–4.9): only `BardSongStarter` is suppressed, and only while a song is playing. Greetings (`WIGreeting`, `WITavern`), radiant encounters, one-off vignettes, and story remain.

The policy is an INI string. Hence a single build: T30 by default, R8 by replacing one string. The trace logs both decisions for every `CLOC` start.

## What is known (T15, T30)

- **The bard song on entry is broken by `BardSongStarter`.** Story Manager starts it on `CLOC`; after +1.5 s `StopAllSongs`, after +2.2 s a new song (run 0.8.27).
- **`CLOC` is born in one place:** `PlayerCharacter::SetParentCell` (`AE#40466`, `PlayerCharacter` vtable slot `0x98`). The event is assembled at `+0x105` (`AE#32288`), enqueued at `+0x11c` (`AE#32359`); at `+0x154` `OnLocationChange` is sent synchronously.
- **The quest is selected later**, when the queue is processed (`AE#40438` → `AE#32484` → `AE#32483`): `BGSStoryManagerQuestFinder` vfunc 3 (`AE#32430`, `VTABLE_BGSStoryManagerQuestFinder`, `RELOCATION_ID(256099, 205026)`). Return 0 — quest started, 2 — not started. The event is `finder+0x48`.
- **T30 tags** (criteria — `LOCATION_TRIGGERS.md`, 4.5; what exists in memory — 4.6, from NG headers, not verified in-game): `story`, `once`, `scene`, `greets`, `encounter`, `technical`, `restarts`, `placeKind`, `placeSpecific`. `restarts` is derived: `technical ∧ placeKind ∧ ¬placeSpecific ∧ ¬once ∧ ¬story ∧ ¬encounter`.
- **Policies in the same tags:**
  - T30: `restarts & !story & !greets & !once` and "already running";
  - R8: `!story & !once & !placeSpecific & (placeKind | encounter)` (matches the R8 from T15 on 186 quests).
- **"Already running"** (`LOCATION_TRIGGERS.md`, 4.7): the controller quest (a starter script property, `BardSongStarter` → `BardSongs`) has a scene playing (`BGSScene::isPlaying`), whose actor is in the destination cell. Without this check, in a world without loading screens a silent bard would never sing.
- **Do not do:** substituting the old location in the event (breaks `MS11Kicker`, `MGR01Trigger`, `T03PostQuest`) and suppressing `OnLocationChange` (49 of 60 scripts drive quests).

## To do

1. **`src/StoryFilter.h/.cpp`.** Hook vfunc `0x98` `VTABLE_PlayerCharacter[0]`:
   - before the original, ask `Portal::ObservedEntry(oldCell, newCell)`: is there a portal whose near side is `old` and far side `new`, and which rendered a picture for ≥ 1 s within the last `fTagSeconds`;
   - if yes — a tag `{oldLocation, newLocation, newCell, portal, t}` in an 8-element ring. FormIDs only, no pointers to exterior cells (`CLAUDE.md`, section 7).
2. **Hook vfunc 3 `BGSStoryManagerQuestFinder`.** Event `*(BGSStoryEvent**)(this + 0x48)`, find the `CLOC` index once via `registeredEvents[i].uniqueID`. If the event's locations (`members[1]`, `members[2]`) match a fresh tag and the policy from the quest's mask says "suppress" — return 2, otherwise the original.
3. **Tag cache** at `kDataLoaded`: for each quest in the `CLOC` nodes — the T30 tag mask (walking nodes with `parent`, `TESQuest::data`, `objectives`, stages with a journal, scenes, aliases, conditions, `TESTopic::ownerQuest` for lines). Log each quest: EditorID, mask, R8 and T30 decisions. Expected in vanilla: R8 — 17, T30 — 1 (`BardSongStarter`). Compare against the section 12 table of `LOCATION_TRIGGERS.md`; discrepancies go into the result.
4. **Policy from INI** — a small expression parser (tag names, `!`, `&`, `|`, parentheses) run once at settings load; the hook only evaluates against the mask.
5. **"Already running".** The controller — from the starter's script property via VM on first access (if readable), otherwise from `sRestartsController`. The hook's check touches only scene fields and the actor's cell FormID, no locks. If the controller cannot be determined — do not suppress and log the reason in the trace.
6. **INI `[Story]`** (comments on separate lines), per `LOCATION_TRIGGERS.md`, 4.7: `bSuppressOnPortalEntry=1` (0 — game behavior), `sSuppressIf=restarts & !story & !greets & !once`, `bRestartsOnlyIfPlaying=1`, `sRestartsController=`, `sForceSuppress=`, `sForceKeep=`, `sTagOverride=`, `fTagSeconds=5`, `bTraceStory=1`.
7. **Trace:** `story.tag` (entry tag), `story.skip`, `story.start` — all starts on `CLOC`: EditorID, mask, R8 and T30 decisions, "already running", thread id.
8. **`Portal::ObservedEntry`** — a portal function, not a global check: one portal now, a registry in T29 (5.14, item 1). The rule does not split transitions into "street/interior", works on entry and exit, for any cell pairs (item 2).

## Safety

- Which thread processes the Story Manager queue (`AE#40438`) is unknown: the tag ring is lock-free (atomic indices), the hook only compares FormIDs and the mask.
- `ObservedEntry` reads the portal state: if the vfunc 3 hook runs off the main thread, copy the state for it into atomic fields on the `Update` tick.
- Reading VM properties — not in the Story Manager hook if it takes VM locks: do it in advance, on the main thread (at `kDataLoaded`, `kPostLoadGame`, or a tick), the result — a FormID in an atomic field.
- New offsets (`finder+0x48`, the `BGSStoryEvent` layout, `AE#40466+0x105/+0x11c/+0x154`, `TESQuest::formEditorID +0x230`, `BGSScene::isPlaying +0xB0`, `TESTopic::ownerQuest +0x40`) — into `docs/PORTABILITY.md`.
- Game data is not modified (`CLAUDE.md`, 5.14, item 7): hooks only.

## How to verify in-game

1. T30 policy (default), F8. In the Bannered Mare wait for a song, exit, enter through the portal: `story.skip BardSongStarter`, the song does not break (no `StopAllSongs` after +1.5 s). The innkeeper approaches (`story.start WITavern`).
2. Exit, wait for the song to end, when the bard is silent, enter: `story.start BardSongStarter` ("not running"), the bard starts a song.
3. Enter a shop or a house through the portal, if a portal is placed there: `story.start WIGreeting`, the owner greets.
4. R8: in INI `sSuppressIf=!story & !once & !placeSpecific & (placeKind | encounter)`, `bRestartsOnlyIfPlaying=0`, restart the game, item 1: `story.skip` for `BardSongStarter` and `WITavern`, the innkeeper does not approach.
5. Control: F8 disabled, same entry — `story.start BardSongStarter`, the song changes as in 0.8.27.
6. From the thread id in `story.start`, record where `AE#40438` runs.

## Conflicts

`Hooks::Install` (one line), `Settings.*`, INI, `Portal.h`. With T20 — if its entry-without-unloading changes the `SetParentCell` call: a seamless transition must call it exactly once per transition.
