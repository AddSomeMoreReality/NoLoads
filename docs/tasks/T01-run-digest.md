# T01. Run summary, branch in archive name and log

Branch `task/T01-run-digest`. Code: `tools/`, `scripts/build.ps1`, a small amount in `src/`. No game run: verify against existing logs.

## Why

Currently every trace analysis uses one-off scripts and their lengthy output in the chat context. A single tool with a short summary is needed. Builds from different branches also need to be distinguishable in Vortex and in logs.

## Tasks

1. **`tools/run_digest.py <run folder> [--full]`.**
   - Standard library only, Python 3.12.
   - Writes `<folder>/digest.md` in UTF-8 and prints only the path to the console: Cyrillic breaks in the console.
   - Each section ≤ 40 lines; `--full` removes the limit.
   - Names in the trace are UTF-8; the trace is `CellLoadResearch.trace.jsonl`, one JSON record per line.

   Sections:
   - **Transitions:** time, source → destination (`event.ActorCell`), durations from `transition.visible` and `loading.summary`.
   - **Keys:** `input.hotkey`.
   - **Portal:** `portal.start`, detach (`interior.sim` with `action` = `detach`), `portal.fault`. For each transition — the last `interior.sim` `status` before it. Key counters from that record:
     - `portal.passes`;
     - `soundsCarried`, `subtitlesCarried`, `distancesCarried`;
     - `imageSpace.swaps` and `imageSpace.held`;
     - `blocker`.

     Find them by examining one `status` record.
   - **`actor.jump`:**
     - by actor: name, count, sum and maximum of `distance`;
     - by stack signature: first 3 frames excluding `CellLoadResearch.dll`;
     - for each jump — time relative to the nearest transition (“+0.4 s after exit”).
   - **`scene.change`:** scenes by quest, time relative to transitions.
   - **Failures:** `frame.hitch`, `portal.fault`, `[error]` and `[warning]` lines from `CellLoadResearch.log`, presence of `*.hang.txt`.

   Verify against `logs/run-0.8.25/` and `logs/run-0.8.24/`. Attach the output for 0.8.25 (≤ 150 lines) to the result.
2. **Archive name.** `scripts/build.ps1` appends the branch name if it is neither `feature/interior-portal` nor `main`. Example: branch `exp/T10-bard-song` → `CellLoadResearch-0.8.26-T10-bard-song.zip`.
3. **Branch and short commit hash** — in the log and in the trace `info` record at startup. For example, `build.ps1` writes a generated header (in `.gitignore`), or a define in `xmake.lua`. Building without git must not fail.
4. **`tools/analyze_trace.py`:** identify overlapping functionality. Do not delete it; state in the result which parts are obsolete.

## Not allowed

- Changing the trace format.
