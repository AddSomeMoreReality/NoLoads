#!/usr/bin/env python3
"""Long-lived Ghidra query server over the READ-ONLY copy of the project. Started by gq.py / mcp_server.py on demand.

Requests run in parallel threads (the program is only read). Decompilation uses a pool of up to
gconfig.SERVER_DECOMPILERS decompiler processes and a pseudocode cache keyed by function and snapshot
(STATE_DIR/cache/<snapshot>/). Exits after gconfig.SERVER_IDLE_MINUTES without requests, or on {"cmd": "stop"}.
Protocol: one JSON line in, one JSON line out, on 127.0.0.1:gconfig.SERVER_PORT. Runs under gconfig.VENV_PYTHON.

Adding a command: a method of Ghidra returning a dict (with "text" and/or "file"), a branch in dispatch(),
a subcommand in gq.py and a tool in mcp_server.py (TOOLS); bump PROTOCOL if old servers lack it.
"""
import argparse
import collections
import hashlib
import json
import os
import queue
import re
import socketserver
import sys
import threading
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import addrlib  # noqa: E402
import gconfig  # noqa: E402
import gstart  # noqa: E402

LOG = None
PROTOCOL = 2  # bump when commands change; gq.py restarts an older server
CACHE_FORMAT = 1  # bump when the cached decompiler rows change shape


def log(msg):
    line = f"{time.strftime('%H:%M:%S')} {msg}"
    print(line, flush=True)
    if LOG:
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(line + "\n")


def snapshot_id(project_dir):
    """Changes whenever snapshot.py copies a new database: hash of names, sizes and times of the database files
    (*.gbf, *.prp under idata; the ~index files change on every open, even read-only)."""
    h = hashlib.sha1()
    for root, _, files in sorted(os.walk(os.path.join(project_dir, [d for d in os.listdir(project_dir)
                                                                     if d.endswith(".rep")][0], "idata"))):
        for fn in sorted(files):
            if not fn.endswith((".gbf", ".prp")):
                continue
            st = os.stat(os.path.join(root, fn))
            h.update(f"{os.path.relpath(os.path.join(root, fn), project_dir)}|{st.st_size}|{int(st.st_mtime)}".encode())
    return h.hexdigest()[:12]


class Ghidra:
    """Everything that touches the JVM. Methods run in parallel request threads and only read the program."""

    def __init__(self, ver, program, project_dir, decompilers):
        from ghidra.util.task import TaskMonitor
        self.ver = ver
        self.prog = program
        self.base = program.getImageBase()
        self.fm = program.getFunctionManager()
        self.st = program.getSymbolTable()
        self.rm = program.getReferenceManager()
        self.listing = program.getListing()
        self.mem = program.getMemory()
        self.monitor = TaskMonitor.DUMMY
        self.snapshot = snapshot_id(project_dir)
        self.cache_dir = gconfig.STATE_DIR / "cache" / f"{ver['name']}-{self.snapshot}"
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        # decompiler pool: one DecompInterface (= one decompile.exe) per concurrent decompilation
        self.pool = queue.LifoQueue()
        self.pool_max = max(1, decompilers)
        self.pool_made = 0
        self.pool_lock = threading.Lock()
        self.ifcs = []
        self.pool.put(self._new_ifc())
        self.mem_cache = collections.OrderedDict()  # entry rva -> rows
        self.cache_lock = threading.Lock()
        self.stats = collections.Counter()
        self.index_lock = threading.Lock()
        self._symbols = None
        self._strings = None
        self._vftables = None
        _, self.id2rva = addrlib.load_addrlib(ver["addrlib"])
        self.rva2ids = collections.defaultdict(list)
        for i, r in self.id2rva.items():
            self.rva2ids[r].append(i)
        self.pe = None
        try:
            from ghidra.program.model.listing import CommentType
            self.PLATE = CommentType.PLATE
        except ImportError:
            from ghidra.program.model.listing import CodeUnit
            self.PLATE = CodeUnit.PLATE_COMMENT

    # ---------------------------------------------------------------- decompiler pool and cache
    def _new_ifc(self):
        from ghidra.app.decompiler import DecompInterface, DecompileOptions
        ifc = DecompInterface()
        opts = DecompileOptions()
        opts.grabFromProgram(self.prog)
        ifc.setOptions(opts)
        ifc.toggleCCode(True)
        ifc.toggleSyntaxTree(True)
        if not ifc.openProgram(self.prog):
            raise RuntimeError("decompiler: " + str(ifc.getLastMessage()))
        with self.pool_lock:
            self.pool_made += 1
            self.ifcs.append(ifc)
        return ifc

    def _acquire_ifc(self):
        try:
            return self.pool.get_nowait()
        except queue.Empty:
            pass
        with self.pool_lock:
            grow = self.pool_made < self.pool_max
            if grow:
                self.pool_made += 1  # reserve the slot; _new_ifc counts again
        if grow:
            try:
                ifc = self._new_ifc()
            finally:
                with self.pool_lock:
                    self.pool_made -= 1
            return ifc
        return self.pool.get()

    def dispose(self):
        for ifc in self.ifcs:
            try:
                ifc.dispose()
            except Exception:  # noqa: BLE001
                pass

    def decompiled_rows(self, f, use_cache=True):
        """[(offsets, text)] of a function's pseudocode: memory cache, disk cache, then a pooled decompiler.
        Returns (rows, source, wait seconds) or raises RuntimeError."""
        from ghidra.app.decompiler.component import DecompilerUtils
        key = self.rva(f.getEntryPoint())
        with self.cache_lock:
            rows = self.mem_cache.get(key) if use_cache else None
            if rows is not None:
                self.mem_cache.move_to_end(key)
                self.stats["cache_mem"] += 1
                return rows, "memory cache", 0.0
        path = self.cache_dir / f"decomp{CACHE_FORMAT}_{key:X}.json"
        rows, source, wait = None, "disk cache", 0.0
        if use_cache and path.exists():
            try:
                rows = [(list(map(tuple, o)), t) for o, t in json.load(open(path, encoding="utf-8"))]
                self.stats["cache_disk"] += 1
            except (OSError, ValueError):
                rows = None
        if rows is None:
            t0 = time.time()
            ifc = self._acquire_ifc()
            wait = time.time() - t0
            try:
                res = ifc.decompileFunction(f, 120, self.monitor)
            finally:
                self.pool.put(ifc)
            if not res.decompileCompleted():
                raise RuntimeError(f"{f.getName(True)}: {res.getErrorMessage()}")
            entry = f.getEntryPoint()
            rows = []
            for ln in DecompilerUtils.toLines(res.getCCodeMarkup()):
                toks = list(ln.getAllTokens())
                text = str(ln.getIndentString()) + "".join(str(tk) for tk in toks)
                offs = []
                for tk in toks:
                    mn, mx = tk.getMinAddress(), tk.getMaxAddress()
                    if mn is not None:
                        offs.append((mn.subtract(entry), (mx or mn).subtract(entry)))
                rows.append((offs, text))
            self.stats["decompiled"] += 1
            source = "decompiler"
            tmp = path.with_suffix(f".{threading.get_ident()}.tmp")
            try:
                json.dump(rows, open(tmp, "w", encoding="utf-8"))
                os.replace(tmp, path)
            except OSError:
                pass
        with self.cache_lock:
            self.mem_cache[key] = rows
            while len(self.mem_cache) > 400:
                self.mem_cache.popitem(last=False)
        return rows, source, wait

    # ---------------------------------------------------------------- targets
    def resolve(self, target):
        """'AE#13316', 'AE#13316+0x65C', '0x14019D490', '0x19D490' (RVA), 'TES::SetCurrentCell+0x10' -> (Address, text)."""
        t = target.strip()
        m = re.match(r"^(.*?)(?:\s*\+\s*(0x[0-9A-Fa-f]+|\d+))?$", t)
        head, off = m.group(1), int(m.group(2), 0) if m.group(2) else 0
        pm = re.match(rf"^(?:{self.ver['prefix']}[#_]|#)(\d+)$", head, re.I)
        if pm:
            rva = self.id2rva.get(int(pm.group(1)))
            if rva is None:
                raise ValueError(f"{head}: not in Address Library {self.ver['name']}")
            return self.base.add(rva + off)
        if re.match(r"^(0x)?[0-9A-Fa-f]+$", head) and head.lower().startswith("0x"):
            v = int(head, 16)
            if v < self.base.getOffset():
                v += self.base.getOffset()
            return self.base.getNewAddress(v + off)
        syms = [s for s in self.st.getSymbols(head.split("::")[-1]) if s.getName(True) == head] or \
            list(self.st.getSymbols(head))
        if not syms:
            from ghidra.app.util import NamespaceUtils
            syms = list(NamespaceUtils.getSymbols(head, self.prog))
        if not syms:
            raise ValueError(f"{head}: no such symbol (use AE#id or 0x address)")
        if len(syms) > 1:
            raise ValueError(f"{head}: ambiguous: " + ", ".join(f"{s.getName(True)}@{s.getAddress()}" for s in syms[:8]))
        return syms[0].getAddress().add(off)

    def rva(self, addr):
        return addr.getOffset() - self.base.getOffset()

    def ids_at(self, addr):
        return self.rva2ids.get(self.rva(addr), [])

    def where(self, addr):
        """'Func+0x1A (AE#123)' or data symbol."""
        f = self.fm.getFunctionContaining(addr)
        if f is not None:
            entry = f.getEntryPoint()
            off = addr.subtract(entry)
            ids = self.ids_at(entry)
            idtxt = f" ({', '.join('AE#%d' % i for i in ids)})" if ids else ""
            return f"{f.getName(True)}+0x{off:X}{idtxt}" if off else f"{f.getName(True)}{idtxt}"
        data = self.listing.getDataContaining(addr)
        if data is None and self.listing.getInstructionContaining(addr) is not None:
            return f"{addr} (code outside any function)"
        start = data.getAddress() if data is not None else addr
        sym = self.st.getPrimarySymbol(start)
        name = sym.getName(True) if sym is not None else str(start)
        off = addr.subtract(start)
        return f"{name}+0x{off:X}" if off else name

    def func_header(self, f):
        entry = f.getEntryPoint()
        ids = self.ids_at(entry)
        lines = [f"// SkyrimSE.exe {self.ver['name']}  {entry} (RVA 0x{self.rva(entry):X})  "
                 f"{' '.join('AE#%d' % i for i in ids) or 'no Address Library ID'}",
                 f"// {f.getName(True)}  size 0x{f.getBody().getNumAddresses():X}  {f.getSignature().getPrototypeString()}"]
        plate = self.listing.getComment(self.PLATE, entry)
        if plate:
            lines += ["// " + ln for ln in str(plate).splitlines()]
        return lines

    # ---------------------------------------------------------------- commands
    def decomp(self, targets, out, addr_prefix, use_cache=True):
        os.makedirs(out, exist_ok=True)
        groups = collections.OrderedDict()
        errors = []
        for t in targets:
            try:
                a = self.resolve(t)
            except Exception as e:  # noqa: BLE001
                errors.append(str(e))
                continue
            f = self.fm.getFunctionContaining(a)
            if f is None:
                errors.append(f"{t}: {a} is not inside a function")
                continue
            groups.setdefault(f.getEntryPoint(), (f, []))[1].append((t, a))
        results = []
        for entry, (f, marks) in groups.items():
            t0 = time.time()
            try:
                rows, source, wait = self.decompiled_rows(f, use_cache)
            except RuntimeError as e:
                errors.append(str(e))
                continue
            mark_lines = collections.defaultdict(list)
            mark_info = []
            for t, a in marks:
                off = a.subtract(entry)
                if off == 0:
                    continue
                hit = [i for i, (offs, _) in enumerate(rows) if any(lo <= off <= hi for lo, hi in offs)]
                exact = bool(hit)
                if not hit:
                    best = max(((max(lo for lo, _ in offs if lo <= off), i) for i, (offs, _) in enumerate(rows)
                                if any(lo <= off for lo, _ in offs)), default=None)
                    hit = [best[1]] if best else []
                for i in hit:
                    mark_lines[i].append(f"+0x{off:X}" + ("" if exact else "~"))
                ins = self.listing.getInstructionContaining(a)
                mark_info.append((f"+0x{off:X}: {ins if ins else '-'}", hit, exact))
            body = []
            for i, (offs, text) in enumerate(rows):
                pre = ""
                if addr_prefix:
                    pre = f"/*{min(lo for lo, _ in offs):5X}*/ " if offs else " " * 10
                suf = f"   //>>> {' '.join(mark_lines[i])}" if i in mark_lines else ""
                body.append(pre + text + suf)
            head = self.func_header(f)
            if mark_info:
                head.append("// marks (file line numbers):")
                first = len(head) + len(mark_info) + 2  # header, marks, blank line -> first body line
                mark_info = [f"{txt}  -> line {', '.join(str(first + i) for i in hit) or '-'}"
                             f"{'' if exact else ' (nearest)'}" for txt, hit, exact in mark_info]
                head += ["//   " + m for m in mark_info]
            ids = self.ids_at(entry)
            fname = (f"AE_{ids[0]}" if ids else f"rva_{self.rva(entry):X}") + "_" + \
                re.sub(r"[^\w.-]+", "_", f.getName())[:60] + ".c"
            path = os.path.join(out, fname)
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("\n".join(head) + "\n\n" + "\n".join(body) + "\n")
            results.append(dict(function=f.getName(True), file=path, lines=len(body), header=len(head) + 1,
                                seconds=round(time.time() - t0, 2), source=source, pool_wait=round(wait, 2),
                                marks=mark_info, mark_rows=sorted({len(head) + 2 + i for i in mark_lines})))
        return dict(results=results, errors=errors)

    def refs_to(self, a):
        refs = list(self.rm.getReferencesTo(a))
        f = self.fm.getFunctionAt(a)
        if f is not None:
            for th in (f.getFunctionThunkAddresses(True) or []):
                refs += list(self.rm.getReferencesTo(th))
        return refs

    EH_PREFIXES = ("UNWIND_INFO", "FuncInfo", "IPToStateMap", "TryBlockMap", "HandlerType", "UnwindMap",
                   "ESTypeList", "SEHScopeTable")

    def ref_where(self, r):
        """(place of a reference, True if it comes from exception-handling tables: .pdata, unwind info)."""
        frm = r.getFromAddress()
        blk = self.mem.getBlock(frm)
        if blk is not None and blk.getName() == ".pdata":
            return f"{frm} (.pdata)", True
        where = self.where(frm)
        data = self.listing.getDataContaining(frm)
        if data is not None:
            start = data.getAddress()
            sym = self.st.getPrimarySymbol(start)
            if sym is not None and "vftable" in sym.getName():
                where = f"{sym.getName(True)}[slot 0x{frm.subtract(start) // 8:X}]"
        return where, where.startswith(self.EH_PREFIXES)

    def xrefs(self, target, out):
        a = self.resolve(target)
        rows, eh = [], []
        for r in self.refs_to(a):
            where, is_eh = self.ref_where(r)
            (eh if is_eh else rows).append(f"{r.getFromAddress()}  {str(r.getReferenceType()):<16} {where}")
        rows.sort(key=lambda s: s.split()[2] if len(s.split()) > 2 else s)
        text = [f"refs to {self.where(a)} at {a}: {len(rows)}" +
                (f" (+{len(eh)} from exception tables, listed last)" if eh else "")] + rows + eh
        res = dict(count=len(rows))
        if out and len(rows) > 60:
            os.makedirs(out, exist_ok=True)
            path = os.path.join(out, f"xrefs_{self.rva(a):X}.txt")
            open(path, "w", encoding="utf-8").write("\n".join(text) + "\n")
            res.update(file=path, text="\n".join(text[:61]) + f"\n... full list: {path}")
        else:
            res["text"] = "\n".join(text)
        return res

    def asm(self, targets, out):
        os.makedirs(out, exist_ok=True)
        files, errors = [], []
        for t in targets:
            try:
                a = self.resolve(t)
            except Exception as e:  # noqa: BLE001
                errors.append(str(e))
                continue
            f = self.fm.getFunctionContaining(a)
            if f is None:
                errors.append(f"{t}: not inside a function")
                continue
            entry = f.getEntryPoint()
            lines = self.func_header(f)
            for ins in self.listing.getInstructions(f.getBody(), True):
                ia = ins.getAddress()
                ref = ""
                flows = ins.getFlows()
                if flows:
                    ref = "  ; " + ", ".join(self.where(x) for x in flows[:2])
                else:
                    for r in ins.getReferencesFrom():
                        if r.isMemoryReference():
                            ref = "  ; " + self.where(r.getToAddress())
                            break
                mark = "  //>>>" if ins.contains(a) and not a.equals(entry) else ""
                lines.append(f"+{ia.subtract(entry):5X}  {ins}{ref}{mark}")
            ids = self.ids_at(entry)
            path = os.path.join(out, (f"AE_{ids[0]}" if ids else f"rva_{self.rva(entry):X}") + ".asm.txt")
            open(path, "w", encoding="utf-8").write("\n".join(lines) + "\n")
            files.append(path)
        return dict(files=files, errors=errors)

    def info(self, targets):
        out = []
        for t in targets:
            try:
                a = self.resolve(t)
            except Exception as e:  # noqa: BLE001
                out.append(f"{t}: {e}")
                continue
            f = self.fm.getFunctionContaining(a)
            names = [s.getName(True) for s in self.st.getSymbols(a)]
            line = f"{t}: {a} (RVA 0x{self.rva(a):X}) {self.where(a)}; symbols here: {', '.join(names[:8]) or '-'}"
            if f is not None:
                callers = {self.fm.getFunctionContaining(r.getFromAddress()) for r in self.refs_to(f.getEntryPoint())
                           if r.getReferenceType().isCall()}
                line += (f"; function {f.getName(True)} size 0x{f.getBody().getNumAddresses():X}, "
                         f"{len([c for c in callers if c])} calling functions, "
                         f"{len(list(f.getCalledFunctions(self.monitor)))} called")
            out.append(line)
        return dict(text="\n".join(out))

    def fieldrefs(self, offset, within, users_of, scan_all, access, out):
        """Instructions with a [reg + offset] memory operand."""
        from ghidra.program.model.lang import OperandType
        from ghidra.program.model.scalar import Scalar
        funcs = []
        for t in within or []:
            f = self.fm.getFunctionContaining(self.resolve(t))
            if f is not None:
                funcs.append(f)
        for t in users_of or []:
            for r in self.refs_to(self.resolve(t)):
                f = self.fm.getFunctionContaining(r.getFromAddress())
                if f is not None:
                    funcs.append(f)
        candidates = []
        if scan_all:
            if offset < 0x80:
                raise ValueError("--all needs an offset >= 0x80 (disp32 byte search); use --in/--users-of")
            with self.index_lock:
                if self.pe is None:
                    self.pe = addrlib.PE(self.ver["exe"])
            pat = offset.to_bytes(4, "little")
            for name, va, size, raw, chars in self.pe.sections:
                if not chars & 0x20000000:
                    continue
                buf = self.pe.data[raw:raw + size]
                pos = buf.find(pat)
                while pos >= 0:
                    ins = self.listing.getInstructionContaining(self.base.add(va + pos))
                    if ins is not None:
                        candidates.append(ins)
                    pos = buf.find(pat, pos + 1)
        seen_f = set()
        for f in funcs:
            if f.getEntryPoint() in seen_f:
                continue
            seen_f.add(f.getEntryPoint())
            candidates.extend(self.listing.getInstructions(f.getBody(), True))
        rows, seen = [], set()
        per_func = collections.Counter()
        for ins in candidates:
            ia = ins.getAddress()
            if ia in seen:
                continue
            for i in range(ins.getNumOperands()):
                if not OperandType.isDynamic(ins.getOperandType(i)):
                    continue
                objs = list(ins.getOpObjects(i))
                if not any(isinstance(o, Scalar) and o.getUnsignedValue() == offset for o in objs):
                    continue
                rt = ins.getOperandRefType(i)
                kind = ("W" if rt.isWrite() else "") + ("R" if rt.isRead() else "")
                if kind == "" and str(ins.getMnemonicString()).upper() == "LEA":
                    kind = "A"
                if access and not any(c in kind for c in access.upper()):
                    continue
                seen.add(ia)
                where = self.where(ia)
                per_func[where.split("+0x")[0]] += 1
                rows.append(f"{ia}  {kind or '?':<2} {where:<60} {ins}")
                break
        rows.sort()
        text = [f"[reg + 0x{offset:X}]: {len(rows)} instructions in {len(per_func)} functions "
                f"(scanned {len(seen_f)} functions{' + whole code by bytes' if scan_all else ''})"]
        text += rows
        os.makedirs(out, exist_ok=True)
        path = os.path.join(out, f"fieldrefs_{offset:X}.txt")
        open(path, "w", encoding="utf-8").write("\n".join(text) + "\n")
        top = "\n".join(f"  {n:4d}  {fn}" for fn, n in per_func.most_common(15))
        return dict(file=path, text=text[0] + "\ntop functions:\n" + top)

    # ---------------------------------------------------------------- helpers for the new commands
    def label(self, addr):
        """'Name (AE#1, AE#2)' for a function entry or any address."""
        f = self.fm.getFunctionAt(addr)
        name = f.getName(True) if f is not None else self.where(addr)
        ids = self.ids_at(addr)
        if f is not None and f.isThunk():
            tf = f.getThunkedFunction(True)
            if tf is not None:
                name += f" -> {tf.getName(True)}"
        ids = [i for i in ids if name != f"{self.ver['prefix']}_{i}"]
        return name + (f" ({', '.join('AE#%d' % i for i in ids)})" if ids and f is not None else "")

    def write_text(self, out, name, lines, inline_max):
        """Whole text inline if short, else a file plus the first inline_max lines."""
        if len(lines) <= inline_max:
            return dict(text="\n".join(lines))
        os.makedirs(out, exist_ok=True)
        path = os.path.join(out, name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("\n".join(lines) + "\n")
        return dict(file=path, text="\n".join(lines[:inline_max]) +
                    f"\n... {len(lines) - inline_max} more lines; full text: {path}")

    def _cached_index(self, name, build):
        path = self.cache_dir / f"{name}.json"
        if path.exists():
            try:
                return json.load(open(path, encoding="utf-8"))
            except (OSError, ValueError):
                pass
        t0 = time.time()
        data = build()
        log(f"index {name}: {len(data)} entries in {time.time() - t0:.1f} s")
        tmp = path.with_suffix(".tmp")
        json.dump(data, open(tmp, "w", encoding="utf-8"))
        os.replace(tmp, path)
        return data

    def symbol_index(self):
        """[(full name, rva, kind)] of non-dynamic symbols except the AE_<id> labels of Address Library."""
        with self.index_lock:
            if self._symbols is None:
                skip = re.compile(rf"^{self.ver['prefix']}_\d+$")
                base = self.base.getOffset()

                def build():
                    rows = []
                    for s in self.st.getAllSymbols(False):
                        a = s.getAddress()
                        if not a.isMemoryAddress() or skip.match(s.getName()):
                            continue
                        rows.append((s.getName(True), a.getOffset() - base, str(s.getSymbolType())))
                    return rows
                self._symbols = self._cached_index("symbols", build)
            return self._symbols

    def string_index(self):
        """[(rva, value)] of defined strings."""
        with self.index_lock:
            if self._strings is None:
                from ghidra.program.util import DefinedStringIterator
                base = self.base.getOffset()

                def build():
                    rows = []
                    for d in DefinedStringIterator.forProgram(self.prog):
                        v = d.getValue()
                        if v is not None:
                            rows.append((d.getAddress().getOffset() - base, str(v)))
                    return rows
                self._strings = self._cached_index("strings", build)
            return self._strings

    def ref_places(self, a, limit):
        """(count without exception tables, first `limit` places: calls first)."""
        refs = sorted(self.refs_to(a), key=lambda r: not r.getReferenceType().isCall())
        places, n = [], 0
        for r in refs:
            where, is_eh = self.ref_where(r)
            if is_eh:
                continue
            n += 1
            if len(places) < limit:
                places.append(f"{where} [{r.getReferenceType()}]")
        return n, places

    # ---------------------------------------------------------------- search
    def search(self, name_regex, string_regex, kinds, max_results, refs, case, out):
        if not name_regex and not string_regex:
            raise ValueError("search needs name_regex or string_regex")
        flags = 0 if case else re.I
        lines, total = [], 0
        if name_regex:
            rx = re.compile(name_regex, flags)
            want = {k.lower() for k in kinds} if kinds else None
            hits = [r for r in self.symbol_index() if rx.search(r[0]) and (want is None or r[2].lower() in want)]
            total += len(hits)
            lines.append(f"symbols matching /{name_regex}/: {len(hits)}" +
                         (f" (first {max_results})" if len(hits) > max_results else ""))
            for name, rva, kind in hits[:max_results]:
                a = self.base.add(rva)
                ids = self.ids_at(a)
                n, places = self.ref_places(a, refs)
                lines.append(f"  {name}  [{kind}] {a} {' '.join('AE#%d' % i for i in ids)}  refs {n}")
                lines += [f"      <- {p}" for p in places]
        if string_regex:
            rx = re.compile(string_regex, flags)
            hits = [r for r in self.string_index() if rx.search(r[1])]
            total += len(hits)
            lines.append(f"strings matching /{string_regex}/: {len(hits)}" +
                         (f" (first {max_results})" if len(hits) > max_results else ""))
            for rva, value in hits[:max_results]:
                a = self.base.add(rva)
                n, places = self.ref_places(a, refs)
                v = value if len(value) <= 120 else value[:117] + "..."
                lines.append(f"  {json.dumps(v, ensure_ascii=False)}  {a}  refs {n}")
                lines += [f"      <- {p}" for p in places]
        res = self.write_text(out, f"search_{int(time.time() * 1000) % 10**8}.txt", lines, 250)
        res["count"] = total
        return res

    # ---------------------------------------------------------------- call graph
    def _callers(self, f):
        """[(function or None, from address, kind)] calling or jumping to f, plus vftable slots holding it."""
        rows = []
        for r in self.refs_to(f.getEntryPoint()):
            rt = r.getReferenceType()
            frm = r.getFromAddress()
            if rt.isCall() or rt.isJump():
                c = self.fm.getFunctionContaining(frm)
                if c is not None and c.getEntryPoint().equals(f.getEntryPoint()):
                    continue
                rows.append((c, frm, "call" if rt.isCall() else "jmp"))
            elif rt.isData():
                data = self.listing.getDataContaining(frm)
                if data is not None:
                    sym = self.st.getPrimarySymbol(data.getAddress())
                    if sym is not None and "vftable" in sym.getName():
                        slot = frm.subtract(data.getAddress()) // 8
                        rows.append((None, frm, f"{sym.getName(True)}[slot 0x{slot:X}]"))
        return rows

    def _callees(self, f):
        """[(function, from address, kind)] in instruction order, plus the number of indirect calls."""
        rows, indirect = [], 0
        body = f.getBody()
        for ins in self.listing.getInstructions(body, True):
            ft = ins.getFlowType()
            if not (ft.isCall() or ft.isJump()):
                continue
            flows = ins.getFlows()
            if not flows:
                if ft.isCall():
                    indirect += 1
                continue
            for to in flows:
                if ft.isJump() and body.contains(to):
                    continue
                g = self.fm.getFunctionAt(to) or self.fm.getFunctionContaining(to)
                if g is not None:
                    rows.append((g, ins.getAddress(), "call" if ft.isCall() else "jmp"))
        return rows, indirect

    def callgraph(self, target, direction, depth, max_nodes, out):
        a = self.resolve(target)
        root = self.fm.getFunctionContaining(a)
        if root is None:
            raise ValueError(f"{target}: not inside a function")
        up = direction.startswith("caller")
        lines = [f"{'callers' if up else 'callees'} of {self.label(root.getEntryPoint())}, depth {depth}"
                 + ("" if up else "; indirect calls (vtable, pointers) are counted, not followed")]
        seen = {root.getEntryPoint()}
        nodes = 0
        truncated = False

        def walk(f, level):
            nonlocal nodes, truncated
            if up:
                edges = self._callers(f)
                note = ""
            else:
                edges, indirect = self._callees(f)
                note = f"{'  ' * level}  ({indirect} indirect calls)" if indirect else ""
            grouped = collections.OrderedDict()
            for g, frm, kind in edges:
                key = g.getEntryPoint() if g is not None else (frm, kind)
                grouped.setdefault(key, (g, kind, []))[2].append(frm)
            arrow = "<-" if up else "->"
            for key, (g, kind, frms) in grouped.items():
                if nodes >= max_nodes:
                    truncated = True
                    return
                nodes += 1
                ind = "  " * (level + 1)
                if g is None:
                    lines.append(f"{ind}{arrow} {kind}")
                    continue
                site_f = f if not up else g
                sites = ", ".join(f"+0x{x.subtract(site_f.getEntryPoint()):X}" for x in frms[:4]) + \
                    (f", +{len(frms) - 4} more" if len(frms) > 4 else "")
                again = g.getEntryPoint() in seen
                lines.append(f"{ind}{arrow} {self.label(g.getEntryPoint())}  [{kind} at {sites}]"
                             + ("  (see above)" if again else ""))
                if not again:
                    seen.add(g.getEntryPoint())
                    if level + 1 < depth:
                        walk(g, level + 1)
            if note:
                lines.append(note)

        walk(root, 0)
        if truncated:
            lines.append(f"... stopped at max_nodes={max_nodes}")
        res = self.write_text(out, f"callgraph_{self.rva(root.getEntryPoint()):X}_{'up' if up else 'down'}.txt",
                              lines, 300)
        res["nodes"] = nodes
        return res

    # ---------------------------------------------------------------- vtables (MSVC RTTI)
    def _rtti(self, vft):
        """Subobject offset and base classes [(name, mdisp, pdisp)] of a vftable, from its CompleteObjectLocator."""
        base = self.base
        col = base.getNewAddress(self.mem.getLong(vft.subtract(8)))
        offset = self.mem.getInt(col.add(4))
        chd = base.add(self.mem.getInt(col.add(16)) & 0xFFFFFFFF)
        n = self.mem.getInt(chd.add(8))
        bca = base.add(self.mem.getInt(chd.add(12)) & 0xFFFFFFFF)
        bases = []
        for i in range(n):
            bcd = base.add(self.mem.getInt(bca.add(4 * i)) & 0xFFFFFFFF)
            td = base.add(self.mem.getInt(bcd) & 0xFFFFFFFF)
            sym = self.st.getPrimarySymbol(td)
            name = sym.getParentNamespace().getName(True) if sym is not None else str(td)
            bases.append((name, self.mem.getInt(bcd.add(8)), self.mem.getInt(bcd.add(12))))
        return offset, bases

    def vftables(self):
        """{class full name: [vftable address]} from Ghidra's RTTI symbols."""
        with self.index_lock:
            if self._vftables is None:
                d = collections.defaultdict(list)
                for s in self.st.getSymbols("vftable"):
                    d[s.getParentNamespace().getName(True)].append(s.getAddress())
                self._vftables = d
            return self._vftables

    def _slots(self, vft):
        data = self.listing.getDataAt(vft)
        n = data.getNumComponents() if data is not None and data.isArray() else 0
        if n == 0:  # not typed: read until the next label
            n = 0
            while n < 2000:
                a = vft.add(8 * n)
                if n and self.st.getPrimarySymbol(a) is not None:
                    break
                if self.fm.getFunctionAt(self.base.getNewAddress(self.mem.getLong(a))) is None:
                    break
                n += 1
        return [self.base.getNewAddress(self.mem.getLong(vft.add(8 * i))) for i in range(n)]

    def vtable(self, cls, offset, out):
        tables = self.vftables()
        vfts = tables.get(cls)
        if not vfts:
            low = cls.lower()
            near = sorted(k for k in tables if low in k.lower())[:20]
            raise ValueError(f"{cls}: no vftable symbol; similar classes: {', '.join(near) or '-'}")
        lines = []
        for vft in sorted(vfts, key=lambda x: x.getOffset()):
            try:
                sub, bases = self._rtti(vft)
            except Exception as e:  # noqa: BLE001
                sub, bases = None, []
                lines.append(f"// RTTI of {vft} unreadable: {e}")
            if offset is not None and sub != offset:
                continue
            slots = self._slots(vft)
            # the base whose subobject sits at the same offset: its primary vftable gives the inherited slots
            parent = next(((b, md, pd) for b, md, pd in bases[1:] if md == sub and pd == -1), None)
            pslots = []
            if parent is not None:
                for pv in tables.get(parent[0], []):
                    try:
                        if self._rtti(pv)[0] == 0:
                            pslots = self._slots(pv)
                            break
                    except Exception:  # noqa: BLE001
                        pass
            ids = self.ids_at(vft)
            lines.append(f"{cls}::vftable at {vft} {' '.join('AE#%d' % i for i in ids)}: subobject offset "
                         f"0x{sub if sub is not None else 0:X}, {len(slots)} slots, base "
                         f"{parent[0] if parent else '-'} ({len(pslots)} slots)")
            lines.append("  slot  byte   function | own/inherited")
            for i, fa in enumerate(slots):
                if i < len(pslots):
                    status = f"inherited from {parent[0]}" if pslots[i].equals(fa) else \
                        f"override of {self.label(pslots[i])}"
                else:
                    status = "new in " + cls if parent else "own"
                lines.append(f"  {i:4X}  {8 * i:5X}  {self.label(fa)} | {status}")
            lines.append("")
        if offset is not None and not any(ln.startswith(cls + "::vftable") for ln in lines):
            raise ValueError(f"{cls}: no vftable for subobject offset 0x{offset:X}")
        bases_txt = ""
        if vfts:
            try:
                _, bases = self._rtti(sorted(vfts, key=lambda x: x.getOffset())[0])
                bases_txt = "bases (RTTI, offset): " + ", ".join(
                    f"{b}@0x{md:X}" + ("v" if pd != -1 else "") for b, md, pd in bases[1:])
            except Exception:  # noqa: BLE001
                pass
        lines.insert(0, bases_txt)
        safe = re.sub(r"[^\w.-]+", "_", cls)[:80]
        return self.write_text(out, f"vtable_{safe}.txt", lines, 200)

    # ---------------------------------------------------------------- data types
    def struct(self, name, out):
        from java.util import ArrayList
        from ghidra.program.model.data import Structure, Union, Enum, TypeDef, FunctionDefinition
        dtm = self.prog.getDataTypeManager()
        found = ArrayList()
        dtm.findDataTypes(name.split("::")[-1] if "/" not in name else name.rsplit("/", 1)[-1], found)
        want = name.replace("::", "/")
        found = [d for d in found if name in (d.getName(), d.getPathName(), d.getDisplayName())
                 or d.getPathName().endswith("/" + want)] or list(found)
        found.sort(key=lambda d: (not d.getPathName().startswith("/NG/"), d.getPathName()))
        if not found:
            low = name.lower()
            near = sorted({d.getPathName() for d in dtm.getAllDataTypes() if low in d.getName().lower()})[:30]
            raise ValueError(f"{name}: no data type; similar: {', '.join(near) or '-'} "
                             f"({dtm.getDataTypeCount(True)} types in the base; NG classes are in /NG/RE)")
        lines = []
        for d in found[:5]:
            while isinstance(d, TypeDef) and isinstance(d.getBaseDataType(), (Structure, Union, Enum)):
                lines.append(f"{d.getPathName()} = typedef of {d.getBaseDataType().getPathName()}")
                d = d.getBaseDataType()
            lines.append(f"{d.getPathName()}  size 0x{d.getLength():X}" +
                         (f"  // {d.getDescription()}" if d.getDescription() else ""))
            if isinstance(d, (Structure, Union)):
                for c in d.getDefinedComponents():
                    cm = c.getComment()
                    lines.append(f"  +0x{c.getOffset():04X}  {c.getDataType().getDisplayName():<32} "
                                 f"{c.getFieldName() or c.getDefaultFieldName()}" + (f"  // {cm}" if cm else ""))
            elif isinstance(d, Enum):
                for n in d.getNames():
                    lines.append(f"  {n} = 0x{d.getValue(n):X}")
            elif isinstance(d, TypeDef):
                lines.append(f"  typedef of {d.getBaseDataType().getPathName()}")
            elif isinstance(d, FunctionDefinition):
                lines.append(f"  {d.getPrototypeString()}")
            lines.append("")
        safe = re.sub(r"[^\w.-]+", "_", name)[:80]
        return self.write_text(out, f"struct_{safe}.txt", lines, 200)

    def status(self):
        return dict(text=f"snapshot {self.snapshot}, decompilers {self.pool_made}/{self.pool_max}, "
                         f"cache {dict(self.stats)}, {len(self.mem_cache)} functions in memory, "
                         f"cache dir {self.cache_dir}")


class State:
    def __init__(self):
        self.ready = threading.Event()
        self.ghidra = None
        self.error = None
        self.last = time.time()
        self.started = time.time()
        self.stop = threading.Event()
        self.requests = 0
        self.busy = 0
        self.busy_lock = threading.Lock()
        self.idle = threading.Condition(self.busy_lock)


STATE = State()


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        try:
            req = json.loads(self.rfile.readline().decode("utf-8"))
        except Exception as e:  # noqa: BLE001
            self._send(dict(ok=False, error=f"bad request: {e}"))
            return
        cmd = req.get("cmd")
        if cmd == "ping" and not req.get("wait"):
            self._send(dict(ok=True, ready=STATE.ready.is_set(), error=STATE.error, pid=os.getpid(),
                            uptime=round(time.time() - STATE.started), requests=STATE.requests,
                            busy=STATE.busy, protocol=PROTOCOL))
            return
        if cmd == "stop":
            self._send(dict(ok=True, stopping=True))
            STATE.stop.set()
            return
        STATE.ready.wait()
        if STATE.error:
            self._send(dict(ok=False, error=STATE.error))
            return
        if STATE.stop.is_set():
            self._send(dict(ok=False, error="server is stopping; retry"))
            return
        with STATE.busy_lock:
            STATE.busy += 1
            STATE.requests += 1
            STATE.last = time.time()
        t0 = time.time()
        try:
            res = dispatch(STATE.ghidra, req)
            res.update(ok=True)
        except Exception as e:  # noqa: BLE001
            res = dict(ok=False, error=f"{type(e).__name__}: {e}")
            if not isinstance(e, (ValueError, KeyError, re.error)):
                res["trace"] = traceback.format_exc()[-1500:]
        finally:
            with STATE.busy_lock:
                STATE.busy -= 1
                STATE.last = time.time()
                STATE.idle.notify_all()
        res["seconds"] = round(time.time() - t0, 2)
        res["queued"] = round(sum(r.get("pool_wait", 0) for r in res.get("results", [])), 2)
        log(f"{cmd} {req.get('targets') or req.get('target') or req.get('name') or ''} -> {res.get('seconds')} s "
            f"ok={res['ok']}" + "".join(f" {r.get('source')}" for r in res.get("results", [])))
        self._send(res)

    def _send(self, obj):
        try:
            self.wfile.write((json.dumps(obj) + "\n").encode("utf-8"))
        except OSError:
            pass


def dispatch(g, req):
    cmd = req["cmd"]
    out = req.get("out") or str(gconfig.STATE_DIR / "out")
    if cmd == "ping":
        return dict(ready=True, pid=os.getpid(), protocol=PROTOCOL)
    if cmd == "status":
        return g.status()
    if cmd == "decomp":
        return g.decomp(req["targets"], out, req.get("addr", False), req.get("cache", True))
    if cmd == "asm":
        return g.asm(req["targets"], out)
    if cmd == "xrefs":
        return g.xrefs(req["target"], out)
    if cmd == "info":
        return g.info(req["targets"])
    if cmd == "fieldrefs":
        return g.fieldrefs(req["offset"], req.get("in"), req.get("users_of"), req.get("all", False),
                           req.get("access"), out)
    if cmd == "search":
        return g.search(req.get("name_regex"), req.get("string_regex"), req.get("kinds"),
                        int(req.get("max_results") or 40), int(req.get("refs", 5)), req.get("case", False), out)
    if cmd == "callgraph":
        return g.callgraph(req["target"], req.get("direction", "callers"), int(req.get("depth") or 2),
                           int(req.get("max_nodes") or 150), out)
    if cmd == "vtable":
        off = req.get("offset")
        return g.vtable(req["name"], int(off, 0) if isinstance(off, str) else off, out)
    if cmd == "struct":
        return g.struct(req["name"], out)
    raise ValueError(f"unknown command {cmd}")


class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True
    allow_reuse_address = False


def main():
    global LOG
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default=None)
    ap.add_argument("--project-dir", default=None, help="default: read-only copy of the version")
    ap.add_argument("--port", type=int, default=gconfig.SERVER_PORT)
    ap.add_argument("--idle-minutes", type=float, default=gconfig.SERVER_IDLE_MINUTES)
    ap.add_argument("--decompilers", type=int, default=gconfig.SERVER_DECOMPILERS)
    a = ap.parse_args()
    ver = gconfig.version(a.version)
    gconfig.STATE_DIR.mkdir(parents=True, exist_ok=True)
    LOG = str(gconfig.STATE_DIR / "server.log")
    try:
        srv = Server((gconfig.SERVER_HOST, a.port), Handler)  # the port is the single-instance mutex
    except OSError as e:
        print(f"port {a.port} busy ({e}); another server is running", flush=True)
        return 1
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    project_dir = a.project_dir or ver["ro_dir"]
    log(f"server pid {os.getpid()} port {a.port}, loading {project_dir}")
    ctx = None
    try:
        if not os.path.isdir(project_dir):
            raise RuntimeError(f"no project at {project_dir}; the curator makes it with snapshot.py")
        t0 = time.time()
        gstart.start_jvm()
        t1 = time.time()
        ctx = gstart.open_program(project_dir, ver["project"], ver["program"], True)
        _, prog = ctx.__enter__()
        STATE.ghidra = Ghidra(ver, prog, project_dir, a.decompilers)
        log(f"ready: jvm {t1 - t0:.1f} s, open+decompiler {time.time() - t1:.1f} s, "
            f"snapshot {STATE.ghidra.snapshot}, up to {a.decompilers} decompilers, protocol {PROTOCOL}")
    except Exception as e:  # noqa: BLE001
        STATE.error = f"server failed to load: {type(e).__name__}: {e}"
        log(STATE.error + "\n" + traceback.format_exc())
    STATE.ready.set()
    STATE.last = time.time()
    while not STATE.stop.is_set():
        if STATE.error and time.time() - STATE.last > 60:
            break
        idle = time.time() - STATE.last
        if idle > a.idle_minutes * 60 and STATE.busy == 0:
            log(f"idle {idle / 60:.0f} min, exiting")
            break
        STATE.stop.wait(10)
    with STATE.busy_lock:  # let running requests finish (new ones are refused once stop is set)
        STATE.stop.set()
        STATE.idle.wait_for(lambda: STATE.busy == 0, timeout=300)
    srv.shutdown()
    srv.server_close()
    if STATE.ghidra is not None:
        STATE.ghidra.dispose()
    if ctx is not None:
        try:
            ctx.__exit__(None, None, None)
        except Exception as e:  # noqa: BLE001
            log(f"close: {e}")
    log("stopped")
    os._exit(0)


if __name__ == "__main__":
    sys.exit(main())
