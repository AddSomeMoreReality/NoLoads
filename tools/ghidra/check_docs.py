#!/usr/bin/env python3
"""Check claims of the form `AE#a+0xNN ... AE#b` in docs/*.md against the instruction in the Ghidra base (T34).

  E:\\Ghidra\\pyghidra-venv\\Scripts\\python.exe tools/ghidra/check_docs.py [--project-dir D] [--docs DIR]

A claim: `AE#a+0xNN` -> / call / вызов `AE#b` on one line (or `AE#b` из/from `AE#a+0xNN`). It holds when the
instruction at a+NN calls or jumps to b (thunks resolved) or references b's address. Read-only; the documents are
not changed. Output: STATE_DIR/check-docs-<version>.txt with file:line for every claim that does not hold.
"""
import argparse
import collections
import os
import pathlib
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import addrlib  # noqa: E402
import gconfig  # noqa: E402
import gstart  # noqa: E402



TOKEN = re.compile(r"AE#(\d+)(?:\+0x([0-9A-Fa-f]+))?")
FORWARD = re.compile(r"^[\s\(`'\"]*(?:→|->|=>|call|вызыва\w*|зовёт|зовет)[\s\(`'\"]*$", re.I)
BACKWARD = re.compile(r"^[\s\(`'\"]*(?:<|←|<-|из|from|called from|вызывается из)[\s\)`'\"]*$", re.I)


def claims(docs):
    """(file, line, a, offset, b, text) for adjacent IDs on a line: `AE#a+0xNN` → / -> / call `AE#b[+..]`,
    or `AE#b[+..]` < / ← / из / from `AE#a+0xNN` (call chains of traces: callee < caller+return offset)."""
    for p in sorted(pathlib.Path(docs).rglob("*.md")):
        for n, line in enumerate(p.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            toks = list(TOKEN.finditer(line))
            for x, y in zip(toks, toks[1:]):
                sep = line[x.end():y.start()]
                if len(sep) > 24:
                    continue
                if FORWARD.match(sep) and x.group(2):
                    yield p, n, int(x.group(1)), int(x.group(2), 16), int(y.group(1)), line.strip()
                elif BACKWARD.match(sep) and y.group(2):
                    yield p, n, int(y.group(1)), int(y.group(2), 16), int(x.group(1)), line.strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default=None)
    ap.add_argument("--project-dir", default=None)
    ap.add_argument("--docs", default=str(gconfig.repo_dir() / "docs"))
    a = ap.parse_args()
    ver = gconfig.version(a.version)
    _, id2rva = addrlib.load_addrlib(ver["addrlib"])
    rva2ids = collections.defaultdict(list)
    for i, r in id2rva.items():
        rva2ids[r].append(i)
    todo = list(claims(a.docs))
    gstart.start_jvm("4G")
    out, stat = [], collections.Counter()
    with gstart.open_program(a.project_dir or ver["master_dir"], ver["project"], ver["program"], True) as (_, prog):
        base, listing, fm = prog.getImageBase(), prog.getListing(), prog.getFunctionManager()

        def name_of(addr):
            f = fm.getFunctionAt(addr)
            if f is not None and f.isThunk() and f.getThunkedFunction(True) is not None:
                f = f.getThunkedFunction(True)
            r = addr.getOffset() - base.getOffset() if f is None else f.getEntryPoint().getOffset() - base.getOffset()
            ids = rva2ids.get(r)
            return (f.getName(True) if f else str(addr)) + (f" (AE#{ids[0]})" if ids else ""), r

        rel = pathlib.Path(a.docs).parent
        for path, line, aid, off, bid, text in todo:
            where = f"{path.relative_to(rel).as_posix()}:{line}"
            if aid not in id2rva or bid not in id2rva:
                stat["ID not in Address Library"] += 1
                out.append(f"{where}: AE#{aid}+0x{off:X} -> AE#{bid}: ID not in Address Library")
                continue
            addr = base.add(id2rva[aid] + off)
            ins = listing.getInstructionContaining(addr)
            if ins is None:
                stat["no instruction"] += 1
                out.append(f"{where}: AE#{aid}+0x{off:X} -> AE#{bid}: no instruction there")
                continue
            note = "" if ins.getAddress().equals(addr) else f" (offset inside {ins.getAddress()})"
            targets = [name_of(x) for x in (ins.getFlows() or [])]
            targets += [name_of(r.getToAddress()) for r in ins.getReferencesFrom() if r.getToAddress().isMemoryAddress()]
            want = id2rva[bid]
            if any(r == want for _, r in targets):
                stat["holds" + (" (offset not at instruction start)" if note else "")] += 1
                continue
            # the claim may name the target one instruction later (offset of the return address)
            prev = ins.getPrevious()
            if prev is not None and any(r == want for _, r in
                                        [name_of(x) for x in (prev.getFlows() or [])]):
                stat["holds at the previous instruction (return address)"] += 1
                continue
            if prev is not None and prev.getFlowType().isCall() and not prev.getFlows():
                stat["return address after an indirect call (not checkable statically)"] += 1
                out.append(f"{where}: AE#{aid}+0x{off:X} -> AE#{bid}: after indirect `{prev}`, not checkable")
                continue
            stat["does not hold"] += 1
            out.append(f"{where}: AE#{aid}+0x{off:X} -> AE#{bid}: instruction `{ins}`{note} goes to "
                       f"{', '.join(n for n, _ in targets) or 'nothing'}")
    head = [f"claims AE#a+0xNN -> AE#b in {a.docs}: {len(todo)}; {dict(stat)}"]
    path = gconfig.STATE_DIR / f"check-docs-{ver['name']}.txt"
    path.write_text("\n".join(head + out) + "\n", encoding="utf-8")
    print("\n".join(head + out[:60]))
    print(f"full list: {path}")


if __name__ == "__main__":
    main()
