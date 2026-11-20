# T35. Interface to the Ghidra base for chats and subagents (MCP, skill, reader agent)

Stage 2, auxiliary, **first of the curator's tasks**: the other tasks (T37–T39) work through it. Executor — the base curator. Branch `task/T35-ghidra-mcp`. Code: `tools/ghidra/`, `.mcp.json` in the repository root, `.claude/skills/ghidra/`, `.claude/agents/`. No run.

## What there is (T33)

- `tools/ghidra/gq.py` — CLI, stdlib. The `gserver.py` server (PyGhidra) works on a read-only copy, first call about 8 s, `decomp` 0.1–0.7 s, queries run one at a time.
- Inconveniences: the chat must know the commands and the scratchpad path. The result is a file that must be read separately. No search by names and strings, call graph, or vtable slot list. Subagents do not know about the tool.

## To do

1. **MCP server** `tools/ghidra/mcp_server.py`: stdio, JSON-RPC, stdlib, no `mcp` package. Forwards requests to `gserver.py` and starts it itself. Tools:
   - `ghidra_decompile(targets, mark?, addr_prefix?, max_lines?)` — a short function is returned as text, a long one — as a file and an excerpt around the marks;
   - `ghidra_asm`, `ghidra_xrefs`, `ghidra_info`, `ghidra_fieldrefs` — as in `gq.py`;
   - `ghidra_search(name_regex | string_regex)` — symbols and strings with the places that reference them;
   - `ghidra_callgraph(target, callers|callees, depth)` — tree with names, node count limit;
   - `ghidra_vtable(class)` — slots: offset, function, whose (own method or inherited);
   - `ghidra_struct(name)` — when T36 lands.
   - Responses with `AE#id` and names. Large output — to a file (`out` from the argument or `%TEMP%\clr-ghidra\out\<session>`).
2. **Server:** 2–3 decompiler instances in a pool, so subagents do not queue. Pseudocode cache by address and copy version. Verify that a parallel decompiler does not corrupt responses.
3. **Project `.mcp.json`** (server `ghidra`): **the user's consent first**, the file affects all chats. Verify that MCP tools are visible to subagents.
4. **Skill** `.claude/skills/ghidra/SKILL.md`: when and how to use it, the "Bethesda code does not go into git" rule, how to ask the curator for a name.
5. **Reader agent** `.claude/agents/re-reader.md`: read-only (MCP `ghidra`, Read, Grep). Sonnet model, answers with conclusions and addresses, no long pseudocode. For mass annotation — a Haiku variant.
6. Measure: MCP call time, 3 parallel requests, server memory.
7. Update `docs/GHIDRA.md`. Propose edits to `CLAUDE.md` and the task README to the coordinator.

## Result

`docs/tasks/results/T35.md`:
- tool list;
- measurements;
- what subagents see;
- how to add a tool.

## Conflicts

T34, T36 — `tools/ghidra/`, same curator, one at a time. `.claude/` — new.
