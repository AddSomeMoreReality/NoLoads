#!/usr/bin/env python3
"""Query the Ghidra base of SkyrimSE.exe (read-only). Standard library only; any Python 3.

Targets: AE#13316, AE#13316+0x65C, 0x14019D490 (VA), 0x19D490 (RVA), TES::SetCurrentCell[+0x10].
Output files go to --out (your scratchpad); the console gets a short summary.

  python tools/ghidra/gq.py decomp AE#13316+0x65C AE#40466 --out <scratchpad>      # pseudocode, marks lines
  python tools/ghidra/gq.py decomp AE#13316 --addr --out <scratchpad>              # + /*offset*/ per line
  python tools/ghidra/gq.py asm AE#19130 --out <scratchpad>                        # disassembly with offsets
  python tools/ghidra/gq.py xrefs AE#40466 [--out <scratchpad>]                    # callers, vtable slots, data refs
  python tools/ghidra/gq.py info AE#36629 AE#40411
  python tools/ghidra/gq.py fieldrefs 0x1D0 --users-of AE#403521 --out <scratchpad> # [reg+0x1D0] in functions using the player
  python tools/ghidra/gq.py fieldrefs 0x1D0 --in AE#13316 AE#40466 --access W --out <scratchpad>
  python tools/ghidra/gq.py fieldrefs 0x6AA --all --out <scratchpad>               # whole code, offsets >= 0x80
  python tools/ghidra/gq.py status | stop

The first call starts the server (about a minute); it exits after 20 idle minutes. Run `stop` before the game
if memory is short. Requests from several chats are queued, one at a time.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gconfig  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def call(req, timeout=1800):
    with socket.create_connection((gconfig.SERVER_HOST, gconfig.SERVER_PORT), timeout=timeout) as s:
        s.sendall((json.dumps(req) + "\n").encode("utf-8"))
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
    return json.loads(buf.decode("utf-8"))


def running():
    try:
        return call({"cmd": "ping"}, timeout=5)
    except OSError:
        return None


def spawn(version):
    gconfig.STATE_DIR.mkdir(parents=True, exist_ok=True)
    out = open(gconfig.STATE_DIR / "server.console.txt", "ab")
    args = [gconfig.VENV_PYTHON, os.path.join(HERE, "gserver.py")]
    if version:
        args += ["--version", version]
    flags = 0
    if os.name == "nt":
        flags = subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP | 0x01000000  # BREAKAWAY_FROM_JOB
    try:
        subprocess.Popen(args, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                         creationflags=flags, close_fds=True, cwd=HERE)
    except OSError:
        flags &= ~0x01000000
        subprocess.Popen(args, stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                         creationflags=flags, close_fds=True, cwd=HERE)


def ensure(version):
    st = running()
    if st is None:
        print("starting Ghidra server (about a minute)...", file=sys.stderr, flush=True)
        spawn(version)
        deadline = time.time() + 300
        while st is None and time.time() < deadline:
            time.sleep(1)
            st = running()
        if st is None:
            sys.exit(f"server did not start; see {gconfig.STATE_DIR / 'server.log'} and server.console.txt")
    if not st.get("ready"):
        print("waiting for the server to load the program...", file=sys.stderr, flush=True)
        st = call({"cmd": "ping", "wait": True}, timeout=600)
    if st.get("error") or not st.get("ok", True):
        sys.exit(f"server error: {st.get('error')}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default=None, help="game build (default 1.6.1170)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("decomp")
    p.add_argument("targets", nargs="+")
    p.add_argument("--out", default=None)
    p.add_argument("--addr", action="store_true", help="prefix lines with the lowest instruction offset")
    p = sub.add_parser("asm")
    p.add_argument("targets", nargs="+")
    p.add_argument("--out", default=None)
    p = sub.add_parser("xrefs")
    p.add_argument("target")
    p.add_argument("--out", default=None)
    p = sub.add_parser("info")
    p.add_argument("targets", nargs="+")
    p = sub.add_parser("fieldrefs")
    p.add_argument("offset", type=lambda s: int(s, 0))
    p.add_argument("--in", dest="within", nargs="+", default=None, help="functions to scan")
    p.add_argument("--users-of", nargs="+", default=None, help="scan functions that reference these")
    p.add_argument("--all", action="store_true", help="whole code by disp32 bytes (offset >= 0x80)")
    p.add_argument("--access", default=None, help="R, W, A (lea) or a combination")
    p.add_argument("--out", default=None)
    sub.add_parser("status")
    sub.add_parser("stop")
    a = ap.parse_args()

    if a.cmd == "status":
        st = running()
        print(json.dumps(st) if st else "server not running")
        return
    if a.cmd == "stop":
        st = running()
        print(json.dumps(call({"cmd": "stop"})) if st else "server not running")
        return

    ensure(a.version)
    out = os.path.abspath(a.out) if getattr(a, "out", None) else None
    t0 = time.time()
    if a.cmd in ("decomp", "asm"):
        req = dict(cmd=a.cmd, targets=a.targets, out=out, addr=getattr(a, "addr", False))
    elif a.cmd == "xrefs":
        req = dict(cmd="xrefs", target=a.target, out=out)
    elif a.cmd == "info":
        req = dict(cmd="info", targets=a.targets)
    else:
        if not (a.within or a.users_of or a.all):
            sys.exit("fieldrefs needs --in, --users-of or --all")
        req = dict(cmd="fieldrefs", offset=a.offset, out=out, access=a.access, all=a.all,
                   users_of=a.users_of, **{"in": a.within})
    res = call(req)
    if not res.get("ok"):
        sys.exit(f"error: {res.get('error')}\n{res.get('trace', '')}")
    for r in res.get("results", []):
        print(f"{r['file']}  ({r['function']}, {r['lines']} lines)")
        for m in r.get("marks", []):
            print(f"    {m}")
    for f in res.get("files", []):
        print(f)
    if res.get("text"):
        print(res["text"])
    if res.get("file") and a.cmd != "xrefs":
        print(f"full list: {res['file']}")
    for e in res.get("errors", []):
        print(f"error: {e}")
    print(f"[{res.get('seconds')} s in Ghidra, {res.get('queued')} s queued, {time.time() - t0:.1f} s total]",
          file=sys.stderr)


if __name__ == "__main__":
    main()
