# T00. Shorten `CLAUDE.md` and `HANDOFF.md`

Branch `task/T00-slim-docs`. No code, no game run. This is a mechanical task suitable for a simpler model.

## Why

- `CLAUDE.md` (895 lines, 123 KB) is loaded in full into every chat.
- `docs/HANDOFF.md` (52 KB) was the first file every chat read.

This consumes tens of thousands of tokens before the first step, and there are now many chats (`docs/tasks/README.md`).

## Tasks

1. **`CLAUDE.md` ≤ 250 lines.** It retains:
   - communication language;
   - git rule;
   - the “coordinator / executor” roles and a link to `docs/tasks/README.md`;
   - environment (section 1, condensed);
   - build and packaging (section 2, condensed);
   - safety rules (section 7): all items, but briefly;
   - requirements 5.14 in full, including item 7;
   - document map: which file covers what.
2. **Move everything else without loss, keeping the text unchanged:**
   - section 3 (engine map) → `docs/ENGINE_MAP.md`;
   - sections 0, 4, 5.1–5.3 and 6 (original task, hypotheses, trace, automated test, analysis) → `docs/ORIGINAL_TASK.md`;
   - 5.4–5.13 and 5.15 (INI, modules, portal) → `docs/FEATURES.md`, with a module table of contents;
   - section 9 (version history) → `docs/HISTORY.md`.
3. **`docs/HANDOFF.md` ≤ 120 lines:**
   - status: what works and what does not;
   - immediate plan — a link to `docs/tasks/README.md`;
   - links to documents.

   Its section 3 (architecture) and sections 4 and 8 (dead ends, shortcomings) → `docs/ARCHITECTURE.md`. Delete obsolete material (6.1 “analysis of 0.8.21”, 6.3) and list it in the result.
4. **References.** Update references of the form “`CLAUDE.md`, 5.12” in `docs/` and in `src/` comments to point to the new files. Find them with Grep `CLAUDE.md, [0-9]` and `раздел 5\.`.
5. **Losslessness check.** Script in the scratchpad: every nonempty line from the old `CLAUDE.md` and `HANDOFF.md` must be found in one of the new files. Exceptions are intentionally condensed lines; list them in the result.

## Not allowed

- Changing the meaning of the rules. When in doubt, leave the text in `CLAUDE.md`.
- Touching code.
