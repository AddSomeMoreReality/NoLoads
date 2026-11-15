# T34–T41 plan: Ghidra database, player and cell loading maps, multiplayer

Prepared by the Ghidra database curator (T33) on 07.11.2026 from the user's tasks. Briefs are `T34`…`T41` in this folder. The coordinator adds them to the README table and to `HANDOFF.md`.

## Tasks

| ID | Task | Who | Code | Run | Depends on |
|---|---|---|---|---|---|
| [T35](T35-ghidra-interface.md) | Database interface: MCP server, skill, reader agent | curator | `tools/ghidra/`, `.mcp.json`, `.claude/` | none | T33 |
| [T36](T36-ghidra-types.md) | NG types in the database: structures, vtables, `this`, singletons | curator | `tools/ghidra/` | none | T35 |
| [T34](T34-ghidra-check-names.md) | Check discrepancies, name unnamed functions | curator | `tools/ghidra/` | none | T35 (T37, T41 — candidates as available) |
| [T37](T37-creation-kit.md) | Creation Kit: whether worthwhile, name candidates | executor | `tools/ghidra/ck/` | none | T35 |
| [T38](T38-player-map.md) | Player map (`PlayerCharacter` and everything related to the player) | executor | none | optional, T40 | T35, preferably T36 |
| [T39](T39-cell-loading-map.md) | Cell loading map | executor | none | old traces; optional T40 | T35, preferably T36 |
| [T40](T40-call-probe.md) | Probe build: call counters and samples of all threads | executor | `src/CallProbe.*`, `Profiler` | yes, one for T38+T39 | ID lists from T38/T39; merge T32 earlier |
| [T41](T41-multiplayer-mods.md) | Multiplayer mods and multiple players in one world | executor | none | none | — |

## Order

```
curator:   T35 ──► T36 ──► T34 ──────────────► (names from T37, T41, T38, T39)
executors: T41 ─────────────────────────────►
           T37 (after T35; not during curator analysis) ──► candidates for curator
           T38 ┐ (after T35, preferably after T36)
           T39 ┘ ──► ID lists ──► T40 (build) ──► user run ──► analysis in T38/T39
```

- **Immediately and in parallel:** T35 (curator) and T41 (web; database not needed until question 4).
- **After T35:** T37 and the static parts of T38, T39. The curator performs T36 and T34 sequentially: one project, one writer.
- **T40** starts when T38/T39 provide ID lists. One user run covers both tasks.
- **Memory (15.8 GB RAM):** Ghidra analysis (T37, repeated `analyze.ps1` in T34) uses up to 10 GB. Do not run analyses together with each other or with the game. The query server uses 0.7 GB; run `gq.py stop` before playing.
- **T32 proceeds in parallel:** T38 provides its static map and does not repeat the read inventory. T40 reuses the T32 stub generator, so T32 should preferably be merged before T40.

## Models and effort

| Task | Model | Effort | Subagents |
|---|---|---|---|
| Coordinator | Opus 5.5 | medium | — |
| T35 interface | Opus 5.5 | medium | none |
| T36 types | Opus 5.5 | high | none (NG analysis is subtle, layout errors are silent) |
| T34 checking and names | Opus 5.5 | high | Sonnet 5.5 / medium — candidate collectors (Papyrus table, console commands, third-party tables); Haiku 5.5 / low — bulk classification of Function ID names as “library or Bethesda” |
| T37 Creation Kit | Opus 5.5 | high | none (judgment about mapping accuracy) |
| T38 player map | Opus 5.5 | high | T35 reader agent on Sonnet 5.5 / medium — batches of 20–40 functions, conclusions as output; Haiku 5.5 / low — classification of hundreds of small functions by subsystem |
| T39 loading map | Opus 5.5 | high | same as T38 |
| T40 probe build | Opus 5.5 | high | none (patches in existing code, threads) |
| T41 multiplayer | Sonnet 5.5 | high | web-fetch for pages and repositories; final synthesis can be assigned to Opus 5.5 / medium |

- Subagents receive the database MCP tools after T35. They return pseudocode as conclusions with addresses, not as text.
- The curator and executors of T36–T39 start **in a new chat**: T33 context is almost exhausted. Use `docs/GHIDRA.md`, `results/T33.md`, and the brief as references.

## Runs

- **One** is needed — using the T40 build, for T38 and T39 (which functions run during each transition phase and on which thread). No ready build exists yet: the `CallProbe` module does not exist yet, and T38/T39 provide the ID lists. The T40 chat provides the build and run steps.
- Before that, T39 analyzes old traces (`logs/run-*`, `profile` records under the loading screen) using names from the database. This costs nothing.
- T34–T37 and T41 require no runs.

## User decisions

- T35: `.mcp.json` in the repository root (the `ghidra` server applies to all chats) and the `.claude/` folder.
- T37: new Ghidra project `My attempts\SkyrimCK-1.6.1378` (about 1.5 GB) and analysis time without the game running.
- T34: third-party name tables only after license verification; the source list is in `docs/GHIDRA.md`.
