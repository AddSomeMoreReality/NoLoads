#!/usr/bin/env python3
"""Curator: consistency checks of the Ghidra base against the exe itself (T34). Read-only.

  E:\\Ghidra\\pyghidra-venv\\Scripts\\python.exe tools/ghidra/check_base.py [--project-dir D] [--ref-id 403521]

1. RIP-relative operands that point at --ref-id (raw byte scan, as T31's census: disp32 after a ModRM
   mod=00 rm=101, immediate of 0/1/4 bytes) against Ghidra's references; every miss with its reason.
2. References from code outside any function, and .pdata function starts (not chained) without a function.
3. Functions with default names (FUN_...).
Report: STATE_DIR/check-<version>.report.txt.
"""
import argparse
import bisect
import collections
import os
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import addrlib  # noqa: E402
import gconfig  # noqa: E402
import gstart  # noqa: E402


def pdata_primary(pe):
    """[(begin, end)] of .pdata entries whose unwind info is not chained (real function starts)."""
    out = []
    d = pe.data
    for b, e in zip(pe.func_begin, pe.func_end):
        out.append((b, e))
    # unwind info: the third RUNTIME_FUNCTION field; re-read the table to get it
    p = struct.unpack_from("<I", d, 0x3C)[0]
    exc_rva, exc_size = struct.unpack_from("<II", d, p + 24 + 112 + 3 * 8)
    off = pe.rva2off(exc_rva)
    chained = set()
    for i in range(exc_size // 12):
        b, _, u = struct.unpack_from("<III", d, off + 12 * i)
        if u & 1:  # odd: points at another RUNTIME_FUNCTION (chained, rare form)
            chained.add(b)
            continue
        flags = d[pe.rva2off(u)] >> 3
        if flags & 0x4:  # UNW_FLAG_CHAININFO: a fragment of a function that starts elsewhere
            chained.add(b)
    return [(b, e) for b, e in out if b not in chained], chained


def rip_scan(pe, target_rva):
    """RVAs of disp32 fields whose RIP-relative target is target_rva (instruction start is unknown)."""
    hits = []
    for name, va, size, raw, chars in pe.sections:
        if not chars & 0x20000000:
            continue
        code = pe.data[raw:raw + size]
        for i in range(1, len(code) - 4):
            if (code[i - 1] & 0xC7) != 0x05:
                continue
            disp = struct.unpack_from("<i", code, i)[0]
            for imm in (0, 1, 4):
                if va + i + 4 + imm + disp == target_rva:
                    hits.append(va + i)
                    break
    return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default=None)
    ap.add_argument("--project-dir", default=None)
    ap.add_argument("--ref-id", type=int, default=403521)
    a = ap.parse_args()
    ver = gconfig.version(a.version)
    pe = addrlib.PE(ver["exe"])
    _, id2rva = addrlib.load_addrlib(ver["addrlib"])
    rva2ids = collections.defaultdict(list)
    for i, r in id2rva.items():
        rva2ids[r].append(i)
    t0 = time.time()
    primary, chained = pdata_primary(pe)
    report = [f".pdata: {len(pe.func_begin)} entries, {len(primary)} function starts, {len(chained)} chained fragments"]
    gstart.start_jvm("6G")
    with gstart.open_program(a.project_dir or ver["master_dir"], ver["project"], ver["program"], True) as (_, prog):
        base = prog.getImageBase()
        fm, rm, listing = prog.getFunctionManager(), prog.getReferenceManager(), prog.getListing()
        A = lambda rva: base.add(rva)  # noqa: E731

        def pdata_of(rva):
            i = bisect.bisect_right(pe.func_begin, rva) - 1
            if i >= 0 and rva < pe.func_end[i]:
                return pe.func_begin[i], pe.func_begin[i] in chained
            return None, False

        def where(rva):
            f = fm.getFunctionContaining(A(rva))
            ids = rva2ids.get(rva)
            return (f"{f.getName(True)}+0x{rva - (f.getEntryPoint().getOffset() - base.getOffset()):X}" if f else
                    "no function") + (f" (AE#{ids[0]})" if ids else "")

        # 1. RIP references to the target
        target = id2rva[a.ref_id]
        scan = rip_scan(pe, target)
        ghidra_from = {}
        for r in rm.getReferencesTo(A(target)):
            ghidra_from[r.getFromAddress().getOffset() - base.getOffset()] = r
        miss = collections.Counter()
        fp = collections.Counter()
        rows = []
        for disp_rva in scan:
            ins = listing.getInstructionContaining(A(disp_rva))
            if ins is None:
                beg, ch = pdata_of(disp_rva)
                reason = "not disassembled" + (f", inside .pdata entry 0x{beg:X}{' (chained)' if ch else ''}"
                                               if beg is not None else ", outside .pdata")
            else:
                irva = ins.getAddress().getOffset() - base.getOffset()
                real = ins.getAddress().add(ins.getLength()).getOffset() - base.getOffset() + \
                    struct.unpack_from("<i", pe.data, pe.rva2off(disp_rva))[0]
                if real != target and irva not in ghidra_from:
                    ids = rva2ids.get(real)
                    miss["scan false positive (other address)"] += 1
                    fp[f"0x{real:X}{' AE#%d' % ids[0] if ids else ''}"] += 1
                    continue
                if irva in ghidra_from:
                    if fm.getFunctionContaining(ins.getAddress()) is None:
                        beg, ch = pdata_of(irva)
                        miss["referenced, outside functions"] += 1
                        rows.append(f"  0x{irva:X} {ins}: in code outside functions; .pdata entry "
                                    f"{('0x%X' % beg) if beg is not None else '-'}{' (chained)' if ch else ''}")
                    continue
                reason = f"instruction {ins} has no reference"
            miss[reason.split(",")[0]] += 1
            rows.append(f"  0x{disp_rva:X}: {reason}")
        report.append(f"1. AE#{a.ref_id}: raw scan {len(scan)} operands, Ghidra references {len(ghidra_from)}; "
                      f"differences {dict(miss)}; false positives point at {dict(fp)}")
        report += rows[:80]

        # 2. code outside functions, .pdata starts without a function
        no_func = [b for b, _ in primary if fm.getFunctionAt(A(b)) is None]
        inside = [b for b in no_func if fm.getFunctionContaining(A(b)) is not None]
        undis = [b for b in no_func if listing.getInstructionAt(A(b)) is None]
        report.append(f"2. .pdata function starts without a Ghidra function: {len(no_func)} "
                      f"(inside another function {len(inside)}, not disassembled {len(undis)}, "
                      f"code without function {len(no_func) - len(inside) - len(undis)})")
        report += [f"  0x{b:X}{' AE#%d' % rva2ids[b][0] if b in rva2ids else ''}: "
                   f"{'inside ' + where(b) if b in inside else 'not disassembled' if b in undis else 'code, no function'}"
                   for b in no_func[:40]]
        outside = collections.Counter()
        for r in rm.getReferenceIterator(prog.getMinAddress()):
            frm = r.getFromAddress()
            if not frm.isMemoryAddress() or listing.getInstructionAt(frm) is None:
                continue
            if fm.getFunctionContaining(frm) is None:
                beg, ch = pdata_of(frm.getOffset() - base.getOffset())
                outside["in .pdata entry (chained)" if ch else "in .pdata entry" if beg is not None
                        else "outside .pdata"] += 1
        report.append(f"   references from instructions outside functions: {dict(outside)}")

        # 3. default names
        fun = [f for f in fm.getFunctions(True) if f.getName().startswith("FUN_")]
        report.append(f"3. functions named FUN_*: {len(fun)}")
        for f in fun[:20]:
            rva = f.getEntryPoint().getOffset() - base.getOffset()
            beg, ch = pdata_of(rva)
            report.append(f"  {f.getName()} size 0x{f.getBody().getNumAddresses():X}, .pdata "
                          f"{'start' if beg == rva else ('inside 0x%X' % beg) if beg is not None else '-'}"
                          f"{' (chained)' if ch else ''}, callers {len(list(f.getCallingFunctions(None)))}, "
                          f"AE id {rva2ids.get(rva, ['-'])[0]}")
        # 4. names by source, most called functions still without a name
        from ghidra.program.model.symbol import SourceType
        kinds = collections.Counter()
        prefix = ver["prefix"] + "_"
        for f in fm.getFunctions(True):
            n, src = f.getName(True), f.getSymbol().getSource()
            if src == SourceType.DEFAULT:
                k = "FUN_ (default)"
            elif src == SourceType.ANALYSIS:
                k = "analysis (Function ID, exceptions)"
            elif n.startswith(prefix) and n[len(prefix):].isdigit():
                k = "AE_<id> only"
            elif n.startswith("Papyrus::"):
                k = "papyrus"
            elif n.split("::")[0] in ("ConsoleCommand", "ScriptCommand", "Condition"):
                k = "console/script command"
            elif "::vf_0x" in n:
                k = "rtti-vslot"
            else:
                k = "NG / curated / RTTI (other)"
            kinds[k] += 1
        report.append(f"4. functions {fm.getFunctionCount()}: " + ", ".join(f"{k} {v}" for k, v in kinds.most_common()))
        calls = collections.Counter()
        for r in rm.getReferenceIterator(prog.getMinAddress()):
            if r.getReferenceType().isCall():
                calls[r.getToAddress().getOffset()] += 1
        top = calls.most_common(500)
        unnamed = []
        for off, n in top:
            f = fm.getFunctionAt(base.getNewAddress(off))
            if f is not None and f.getName().startswith(prefix):
                unnamed.append(f"{f.getName()} ({n} calls)")
        report.append(f"   top 500 by call sites: {len(unnamed)} still {prefix}<id>: " + ", ".join(unnamed[:40]))

    report.insert(0, f"check_base {ver['name']}: {time.time() - t0:.0f} s")
    path = gconfig.STATE_DIR / f"check-{ver['name']}.report.txt"
    path.write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report[:120]))
    print(f"report: {path}")


if __name__ == "__main__":
    main()
