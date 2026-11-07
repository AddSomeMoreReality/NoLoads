#!/usr/bin/env python3
"""MCP server (stdio, JSON-RPC 2.0, standard library only) over the Ghidra base of SkyrimSE.exe.

Forwards tool calls to gserver.py (starts it on demand, like gq.py) and turns the answers into short text:
short pseudocode inline, long pseudocode as a file plus excerpts around the marked lines. Tool calls run in
parallel threads; the Ghidra server decompiles with a pool of decompilers.

  .mcp.json:  {"mcpServers": {"ghidra": {"command": "python", "args": ["tools/ghidra/mcp_server.py"]}}}
  self-test:  python tools/ghidra/mcp_server.py --selftest

Output files: the tool argument `out`, else %TEMP%\\clr-ghidra\\out\\mcp-<date>-<pid> (kept 7 days).
Adding a tool: an entry in TOOLS (name, description, schema, handler) and, if Ghidra must do new work,
a command in gserver.py (see its docstring).
"""
import concurrent.futures
import json
import os
import shutil
import sys
import threading
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gconfig  # noqa: E402
import gq  # noqa: E402

SERVER_NAME = "ghidra"
SERVER_VERSION = "1.0"
PROTOCOLS = ["2025-06-18", "2025-03-26", "2024-11-05"]
SESSION = f"mcp-{time.strftime('%Y%m%d-%H%M%S')}-{os.getpid()}"
OUT_DIR = gconfig.STATE_DIR / "out" / SESSION
LOG_FILE = gconfig.STATE_DIR / "mcp.log"

INSTRUCTIONS = """Read-only Ghidra base of SkyrimSE.exe 1.6.1170 (AE). Targets: AE#13316, AE#13316+0x65C, \
0x14019D490 (VA), 0x19D490 (RVA) or a name like TES::SetCurrentCell. Names come from CommonLibSSE-NG, the \
curator list and Address Library (AE_<id> = unnamed). Pseudocode and the project are Bethesda code: never put \
them into git or docs, only conclusions with addresses. Missing name, type or analysis -> ask the curator of the \
base (chat T33/T35) through the user. Long outputs are files: read them with Read (offset/limit)."""

_write_lock = threading.Lock()
_ensure_lock = threading.Lock()


def log(msg):
    try:
        gconfig.STATE_DIR.mkdir(parents=True, exist_ok=True)
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write(f"{time.strftime('%H:%M:%S')} [{os.getpid()}] {msg}\n")
    except OSError:
        pass


def send(obj):
    data = (json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8")
    with _write_lock:
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()


# ------------------------------------------------------------------------------------------------ Ghidra calls
def ghidra(req):
    """Send one request to gserver, starting it if needed; raise RuntimeError with its message on failure."""
    with _ensure_lock:
        try:
            gq.ensure_server(log=lambda m: log(m))
        except gq.ServerError as e:
            raise RuntimeError(str(e)) from None
    try:
        res = gq.call(req)
    except (OSError, ValueError):
        with _ensure_lock:  # the server may have exited between ping and call (idle timeout, snapshot)
            gq.ensure_server(log=lambda m: log(m))
        res = gq.call(req)
    if not res.get("ok"):
        raise RuntimeError(res.get("error", "unknown error") + ("\n" + res["trace"] if res.get("trace") else ""))
    return res


def out_dir(args):
    d = args.get("out") or str(OUT_DIR)
    os.makedirs(d, exist_ok=True)
    return os.path.abspath(d)


def timing(res):
    q = res.get("queued") or 0
    return f"[{res.get('seconds')} s in Ghidra" + (f", {q} s waiting for a decompiler" if q else "") + "]"


def excerpt(path, header_lines, mark_rows, max_lines, context, marker="//>>>"):
    """Whole file if short; else the header plus windows around marked lines (numbered as in the file)."""
    with open(path, encoding="utf-8") as fh:
        lines = fh.read().splitlines()
    body = len(lines) - header_lines
    if body <= max_lines:
        return "\n".join(lines), False
    if not mark_rows:
        mark_rows = [i + 1 for i, ln in enumerate(lines) if marker in ln and i >= header_lines]
    head = lines[:header_lines]
    if not mark_rows:
        shown = lines[header_lines:header_lines + max_lines]
        return "\n".join(head + shown + [f"... {body - max_lines} more lines: Read {path} "
                                         f"offset={header_lines + max_lines + 1}"]), True
    keep = set()
    for r in mark_rows:  # 1-based file line numbers
        keep.update(range(max(header_lines + 1, r - context), min(len(lines), r + context) + 1))
    keep = sorted(keep)[:max_lines]
    outl, prev = list(head), None
    for n in keep:
        if prev is not None and n != prev + 1:
            outl.append(f"      ... lines {prev + 1}-{n - 1} omitted")
        elif prev is None and n > header_lines + 1:
            outl.append(f"      ... lines {header_lines + 1}-{n - 1} omitted")
        outl.append(f"{n:5d}: {lines[n - 1]}")
        prev = n
    if prev is not None and prev < len(lines):
        outl.append(f"      ... lines {prev + 1}-{len(lines)} omitted")
    outl.append(f"(excerpt: {len(keep)} of {body} lines; whole function: Read {path})")
    return "\n".join(outl), True


# ------------------------------------------------------------------------------------------------ tools
def t_decompile(a):
    targets = list(a["targets"])
    marks = a.get("marks") or []
    if marks:
        targets = [f"{t}+{m}" if "+" not in t else t for t in targets for m in marks]
    res = ghidra(dict(cmd="decomp", targets=targets, out=out_dir(a), addr=bool(a.get("addr_prefix"))))
    parts = []
    for r in res.get("results", []):
        text, cut = excerpt(r["file"], r.get("header", 0), r.get("mark_rows"),
                            int(a.get("max_lines") or 150), int(a.get("context") or 10))
        src = r.get("source", "")
        parts.append(f"=== {r['function']}: {r['lines']} lines ({src}), file {r['file']}" +
                     (" - excerpt" if cut else "") + "\n" + text)
    parts += [f"error: {e}" for e in res.get("errors", [])]
    return "\n\n".join(parts) + "\n" + timing(res)


def t_asm(a):
    res = ghidra(dict(cmd="asm", targets=list(a["targets"]), out=out_dir(a)))
    parts = []
    for path in res.get("files", []):
        with open(path, encoding="utf-8") as fh:
            n_head = sum(1 for ln in fh if ln.startswith("//"))
        text, cut = excerpt(path, n_head, None, int(a.get("max_lines") or 150), int(a.get("context") or 15))
        parts.append(f"=== {path}" + (" - excerpt" if cut else "") + "\n" + text)
    parts += [f"error: {e}" for e in res.get("errors", [])]
    return "\n\n".join(parts) + "\n" + timing(res)


def t_text(cmd, **fields):
    def run(a):
        req = dict(cmd=cmd, out=out_dir(a))
        for k, v in fields.items():
            req[k] = v(a) if callable(v) else a.get(v)
        res = ghidra(req)
        return (res.get("text") or "") + "\n" + timing(res)
    return run


def t_fieldrefs(a):
    off = a["offset"]
    off = int(off, 0) if isinstance(off, str) else int(off)
    if not (a.get("within") or a.get("users_of") or a.get("all")):
        raise RuntimeError("fieldrefs needs within, users_of or all")
    res = ghidra(dict(cmd="fieldrefs", offset=off, out=out_dir(a), access=a.get("access"),
                      all=bool(a.get("all")), users_of=a.get("users_of"), **{"in": a.get("within")}))
    return f"{res.get('text')}\nfull list: {res.get('file')}\n{timing(res)}"


def t_status(a):
    st = gq.running()
    if st is None:
        return "Ghidra server not running (the next tool call starts it, about 10 s)."
    detail = ""
    if st.get("ready") and not st.get("error"):
        detail = gq.call({"cmd": "status"}).get("text", "")
    return (f"server pid {st.get('pid')}, protocol {st.get('protocol', 1)}, uptime {st.get('uptime')} s, "
            f"{st.get('requests')} requests, {st.get('busy', '?')} running; {detail}\n"
            f"MCP session output: {OUT_DIR}")


S_TARGET = {"type": "string", "description": "AE#id[+0xOFF], 0x VA/RVA, or a name (Class::Method)"}
S_TARGETS = {"type": "array", "items": S_TARGET, "minItems": 1}
S_OUT = {"type": "string", "description": "directory for output files (default: per-session temp dir)"}


def schema(props, required=()):
    return {"type": "object", "properties": props, "required": list(required), "additionalProperties": False}


TOOLS = [
    ("ghidra_decompile",
     "Pseudocode of the functions containing the targets. Lines with target offsets (AE#id+0xOFF, or `marks`) "
     "are marked //>>>. Short functions come back whole, long ones as a file plus numbered excerpts around marks.",
     schema({"targets": S_TARGETS,
             "marks": {"type": "array", "items": {"type": "string"},
                       "description": "offsets like 0x65C to mark in every target without its own +offset"},
             "addr_prefix": {"type": "boolean", "description": "prefix lines with /*instruction offset*/"},
             "max_lines": {"type": "integer", "description": "inline limit, default 150"},
             "context": {"type": "integer", "description": "lines around each mark in an excerpt, default 10"},
             "out": S_OUT}, ["targets"]),
     t_decompile),
    ("ghidra_asm",
     "Disassembly of the functions containing the targets: offset, instruction, call/data target names. "
     "Target offset marked //>>>; long functions: file plus excerpt.",
     schema({"targets": S_TARGETS, "max_lines": {"type": "integer"}, "context": {"type": "integer"},
             "out": S_OUT}, ["targets"]),
     t_asm),
    ("ghidra_xrefs",
     "All references to a function or data: calling places (function+offset, AE#), vftable slots, data reads/"
     "writes. Exception-table references are listed last. Over 60 rows: file plus first rows.",
     schema({"target": S_TARGET, "out": S_OUT}, ["target"]),
     t_text("xrefs", target="target")),
    ("ghidra_info",
     "Address, RVA, AE# ids, name and its source comment, size, number of callers and callees.",
     schema({"targets": S_TARGETS}, ["targets"]),
     t_text("info", targets="targets")),
    ("ghidra_fieldrefs",
     "Instructions with a [reg + offset] operand (field access candidates, no type check): within given "
     "functions, in all functions referencing given data (users_of, e.g. AE#403521 = player singleton), or the "
     "whole code by bytes (all, offset >= 0x80). access: R, W, A (lea).",
     schema({"offset": {"type": ["string", "integer"], "description": "0x1D0"},
             "within": {"type": "array", "items": {"type": "string"}},
             "users_of": {"type": "array", "items": {"type": "string"}},
             "all": {"type": "boolean"}, "access": {"type": "string"}, "out": S_OUT}, ["offset"]),
     t_fieldrefs),
    ("ghidra_search",
     "Find symbols by regex over full names (Class::Method; AE_<id> labels excluded) and/or defined strings, "
     "each with its address, AE#, reference count and first referencing places (calls first).",
     schema({"name_regex": {"type": "string"}, "string_regex": {"type": "string"},
             "kinds": {"type": "array", "items": {"type": "string"},
                       "description": "symbol types: Function, Label, Class, Namespace"},
             "max_results": {"type": "integer", "description": "default 40"},
             "refs": {"type": "integer", "description": "referencing places per hit, default 5"},
             "case": {"type": "boolean", "description": "case-sensitive, default false"}, "out": S_OUT}),
     t_text("search", name_regex="name_regex", string_regex="string_regex", kinds="kinds",
            max_results="max_results", refs=lambda a: a.get("refs", 5), case="case")),
    ("ghidra_callgraph",
     "Call tree of a function: callers (direct calls, tail jumps, vftable slots holding it) or callees "
     "(direct calls; indirect vtable calls are counted). Names with AE#, call-site offsets; repeated nodes "
     "marked (see above).",
     schema({"target": S_TARGET, "direction": {"type": "string", "enum": ["callers", "callees"]},
             "depth": {"type": "integer", "description": "default 2"},
             "max_nodes": {"type": "integer", "description": "default 150"}, "out": S_OUT}, ["target"]),
     t_text("callgraph", target="target", direction=lambda a: a.get("direction", "callers"), depth="depth",
            max_nodes="max_nodes")),
    ("ghidra_vtable",
     "Virtual function tables of a class from MSVC RTTI: per subobject, slot index, byte offset, function "
     "(name, AE#) and whether it is inherited from the base, overrides it, or is new. `offset` picks one "
     "subobject (0 = primary).",
     schema({"class": {"type": "string", "description": "e.g. PlayerCharacter"},
             "offset": {"type": ["string", "integer"]}, "out": S_OUT}, ["class"]),
     t_text("vtable", name="class", offset="offset")),
    ("ghidra_struct",
     "Data type layout from the Ghidra base (fields with offsets, enums, typedefs). NG structures (TES, "
     "PlayerCharacter, TESObjectCELL) arrive with task T36; until then mostly Windows/RTTI types.",
     schema({"name": {"type": "string"}, "out": S_OUT}, ["name"]),
     t_text("struct", name="name")),
    ("ghidra_status",
     "State of the Ghidra query server: pid, snapshot of the base, decompiler pool, cache hits.",
     schema({}),
     t_status),
]
HANDLERS = {name: fn for name, _, _, fn in TOOLS}


def tool_list():
    return [{"name": n, "description": d, "inputSchema": s,
             "annotations": {"readOnlyHint": True, "openWorldHint": False}} for n, d, s, _ in TOOLS]


# ------------------------------------------------------------------------------------------------ JSON-RPC
def call_tool(msg_id, params):
    name = params.get("name")
    args = params.get("arguments") or {}
    t0 = time.time()
    try:
        fn = HANDLERS.get(name)
        if fn is None:
            raise RuntimeError(f"unknown tool {name}")
        text, err = fn(args), False
    except Exception as e:  # noqa: BLE001
        text, err = f"error: {e}", True
        if not isinstance(e, (RuntimeError, KeyError, ValueError)):
            text += "\n" + traceback.format_exc()[-1200:]
    log(f"{name} {json.dumps(args, ensure_ascii=False)[:200]} -> {time.time() - t0:.2f} s err={err}")
    send({"jsonrpc": "2.0", "id": msg_id,
          "result": {"content": [{"type": "text", "text": text}], "isError": err}})


def handle(msg, pool):
    method = msg.get("method")
    msg_id = msg.get("id")
    if method is None:  # a response to us; we send no requests
        return
    if msg_id is None:  # notification: initialized, cancelled, ...
        return
    if method == "initialize":
        want = (msg.get("params") or {}).get("protocolVersion")
        send({"jsonrpc": "2.0", "id": msg_id, "result": {
            "protocolVersion": want if want in PROTOCOLS else PROTOCOLS[0],
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
            "instructions": INSTRUCTIONS}})
    elif method == "ping":
        send({"jsonrpc": "2.0", "id": msg_id, "result": {}})
    elif method == "tools/list":
        send({"jsonrpc": "2.0", "id": msg_id, "result": {"tools": tool_list()}})
    elif method == "tools/call":
        pool.submit(call_tool, msg_id, msg.get("params") or {})
    elif method in ("resources/list", "prompts/list"):
        send({"jsonrpc": "2.0", "id": msg_id, "result": {method.split("/")[0]: []}})
    else:
        send({"jsonrpc": "2.0", "id": msg_id, "error": {"code": -32601, "message": f"method not found: {method}"}})


def clean_old_outputs(days=7):
    root = gconfig.STATE_DIR / "out"
    if not root.is_dir():
        return
    limit = time.time() - days * 86400
    for d in root.iterdir():
        try:
            if d.is_dir() and d.name.startswith("mcp-") and d.stat().st_mtime < limit:
                shutil.rmtree(d, ignore_errors=True)
        except OSError:
            pass


def serve():
    log(f"start {SESSION}, cwd {os.getcwd()}")
    clean_old_outputs()
    pool = concurrent.futures.ThreadPoolExecutor(max_workers=8)
    for raw in sys.stdin.buffer:
        raw = raw.strip()
        if not raw:
            continue
        try:
            msg = json.loads(raw.decode("utf-8"))
        except ValueError as e:
            send({"jsonrpc": "2.0", "id": None, "error": {"code": -32700, "message": f"parse error: {e}"}})
            continue
        for m in (msg if isinstance(msg, list) else [msg]):
            try:
                handle(m, pool)
            except Exception as e:  # noqa: BLE001
                log(f"handle: {e}\n{traceback.format_exc()}")
                if isinstance(m, dict) and m.get("id") is not None:
                    send({"jsonrpc": "2.0", "id": m["id"], "error": {"code": -32603, "message": str(e)}})
    pool.shutdown(wait=True)
    log(f"stdin closed, exit {SESSION}")


def selftest():
    """Start this file as an MCP child process, list tools, run a few calls (3 in parallel), print timings."""
    import subprocess
    p = subprocess.Popen([sys.executable, os.path.abspath(__file__)], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    lock = threading.Lock()
    pending = {}

    def reader():
        for line in p.stdout:
            m = json.loads(line)
            with lock:
                ev = pending.get(m.get("id"))
            if ev:
                ev[1] = m
                ev[0].set()
    threading.Thread(target=reader, daemon=True).start()
    counter = [0]

    def rpc(method, params=None):
        with lock:
            counter[0] += 1
            i = counter[0]
            pending[i] = [threading.Event(), None]
        p.stdin.write((json.dumps({"jsonrpc": "2.0", "id": i, "method": method, "params": params or {}}) + "\n")
                      .encode())
        p.stdin.flush()
        pending[i][0].wait(600)
        return pending[i][1]

    t = time.time()
    init = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "t"}})
    print("initialize:", init["result"]["serverInfo"], init["result"]["protocolVersion"], f"{time.time() - t:.2f} s")
    p.stdin.write(b'{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
    p.stdin.flush()
    tools = rpc("tools/list")["result"]["tools"]
    print("tools:", ", ".join(x["name"] for x in tools))

    def tool(name, args):
        t0 = time.time()
        r = rpc("tools/call", {"name": name, "arguments": args})["result"]
        txt = r["content"][0]["text"]
        return time.time() - t0, r["isError"], txt

    for name, args in [("ghidra_status", {}), ("ghidra_info", {"targets": ["AE#40466"]}),
                       ("ghidra_decompile", {"targets": ["AE#13316"], "marks": ["0x65C"], "max_lines": 40})]:
        dt, err, txt = tool(name, args)
        print(f"{name}: {dt:.2f} s err={err} {len(txt.splitlines())} lines: {txt.splitlines()[0][:110]}")
    batch = ["AE#19130", "AE#36629", "AE#40411"]
    res = [None] * 3

    def one(i):
        res[i] = tool("ghidra_decompile", {"targets": [batch[i]], "max_lines": 1000})
    t0 = time.time()
    ths = [threading.Thread(target=one, args=(i,)) for i in range(3)]
    for th in ths:
        th.start()
    for th in ths:
        th.join()
    print(f"3 parallel decompiles: {time.time() - t0:.2f} s wall; each " +
          ", ".join(f"{r[0]:.2f} s" for r in res))
    p.stdin.close()
    p.wait(30)


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        selftest()
    else:
        serve()
