# Game and script reactions to player location changes

Task T15, 04.11.2026. Research only: game data was read and nothing was changed. Sources: `Skyrim.esm`, `Update.esm`, `Dawnguard.esm`, `HearthFires.esm`, `Dragonborn.esm` (FormIDs adjusted to load order 00–04), Papyrus sources from `Data\Source\Scripts`, `SkyrimSE.exe` 1.6.1170 (dumpbin, `tools/name_functions.py`). USSEP 4.3.8a was checked separately (section 4.4). The analysis scripts are in the T15 scratchpad, not in the repository.

Task T30, 04.11.2026: reaction tags and the "suppress or not" policy — sections 4.5–4.9 and 12, the "Tags (T30)" and "T30" columns in section 9, and "Tags (T30)" in section 10 (for completeness: `OnLocationChange` is not suppressed). Scenes, dialogue topics, and quest script properties were also read (the same game files). The analysis scripts are in the T30 scratchpad.

Notation: `L1` — the old location in the event, `L2` — the new one; `in X` — the player's current location is `X` or is nested within it (`GetInCurrentLoc`); `L2∋K` — `L2` has keyword `K`; `here∋K` — `LocationHasKeyword` on the player.

## 1. Mechanisms

| Mechanism | What it reports | Count in the data | Where it originates in the engine | What it means for the portal |
|---|---|---|---|---|
| Story Manager, `CLOC` event | the player changed location: `R1` = player, `L1`, `L2` | 186 quests in `CLOC` nodes (172 with `ENAM=CLOC`, another 14 with empty `ENAM`); 657 SM nodes in total | only `PlayerCharacter::SetParentCell` (`AE#40466`, vfunc `0x98`) | the main source of "entry reactions", including bard songs |
| Papyrus `OnLocationChange` | the same, for an actor and its aliases | 77 scripts, 60 attached, 144 attachments: 101 quest aliases, 38 `NPC_`, 5 `ACHR` | for the player — the same `AE#40466` call; for NPCs — `AE#19799` (transfer through a door), `AE#37313`, `AE#20541`, `AE#20542` | almost all advance story quests; must not be suppressed (section 6) |
| Papyrus `OnStoryChangeLocation` | a quest was started by a `CLOC` event | 2 scripts (`CWMapTableSceneScript`, `dunTalkingStoneChangeLocationScript`), 5 quests | dispatcher `AE#53828` (checks against the `CLOC` index) | a consequence of `CLOC`; not hooked separately |
| `OnCellLoad`, `OnCellAttach`/`OnCellDetach`, `OnLoad`/`OnUnload`, `OnAttachedToCell`/`OnDetachedFromCell` | a cell or an object's 3D is loaded/attached | 110 / 184 / 85 / 432 / 116 / 4 / 10 scripts; 1 834 / 4 051 / 1 471 / 4 232 / 1 890 / 466 / 494 attachments | cell attachment, not location | fire when the portal attaches an interior, not on entry. If entry detaches and reattaches the interior, they fire again (this is T20) |
| `OnTriggerEnter`/`OnTriggerLeave` | the player entered a volume | 611 / 140 scripts; 2 713 volumes with `OnTriggerEnter`, including 608 within 512 units of a load door | physics (Havok phantoms) | work as in vanilla while the player physically passes through the volume (section 7) |
| `CTDA` conditions | check the current location when evaluated | `GetInCurrentLoc` 1 590, `GetInCell` 1 286, `GetKeywordDataForLocation` 783, `LocationHasKeyword` 752, etc. (section 8) | evaluated on demand | do not generate events; read the player's `currentLocation`, which the same `AE#40466` changes |

There is no other Story Manager event for location. The game calls registration function `AE#32348` 31 times: 23 events in `AE#5163`…`AE#5220`, plus 8 for crimes and relationships in `AE#32284`…`AE#32323`. Only `CLOC` concerns location changes (`AE#5197`: members `Actor`, `Old Location`, `New Location`).

## 2. `CLOC` path in the engine (1.6.1170)

1. **`PlayerCharacter::SetParentCell(newCell)`** — `AE#40466`, `PlayerCharacter` vtable slot `0x98` (in NG `TESObjectREFR::SetParentCell`, `// 98`). High confidence (disassembly).
   - `+0x29`: exit if the cell has not changed.
   - `+0xe3`: `TESObjectCELL::GetLocation` (`AE#18905`, in NG `RELOCATION_ID(18474, 18905)`) — the new location.
   - `+0xeb`: comparison with the player's `currentLocation` (`[player+0xAD0]` on 1.6.1170; in NG `GetPlayerRuntimeData().currentLocation`). If equal — neither `CLOC` nor `OnLocationChange`.
   - `+0x105`: `AE#32288` builds event `{player handle, BGSLocation* old, BGSLocation* new}`. There is no other call to `AE#32288`.
   - `+0x10a`…`+0x11c`: `BGSStoryEventManager::GetSingleton` (`AE#22790`) and `AddEvent_Impl` (`AE#32359`, in NG `RELOCATION_ID(31576, 32359)`) with the index from global `AE#380069` (written by registration `AE#5197+0x188`).
   - `+0x130`…`+0x154`: `ScriptEventSourceHolder` (`AE#14298`) `+0x108` → `AE#20039` synchronously dispatches `TESActorLocationChangeEvent {player, old, new}`.
   - `+0x19b`: `currentLocation = new`.
2. **`AddEvent_Impl` only queues the event** in `BGSStoryEventManager::events` (`BGSStoryEvent`, 0x38 bytes: `+0x00` id, `+0x04` type index, `+0x08…` members; for `CLOC`: `[0]` player handle, `[1]` old `BGSLocation*`, `[2]` new). Queue processing is deferred: `AE#40438+0x839` → `AE#32484` → `AE#32483` (event loop). Medium confidence: the thread from which `AE#40438` is called has not been checked.
3. **Quest selection** — the `BGSStoryManagerQuestFinder` visitor (vtable in NG `VTABLE_BGSStoryManagerQuestFinder`, `RELOCATION_ID(256099, 205026)`; created in `AE#32370`, the `BGSStoryManager` constructor):
   - slot 1 `VisitBranchNode` — `AE#32428`, slot 2 `VisitQuestNode` — `AE#32429` (node conditions, "random" and "shared event" flags);
   - slot 3 **`VisitQuest(TESQuest&)` — `AE#32430`**: checks `TESQuest::storyManagerConditions` (`+0x110`), then `AE#25048(quest, event = finder+0x48, …)` starts the quest and writes `quest->startEventData`. Returns: 0 — started; 2 — conditions not met (log code 9) or start failed (code `0xA`); 1 — `AE#25048` returned "retry later" (code `0xB`). Medium-high confidence (disassembly; return semantics not verified in game).
4. **`OnLocationChange`**: the `SkyrimVM` receiver (`SkyrimVM[+0x20]` slot 1, `AE#53961`, string `"OnLocationChange"`) dispatches the event to the actor's scripts and aliases.

This explains the timing from run 0.8.27: `CLOC` is queued in the entry frame, the quest starts a frame or two later, the `BardSongStarter` fragment calls `PlaySongRequest`, which calls `StopAllSongs` via `Utility.Wait(1)`. Hence +1.5 s and +2.2 s.

## 3. Doors, gates, and locations

The script associated every load door (`REFR` with `XTEL`) with its cell's location: for an interior — the cell's `XLCN`; for an exterior — the grid cell's `XLCN`, then location `LCEC`, then the world's `XLCN`.

- There are 2 204 door transitions (one direction). The location **does not change** in 1 174: a door to a basement, a cave and its exterior area in one location, city gates. In these cases there is neither `CLOC` nor `OnLocationChange`.
- The location changes in 922 transitions (in another 82, one side has no location): `int→ext` 415, `ext→int` 415, `int→int` 62, `ext→ext` 30.
- **City gates almost never change the location.** Outside the gates of Whiterun, Markarth, Solitude (2 of 3), Riften (2 of 4), and Windhelm (1 of 2), the city location is already the same (`WhiterunLocation`, etc.). `CLOC` fires earlier, when the player walks into the city cells near the walls. Location-changing gates include Riften's doors to Mistveil Keep, the East Empire Company warehouse, Blackreach, parts of Labyrinthian, and Solstheim.
- 55 of 186 quests never fire at any door (only while walking through an exterior or after fast travel).

For the portal, this means the rule is needed on all three kinds of doors, but at gates there is usually nothing to trigger.

## 4. Rule without FormIDs

### 4.1 Categories (manual classification, "Category" column)

- **ambience** (26): bards, greetings, short scenes, `WI*`. On entry through a working portal, the reaction is unnecessary or disruptive.
- **story** (148): quests with journals and objectives, dungeon logic, technical monitors (Companions, Civil War garrisons, Clear Skies, Elder Scrolls). Must fire.
- **unclear** (12): radiant attacks and spawns (vampires, ash spawn), King Olaf's festival, the steward offer, Maul's scene.

### 4.2 Rule R8

"Ambience" determined from quest data and its nodes, without FormIDs:

```
suppress(quest, event) =
    event — CLOC caused by entry through a portal that was showing L2   (section 5)
  ∧ quest type (QUST DNAM) — None or Misc
  ∧ no stage writes to the journal; the quest has no objectives (QOBJ)
  ∧ no RunOnce flag
  ∧ no SM node or quest condition checks quest progress:
        GetStage, GetStageDone, GetQuestRunning, GetQuestCompleted,
        GetVMQuestVariable, GetQuestVariable (except references to the quest itself)
  ∧ no positive check for a specific place:
        GetEventData(GetIsID|IsInList, L1|L2)==1, GetInCurrentLoc(X)==1,
        LocationHasKeyword(K)==1, GetInCell, GetIsEditorLocation, etc.
        (negations such as "not in Sovngarde" are allowed)
  ∧ ( has GetEventData(HasKeyword, L2, K)==1   — "place kind": inn, dwelling, settlement
    ∨ has GetRandomPercent                      — "random encounter" )
```

All these features are available in game: `TESQuest::data` (type, flags), `objectives`, `executedStages`/`waitingStages` (`logEntry`), `storyManagerConditions`, `BGSStoryManagerNodeBase::conditions` with the `parent` chain.

### 4.3 R8 errors on the table

| Classification → rule | suppress | keep |
|---|---|---|
| ambience (26) | 8: `BardSongStarter`, `WITavern`, `WIGreeting`, `WICourier`, `WIChangeLocation01`, `02`, `08`, `09` | 18 |
| story (148) | **1**: `FreeformWinterholdCollegeA` | 147 |
| unclear (12) | 8: `DLC1EclipseAttack1`–`6`, `DLC1RadiantDisguisedVampireLordSpawner`, `BYOHHouseStewardOffer` | 4 |

- **One dangerous error.** `FreeformWinterholdCollegeA` (Nirya's notes errand, `FreeformCollegeANotesScript`) has no journal, objectives, or `RunOnce`; its condition is `L2∋LocTypeHabitation`. If suppressed, it will fire on the next location change.
- **18 "ambience" quests missed:**
  - 14 short scenes with a specific place and a one-shot flag (`Dialogue*`, `CW*MapTableScene`, `MS13*`, `DA07MuseumScenes`). They do not restart anything already running: on entry through a portal, the scene simply starts on entry, as in vanilla;
  - `WIChangeLocation03`, `04`, `05` — have objectives or type Main;
  - `WIThief01` — has a condition on `TG00` progress.
- **8 "unclear" quests are suppressed.** After being skipped, radiant spawns will fire on the next location change.
- **No dungeon quest, errand, monitor, or DLC quest is suppressed.**

I checked other variants on the same table. Story misclassified as ambience / ambience misclassified as story:

| Variant | Story → ambience | Ambience → story |
|---|---|---|
| R0 "no journal" | 68 | 3 |
| R1 R0 + "no quest-progress conditions" | 46 | 13 |
| R2 R1 + "no RunOnce" | 30 | 13 |
| R4 R1 + "no `LocTypeDungeon`" (reference to a specific form) | 12 | 14 |
| R7 R8 without "random encounters" | 1 | 21 |
| R9 R8 + "has scenes, ≤ 3 stages" | 4 | 16 |

Dungeon quests (`dun*`) cannot be distinguished from ambience by their journals: most have none. Their conditions distinguish them: a specific place (`in X`) and `here∋LocTypeDungeon`.

### 4.4 USSEP 4.3.8a

- USSEP overrides 60 of 186 quests and 19 `CLOC` nodes. It adds conditions to 14 nodes: `RiverwoodKidsSceneNode`, `DLC1VQ00Node`, `MS05KingOlafsFestivalNode`, `C00LocationMonitoringHardcoreNode`, `SkyHavenTempleNode`, `FreeformOldHroldanNode`, and eight dungeon nodes.
- It changes flags on three quests: `dunHighGateRuinsQST` and `dunRebelsCairnQST` gain `RunOnce`; `dunDarklightQST` loses `AllowRepeatedStages`. It changes no quest's type or objectives. These three quests are already "story".
- Of the eight "ambience" quests suppressed by R8, USSEP touches only `BardSongStarter` (`DNAM`, `FNAM`, `VMAD`: alias `Lurbuk` becomes Optional). Its type, flags, journal, and conditions remain the same.
- **Conclusion: R8 produces the same result with and without USSEP.**
- USSEP replaces `BardSongsScript`, but the rule operates at the starter-quest level, before the script.

### 4.5 Reaction tags (T30)

Task T30, 04.11.2026. R8 classifies quests using the single "ambience" feature. Instead, each reaction has several tags, and the "suppress or not" decision is derived from them (4.7). Tags for `CLOC` quests are in the "Tags (T30)" column in section 9, tags for `OnLocationChange` scripts are in section 10, and "manual ≠ data-derived" discrepancies are in section 12.

"Manual" classification was done from scripts, scenes, and the T15 categories. "Data-derived" classification uses the criteria below, without FormIDs or EditorIDs. Counts are manual / data-derived, across 186 quests.

| Tag | Meaning | Examples | Data-derived criterion | Count |
|---|---|---|---|---|
| `story` | needed for story progression: journal, objectives, dungeon setup, tracking other quests' progress | `BQ01`, `DA10`, `dunForelhostQST`, `C00LocationMonitoring1`, `MS11Kicker` | `DNAM` type other than None/Misc, or a stage with a journal entry, or `QOBJ` objectives, or a `GetStage*`/`GetQuest*`/`GetVMQuestVariable` condition on another quest | 150 / 119 |
| `once` | fires once per playthrough | `DialogueRiverwoodIntroScene`, `DA07MuseumScenes`, `dunAlftandQST` | `RunOnce` flag or a condition on its own stage or completion (`GetStageDone(self)`, `GetQuestCompleted(self)`) | 65 / 63 |
| `scene` | the quest has scenes | `WIGreeting`, `CW00SolitudeMapTableScene`, `dun*` | the quest has `SCEN` | 66 / 66 |
| `greets` | the reaction addresses the arriving player: an NPC greets, approaches, comments, or hands over something | `WIGreeting`, `WITavern`, `WIChangeLocation01`, `WICourier` | a scene with `BeginOnQuestStart` in which a condition-filled alias (not a unique actor or specified reference) speaks: "local NPC by role" | 12 / 6 |
| `encounter` | random encounter, attack, NPC spawn | `WIChangeLocation09`, `DLC1EclipseAttack1`, `WIThief01` | `GetRandomPercent` in node or quest conditions | 21 / 18 |
| `technical` | technical: no scenes, dialogue, objectives, or journal | `BardSongStarter`, `ClearSkiesChangeLocationQuest`, `CWGarrisonObserver1`, `MS11Kicker` | the quest has no scenes, no dialogue topics with responses (`INFO`), no objectives, and no journal entries | 45 / 55 |
| `restarts` | stops or restarts something already running at the location: a song, music, packages. The main harm on entry through a portal | `BardSongStarter` (via `PlaySongRequest` → `StopAllSongs`) | indirect: `technical ∧ placeKind ∧ ¬placeSpecific ∧ ¬once ∧ ¬story ∧ ¬encounter` — "technical place-kind switch" | 1 / 1 |
| `placeKind` | condition on the kind of new place | `BardSongStarter` (`L2∋LocTypeInn`), `WIGreeting` (`L2∋LocTypeDwelling`), `Favor*` | `GetEventData(HasKeyword, L2, K)==1` | 48 / 48 |
| `placeSpecific` | positive check for a specific place or the player's current location | `dun*`, `DialogueRiftenSS05`, `CW*MapTableScene` | `GetEventData(GetIsID/IsInList, L1/L2)`, `GetInCurrentLoc`, `GetInCell`, `LocationHasKeyword` on the player, `GetIsEditorLocation`, etc. with a positive result | 145 / 145 |

Notes on the criteria:
- **Data-derived `restarts` is indirect.** The direct feature — a call to `StopAllSongs` or `Stop()` on another scene — exists only in script bytecode. The indirect feature describes a quest that, on every entry into this kind of place, does something to the place itself and says nothing to the player. In vanilla, only `BardSongStarter` matches it.
- **Story Manager does not start an already running quest again**: starting a running quest fails (based on `Quest.Start()` behavior; not checked in `AE#25048`). Therefore, only a quest that acts on another, continuously running quest can restart something already running. `BardSongStarter` itself stops immediately (`stop()`), while `BardSongs` manages the song (`StartsEnabled`, 8 song scenes; instrumental scenes are in other quests).
- **Data-derived `greets` detects only scene-based greetings.** It misses approaches through ForceGreet packages (`WICourier`, `WIChangeLocation03/04/05/08`, `BYOHHouseStewardOffer`, `DA14Start`, `DLC2CultistAmbush`). False positives are `DialogueRiftenSS04`, `MS08ArgueQuest` (NPCs talking to each other); both are `once`, so this does not change the policy.
- **Data-derived `story` does not detect dungeons and "kickers" without journals** (46 quests: `dun*`, `MS11Kicker`, `CWGarrisonObserver*`). However, they have `placeSpecific` or lack `placeKind`, so they are not tagged `restarts`.

### 4.6 What is read from game memory at `kDataLoaded`

Tags must be computed for quests from any mod without FormIDs. Fields are based on CommonLibSSE-NG v13.1.0 headers. None of this has been read in game yet.

| What | Source (NG) | For tags | Assessment |
|---|---|---|---|
| Quest type and flags | `TESQuest::data.questType`, `data.flags` (`kRunOnce`) | `story`, `once` | yes, NG fields |
| Quest EditorID | `TESQuest::formEditorID` (`+0x230`, `GetFormEditorID`) | INI keys by EditorID | yes: the quest stores its EditorID, unlike many forms |
| Objectives | `TESQuest::objectives` | `story` | yes |
| Journal entries | `executedStages`, `waitingStages` → `TESQuestStage` → `TESQuestStageItem::logEntry` | `story` | the field exists; whether it is populated before the journal is first read has not been checked |
| SM node and quest conditions | `BGSStoryManagerNodeBase::conditions`, `parent`; `TESQuest::storyManagerConditions` | `story`, `once`, `encounter`, `placeKind`, `placeSpecific` | yes (T15, 4.2) |
| Scenes | `TESQuest::scenes`; `BGSScene::flags`, `actions` (`actorID`, action type) | `scene`, `greets` | yes, NG fields |
| Aliases | `TESQuest::aliases`: `BGSBaseAlias::aliasID`, `fillType`, `flags` | `greets` | yes, NG fields |
| Quest dialogue | iterate all `TESTopic`: `ownerQuest`, `numTopicInfos`, `data.subtype` | `technical` | yes; one pass at `kDataLoaded` |
| Script names and properties | VM: scripts by quest handle, `BSScript::Object` → properties | `restarts` directly (the `BardSongs` property of `BardSongStarter`) | not checked whether available at `kDataLoaded`; more reliable to read on the quest's first start, when the VM is already running |
| Calls inside scripts (`StopAllSongs`) | no: only `.pex` bytecode; no bytecode parsing | `restarts` directly | no → indirect criterion and the `sTagOverride` key |
| Dialogue text | no: loaded from disk on demand | — | not needed |

Conclusion: all nine data-derived tags are computed from quest, node, scene, alias, and topic fields. The VM is needed only to refine `restarts` (4.7).

### 4.7 T30 policy

```
suppress(quest, event) =
    event — CLOC caused by entry through a portal that was showing L2   (section 5)
  ∧ restarts ∧ ¬story ∧ ¬greets ∧ ¬once
  ∧ alreadyRunning(quest, destinationCell)
```

**`alreadyRunning`.** The rule is harmful without this condition. In a world without loading screens, every entry goes through a portal. If the bard was silent (the player has never entered, and only `BardSongStarter` starts the song), skipping the start means the bard will never begin singing.
- Therefore, `restarts` is suppressed only if what the quest restarts is already running.
- From the data: a scene (`BGSScene::isPlaying`) is currently playing in the controller quest referenced by the quest script property (`BardSongStarter` → `BardSongs`), and its actor is in the destination cell.
- If VM properties cannot be read, the controller is specified in INI (`sRestartsController`). Not checked.

Check against the table in section 9 (data-derived tags):

| Rule | Suppresses | Of these, "ambience" / "story" / "unclear" |
|---|---|---|
| R8 (T15) | 17 | 8 / 1 / 8 |
| **T30** | **1: `BardSongStarter`** | 1 / 0 / 0 |
| T30 + encounters (variant, 4.8) | 10: `BardSongStarter`, `DLC1EclipseAttack1`–`6`, `DLC1RadiantDisguisedVampireLordSpawner`, `WIChangeLocation08`, `WIChangeLocation09` | 3 / 0 / 7 |

- Manual tags produce the same T30 result. Classification discrepancies (section 12) do not affect the policy.
- **R8 can be expressed with the same tags:** `!story & !once & !placeSpecific & (placeKind | encounter)`. Across 186 quests, this exactly matches 4.2. Thus the first T16 iteration's cache (R8) can accept tags without rework: a per-quest tag bitmask plus an expression.

**What T30 keeps from what R8 suppressed** (all 16 are R8 errors from the user's perspective or "as in vanilla"):
- `WIGreeting` — greeting from a merchant or homeowner (`greets`, 4.9);
- `WITavern` — the innkeeper greets the player and offers a seat (`greets`);
- `WIChangeLocation01`, `02` — comments on the player's appearance and drawn weapon (`greets`, `encounter`);
- `WIChangeLocation08`, `09`, `WICourier` — street encounters, dragon attack, courier (`encounter`; the courier will come on the next location change anyway);
- `BYOHHouseStewardOffer` — a follower offers to become a steward (`greets`);
- `DLC1EclipseAttack1`–`6`, `DLC1RadiantDisguisedVampireLordSpawner` — radiant attacks (`encounter`);
- `FreeformWinterholdCollegeA` — Nirya's errand (manually `story`: the data shows no story features, but no `technical` either).

T30 also does not suppress the 18 short scenes that R8 missed: one-shot scenes in ordinary quests remain, as the user requested.

**INI layout for T16** (comments on separate lines: `GetPrivateProfileString` does not strip `; ...` after a value):

```ini
[Story]
bSuppressOnPortalEntry=1
; expression over tags: tag names, !, &, |, parentheses
; tags: story once scene greets encounter technical restarts placeKind placeSpecific
sSuppressIf=restarts & !story & !greets & !once
; suppress restarts only if the restarted activity is already running in the destination cell
bRestartsOnlyIfPlaying=1
; controller by EditorID if VM properties cannot be read: Starter:Controller,...
sRestartsController=
; comma-separated EditorIDs: always suppress / never suppress
sForceSuppress=
sForceKeep=
; add or remove a tag by EditorID: Quest:+tag-tag,...
sTagOverride=
fTagSeconds=5
bTraceStory=1
```

- First-iteration R8: `sSuppressIf=!story & !once & !placeSpecific & (placeKind | encounter)`, `bRestartsOnlyIfPlaying=0`.
- At `kDataLoaded`, the log records every `CLOC` quest's tag mask and the policy decision. This shows what is suppressed for quests from other mods.
- EditorID lists are exceptions for third-party mods, not the basis: the decision is data-driven.

**5.14.** The policy examines quest tags, not cell types. It is identical on entry and exit, for `int→ext`, `ext→int`, `ext→ext`, and `int→int` (item 2). The entry marker is tied to its portal and cell pair (section 5), so there can be many portals (item 1). `alreadyRunning` checks the destination cell, not the "interior".

### 4.8 Variants and their effects

- **T30 + encounters** (`sSuppressIf=restarts & !story & !greets & !once | encounter & !story & !greets & !once`) suppresses another 9 radiant encounters on entry through a portal. There is no reason to do this: in vanilla they fire on location changes in the same way, and portal entry restarts nothing in them. This variant is needed only if encounters on every entry feel intrusive.
- **Suppressing repeatable greetings** (`greets & !once`) contradicts the user's decision: merchant and innkeeper greetings must remain.
- **Suppressing all `technical` quests** is dangerous: of the 45 such quests, almost all are technical quest-progress monitors (Companions, Civil War, Clear Skies, Elder Scrolls).

### 4.9 Merchant greeting in vanilla

The greeting on entering a shop has two layers.

1. **`WIGreeting` — a `CLOC` quest.** The T16 filter affects it.
   - Node: the player is not trespassing, `L2∋LocTypeDwelling`. All 30 shop locations (`LocTypeStore`) also have `LocTypeDwelling`.
   - Alias `Merchant`: `JobMerchantFaction`, place `LocTypeStore`, `GetOffersServicesNow`, `GetDetected(player)`, not an innkeeper, not sleeping, not in `WINoGreetingFaction`. Alias `HomeOwner` — the same for homes (`LocTypeHouse`, not a shop).
   - Scene `WIGreetingScene` (`BeginOnQuestStart`, `StopOnQuestEnd`): one line from the merchant or homeowner. The scene's two topics have 12 and 16 lines, with `GetRelationshipRank` and `GetIsID` conditions.
   - The quest is repeatable: no `RunOnce`; the greeting plays on every entry. R8 suppressed it; T30 keeps it (`greets`).
2. **`Hello` lines** (subtype `HELO`) — 6 687 `INFO` records in `Dialogue<city>` quests. Merchants have `GetIsID` and `GetOffersServicesNow` conditions: for example, 50 such `INFO` records in `DialogueMarkarth`, 13 in `DialogueDawnstar`. The NPC says them when the player comes close. `CLOC` is not involved, and the T16 filter does not affect them.

Inns work the same way. `WITavern` (`L2∋LocTypeInn`): alias `Innkeeper` (`JobInnkeeperFaction`), scene `WITavernGreeting` (`BeginOnQuestStart`). The `OnSit` event starts scene `WITavernPlayerSits`. `WITavernPlayerScript` stops the quest when the player leaves (section 6).

## 5. Hook points

| What | Where | How to hook | Portability | Confidence |
|---|---|---|---|---|
| Mark entry through a portal | `PlayerCharacter::SetParentCell` `AE#40466` | vfunc `0x98` in `VTABLE_PlayerCharacter[0]` | NG, SE and AE | high |
| Replace the old location in `CLOC` | `AE#40466+0x105` (`call AE#32288`) or `+0x11c` (`call AE#32359`) | `write_call`, modify `oldLoc` in the event | AE offsets → PORTABILITY | high from the code, but useless (below) |
| Cancel the entire `CLOC` | `AE#40466+0x11c` | `write_call` without calling the original | same | high, but breaks 148 story quests |
| **Do not start one quest** | `BGSStoryManagerQuestFinder::VisitQuest` `AE#32430` | vfunc 3 in `VTABLE_BGSStoryManagerQuestFinder` (`256099`/`205026`); return 2 without calling the original | NG, SE and AE; field `finder+0x48` (event) — check on SE | medium-high |
| Node condition | `VisitQuestNode` `AE#32429` | vfunc 2 | same | medium: a node can start several quests; a quest-level rule is more convenient |
| Player `OnLocationChange` | `AE#40466+0x154` (`call AE#20039`) or `SkyrimVM` slot 1 of subtable `+0x20` (`AE#53961`) | `write_call` / vfunc | AE offset / NG vtable | high, but suppression is unnecessary (section 6) |

**Why not replace `L1`.**
- `CLOC` conditions examine both `L1` and `L2`. `BardSongStarter` checks only `L2∋LocTypeInn`; replacing `L1` cannot disable it.
- Setting `L1 = L2` would break quests with the condition "`L1≠X`, `L2=X`" (`MS11Kicker`, `MGR01Trigger`, `MS10CompanySwapPostQuest`, `T03PostQuest`). These are story quests.
- The decision must be made per quest, which means `VisitQuest`.

**How the hook detects "entry through a portal that was already showing `L2`".**
1. `SetParentCell` hook before the original:
   - takes `old = player->parentCell`, `new = a_cell`;
   - searches the portal registry for an entry with near cell `old` and far cell `new` whose image has already been rendered (for example, ≥ 1 s within the last 5 s);
   - if found, adds marker `{oldLocID, newLocID, portalID, time}` to a small list;
   - markers store FormIDs, not pointers (`CLAUDE.md`, section 7);
   - `GetLocation` for the old and new cells is computed by the same `TESObjectCELL::GetLocation`.
2. `VisitQuest` hook:
   - event `e = *(BGSStoryEvent**)(finder+0x48)`;
   - if `e->index` is the `CLOC` index (found once via `BGSStoryEventManager::registeredEvents[i].uniqueID == "CLOC"`), and `members[1]`/`members[2]` match a marker, apply the rule to the quest: R8 (4.2) in the first T16 iteration, the tag policy (4.7) in the second;
   - the marker is removed after 5 s or on the next `SetParentCell`.
3. Multiple portals (5.14, item 1): the marker is tied to the cell pair and its portal; there is no global "current portal". Two consecutive entries produce two markers. Recursive portals do not matter: `SetParentCell` occurs only on an actual player transition.
4. Any cell pairs (5.14, item 2): the rule examines the quest and node, not "interior/exterior". `int→ext`, `ext→ext`, and `int→int` are handled identically. Exiting through a portal also produces `CLOC`, and the same rule applies: R8 suppresses `WIChangeLocation*` and `WICourier` on exit; the T30 policy keeps them.

**Requirement for future seamless entry (T20, T27).** Player `CLOC` and `OnLocationChange` depend only on `SetParentCell`. If seamless traversal does not call `SetParentCell(newCell)` exactly once, none of the 148 story quests will fire. If it calls it twice (there and back), the reactions will fire twice.

## 6. `OnLocationChange`: no suppression needed

- 60 attached scripts. 49 of them advance quests: `MQ*`, `DLC2MQ*`, `CW*`, `TG*`, `BQPlayerScript`, `JailQuestPlayerScript`, etc. They set stages and stop the quest when the player leaves.
- "Ambience" here only reacts to departure:
  - `WITavernPlayerScript` stops `WITavern`;
  - `WIPlayerScript` (17 quests) stops the encounter;
  - `RemoveSilenceMUS` removes `MUSTavernSilence` on every change. This matters for T23: tavern silence is also removed on exit through a portal.
- They must not be suppressed: after exiting, the player really is in another location.
- Scripts on `NPC_` fire when an NPC passes through a door (`AE#19799`): `dragonActorSCRIPT`, `CartDriverScript`, `DLC2NelothScript`. For NPC transfer through a portal (T11, T20), this means the event will arrive if the engine transfers the NPC normally.

## 7. Trigger markers instead of `CLOC` (assessment)

Idea: suppress all ambience reactions and trigger them with custom dynamic "markers", for example when the player approaches or sees the portal.

- **Benefit.** Reaction timing can be decoupled from `SetParentCell`. A world without loading screens needs this because the "entered" boundary is blurred: the bard starts singing when the player approaches the door, not when crossing it. This also works for many portals at once and for non-player cameras.
- **Cost.**
  - Markers are either forms and references (`PlaceAtMe`, `ACTI` with a primitive), which require an `.esp` — forbidden (5.14, item 7) — and accumulate in the save, or "virtual" zones in C++ that queue an event like `CLOC` themselves via `AddEvent_Impl`.
  - In the second case, conditions `in X` and `here∋K` examine the player's `currentLocation`, which is still outside. Only quests with `L2∋K` or without place conditions can fire early — exactly the R8 set.
  - Required: protection against firing again on entry (the same `VisitQuest` hook), zone-state persistence, and compatibility with mods that expect `CLOC` at the moment of entry.
- **Risks.** Double starts, divergence from vanilla in quests with `RandomPercent` and reset timers (`RNAM`), and maintaining a custom "location model". Early firing is also bad for `BardSongStarter`: the bard is already playing and visible through the portal.
- **Conclusion.** Not needed now. The R8 hook solves the original problem at lower cost. Markers are worth revisiting when seamless entry (T20, T27) no longer calls `SetParentCell` at the door boundary. Then virtual C++ zones will replace that boundary, and R8 will decide what can start early.

## 8. Location `CTDA` conditions (by record type)

| Function | Total | By record type |
|---|---|---|
| `GetInCurrentLoc` | 1590 | INFO: 902, LSCR: 191, QUST: 181, SMQN: 171, PACK: 76, SMBN: 54 |
| `GetInCell` | 1286 | INFO: 777, QUST: 293, PACK: 99, SCEN: 76, SMQN: 37, SMBN: 2 |
| `GetKeywordDataForLocation` | 783 | INFO: 595, SMQN: 154, PACK: 30, QUST: 4 |
| `LocationHasKeyword` | 752 | QUST: 380, INFO: 360, PACK: 5, SMQN: 4, SMBN: 3 |
| `GetIsEditorLocAlias` | 443 | QUST: 441, PACK: 2 |
| `GetInCurrentLocAlias` | 411 | QUST: 307, INFO: 85, PACK: 18, SCEN: 1 |
| `LocAliasIsLocation` | 232 | INFO: 121, QUST: 101, SCEN: 8, PACK: 2 |
| `GetEventData` | 173 | SMQN: 122, QUST: 37, SMBN: 14, SCEN: 0 |
| `GetIsEditorLocation` | 165 | QUST: 84, SMQN: 37, SMBN: 35, PACK: 9 |
| `HasSameEditorLocAsRefAlias` | 147 | QUST: 147 |
| `LocationHasRefType` | 118 | QUST: 118 |
| `GetKeywordDataForCurrentLocation` | 61 | QUST: 49, INFO: 7, SMQN: 5 |
| `LocAliasHasKeyword` | 41 | INFO: 26, QUST: 15 |
| `GetInCurrentLocFormList` | 17 | QUST: 12, SPEL: 5 |
| `IsInSameCurrentLocAsRefAlias` | 7 | QUST: 5, PACK: 2 |
| `IsLocationLoaded` | 2 | INFO: 1, PACK: 1 |
| `GetLocationCleared` | 1 | INFO: 1 |
| `HasSameEditorLocAsRef` | 1 | QUST: 1 |

For `GetEventData`, only `L1`/`L2` checks are counted. `QUST` includes quest alias and dialogue conditions.

`INFO` and `PACK` evaluate them locally using the player's or NPC's current location. While the player is outside looking through the portal, tavern inhabitants do not "see" them in the tavern. On entry, everything switches together with `currentLocation`. This is consistent for the portal and requires no separate change. `LSCR` (191) — loading-screen selection.

## 9. Table: quests started by `CLOC`

Columns:
- "Condition" — SM nodes from the event to the quest and the quest's own Story Manager conditions. If there are several nodes, the conditions are combined.
- "Features" — `DNAM` type, journal entries, `QOBJ` objectives, `RunOnce`, scenes (`SCEN` with this quest), "quest progress" (conditions on other quests' stages), "ENAM empty".
- "Tags (T30)" — manual tags (section 4.5); after "data:" — how tags derived from the data criteria differ (`+` present only in data-derived tags, `−` only in manual tags).
- "R8" — what the rule does on entry through a portal.
- "T30" — what the T30 policy (section 4.7) does using data-derived tags.
- "What is lost if suppressed" — what the player will not see or what will not fire if this start is skipped.
- "Doors" — how many location-changing load doors satisfy the place conditions (`L1`, `L2`, `in X`, `here∋K`; all other conditions are assumed true).

| # | Quest (FormID, file) | Start condition (SM nodes + quest conditions) | What it does | Features | Category | Tags (T30) | R8 | T30 | What is lost if suppressed | Location-changing doors |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | `BardSongStarter` `000BE159` Skyrim | L2∋LocTypeInn | bard: StopAllSongs and a new song (PlaySongRequest) | None | ambience | technical restarts placeKind | suppress | suppress | nothing visible: the current song does not restart | ext→int 25 |
| 2 | `CW00SolitudeMapTableScene` `000D5165` Skyrim | GetQuestRunning(CWFinale)==0; GetVMQuestVariable(CW,3377E0)==0; GetStageDone(CW00A,20)==0; GetStageDone(CW01A,1)==0; L2=SolitudeCastleDourLocation | generals' scene at the map | None, scenes 1, quest progress | ambience | scene placeSpecific; data: +story | — | — | scene on entry | ext→int 2 int→int 1 |
| 3 | `CW00WindhelmMapTableScene` `000E1B36` Skyrim | GetQuestRunning(CWFinale)==0; GetVMQuestVariable(CW,2EE0F0)==0; GetStageDone(CW00B,20)==0; GetStageDone(CW01B,1)==0; L2=WindhelmPalaceOfTheKingsLocation | generals' scene at the map | None, scenes 1, quest progress | ambience | scene placeSpecific; data: +story | — | — | scene on entry | ext→int 2 int→int 1 |
| 4 | `CW01SolitudeMapTableScene` `000D5F0A` Skyrim | GetQuestRunning(CWFinale)==0; GetStageDone(CW01A,100)==1; GetStageDone(CW02A,30)==0; L2=SolitudeCastleDourLocation; GetStageDone(CW02A,40)==0; GetStageDone(CW02B,40)==0 | generals' scene at the map | None, scenes 1, quest progress | ambience | scene placeSpecific; data: +story | — | — | scene on entry | ext→int 2 int→int 1 |
| 5 | `CW01WindhelmMapTableScene` `000E2D3A` Skyrim | GetQuestRunning(CWFinale)==0; GetStageDone(CW01B,100)==1; GetStageDone(CW02B,30)==0; L2=WindhelmPalaceOfTheKingsLocation; GetStageDone(CW02A,40)==0; GetStageDone(CW02B,40… | generals' scene at the map | None, scenes 1, quest progress | ambience | scene placeSpecific; data: +story | — | — | scene on entry | ext→int 2 int→int 1 |
| 6 | `DA07MuseumScenes` `00096FD7` Skyrim | GetQuestCompleted(DA07MuseumScenes)==0; GetQuestRunning(DA07MuseumScenes)==0; L2=DawnstarSilussHouseLocation | scenes in Silus's house | None, scenes 6 | ambience | once scene placeSpecific | — | — | one-shot scene (out of place later or never) | ext→int 1 |
| 7 | `DialogueMarkarthPostAttackScene01` `00055A76` Skyrim | GetStageDone(MS01IntroWorldScene,500)==1; L1=MarkarthLocation; GetStageDone(DialogueMarkarthPostAttackScene01,100)==0 | scene after the attack in Markarth | None, RunOnce, scenes 1, quest progress | ambience | once scene placeSpecific; data: +story | — | — | one-shot scene (out of place later or never) | ext→int 23 |
| 8 | `DialogueRiftenSS04` `000B668C` Skyrim | L2=RiftenMistveilKeepLocation; GetGlobalValue(DialogueRiftenScene04)==0 | scene in Mistveil Keep | Misc, scenes 1 | ambience | once scene placeSpecific; data: +greets −once | — | — | one-shot scene (out of place later or never) | ext→ext 1 ext→int 4 int→ext 3 |
| 9 | `DialogueRiftenSS05` `0005CE25` Skyrim | L2=RiftenBeeandBarbLocation; GetGlobalValue(DialogueRiftenScene05)==0 | scene in the Bee and Barb | Misc, scenes 1 | ambience | once scene placeSpecific; data: −once | — | — | one-shot scene (out of place later or never) | ext→int 2 |
| 10 | `DialogueRiverwoodIntroScene` `000954D9` Skyrim | GetStage(MQ102)<50; GetStageDone(DialogueRiverwoodIntroScene,10)==0; GetCurrentTime()>=8; GetCurrentTime()<=20; L2=RiverwoodLocation | scene on first arrival in Riverwood | None, RunOnce, scenes 1, quest progress | ambience | once scene placeSpecific; data: +story | — | — | one-shot scene (out of place later or never) | int→ext 7 |
| 11 | `DialogueRiverwoodKidsScene` `000340A4` Skyrim | (2 nodes) L2=RiverwoodLocation; GetCurrentTime()>=5; GetCurrentTime()<=19; GetStage(MQ102A)>=40 OR; GetStage(MQ102B)>=40 OR | Riverwood children scene | None, RunOnce, scenes 1, quest progress | ambience | once scene placeSpecific; data: +story | — | — | one-shot scene (out of place later or never) | int→ext 7 |
| 12 | `DialogueWhiterunArgueScene` `00037CC2` Skyrim | GetGlobalValue(56)>=8; GetGlobalValue(56)<=20; GetVMQuestVariable(MS09,D0ED30)==0; GetStage(MS09)<=40; L2=WhiterunArcadiasCauldronLocation OR; L2=WhiterunBelethorsGenera… | argument about the war in Whiterun shops | None, RunOnce, scenes 1, quest progress | ambience | once scene placeSpecific; data: +story | — | — | one-shot scene (out of place later or never) | ext→int 6 |
| 13 | `DialogueWhiterunHeimskrPreachScene` `0003A15A` Skyrim | L2=WhiterunLocation; in WhiterunLocation; GetGlobalValue(56)>5; GetGlobalValue(56)<20 | Heimskr's sermon | None, scenes 1 | ambience | scene placeSpecific | — | — | scene on entry | int→ext 38 |
| 14 | `MS13BleakFallsBarrowCampfireScene` `0003A17C` Skyrim | L2=BleakFallsBarrowLocation; here∋LocTypeDungeon | bandit campfire scene | None, scenes 1 | ambience | scene placeSpecific | — | — | scene on entry | int→ext 1 |
| 15 | `MS13LucanCamillaScene` `00038386` Skyrim | L2=RiverwoodLocation; GetStageDone(MS13,5)==0 | Lucan/Camilla scene | None, RunOnce, scenes 1, quest progress, ENAM empty | ambience | once scene placeSpecific; data: +story | — | — | one-shot scene (out of place later or never) | int→ext 7 |
| 16 | `WIChangeLocation01` `000350F4` Skyrim | not in SovngardeLocation; chance WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; player.GetInCell(CidhnaMine01)==0; player.Ge… | NPC comment on the player's appearance | None, scenes 1 | ambience | scene greets encounter | suppress | — | player address or random encounter on entry | ext→ext 29 ext→int 411 int→ext 411 int→int 62 |
| 17 | `WIChangeLocation02` `0006C9C6` Skyrim | not in SovngardeLocation; chance WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; player.IsWeaponMagicOut()==1; player.GetEqui… | guard comment on the drawn weapon | None, scenes 1 | ambience | scene greets encounter | suppress | — | player address or random encounter on entry | ext→ext 29 ext→int 411 int→ext 411 int→int 62 |
| 18 | `WIChangeLocation03` `0006E028` Skyrim | not in SovngardeLocation; chance WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; not in WinterholdCollegeLocation; player.GetK… | encounter: challenge to a fight (WI) | Misc, objectives 1 | ambience | greets encounter; data: +story −greets | — | — | player address or random encounter on entry | ext→ext 29 ext→int 402 int→ext 400 int→int 55 |
| 19 | `WIChangeLocation04` `0006F211` Dragonborn | not in SovngardeLocation; chance WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; not in DLC2SolstheimLocation; not in Winterhol… | encounter: apprentice asks to be taught (WI) | Misc, objectives 1, scenes 1 | ambience | scene greets encounter; data: +story −greets | — | — | player address or random encounter on entry | ext→ext 29 ext→int 369 int→ext 367 int→int 49 |
| 20 | `WIChangeLocation05` `00070E30` Skyrim | not in SovngardeLocation; chance WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; player.GetLevel()>=5; L1∌LocTypeDwelling | encounter: courier/gift (WI) | Main | ambience | greets encounter; data: +story −greets | — | — | player address or random encounter on entry | ext→ext 26 ext→int 407 int→ext 65 int→int 24 |
| 21 | `WIChangeLocation08` `0004BCF1` HearthFires | not in SovngardeLocation; chance WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; player.GetKeywordDataForCurrentLocation(WIC… | daytime street encounter, sends SendStoryEvent (WI) | None | ambience | greets encounter; data: −greets | suppress | — | player address or random encounter on entry | ext→ext 29 ext→int 411 int→ext 411 int→int 62 |
| 22 | `WIChangeLocation09` `00050AF1` Skyrim | not in SovngardeLocation; chance WIEventChance; GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WINextChangeLoc)<GameDaysPassed; player.GetLevel()>=10; player.GetKeywordData… | dragon attack on a settlement (WI) | None, scenes 1 | ambience | scene encounter placeKind | suppress | — | scene on entry | ext→ext 3 int→ext 362 |
| 23 | `WICourier` `00039F82` Skyrim | GetGlobalValue(WIEventsEnabled)==1; GetGlobalValue(WICourierItemCount)>=1; L2∋LocTypeHabitation; L2∌LocTypeOrcStronghold; player.GetKeywordDataForCurrentLocation(WIComplex… | courier with a letter; without the event, fires on the next location change | None | ambience | greets encounter placeKind; data: −greets −encounter | suppress | — | player address or random encounter on entry | ext→ext 3 int→ext 342 |
| 24 | `WIGreeting` `000C7919` Skyrim | player.IsTrespassing()==0; L2∋LocTypeDwelling | homeowner greets the player | None, scenes 1 | ambience | scene greets placeKind | suppress | — | greeting or player address on entry | ext→ext 3 ext→int 346 int→ext 4 int→int 38 |
| 25 | `WITavern` `000DEE92` Skyrim | L2∋LocTypeInn | innkeeper approaches/offers a seat, scenes | Misc, scenes 2 | ambience | scene greets placeKind | suppress | — | greeting or player address on entry | ext→int 25 |
| 26 | `WIThief01` `00023EEB` Skyrim | player.GetLevel()>=5; GetStageDone(TG00,200)==1; chance WIThiefChance; GetGlobalValue(56)>22 OR; GetGlobalValue(56)<4 OR | night thief (WI) | None, quest progress | ambience | encounter; data: +story | — | — | random encounter or attack | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 27 | `BYOHHouseStewardOffer` `0300308B` HearthFires | L2∋BYOHHouseLocationKeyword; GetGlobalValue(PlayerFollowerCount)>0 | follower offers to become a steward | None, scenes 1 | unclear | scene greets placeKind; data: −greets | suppress | — | greeting or player address on entry | ext→int 39 int→ext 39 |
| 28 | `DialogueRiftenMaulHandler` `000EC199` Skyrim | GetQuestRunning(DialogueRiftenMaul)==1; GetStageDone(DialogueRiftenMaul,30)==1; GetStageDone(DialogueRiftenMaul,35)==0; GetStageDone(DialogueRiftenMaul,40)==0 | progress of Maul's introductory scene in Riften | None, RunOnce, quest progress | unclear | story once technical | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 29 | `DLC1EclipseAttack1` `0201629F` Dawnguard | GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; chance DLC1EclipseAttackNextChanceEclipse OR; GetGlobalValue(DLC1Eclips… | vampire attack on a settlement (radiant) | None | unclear | encounter; data: +technical | suppress | — | random encounter or attack | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 30 | `DLC1EclipseAttack2` `02017648` Dawnguard | (3 nodes) GetGlobalValue(TRUEGlobal)==0; GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; chance DLC1EclipseAttackNextChan… | vampire attack on a settlement (radiant) | None | unclear | encounter; data: +technical | suppress | — | random encounter or attack | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 31 | `DLC1EclipseAttack3` `02017649` Dawnguard | GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; chance DLC1EclipseAttackNextChanceEclipse OR; GetGlobalValue(DLC1Eclips… | vampire attack on a settlement (radiant) | None | unclear | encounter; data: +technical | suppress | — | random encounter or attack | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 32 | `DLC1EclipseAttack4` `0201764A` Dawnguard | GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; chance DLC1EclipseAttackNextChanceEclipse OR; GetGlobalValue(DLC1Eclips… | vampire attack on a settlement (radiant) | None | unclear | encounter; data: +technical | suppress | — | random encounter or attack | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 33 | `DLC1EclipseAttack5` `0201764B` Dawnguard | (3 nodes) GetGlobalValue(TRUEGlobal)==0; GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; chance DLC1EclipseAttackNextChan… | vampire attack on a settlement (radiant) | None | unclear | encounter; data: +technical | suppress | — | random encounter or attack | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 34 | `DLC1EclipseAttack6` `0201764C` Dawnguard | GetGlobalValue(DLC1EclipseActive)==1; GetGlobalValue(DLC1EclipseAttackNextAllowed)<=GameDaysPassed; chance DLC1EclipseAttackNextChanceEclipse OR; GetGlobalValue(DLC1Eclips… | vampire attack on a settlement (radiant) | None | unclear | encounter; data: +technical | suppress | — | random encounter or attack | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 35 | `DLC1RadiantDisguisedVampireLordSpawner` `0200420E` Dawnguard | chance DLC1RadiantDisguisedVampireLordChance; player.GetInFaction(DLC1HunterFaction)==1; GetGlobalValue(DLC1HarkonDead)==0; GetGlobalValue(56)>=22 OR; GetGlobalValue(56)<4 … | disguised vampire at night (radiant) | None | unclear | encounter | suppress | — | random encounter or attack | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 36 | `DLC1ScoutPatrol` `020041CF` Dawnguard | GetStageDone(DLC1VQ02,20)==1; GetStageDone(DLC1VQ08,200)==0; chance DLC1ScoutPatrolChance; in DLC1HunterHQLocation OR; in DLC1VampireCastleLocation OR | scout patrol (radiant) | None, quest progress | unclear | encounter placeSpecific; data: +story +technical | — | — | random encounter or attack | ext→int 13 int→ext 12 int→int 1 |
| 37 | `DLC2AshSpawnAttack` `04038423` Dragonborn | L2=DLC2RavenRockLocation; L1∌LocTypeDwelling; GetStage(DLC2RR01)>=40; GetStage(DLC2RR01)<90; chance DLC2AshSpawnAttackChance | ash spawn attack (radiant) | None, quest progress | unclear | encounter placeSpecific; data: +story +technical | — | — | random encounter or attack | int→ext 2 |
| 38 | `KingOlafsFestivalStarter` `0001703C` Skyrim | GetDayOfWeek()==6; L2=SolitudeLocation; GetQuestCompleted(MS05)==1 | starts the festival in Solitude | None, quest progress | unclear | story technical placeSpecific | — | — | technical quest-progress tracking (stage, start) | ext→ext 2 int→ext 35 |
| 39 | `BQ01` `00095125` Update | (9 nodes) L2∋LocTypeHabitation; in ReachHoldLocation; GetKeywordDataForLocation(ReachHoldLocation,BQActiveQuest)==0; in RiftHoldLocation; GetKeywordDataForLocation(RiftHold… | bounty notice (objectives) | Misc, objectives 3 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 3 int→ext 333 |
| 40 | `BQ02` `000BD77F` Update | (9 nodes) L2∋LocTypeHabitation; in ReachHoldLocation; GetKeywordDataForLocation(ReachHoldLocation,BQActiveQuest)==0; in RiftHoldLocation; GetKeywordDataForLocation(RiftHold… | bounty notice (objectives) | Misc, objectives 3 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 3 int→ext 333 |
| 41 | `BQ03` `000BD78C` Update | (9 nodes) L2∋LocTypeHabitation; in ReachHoldLocation; GetKeywordDataForLocation(ReachHoldLocation,BQActiveQuest)==0; in RiftHoldLocation; GetKeywordDataForLocation(RiftHold… | bounty notice (objectives) | Misc, objectives 3 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 3 int→ext 333 |
| 42 | `BQ04` `000CBA9A` Update | (9 nodes) L2∋LocTypeHabitation; in ReachHoldLocation; GetKeywordDataForLocation(ReachHoldLocation,BQActiveQuest)==0; in RiftHoldLocation; GetKeywordDataForLocation(RiftHold… | bounty notice (objectives) | Misc, objectives 3 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 3 int→ext 333 |
| 43 | `BYOHRelationshipAdoptableOrphanageCL` `0300E29A` HearthFires | in RiftenHonorhallOrphanageLocation | Hearthfire: adoption/house | None, RunOnce | story | story once technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | ext→int 2 |
| 44 | `C00LocationMonitoring1` `000A3EC5` Skyrim | L2≠WhiterunLocation; L1∌LocTypeDwelling; GetVMQuestVariable(C00,5BCA60)==0; GetStage(C00)>=40 | Companions: technical radiant monitors | Companions, quest progress | story | story technical | — | — | technical quest-progress tracking (stage, start) | ext→ext 27 ext→int 411 int→ext 66 int→int 24 |
| 45 | `C00LocationMonitoring2` `000A3EE4` Skyrim | L2≠WhiterunJorrvaskrLocation; GetStage(C00)==100 | Companions: technical radiant monitors | Companions, quest progress | story | story technical | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 411 int→ext 415 int→int 61 |
| 46 | `C00LocationMonitoring3` `000A3EE5` Skyrim | (2 nodes) player.GetInFaction(CompanionsFaction)==1; GetQuestCompleted(C03)==1; GetQuestRunning(C04)==0; L2=WhiterunLocation; L1≠WhiterunLocation; L1∌LocTypeDwelling; GetVM… | Companions: technical radiant monitors | Companions, quest progress | story | story technical placeSpecific | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 47 | `ClearSkiesChangeLocationQuest` `0001E3C8` Skyrim | GetQuestRunning(ClearSkiesQuest)==1 | mechanic: remove the Clear Skies shout effect | None, quest progress | story | story technical | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 48 | `CR08EditorMonitoring` `0005478C` Skyrim | GetQuestRunning(CR08)==1; GetStage(CR08)==21 | Companions: technical radiant monitors | Companions, quest progress | story | story technical | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 49 | `CRHoldExpansion` `000F9075` Skyrim | — | Companions: technical radiant monitors | None | story | story technical; data: −story | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 50 | `CRSilverHandNameChangerQuest` `0009D6F6` Skyrim | GetVMQuestVariable(C00,106E90)==1; player.GetKeywordDataForCurrentLocation(SilverHandTarget)==1 | Companions: technical radiant monitors | None, quest progress | story | story technical | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 51 | `CWFortObserver` `000D5F08` Skyrim | L1∋CWFort; L1≠WhiterunWatchtowerLocation | Civil War: garrison tracking | CivilWar | story | story technical | — | — | technical quest-progress tracking (stage, start) | 0 |
| 52 | `CWGarrisonObserver1` `0002994C` Skyrim | GetGlobalValue(TRUEGlobal)==0; player.GetKeywordDataForCurrentLocation(CWGarrison)>=0; here∋CWGarrison OR; here∋CWGarrisonDefenderOnly OR; here∋CWGarrisonNonContestab… | Civil War: garrison tracking | None | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | ext→int 1 int→ext 60 |
| 53 | `CWGarrisonObserver2` `0002E8F0` Skyrim | GetGlobalValue(TRUEGlobal)==0; player.GetKeywordDataForCurrentLocation(CWGarrison)>=0; here∋CWGarrison OR; here∋CWGarrisonDefenderOnly OR; here∋CWGarrisonNonContestab… | Civil War: garrison tracking | None | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | ext→int 1 int→ext 60 |
| 54 | `CWGarrisonObserver3` `0002E8F1` Skyrim | GetGlobalValue(TRUEGlobal)==0; player.GetKeywordDataForCurrentLocation(CWGarrison)>=0; here∋CWGarrison OR; here∋CWGarrisonDefenderOnly OR; here∋CWGarrisonNonContestab… | Civil War: garrison tracking | None | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | ext→int 1 int→ext 60 |
| 55 | `DA02ChangeLoc` `000B18E1` Skyrim | GetQuestRunning(DA02)==1; GetStage(DA02)>=50; L1≠SolitudeLocation; L2=SolitudeLocation OR; L2=SolitudeBluePalaceLocation OR | Daedric quest: hook or progress | None, quest progress | story | story technical placeSpecific | — | — | technical quest-progress tracking (stage, start) | ext→ext 2 int→ext 35 int→int 1 |
| 56 | `DA04SeptimusSendForPlayerQuest` `0010481C` Skyrim | GetStage(DA04)==35; player.GetLevel()>=DA04MinLevel | Daedric quest: hook or progress | None, RunOnce, quest progress | story | story once technical | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 57 | `DA05LocationCheck` `0005D330` Skyrim | L2=FalkreathLocation; GetStage(DA05)<10 | Daedric quest: hook or progress | Daedric, quest progress | story | story technical placeSpecific | — | — | technical quest-progress tracking (stage, start) | int→ext 12 |
| 58 | `DA07intro` `00094D89` Skyrim | GetQuestRunning(DA07intro)==0; GetQuestCompleted(DA07intro)==0; player.GetLevel()>=20 | Daedric quest: hook or progress | Misc, objectives 1, scenes 1 | story | story once scene | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 59 | `DA09ChangeLocation` `00081C59` Skyrim | GetQuestRunning(DA09)==1; chance DA09ChangeLocChance | Daedric quest: hook or progress | None, quest progress | story | story encounter; data: +technical | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 60 | `DA10` `00022F08` Skyrim | GetStageDone(MS01IntroWorldScene,300)==1; L1=MarkarthLocation; GetQuestRunning(DA10)==0; GetQuestCompleted(DA10)==0 | Daedric quest: hook or progress | Daedric, journal 5, objectives 8, scenes 7, quest progress | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | ext→int 23 |
| 61 | `DA11HallScene` `0007D950` Skyrim | GetStage(DA11Intro)==10; L2=MarkarthHalloftheDeadLocation | DA11 hook scene, advances the stage | None, scenes 1, quest progress | story | story scene placeSpecific | — | — | quest start or stage (fires later or never) | ext→int 1 int→int 1 |
| 62 | `DA11Intro` `0007D949` Skyrim | L2=MarkarthLocation; GetQuestCompleted(DA11Intro)==0 | Daedric quest: hook or progress | Misc, objectives 3 | story | story once placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 23 |
| 63 | `DA11IntroScene` `000AEC22` Skyrim | L2=MarkarthLocation; GetQuestCompleted(DA11IntroScene)==0 | Daedric quest: hook or progress | Misc, scenes 1, ENAM empty | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | int→ext 23 |
| 64 | `DA14Start` `0003FA17` Skyrim | player.GetLevel()>=14; GetStage(DA14)<5; L2∋LocTypeHabitation | Daedric quest: hook or progress | Misc, objectives 1, scenes 2, quest progress | story | story scene greets placeKind; data: −greets | — | — | greeting or player address on entry | ext→ext 3 int→ext 362 |
| 65 | `DA16WalkSceneMonitor` `000E16C6` Skyrim | GetStage(DA16)==25; GetGlobalValue(DA16Walk)==1; GetGlobalValue(DA16OnTheWay)==0 | Daedric quest: hook or progress | None, quest progress | story | story technical | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 66 | `DB11_dunKatariahQST` `000C6020` Skyrim | in KatariahLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, scenes 2 | story | story scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 67 | `DLC1dunRedwaterDenQST` `02007C4F` Dawnguard | in DLC1RedwaterDenLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, RunOnce, scenes 6 | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 68 | `DLC1HunterBaseEnableSet1` `020198B2` Dawnguard | GetStage(DLC1HunterBaseIntro)==200; GetStage(DLC1HunterBaseEnableSet1)<200; L2≠DLC1HunterHQLocation; L1=DLC1HunterHQLocation | DLC quest progress | None, quest progress | story | story once technical placeSpecific | — | — | technical quest-progress tracking (stage, start) | ext→int 4 |
| 69 | `DLC1HunterBaseEnableSet2` `0201991F` Dawnguard | GetStage(DLC1HunterBaseStage2)==200; GetStage(DLC1HunterBaseEnableSet2)<200; L2≠DLC1HunterHQLocation; L1=DLC1HunterHQLocation | DLC quest progress | None, quest progress | story | story once technical placeSpecific | — | — | technical quest-progress tracking (stage, start) | ext→int 4 |
| 70 | `DLC1HunterBaseFlorentiusChange` `02011F0D` Dawnguard | GetStage(DLC1HunterBaseFlorentiusChange)<10; DLC1FlorentiusRef.in DLC1HunterHQLocation; GetStage(DLC1HunterBaseStage2)==200 | DLC quest progress | None, RunOnce, quest progress | story | story once technical placeSpecific | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 71 | `DLC1LD_Arkngthamz` `02004D6B` Dawnguard | in DLC1ArkngthamzLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, RunOnce, scenes 30 | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 72 | `DLC1LD_Bthalft` `0200CFFF` Dawnguard | in BthalftLocation; GetStageDone(DLC1LD,20)==1; GetStageDone(DLC1LD,30)==1; GetStageDone(DLC1LD,40)==1; GetStageDone(DLC1LD,50)==1; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, RunOnce, scenes 16, quest progress | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | ext→int 1 int→ext 1 |
| 73 | `DLC1LD_ChangeLocation` `02004D6C` Dawnguard | in DLC1ArkngthamzLocation OR; in RaldbtharLocation OR; in DeepFolkCrossingLocation OR; in MzulftLocation OR; in BthalftLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | ext→ext 1 ext→int 2 int→ext 1 |
| 74 | `DLC1ScrollHandlingChangeLoc` `02012F92` Dawnguard | GetVMQuestVariable(DialogueWinterholdCollege,218FBC8)==0 | mechanic: Elder Scrolls in the Arcanaeum | None, quest progress | story | story technical | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 75 | `DLC1VQ00ChangeLocation` `0200D90B` Dawnguard | player.GetLevel()>=DLC1VQMinLevel; GetStage(DLC1VQ01MiscObjective)<10; L2∋LocTypeHabitation; L2∌LocTypeOrcStronghold; L1∌LocTypeDwelling; L2≠WinterholdCollegeLocation; not … | DLC quest progress | Misc, quest progress | story | story technical placeKind | — | — | technical quest-progress tracking (stage, start) | ext→ext 1 int→ext 23 |
| 76 | `DLC1VQ08HunterMove` `0201A8BB` Dawnguard | GetStage(DLC1VQ08)==40; L2≠DLC1HunterHQLocation; L1=DLC1HunterHQLocation | DLC quest progress | None, RunOnce, quest progress | story | story once technical placeSpecific | — | — | technical quest-progress tracking (stage, start) | ext→int 4 |
| 77 | `DLC2CultistAmbush` `0403546D` Dragonborn | not in DLC2SolstheimLocation; GetStage(MQ105)>=160; GetStage(DLC2MQ01)<5; GetStageDone(DLC2WE09,1)==0 | DLC quest progress | Misc, quest progress | story | story greets encounter; data: −greets −encounter | — | — | player address or random encounter on entry | ext→ext 30 ext→int 382 int→ext 382 int→int 56 |
| 78 | `DLC2dunFrostmoonQST` `0401F9E6` Dragonborn | in DLC2FrostmoonCragLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, RunOnce, scenes 3 | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 79 | `DLC2dunKarstaagQST` `040247F8` Dragonborn | in DLC2CastleKarstaagRuinsLocation OR; in DLC2GlacialCaveLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, RunOnce, scenes 3 | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 80 | `DLC2dunKolbjornQST` `0401810B` Dragonborn | in DLC2KolbjornBarrowLocation OR; in DLC2RavenRockLocation OR | dungeon/place logic: bosses, ambushes, scenes, stages | Side, journal 28, objectives 24, RunOnce, scenes 1 | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | ext→int 12 int→ext 13 int→int 3 |
| 81 | `DLC2dunNchardakTracking` `040173BE` Dragonborn | in DLC2NchardakLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, RunOnce, ENAM empty | story | story once technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | 0 |
| 82 | `DLC2EbonyWarriorQuest` `0402C49E` Dragonborn | player.GetLevel()>=80; L1≠DLC2SolstheimLocation; L2=WhiterunLocation OR; L2=RiftenLocation OR; L2=WindhelmLocation OR; L2=MarkarthLocation OR; L2=SolitudeLocation OR;… | DLC quest progress | Side, journal 3, objectives 2, RunOnce | story | story once placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 411 int→ext 415 int→int 62 |
| 83 | `DLC2RR03MineCL` `0403332C` Dragonborn | GetStageDone(DLC2RR03,200)==1; L1=DLC2RavenRockLocation | DLC quest progress | None, RunOnce, quest progress | story | story once technical placeSpecific | — | — | technical quest-progress tracking (stage, start) | ext→int 13 |
| 84 | `DLC2ThirskRieklingSpawner` `040208CC` Dragonborn | GetGlobalValue(DLC2ThirskRieklingOwned)==1; L2=DLC2ThirskMeadHallLocation; L1≠DLC2ThirskMeadHallInteriorLocation; GetStage(DLC2MH01)!=20 | DLC quest progress | None, quest progress | story | story encounter placeSpecific; data: +technical −encounter | — | — | quest start or stage (fires later or never) | 0 |
| 85 | `DLC2TTR1` `0401B65F` Dragonborn | L2=DLC2TelMithrynApothecaryLocation OR; L2=DLC2TelMithrynKitchenLocation OR; L2=DLC2TelMithrynStewardLocation OR; L2=DLC2TelMithrynTowerLocation OR; GetQuestCompleted… | DLC quest progress | Side, journal 2, objectives 2, quest progress | story | story placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 86 | `DLC2TTR2` `0401C06E` Dragonborn | L2=DLC2TelMithrynApothecaryLocation OR; L2=DLC2TelMithrynKitchenLocation OR; L2=DLC2TelMithrynStewardLocation OR; L2=DLC2TelMithrynTowerLocation OR; GetStage(DLC2TT1)… | DLC quest progress | Side, journal 1, objectives 2, RunOnce, quest progress | story | story once placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 87 | `DLC2TTR3a` `0401B65D` Dragonborn | L2=DLC2TelMithrynApothecaryLocation OR; L2=DLC2TelMithrynKitchenLocation OR; L2=DLC2TelMithrynStewardLocation OR; L2=DLC2TelMithrynTowerLocation OR; GetStage(DLC2TT1)… | DLC quest progress | Side, journal 3, objectives 2, RunOnce, quest progress | story | story once placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 88 | `DLC2TTR3b` `0401D8D1` Dragonborn | L2=DLC2TelMithrynApothecaryLocation OR; L2=DLC2TelMithrynKitchenLocation OR; L2=DLC2TelMithrynStewardLocation OR; L2=DLC2TelMithrynTowerLocation OR; GetStage(DLC2TT1)… | DLC quest progress | Side, journal 3, objectives 2, RunOnce, quest progress | story | story once placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 89 | `DLC2TTR4a` `0401B664` Dragonborn | L2=DLC2TelMithrynApothecaryLocation OR; L2=DLC2TelMithrynKitchenLocation OR; L2=DLC2TelMithrynStewardLocation OR; L2=DLC2TelMithrynTowerLocation OR; GetStage(DLC2TT1)… | DLC quest progress | Side, journal 3, objectives 2, RunOnce, scenes 1, quest progress | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 90 | `DLC2TTR4b` `0401CDE1` Dragonborn | L2=DLC2TelMithrynApothecaryLocation OR; L2=DLC2TelMithrynKitchenLocation OR; L2=DLC2TelMithrynStewardLocation OR; L2=DLC2TelMithrynTowerLocation OR; GetStage(DLC2TT1)… | DLC quest progress | Misc, objectives 1, RunOnce, scenes 1, quest progress | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 91 | `DLC2TTR7` `0401BD18` Dragonborn | L2=DLC2TelMithrynApothecaryLocation OR; L2=DLC2TelMithrynKitchenLocation OR; L2=DLC2TelMithrynStewardLocation OR; L2=DLC2TelMithrynTowerLocation OR; GetStage(DLC2TT1)… | DLC quest progress | Side, journal 3, objectives 2, quest progress | story | story placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 92 | `DLC2TTR8` `0401F13B` Dragonborn | L2=DLC2TelMithrynApothecaryLocation OR; L2=DLC2TelMithrynKitchenLocation OR; L2=DLC2TelMithrynStewardLocation OR; L2=DLC2TelMithrynTowerLocation OR; GetQuestCompleted… | DLC quest progress | Misc, objectives 6, quest progress | story | story placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 93 | `DLC2WaterStoneSailAwayQuest` `04027826` Dragonborn | DLC2PillarBuildSandboxMarkerTOGGLEWater.GetDisabled()==1; L2≠DLC2StoneWaterLocation; L1≠DLC2StoneWaterLocation | DLC quest progress | None, RunOnce | story | story once technical; data: −story | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 94 | `dunAlftandQST` `0003B0FB` Skyrim | in AlftandLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, RunOnce, scenes 5 | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | ext→ext 1 |
| 95 | `dunAnsilvundQST` `0003D721` Skyrim | in AnsilvundLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, objectives 1, RunOnce, scenes 11 | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 96 | `dunBluePalaceWingQST` `00048364` Skyrim | in BluePalaceWingLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, ENAM empty | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | 0 |
| 97 | `dunBrokenOarQST` `0001E437` Skyrim | in BrokenOarGrottoLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, scenes 2 | story | story scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 98 | `dunDarklightQST` `0003FA89` Skyrim | (2 nodes) in DarklightTowerLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Side, journal 3, objectives 2, RunOnce, scenes 6, ENAM empty | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 99 | `dunFaldarsToothQST` `0004E667` Skyrim | in FaldarsToothLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, RunOnce, scenes 5 | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 100 | `dunFolgunthurQST` `0004CEDE` Skyrim | in FolgunthurLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, RunOnce | story | story once technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | 0 |
| 101 | `dunForelhostQST` `00035354` Skyrim | in ForelhostLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, journal 6, objectives 6, RunOnce, scenes 5 | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 102 | `dunFrostflowAbyssQST` `00048121` Skyrim | in FrostflowLighthouseLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, objectives 1, RunOnce | story | story once placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 103 | `dunFrostmereCryptQST` `000CF915` Skyrim | in FrostmereCryptLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Side, journal 6, objectives 4, scenes 9 | story | story scene placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 104 | `dunGauldursonChangeLocationQST` `000ECEB7` Skyrim | in ReachwaterRockLocation OR; in FolgunthurLocation OR; in SaarthalLocation OR; in GeirmundsHallLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | 0 |
| 105 | `dunGeirmundQST` `000E738F` Update | in GeirmundsHallLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, RunOnce | story | story once technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | 0 |
| 106 | `dunHalldirsCairnQST` `0003AD4A` Skyrim | in HalldirsCairnLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | 0 |
| 107 | `dunHaltedStreamCampQST` `000A409D` Skyrim | in HaltedStreamCampLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, RunOnce, scenes 1 | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 108 | `dunHighGateRuinsQST` `000443F4` Skyrim | in HighGateRuinsLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, journal 5, objectives 3, scenes 8 | story | story scene placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 109 | `dunHillgrundsTombQST` `0003E897` Skyrim | here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, journal 1, objectives 4, RunOnce, scenes 11 | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 9 ext→int 12 int→ext 12 int→int 9 |
| 110 | `dunKarthspireRedoubtQST` `000357E5` Skyrim | in KarthspireLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | int→ext 1 |
| 111 | `dunLiarsRetreatQST` `000AA5D6` Skyrim | in LiarsRetreatLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, RunOnce, scenes 4 | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 112 | `dunLostValleyRedoubtQST` `000B0F5B` Skyrim | in LostValleyRedoubtLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, RunOnce, scenes 3 | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 113 | `dunMistwatchQST` `00030B36` Skyrim | in MistwatchLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, objectives 3, scenes 3, ENAM empty | story | story scene placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 114 | `dunMossMotherQST` `000376EE` Skyrim | in MossMotherCavernLocation OR; in FalkreathLocation OR | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, objectives 4, scenes 1 | story | story scene placeSpecific | — | — | quest start or stage (fires later or never) | ext→int 12 int→ext 12 int→int 2 |
| 115 | `dunNchuandZelQst` `000C3709` Skyrim | in NchuandZelLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, journal 5, objectives 6, RunOnce | story | story once placeSpecific | — | — | quest start or stage (fires later or never) | int→int 1 |
| 116 | `dunPinewatchQST` `000786B9` Skyrim | in PinewatchLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Main, RunOnce, scenes 1 | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 117 | `dunPOITundraWitchShackQST` `000DDF8D` Skyrim | in AnisesCabinLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None | story | story placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 118 | `dunRaldbtharQST` `000B7C01` Skyrim | in RaldbtharLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | ext→ext 1 ext→int 1 |
| 119 | `dunRannveigQST` `0005160C` Skyrim | in RannveigsFastLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, scenes 2 | story | story scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 120 | `dunRavenscarQST` `0003D26C` Skyrim | in RavenscarHollowLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, scenes 1 | story | story scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 121 | `dunRebelsCairnChangeLocationQST` `000EB9E2` Skyrim | in RebelsCairnLocation OR; in RedEagleRedoubtLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | 0 |
| 122 | `dunRebelsCairnQST` `000C1ACC` Skyrim | in RebelsCairnLocation OR; in RedEagleRedoubtLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, objectives 2, ENAM empty | story | story placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 123 | `dunRobbersGorgeQST` `000B8C9F` Skyrim | in RobbersGorgeLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, journal 1 | story | story placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 124 | `dunSaarthal` `00049DB2` Skyrim | in SaarthalLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | 0 |
| 125 | `dunShipwreck04QST` `00043BAB` Skyrim | in PilgrimsTrenchLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, journal 2, objectives 2, ENAM empty | story | story placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 126 | `dunShroudHearthBarrowQST` `00072206` Skyrim | in ShroudHearthBarrowLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, scenes 1 | story | story scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | int→ext 1 |
| 127 | `dunSleepingTreeCampQST` `000E1A9D` Skyrim | in SleepingTreeCampLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, objectives 1 | story | story placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 128 | `dunSunderstoneGorgeQST` `0008B72A` Skyrim | in SunderstoneGorgeLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | 0 |
| 129 | `dunTalkingStoneQST` `0004BA6C` Skyrim | in OrotheimLocation OR; in TalkingStoneCampLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, scenes 2 | story | story scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 130 | `dunTolvaldsCaveQST` `0003CD36` Skyrim | in TolvaldsCaveLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, scenes 4 | story | story scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 131 | `dunTrevasWatchQST` `0004B2A1` Skyrim | in TrevasWatchLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Side, journal 4, objectives 3, RunOnce, scenes 2 | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 132 | `dunValtheimKeepQST` `000ADFE7` Skyrim | in ValtheimKeepLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None | story | story placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 133 | `dunValthumeQST` `0003ED82` Skyrim | in ValthumeLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Side, journal 4, objectives 5, RunOnce, scenes 4, ENAM empty | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 134 | `dunWhiteRiverWatchQST` `0001C3FB` Skyrim | in WhiteRiverWatchLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, scenes 3 | story | story scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 135 | `Favor001` `000403B0` Skyrim | (6 nodes) L2=MorthalLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=DarkwaterCrossingLocation; L2=AngasMillLocation; L2=FalkreathLocation; L2=WindhelmLocation; L2=… | favor or minor quest (journal, objectives) | Misc, objectives 1 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 10 |
| 136 | `Favor013` `000CA209` Skyrim | (6 nodes) L2=WinterholdLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=KynesgroveLocation; L2=SolitudeLocation; L2=WindhelmLocation; L2=MarkarthLocation; L2=Whiter… | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 1 int→ext 11 |
| 137 | `Favor017` `000CA210` Dragonborn | (8 nodes) L2=MorthalLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=NarzulburLocation; L2=MorKhazgurLocation; L2=DushnikhYalLocation; L2=WindhelmLocation; L2=Marka… | favor or minor quest (journal, objectives) | Misc | story | story placeKind placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | int→ext 9 |
| 138 | `Favor018` `0003F46A` Skyrim | (3 nodes) L2=WinterholdLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=FalkreathLocation; L2=WindhelmLocation | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 4 |
| 139 | `Favor019` `0003F4EC` Skyrim | L2=WindhelmLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 140 | `Favor104` `0006FD35` Skyrim | (4 nodes) L2=DarkwaterCrossingLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=SolitudeLocation; L2=WindhelmLocation; L2=DLC2SkaalVillageLocation | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 1 int→ext 2 |
| 141 | `Favor109` `000940E0` Skyrim | L2=SolitudeLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; player.GetLevel()>=10 | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 1 int→ext 1 |
| 142 | `Favor110` `000CA439` Skyrim | (6 nodes) L2=MorthalLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=DawnstarLocation; L2=FalkreathLocation; L2=SolitudeLocation; L2=WindhelmLocation; L2=WhiterunLo… | favor or minor quest (journal, objectives) | Misc, objectives 1 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 1 int→ext 9 |
| 143 | `Favor151` `0006C824` Skyrim | L2=MarkarthLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 6 |
| 144 | `Favor153` `00090E36` Skyrim | L2=DawnstarLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; player.GetLevel()>=22 | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 2 |
| 145 | `Favor154` `0007172A` Skyrim | L2=FalkreathLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 3 |
| 146 | `Favor157` `000A3504` Skyrim | L2=MarkarthLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; player.GetLevel()>=20 | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 6 |
| 147 | `Favor158` `000940D0` Update | L2=WinterholdLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 1 |
| 148 | `Favor204` `0007105B` Skyrim | (3 nodes) L2=WindhelmLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=MarkarthLocation; L2=WhiterunLocation | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 9 |
| 149 | `Favor205` `0006FE37` Skyrim | (5 nodes) L2=KynesgroveLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; L2=DawnstarLocation; L2=FalkreathLocation; L2=SolitudeLocation; L2=WindhelmLocation | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 1 int→ext 6 |
| 150 | `Favor250` `000A2C86` Skyrim | L2=MarkarthLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 6 |
| 151 | `Favor252` `000A2C9B` Skyrim | L2=SolitudeLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | ext→ext 1 int→ext 1 |
| 152 | `Favor253` `000A2C9E` Skyrim | L2=WhiterunLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 3 |
| 153 | `Favor254` `000A2CA6` Skyrim | L2=WindhelmLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 154 | `Favor255` `000A34CE` HearthFires | L2=MorthalLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 1 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 155 | `Favor256` `000A34D4` HearthFires | L2=DawnstarLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 1 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 2 |
| 156 | `Favor257` `000A34D7` Skyrim | L2=WinterholdLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 1 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 1 |
| 157 | `Favor258` `000A34DE` HearthFires | L2=FalkreathLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 1 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 3 |
| 158 | `FreeformCidhnaMineA` `000DB851` Skyrim | L2=CidhnaMineLocation; L2∋LocTypeHabitation | favor or minor quest (journal, objectives) | Misc, objectives 3, ENAM empty | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 159 | `FreeformDragonBridge01` `000555DD` Skyrim | L2=DragonBridgeLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 2, RunOnce | story | story once placeKind placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 160 | `FreeformFrostRiver` `000681D4` Skyrim | GetQuestRunning(FreeformFrostRiver)==0; L2=FrostRiverFarmLocation; GetGlobalValue(FreeformFrostRiverReset)<GameDaysPassed OR; GetGlobalValue(FreeformFrostRiverReset)==0;… | favor or minor quest (journal, objectives) | None | story | story once placeKind placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 161 | `FreeformHighHrothgarA` `000E76DE` Skyrim | L2=HighHrothgarLocation | favor or minor quest (journal, objectives) | Misc, objectives 1 | story | story placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 162 | `FreeformKarthwastenA` `000622E8` Skyrim | GetStageDone(FreeformKarthwastenA,10)==0; L2=KarthwastenLocation; L2∋LocTypeHabitation | favor or minor quest (journal, objectives) | Misc, objectives 4, scenes 1, ENAM empty | story | story once scene placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 6 |
| 163 | `FreeformKolskeggrA` `0001FD72` Skyrim | L2=LeftHandMineLocation; L2∋LocTypeHabitation; GetQuestRunning(FreeformKolskeggrA)==0 | favor or minor quest (journal, objectives) | Misc, objectives 2, ENAM empty | story | story once placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 4 |
| 164 | `FreeformKynesgroveA` `00069043` Skyrim | L2=KynesgroveLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 1 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 165 | `FreeformMarkarthJ` `000790E1` Skyrim | L2=MarkarthLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling; GetStageDone(FreeformMarkarthJ,20)==0 | favor or minor quest (journal, objectives) | Misc, objectives 1, RunOnce | story | story once placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 6 |
| 166 | `FreeformMarkarthMPostQuest` `000647A6` Skyrim | GetStageDone(FreeformMarkarthM,20)==1; L1=MarkarthLocation; GetStageDone(FreeformMarkarthM,10)==0 | favor or minor quest (journal, objectives) | None, RunOnce, quest progress | story | story once placeSpecific | — | — | quest start or stage (fires later or never) | ext→int 23 |
| 167 | `FreeformOldHroldanB` `0006819A` Skyrim | L2=OldHroldanLocation; L2∋LocTypeHabitation | favor or minor quest (journal, objectives) | Misc, objectives 1, scenes 1 | story | story scene placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 1 |
| 168 | `FreeformRiverwood01` `00074A83` Skyrim | L2∋LocTypeHabitation; L2=RiverwoodLocation | favor or minor quest (journal, objectives) | Misc, objectives 6, RunOnce | story | story once placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 7 |
| 169 | `FreeformShipsDawnstar` `00094152` Skyrim | L2=DawnstarLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 2 |
| 170 | `FreeformShipsWindhelm` `00094158` Skyrim | L2=WindhelmLocation; L2∋LocTypeHabitation; L1∌LocTypeDwelling | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeKind placeSpecific | — | — | quest start or stage (fires later or never) | 0 |
| 171 | `FreeformSkyHavenTempleB` `000E3E67` Skyrim | L2=SkyHavenTempleLocation | favor or minor quest (journal, objectives) | Misc, objectives 2 | story | story placeSpecific | — | — | quest start or stage (fires later or never) | ext→int 1 |
| 172 | `FreeformWinterholdCFin` `0006531D` Skyrim | GetStageDone(FreeformWinterholdC,40)==1; L2=WinterholdLocation; L2∋LocTypeHabitation | favor or minor quest (journal, objectives) | Main, quest progress | story | story technical placeKind placeSpecific | — | — | technical quest-progress tracking (stage, start) | int→ext 6 |
| 173 | `FreeformWinterholdCollegeA` `0005D2E9` Skyrim | FaraldaRef.GetDead()==0; NiryaRef.GetDead()==0; GetGlobalValue(FreeformCollegeAReset)<GameDaysPassed; player.GetInFaction(CollegeofWinterholdFaction)==1; player.GetRelation… | Nirya's errand with notes, no journal | Misc | story | story placeKind; data: −story | suppress | — | quest start or stage (fires later or never) | ext→ext 3 int→ext 362 |
| 174 | `MGR01Trigger` `000E8B50` Skyrim | GetQuestRunning(MGR01)==0; L2=WinterholdCollegeLocation; L1≠WinterholdCollegeLocation | story quest progress | None, quest progress | story | story technical placeSpecific | — | — | technical quest-progress tracking (stage, start) | int→ext 11 |
| 175 | `MGScrollSwap` `0010F4AC` Skyrim | GetVMQuestVariable(DialogueWinterholdCollege,92010)==1; L2≠WinterholdCollegeArcanaeumLocation | mechanic: return the Elder Scroll to its place | None, quest progress | story | story technical | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 413 int→ext 415 int→int 60 |
| 176 | `MQ201ThalmorEmbassy` `00030B15` Skyrim | in ThalmorEmbassyLocation; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, scenes 1, ENAM empty | story | story scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 177 | `MS02EscapeMadanachEnding` `00054880` Skyrim | MadanachREF.GetDead()==0; GetQuestRunning(MS02)==1; L2=MarkarthRuinsLocation | story quest progress | Side, RunOnce, scenes 2, quest progress | story | story once scene placeSpecific | — | — | quest start or stage (fires later or never) | ext→int 1 int→int 1 |
| 178 | `MS02EscapeThonarEnding` `0006A9A0` Skyrim | MadanachREF.GetDead()==1; GetQuestRunning(MS02)==1; L2=MarkarthLocation | story quest progress | Side, RunOnce, quest progress | story | story once placeSpecific | — | — | quest start or stage (fires later or never) | int→ext 23 |
| 179 | `MS05_dunDeadMensRespiteQST` `00016C8D` Skyrim | in DeadMensRespiteLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | Misc, RunOnce, scenes 12 | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 180 | `MS08ArgueQuest` `00055BFD` Skyrim | GetVMQuestVariable(MS08ArgueQuest,CCA670)==0; L1=WhiterunLocation; L2≠WhiterunLocation; GetStageDone(DialogueWhiterun,40)==1; GetStage(MS08)<25; GetStage(MQ104)>45 | MS08 quest setup (Enable, MoveTo) | Main, RunOnce, scenes 1, quest progress | story | story once scene placeSpecific; data: +greets | — | — | quest start or stage (fires later or never) | ext→int 38 |
| 181 | `MS10CompanySwapPostQuest` `0006BC1A` Skyrim | L2=WindhelmLocation; L1≠WindhelmLocation; GetQuestCompleted(MS10)==1 | story quest progress | None, RunOnce, quest progress | story | story once technical placeSpecific | — | — | technical quest-progress tracking (stage, start) | int→ext 28 |
| 182 | `MS11Kicker` `000BAC24` Update | L2=WindhelmLocation; L1≠WindhelmLocation; L1∌LocTypeDwelling | story quest progress | None | story | story technical placeSpecific; data: −story | — | — | technical quest-progress tracking (stage, start) | 0 |
| 183 | `MS12b` `0002499C` Update | GetStageDone(MQ204,200)==1; GetStageDone(MS12,100)==1; GetQuestRunning(MS12b)==0; GetGlobalValue(57)>=MS12FollowUpStartDay | story quest progress | Side, journal 3, objectives 5, RunOnce, quest progress | story | story once | — | — | quest start or stage (fires later or never) | ext→ext 30 ext→int 415 int→ext 415 int→int 62 |
| 184 | `T03PostQuest` `000FE479` Skyrim | GetQuestCompleted(T03)==1; GetStageDone(T03PostQuest,100)==0; L2≠WhiterunLocation; L1≠WhiterunLocation | story quest progress | None, quest progress | story | story once technical | — | — | technical quest-progress tracking (stage, start) | ext→ext 30 ext→int 377 int→ext 377 int→int 62 |
| 185 | `TG04Brinewater` `0007A282` Skyrim | in BrinewaterGrottoLocation OR; here∋LocTypeDungeon | dungeon/place logic: bosses, ambushes, scenes, stages | None, RunOnce, scenes 2 | story | story once scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | 0 |
| 186 | `TG06_dunMarkarthWizardsQuartersQST` `00087105` Skyrim | in MarkarthLocation OR | dungeon/place logic: bosses, ambushes, scenes, stages | None, scenes 6, ENAM empty | story | story scene placeSpecific; data: −story | — | — | quest start or stage (fires later or never) | ext→int 22 int→ext 23 int→int 9 |

**Quests with empty `ENAM`** (14) sit in `CLOC` nodes but have no event: `DA11IntroScene`, `DLC2dunNchardakTracking`, `FreeformCidhnaMineA`, `FreeformKarthwastenA`, `FreeformKolskeggrA`, `MQ201ThalmorEmbassy`, `MS13LucanCamillaScene`, `TG06_dunMarkarthWizardsQuartersQST`, `dunBluePalaceWingQST`, `dunDarklightQST`, `dunMistwatchQST`, `dunRebelsCairnQST`, `dunShipwreck04QST`, `dunValthumeQST`.
- Some of them start at game start (`StartGameEnabled`).
- `AE#25048` has no `eventID` check. Whether their node actually starts them has not been verified.

## 10. `OnLocationChange`: attached scripts

| Script | Attached to (count, examples) | What it does in OnLocationChange | Category | Tags (T30) |
|---|---|---|---|---|
| `BQPlayerScript` | QUST.alias 8: BQ01, BQ02, BQ03 | quest progress: GetOwningQuest, PlayerChangedLocation | story | story |
| `BYOHHouseBuildingPlayerScript` | QUST.alias 1: BYOHHouseBuilding | quest progress: GetOwningQuest, PlayerChangeLocation | story | story |
| `BYOHRelationshipAdoptionChildScript` | QUST.alias 2: BYOHRelationshipAdoption | quest progress: ChildLocationChanged, GetActorRef | story | story |
| `BYOHRelationshipAdoptionPlayerScript` | QUST.alias 1: BYOHRelationshipAdoption | quest progress: PlayerLocationChanged | story | story |
| `C00PlayerWatchingScript` | QUST.alias 2: C00 | quest progress: GetOwningQuest, GetStage | story | story |
| `C05WatchedPlayerScript` | QUST.alias 2: C05 | quest progress: GetReference, IsInInterior, IsRunning, Stop | story | story |
| `CartDriverScript` | NPC_ 1: CarriageDriverTemplate | carriage driver speaks a line | ambience | greets |
| `CidhnaMineJailEventPlayerScript` | QUST.alias 1: CidhnaMineJailEventScene | quest progress: GetOwningQuest, SetStage | story | story |
| `CompanionsSingleCombatPlayerScript` | QUST.alias 1: CompanionsSingleCombatQuest | quest progress: GetOwningQuest, SetStage | story | story |
| `CR01PlayerWatchingScript` | QUST.alias 1: CR01 | quest progress: GetActorReference, GetLocation, GetOwningQuest, GetReference, GetStage, Lock | story | story |
| `CWFinalePlayerScript` | QUST.alias 1: CWFinale | quest progress: GetOwningQuest, GetStageDone, stop | story | story |
| `CWFortSiegeCapitalPlayerScript` | QUST.alias 1: CWFortSiegeCapital | quest progress: GetLocation, GetOwningQuest, GetReference, GetStageDone, IsInLocation, Stop | story | story |
| `CWGarrisonObserverPlayerScript` | QUST.alias 3: CWGarrisonObserver1, CWGarrisonObserver2, CWGarrisonObserver3 | quest progress: GetLocation, GetOwningQuest, isChild, playerLeftLocation | story | story |
| `CWPlayerScript` | QUST.alias 3: CWAttackCity, CWMission03, CWMission04 | quest progress: GetLocation, GetOwningQuest, GetPlayer, GetStage, GetStageDone, IsInLocation | story | story |
| `DA11IntroPlayerScript` | QUST.alias 1: DA11HallScene | quest progress: IsPlaying, Stop | story | story |
| `DCL1VampireTurnPlayerScript` | QUST.alias 1: DLC1VampireTurn | quest progress: GetOwningQuest, PlayerChangedLocationCompleteChange | story | story |
| `defaultAliasChangeOutfit` | QUST.alias 1: CW02A | NPC outfit change | unclear | technical |
| `DGIntimidatePlayerScript` | QUST.alias 1: DGIntimidateQuest | quest progress: GetActorRef, GetCurrentLocation, GetOwningQuest, GetPlayer, IsInLocation, SetStage | story | story |
| `DLC1EclipseAttackPlayerScript` | QUST.alias 6: DLC1EclipseAttack1, DLC1EclipseAttack2, DLC1EclipseAttack3 | quest progress: GetLocation, GetOwningQuest, GetPlayer, IsInLocation, stop | story | encounter technical |
| `DLC1RadiantPlayerLeaveScript` | QUST.alias 1: DLC1RadiantDisguisedVampireLordSpawner | quest progress: GetActorReference, GetLocation, GetOwningQuest, IsInLocation, Stop | story | encounter technical |
| `DLC1RadiantPlayerScript` | QUST.alias 1: DLC1Radiant | quest progress: CreateQuestBasedOnLocation | story | story |
| `DLC1RNPCAliasScript` | QUST.alias 1: DLC1NPCMentalModel | NPC outfit change | unclear | technical |
| `DLC2AshSpawnAttackChanceScript` | QUST.alias 1: DLC2Init | ash spawn attack chance by LocTypeDwelling | ambience | encounter |
| `DLC2EbonyWarriorPlayerScript` | QUST.alias 1: DLC2EbonyWarriorQuest | quest progress: GetActorRef, GetPlayer, GetRef, GetStage, MoveTo, RemoveFromFaction | story | story |
| `DLC2InitCrossDLCScript` | QUST.alias 1: DLC2Init | quest progress: GetOwningQuest, GetStageDone, setstage | story | story |
| `DLC2MQ04PlayerScript` | QUST.alias 1: DLC2MQ04 | quest progress: GetOwningQuest, SetStage, getStageDone | story | story |
| `DLC2MQ05PlayerScript` | QUST.alias 1: DLC2MQ05 | quest progress: GetOwningQuest, GetStageDone, SetStage, getStageDone | story | story |
| `DLC2MQ06PlayerScript` | QUST.alias 1: DLC2MQ06 | quest progress: GetOwningQuest, GetStageDone, SetStage | story | story |
| `DLC2NelothScript` | NPC_ 1: DLC2Neloth | Neloth locks the door | unclear | technical |
| `DLC2PillarPlayerScript` | QUST.alias 1: DLC2Pillar | quest progress: CheckLocationAndRegisterForUpdate | story | story |
| `DLC2RavenRockDragonScript` | QUST.alias 1: DLC2Init | keyword data for the Raven Rock dragon | unclear | technical |
| `DLC2TelMithrynPlayerScript` | QUST.alias 1: DLC2DialogueTelMithryn | faction in Tel Mithryn | story | story |
| `DLC2ThirskFFHilundPlayerScript` | QUST.alias 1: DLC2ThirskFFHilund | quest progress: Enable, GetOwningQuest, GetStage, Stop | story | story |
| `DLC2TT1PlayerScript` | QUST.alias 1: DLC2TT1 | quest progress: GetStageDone, SetStage | story | story |
| `DLC2TTF1PlayerScript` | QUST.alias 1: DLC2TTF1 | quest progress: GetLevel, GetPlayer, SetStage | story | story |
| `DLC2TTR4bPlayerScript` | QUST.alias 1: DLC2TTR4b | quest progress: SetStage | story | story |
| `DLC2WECultistAmbushPlayerScript` | QUST.alias 1: DLC2CultistAmbush | quest progress: StopQuestIfNotInLocationAndCultistsNotLoaded | story | story |
| `dragonActorSCRIPT` | NPC_ 36, ACHR 5: BFBDragon, DLC1Durnehviir, DLC1DurnehviirSummon | NPC dragon: attack registration, phantom | unclear | encounter |
| `JailQuestPlayerScript` | QUST.alias 1: JailQuest | quest progress: GetLocation, GetOwningQuest, GetPlayer, GetStage, IsInLocation, SetStage | story | story |
| `MarriageFINPlayerScript` | QUST.alias 2: RelationshipMarriageFIN | quest progress: GetCurrentLocation, GetOwningQuest, GetRef, IsObjectiveDisplayed, SetStage | story | story |
| `MQ102PlayerScript` | QUST.alias 2: MQ102A, MQ102B | quest progress: GetOwningQuest, GetPlayer, GetStage, GetStageDone, IsInLocation, SetStage | story | story |
| `MQ104PlayerScript` | QUST.alias 1: MQ104 | quest progress: GetActorRef, GetOwningQuest, GetParentCell, GetPlayer, GetStageDone, GetValue | story | story |
| `MQ105PlayerScript` | QUST.alias 2: MQ105 | quest progress: GetOwningQuest, SetStage, getStageDone | story | story |
| `MQ106DelphineScript` | QUST.alias 1: MQ106 | quest progress: GetActorRef, GetDistance, GetOwningQuest, GetPlayer, GetStage, IsInCombat | story | story |
| `MQ106PlayerScript` | QUST.alias 1: MQ106 | quest progress: GetOwningQuest, SetStage, getStageDone | story | story |
| `MQ201PlayerScript` | QUST.alias 2: MQ201 | quest progress: GetCurrentLocation, GetOwningQuest, GetPlayer, GetStage, GetStageDone, setStage | story | story |
| `MQ202MakeAggressiveInLocation` | QUST.alias 2: MQ202 | quest progress: GetActorRef, GetActorValue, SetActorValue | story | story |
| `MQ203EsbernScript` | QUST.alias 1: MQ203 | quest progress: GetOwningQuest, GetStageDone, SetStage | story | story |
| `MQ203PlayerScript` | QUST.alias 1: MQ203 | quest progress: GetActorRef, GetOwningQuest, GetStage, GetStageDone, HasKeyword, Say | story | story |
| `MQ302PlayerScript` | QUST.alias 2: MQ302 | quest progress: GetOwningQuest, SetStage, getStageDone | story | story |
| `MQ304PlayerScript` | QUST.alias 2: MQ304 | quest progress: DisablePlayerControls, EnablePlayerControls, GetOwningQuest | story | story |
| `MS02PlayerScript` | QUST.alias 2: MS02 | quest progress: GetOwningQuest, GotoState, SetStage | story | story |
| `MS14AlvaScript` | QUST.alias 1: MS14 | Alva's crime faction | story | story |
| `PlayerWeddingScript` | QUST.alias 1: RelationshipMarriageWedding | quest progress: GetOwningQuest, GetStage, SetStage | story | story |
| `RemoveSilenceMUS` | QUST.alias 1: BardSongs | remove MUSTavernSilence silence (music) on any change | ambience | technical |
| `SvenScript` | QUST.alias 1: DialogueRiverwood_Revised | Sven's faction rank | unclear | technical |
| `TG00PlayerScript` | QUST.alias 1: TG00 | quest progress: GetCurrentLocation, GetPlayer, GetStage, SetStage | story | story |
| `TG01PlayerAliasScript` | QUST.alias 2: TG01 | quest progress: AddToFaction, Disable, EvaluatePackage, GetActorRef, GetStage, IsRunning | story | story |
| `WIPlayerScript` | QUST.alias 17: WIAddItem01, WICastMagic01, WICastMagic03 | stop the WI encounter on location change | ambience | technical |
| `WITavernPlayerScript` | QUST.alias 1: WITavern | stop WITavern if the player left the tavern | ambience | technical |

## 11. Trigger volumes at load doors

There are 2 713 volumes with `OnTriggerEnter` in scripts; 608 are within 512 units (plus half the volume's size) of a load door in the same cell or world. The first 25 scripts:

| At door / total | Script | Example cell |
|---|---|---|
| 176 / 819 | `FXCameraAttachScript` | GoldenglowEstate02 |
| 119 / 452 | `defaultSetStageTrigSCRIPT` | Ragnvald01 |
| 57 / 129 | `CWThreatTriggerScript` | DawnstarExterior04 |
| 33 / 33 | `BYOHHouseInteriorSwapTrigger` | 0xd74 |
| 30 / 181 | `defaultActivateSelf` | DaintySload01 |
| 28 / 87 | `defaultStartSceneTrigScript` | RiverwoodSleepingGiantInn |
| 23 / 216 | `defaultAddMusicSCRIPT` | BrokenTowerRedoubt02 |
| 23 / 178 | `WICommentTriggerScript` | LiarsRetreat01 |
| 9 / 19 | `DLC2ApocryphaBookMarkerTriggerScript` | DLC2Book01Dungeon |
| 7 / 14 | `defaultSetStageOnEnter` | RiverwoodSleepingGiantInn |
| 7 / 10 | `CWDisableHoldPositionMarkerTrigger` | RiftenOrigin |
| 6 / 25 | `defaultPlayerEnableDisableLinkedRef` | StillbornCave01 |
| 6 / 10 | `defaultOnEnter` | ThalmorEmbassy01 |
| 5 / 6 | `StartSceneTriggerSpecificTime` | WhiterunDragonsreachBasement |
| 5 / 11 | `dunCGEnableDisableScript` | HelgenExterior02 |
| 3 / 65 | `TrapTriggerBASE` | BlindCliffCave01 |
| 3 / 66 | `defaultSetStageTRIGSpecificActor` | HunterShack |
| 3 / 4 | `DA07MuseumTriggerScript` | DawnstarSilussHouse |
| 3 / 14 | `defaultEnableDisableLinkedRef` | DLC1VampireCastleExterior09 |
| 2 / 6 | `defaultActorEnableDisableLinkedRef` | SnaplegCave01 |
| 2 / 2 | `TG03BreadcrumbTriggerScript` | HonningbrewMeadery01 |
| 2 / 2 | `MQ101InsideHelgenSetStageScript` | HelgenKeep01 |
| 2 / 5 | `HelgenRandomSoundScript` | HelgenKeep01 |
| 2 / 2 | `DunBrokenOarMakeEnemySCRIPT` | BrokenOarGrotto01 |
| 2 / 2 | `dunGlenmorilWeather` | DarklightTowerExterior02 |

Scripts `default*` set a stage, start a scene, or enable references when the player first enters the volume. With a portal, they fire the same way if the player walks through the volume physically. The dangerous part is different: moving the player back and forth through the volume at the door (for example, a portal teleport back) will produce `OnTriggerLeave`/`OnTriggerEnter` twice.

## 12. Tags: manual and data-derived (T30)

Markup discrepancies across the 186 `CLOC` quests (section 4.5). None of them affect the T30 policy: in both markups, `restarts` is only `BardSongStarter`.

| Tag | Manual / data-derived | Manual only | Data-derived only | Why |
|---|---|---|---|---|
| `story` | 150 / 119 | 46: dungeon quests `dun*`, `DLC1LD_*`, `DLC2dun*`, `TG04Brinewater`, `TG06_dun…`, `MS05_dun…`, `MQ201ThalmorEmbassy`; "kickers" `MS11Kicker`, `DLC2WaterStoneSailAwayQuest`, `CRHoldExpansion`, `CWGarrisonObserver1`–`3`; `FreeformWinterholdCollegeA`, `FreeformFrostRiver`, `Favor017`, `DA11IntroScene`, `BYOHRelationshipAdoptableOrphanageCL` | 15: short scenes with conditions on other quests' progress (`CW00/01*MapTableScene`, `DialogueRiverwood*`, `DialogueWhiterunArgueScene`, `DialogueMarkarthPostAttackScene01`, `MS13LucanCamillaScene`); encounters with objectives (`WIChangeLocation03`, `04`, `05`); `WIThief01`, `DLC1ScoutPatrol`, `DLC2AshSpawnAttack` | dungeons have no journal; stages and scenes drive their progress. A condition on another quest does not make a short scene story |
| `once` | 65 / 63 | `DialogueRiftenSS04`, `DialogueRiftenSS05` | — | one-shot behavior is set by a global set by the scene; the data does not show who sets it |
| `scene` | 66 / 66 | — | — | — |
| `greets` | 12 / 6 | `WICourier`, `WIChangeLocation03`, `04`, `05`, `08`, `BYOHHouseStewardOffer`, `DA14Start`, `DLC2CultistAmbush` | `DialogueRiftenSS04`, `MS08ArgueQuest` | address via a ForceGreet package, not a scene; false positives are NPCs talking to each other (both `once`) |
| `encounter` | 21 / 18 | `WICourier`, `DLC2CultistAmbush`, `DLC2ThirskRieklingSpawner` | — | NPC spawn without `GetRandomPercent` |
| `technical` | 45 / 55 | — | `DLC1EclipseAttack1`–`6`, `DLC1ScoutPatrol`, `DLC2AshSpawnAttack`, `DLC2ThirskRieklingSpawner`, `DA09ChangeLocation` | spawners act through other quests and scripts: they have no scenes or lines of their own |
| `restarts` | 1 / 1 | — | — | — |
| `placeKind`, `placeSpecific` | 48 / 48, 145 / 145 | — | — | manual ones are taken from the data: they are the conditions |
