#!/usr/bin/env python3
"""Describe SkyrimSE.exe functions by Address Library ID, using only the executable's own metadata.

For every ID it prints:
  - RVA and bounds of the function (.pdata);
  - vtable slots that point to it, with MSVC RTTI class names ("Class::vfunc[i]");
  - strings its code references (RIP-relative lea);
  - direct callers (call rel32), recursively up to --depth, with their own vtable names.

Usage:
    python tools/name_functions.py --exe <SkyrimSE.exe> --idb <versionlib-1-6-1170-0.bin> 64742 65030
    python tools/name_functions.py --exe ... --idb ... --from-trace logs/run-0.5.0/CellLoadResearch.trace.jsonl \
        --phase "kPost->kFinish" --top 15

Standard library only.
"""
import argparse
import bisect
import collections
import json
import re
import struct
import sys


# ----------------------------------------------------------------------------
# Address Library (format 2)
# ----------------------------------------------------------------------------
def load_idb(path):
    data = open(path, "rb").read()
    pos = 0

    def read(fmt):
        nonlocal pos
        values = struct.unpack_from(fmt, data, pos)
        pos += struct.calcsize(fmt)
        return values[0] if len(values) == 1 else values

    fmt = read("<i")
    if fmt != 2:
        raise SystemExit(f"{path}: Address Library format {fmt} is not supported (expected 2)")
    version = read("<4i")
    name_len = read("<i")
    pos += name_len
    ptr_size = read("<i")
    count = read("<i")

    id2rva = {}
    prev_id = prev_off = 0
    for _ in range(count):
        kind = read("<B")
        lo, hi = kind & 0xF, kind >> 4
        if lo == 0:
            id_ = read("<Q")
        elif lo == 1:
            id_ = prev_id + 1
        elif lo == 2:
            id_ = prev_id + read("<B")
        elif lo == 3:
            id_ = prev_id - read("<B")
        elif lo == 4:
            id_ = prev_id + read("<H")
        elif lo == 5:
            id_ = prev_id - read("<H")
        elif lo == 6:
            id_ = read("<H")
        elif lo == 7:
            id_ = read("<I")
        else:
            raise SystemExit("bad id encoding")
        base = prev_off // ptr_size if hi & 8 else prev_off
        mode = hi & 7
        if mode == 0:
            off = read("<Q")
        elif mode == 1:
            off = base + 1
        elif mode == 2:
            off = base + read("<B")
        elif mode == 3:
            off = base - read("<B")
        elif mode == 4:
            off = base + read("<H")
        elif mode == 5:
            off = base - read("<H")
        elif mode == 6:
            off = read("<H")
        else:
            off = read("<I")
        if hi & 8:
            off *= ptr_size
        id2rva[id_] = off
        prev_id, prev_off = id_, off
    return version, id2rva


# ----------------------------------------------------------------------------
# PE
# ----------------------------------------------------------------------------
class PE:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise SystemExit(f"{path}: not a PE file")
        n_sections, = struct.unpack_from("<H", d, pe + 6)
        opt_size, = struct.unpack_from("<H", d, pe + 20)
        opt = pe + 24
        self.image_base, = struct.unpack_from("<Q", d, opt + 24)
        exc_rva, exc_size = struct.unpack_from("<II", d, opt + 112 + 3 * 8)
        self.sections = []
        for i in range(n_sections):
            s = opt + opt_size + i * 40
            name = d[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, vaddr, raw_size, raw_ptr = struct.unpack_from("<IIII", d, s + 8)
            self.sections.append((name, vaddr, max(vsize, raw_size), raw_ptr, raw_size))
        self.text = self.section(".text")
        self.rdata = self.section(".rdata")
        # .pdata: sorted RUNTIME_FUNCTION {begin, end, unwind}
        self.funcs = []
        if exc_size:
            raw = self.rva2off(exc_rva)
            self.funcs = [struct.unpack_from("<III", d, raw + i * 12) for i in range(exc_size // 12)]
            self.funcs.sort()
        self.func_starts = [f[0] for f in self.funcs]

    def section(self, name):
        for sec in self.sections:
            if sec[0] == name:
                return sec
        raise SystemExit(f"section {name} not found")

    def rva2off(self, rva):
        for name, vaddr, vsize, raw_ptr, raw_size in self.sections:
            if vaddr <= rva < vaddr + raw_size:
                return raw_ptr + rva - vaddr
        return None

    def in_text(self, rva):
        _, vaddr, vsize, _, _ = self.text
        return vaddr <= rva < vaddr + vsize

    def function_at(self, rva):
        """(begin, end) of the .pdata entry containing rva."""
        i = bisect.bisect_right(self.func_starts, rva) - 1
        if i >= 0 and self.funcs[i][0] <= rva < self.funcs[i][1]:
            return self.funcs[i][0], self.funcs[i][1]
        return None

    def cstring(self, rva, limit=120):
        off = self.rva2off(rva)
        if off is None:
            return None
        end = self.data.find(b"\0", off, off + limit)
        if end < 0 or end - off < 4:
            return None
        raw = self.data[off:end]
        if not all(32 <= b < 127 for b in raw):
            return None
        return raw.decode("ascii")


# ----------------------------------------------------------------------------
# RTTI: vtable slot -> class names
# ----------------------------------------------------------------------------
def demangle(name):
    if name.startswith((".?AV", ".?AU")) and name.endswith("@@") and "?$" not in name:
        return "::".join(reversed(name[4:-2].split("@")))
    return name


def scan_vtables(pe):
    d = pe.data
    _, r_va, r_size, r_raw, r_rawsize = pe.rdata
    cols = {}
    # Complete Object Locator (x64): signature=1, offset, cdOffset, pTypeDescriptor, pClassDescriptor, pSelf
    for off in range(r_raw, r_raw + r_rawsize - 24, 4):
        if d[off] != 1 or d[off + 1:off + 4] != b"\0\0\0":
            continue
        sig, vt_off, cd_off, td_rva, chd_rva, self_rva = struct.unpack_from("<IIIiii", d, off)
        rva = r_va + off - r_raw
        if self_rva != rva:
            continue
        name_off = pe.rva2off(td_rva + 16)
        if name_off is None:
            continue
        end = d.find(b"\0", name_off, name_off + 512)
        name = d[name_off:end].decode("ascii", "replace")
        cols[pe.image_base + rva] = (demangle(name), vt_off)

    slots = collections.defaultdict(list)
    vtables = 0
    _, t_va, t_size, _, _ = pe.text
    lo, hi = pe.image_base + t_va, pe.image_base + t_va + t_size
    start = r_raw + (-r_raw % 8)
    for off in range(start, r_raw + r_rawsize - 8, 8):
        value, = struct.unpack_from("<Q", d, off)
        col = cols.get(value)
        if not col:
            continue
        vtables += 1
        cls, vt_off = col
        label = cls if vt_off == 0 else f"{cls}[+{vt_off:#x}]"
        i = 0
        p = off + 8
        while p + 8 <= r_raw + r_rawsize:
            fn, = struct.unpack_from("<Q", d, p)
            if not lo <= fn < hi or (i > 0 and fn in cols):
                break
            slots[fn - pe.image_base].append(f"{label}::vfunc[{i:#x}]")
            i += 1
            p += 8
    return slots, vtables


# ----------------------------------------------------------------------------
# code scans
# ----------------------------------------------------------------------------
def strings_in(pe, begin, end):
    d = pe.data
    off = pe.rva2off(begin)
    found = []
    for i in range(off, off + (end - begin) - 7):
        # lea r64, [rip+disp32]: REX.W (48/4C) 8D modrm(mod=00, rm=101)
        if d[i] in (0x48, 0x4C) and d[i + 1] == 0x8D and (d[i + 2] & 0xC7) == 0x05:
            disp, = struct.unpack_from("<i", d, i + 3)
            target = begin + (i - off) + 7 + disp
            s = pe.cstring(target)
            if s and s not in found:
                found.append(s)
    return found


def build_lea_index(pe, targets):
    """lea r64, [rip+disp32] sites in .text whose target is in `targets` -> {target: [site rva]}."""
    _, t_va, t_size, t_raw, t_rawsize = pe.text
    text = pe.data[t_raw:t_raw + t_rawsize]
    result = collections.defaultdict(list)
    for m in re.finditer(rb"[\x48\x4C]\x8D[\x05\x0D\x15\x1D\x25\x2D\x35\x3D]", text):
        i = m.start()
        disp, = struct.unpack_from("<i", text, i + 3)
        target = t_va + i + 7 + disp
        if target in targets:
            result[target].append(t_va + i)
    return result


def build_call_index(pe, targets):
    """call rel32 sites in .text whose target is in `targets` -> {target: [site rva]}."""
    d = pe.data
    _, t_va, t_size, t_raw, t_rawsize = pe.text
    result = collections.defaultdict(list)
    i = d.find(b"\xE8", t_raw, t_raw + t_rawsize)
    end = t_raw + t_rawsize - 5
    while 0 <= i < end:
        disp, = struct.unpack_from("<i", d, i + 1)
        target = t_va + (i - t_raw) + 5 + disp
        if target in targets:
            result[target].append(t_va + (i - t_raw))
        i = d.find(b"\xE8", i + 1, t_raw + t_rawsize)
    return result


# ----------------------------------------------------------------------------
def ids_from_trace(path, phase, top):
    counts = collections.Counter()
    for line in open(path, encoding="utf-8", errors="replace"):
        if '"ev":"profile"' not in line:
            continue
        rec = json.loads(line)
        for ph in rec.get("args", {}).get("phases", []):
            if phase and ph.get("phase") != phase:
                continue
            for name, n in ph.get("self", []) + ph.get("functions", []):
                counts[name] += n
    ids = []
    for name, n in counts.most_common():
        m = re.fullmatch(r"AE#(\d+)", name)
        if m:
            ids.append((int(m.group(1)), n))
    return ids[:top] if top else ids


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--idb", required=True, help="versionlib-*.bin (Address Library, format 2)")
    parser.add_argument("--from-trace", help="take function IDs from profile records of this trace")
    parser.add_argument("--phase", help="profile phase to take IDs from, e.g. kPost->kFinish")
    parser.add_argument("--top", type=int, default=15)
    parser.add_argument("--depth", type=int, default=2, help="caller levels to resolve")
    parser.add_argument("--tree", metavar="PHASE", help="print the profile call trees of this phase (needs --from-trace), with names")
    parser.add_argument("--min-share", type=float, default=3.0, help="--tree: hide nodes under this percent")
    parser.add_argument("--xref", type=int, action="append", metavar="ID",
                        help="list functions that take the address of this ID with a RIP-relative lea (e.g. a vtable: constructors)")
    parser.add_argument("ids", nargs="*", type=int)
    options = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")

    version, id2rva = load_idb(options.idb)
    rva2id = {rva: id_ for id_, rva in id2rva.items()}
    sorted_rvas = sorted(rva2id)
    pe = PE(options.exe)
    slots, vtable_count = scan_vtables(pe)
    print(f"# Address Library {'.'.join(map(str, version))}: {len(id2rva)} IDs; "
          f"{len(pe.funcs)} functions in .pdata; {vtable_count} vtables with RTTI\n")

    def name_of(rva):
        """AE#id (+off) and vtable names for a code address."""
        i = bisect.bisect_right(sorted_rvas, rva) - 1
        label = f"rva {rva:#x}"
        if i >= 0:
            base = sorted_rvas[i]
            label = f"AE#{rva2id[base]}" + (f"+{rva - base:#x}" if rva != base else "")
            names = slots.get(base)
            if names:
                label += " = " + ", ".join(names[:3]) + (" …" if len(names) > 3 else "")
        return label

    if options.tree:
        if not options.from_trace:
            parser.error("--tree needs --from-trace")
        string_cache = {}

        def annotate(label):
            def one(match):
                id_ = int(match.group(1))
                rva = id2rva.get(id_)
                if rva is None:
                    return match.group(0)
                extra = []
                if slots.get(rva):
                    extra.append(slots[rva][0])
                if rva not in string_cache:
                    fn = pe.function_at(rva)
                    string_cache[rva] = strings_in(pe, fn[0], fn[1])[:2] if fn and fn[1] - fn[0] < 0x4000 else []
                if string_cache[rva]:
                    extra.append(" ".join(json.dumps(s) for s in string_cache[rva]))
                return match.group(0) + (" {" + "; ".join(extra) + "}" if extra else "")
            return re.sub(r"AE#(\d+)", one, label)

        def walk(node, total, depth):
            label, count, children = node
            share = 100 * count / total
            if share < options.min_share:
                return
            print(f"{'  ' * depth}{share:5.1f}%  {annotate(label)}")
            for child in children:
                walk(child, total, depth + 1)

        for line in open(options.from_trace, encoding="utf-8", errors="replace"):
            if '"ev":"profile"' not in line:
                continue
            rec = json.loads(line)
            for ph in rec.get("args", {}).get("phases", []):
                if ph.get("phase") == options.tree and ph.get("tree"):
                    print(f"## {options.tree} at {rec['t_us'] / 1e6:.1f} s, {ph.get('samples')} samples")
                    walk(ph["tree"], ph.get("samples") or 1, 0)
                    print()
        return

    if options.xref:
        targets = {id2rva[i]: i for i in options.xref if i in id2rva}
        for target, sites in build_lea_index(pe, set(targets)).items():
            print(f"## AE#{targets[target]} (rva {target:#x}): address taken in")
            for site in sites:
                fn = pe.function_at(site)
                print(f"- {name_of(fn[0]) if fn else '?'} at {name_of(site)}")
            print()
        if not options.ids:
            return

    requested = [(i, None) for i in options.ids]
    if options.from_trace:
        requested += ids_from_trace(options.from_trace, options.phase, options.top)
    if not requested:
        parser.error("give IDs or --from-trace")

    # callers, level by level
    level = {id2rva[i] for i, _ in requested if i in id2rva}
    callers_of = {}
    for _ in range(options.depth):
        index = build_call_index(pe, level)
        nxt = set()
        for target in level:
            fns = collections.Counter()
            for site in index.get(target, []):
                fn = pe.function_at(site)
                if fn:
                    # chained unwind chunks: attribute to the nearest ID at or below the chunk start
                    fns[fn[0]] += 1
            callers_of[target] = fns
            nxt.update(fns)
        level = nxt - set(callers_of)

    for id_, samples in requested:
        rva = id2rva.get(id_)
        if rva is None:
            print(f"## AE#{id_}: not in the Address Library\n")
            continue
        fn = pe.function_at(rva)
        size = f", size {fn[1] - fn[0]:#x}" if fn and fn[0] == rva else ""
        print(f"## AE#{id_}" + (f" ({samples} samples)" if samples else "") + f": rva {rva:#x}{size}")
        names = slots.get(rva)
        print("- vtables: " + (", ".join(names) if names else "none (not virtual)"))
        if fn:
            strs = strings_in(pe, fn[0], fn[1])
            if strs:
                print("- strings: " + ", ".join(json.dumps(s) for s in strs[:12]))

        def print_callers(target, depth, indent):
            fns = callers_of.get(target)
            if not fns:
                if depth == 0:
                    print(f"{indent}- callers: none by call rel32 (virtual or indirect)")
                return
            for caller, n in fns.most_common(6):
                print(f"{indent}- called by {name_of(caller)} ({n} site{'s' if n > 1 else ''})")
                if depth + 1 < options.depth:
                    print_callers(caller, depth + 1, indent + "  ")

        print_callers(rva, 0, "")
        print()


if __name__ == "__main__":
    main()
