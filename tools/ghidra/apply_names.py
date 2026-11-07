#!/usr/bin/env python3
"""Apply names from collect_names.py to the MASTER Ghidra project. Curator only; Ghidra must be closed.

Runs under the PyGhidra venv:
  E:\\Ghidra\\pyghidra-venv\\Scripts\\python.exe tools/ghidra/apply_names.py [--version 1.6.1170] [--names names.json]

Repeatable: existing names equal to ours are skipped, our plate-comment lines (prefix "[clr] ") are replaced,
other comments are kept. Names from Ghidra analysis are replaced (old name stays as a label), names made by hand
in the GUI (USER_DEFINED) are kept and listed; put lasting names into curated_names.txt instead. Order: NG names, then <PREFIX>_<id> labels (default-named functions take that name),
then comments. Ghidra's own RTTI symbols are compared with NG VTABLE_*/RTTI_*, not replaced.

Phase 0 (T34) creates the functions Ghidra missed: .pdata starts that are not chained fragments, and code addresses
held in data (vtables, callback tables; leaf functions have no .pdata entry).

Function ID rule (T34): a name from Ghidra analysis (Function ID, exception parsing) is replaced by any name of ours,
so a function in a vftable slot of a game class (rtti-vslot), with an NG ID, a Papyrus or command registration never
keeps an STL/CRT name. Functions that sit in slots of several unrelated classes keep the analysis name and get a
"[clr] vftable slots: ..." comment; the report counts the analysis names that remain, by namespace.
"""
import argparse
import collections
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gconfig  # noqa: E402
import gstart  # noqa: E402

TAG = "[clr] "


NORM_WORDS = re.compile(r"\b(class|struct|enum|union|const|near|far|public|private|protected|static|virtual|"
                        r"cdecl|__cdecl|ptr64|__ptr64)\b")
NORM_TYPES = [("unsignedchar", "uchar"), ("unsignedshort", "ushort"), ("unsignedint64", "uint64"),
              ("unsignedlonglong", "uint64"), ("ulonglong", "uint64"), ("uint64t", "uint64"), ("unsignedint", "uint"),
              ("uint32t", "uint"), ("unsignedlong", "ulong"), ("signedchar", "char"), ("int64t", "int64"),
              ("longlong", "int64"), ("int32t", "int"), ("uint16t", "ushort"), ("uint8t", "uchar")]


# MFC classes that Function ID matched on Bethesda code (T34): the game links no MFC
MFC = re.compile(r"(^|[^A-Za-z0-9])(CMFC[A-Za-z]+|CWinApp|CWinThread|CWnd|CFrameWnd|CDocument|CView|CDialog|"
                 r"CTaskDialog|CControlRectTracker|COleControl|CObject|CCmdTarget|CArray|CMap|CList)([^A-Za-z0-9]|$)")


def norm(s):
    """Class name without spelling differences of NG (flattened templates) and Ghidra (demangled): keywords class/
    struct/const/near, pointer and reference marks, separators and integer type spellings are dropped."""
    s = re.sub(r"_anon_[0-9A-Fa-f]+|A0x[0-9A-Fa-f]{8}|`?anonymous[ _]namespace'?", "", s or "").replace("_", " ")
    s = NORM_WORDS.sub(" ", s)
    s = re.sub(r"[^0-9A-Za-z]", "", s).lower()
    for a, b in NORM_TYPES:
        s = s.replace(a, b)
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default=None)
    ap.add_argument("--names", default=None, help="collect_names.py output (default: state dir)")
    ap.add_argument("--project-dir", default=None, help="default: master project of the version")
    ap.add_argument("--report", default=None)
    ap.add_argument("--heap", default="8G")
    ap.add_argument("--dry-run", action="store_true", help="do everything, then discard the transaction")
    a = ap.parse_args()

    ver = gconfig.version(a.version)
    names_path = a.names or str(gconfig.STATE_DIR / f"names-{ver['name']}.json")
    report_path = a.report or str(gconfig.STATE_DIR / f"apply-{ver['name']}.report.txt")
    data = json.load(open(names_path, encoding="utf-8"))
    t0 = time.time()
    gstart.start_jvm(a.heap)
    with gstart.open_program(a.project_dir or ver["master_dir"], ver["project"], ver["program"], False) as (_, prog):
        t1 = time.time()
        tx = prog.startTransaction("CLR names")
        ok = False
        try:
            report = apply(prog, data)
            ok = not a.dry_run
        finally:
            prog.endTransaction(tx, ok)
    report.insert(0, f"jvm+open {t1 - t0:.0f} s, apply+save {time.time() - t1:.0f} s, committed={ok}")
    open(report_path, "w", encoding="utf-8").write("\n".join(report) + "\n")
    print("\n".join(report[:40]))
    print(f"report: {report_path}")


def apply(prog, data):
    from ghidra.app.cmd.function import CreateFunctionCmd
    from ghidra.app.util import NamespaceUtils
    from ghidra.app.util import SymbolPath
    from ghidra.program.model.symbol import SourceType
    try:
        from ghidra.program.model.listing import CommentType
        PLATE = CommentType.PLATE
    except ImportError:
        from ghidra.program.model.listing import CodeUnit
        PLATE = CodeUnit.PLATE_COMMENT

    report = []
    stats = collections.Counter()
    base = prog.getImageBase()
    if base.getOffset() != data["image_base"]:
        raise SystemExit(f"image base {base} != {data['image_base']:#x}")
    st = prog.getSymbolTable()
    fm = prog.getFunctionManager()
    listing = prog.getListing()
    glob = prog.getGlobalNamespace()
    prefix = data["prefix"]
    ns_cache = {}
    plates = collections.defaultdict(list)
    renamed = []
    errors = []

    def namespace_of(path):
        if path not in ns_cache:
            try:
                ns_cache[path] = NamespaceUtils.createNamespaceHierarchy(path, glob, prog, SourceType.IMPORTED)
            except Exception as e:  # noqa: BLE001 - Java exceptions
                errors.append(f"namespace {path}: {e}")
                ns_cache[path] = None
        return ns_cache[path]

    def split(full):
        full = full.replace(" ", "_")  # Ghidra symbol names have no spaces (RTTI spelling: unsigned_char)
        sp = SymbolPath(full)
        parent = sp.getParent()
        ns = glob if parent is None else namespace_of(parent.getPath())
        if ns is None:
            return glob, full.replace("::", "__")
        return ns, sp.getName()

    total_funcs = fm.getFunctionCount()
    default_before = sum(1 for f in fm.getFunctions(True) if f.getSymbol().getSource() == SourceType.DEFAULT)

    # 0. missing functions: .pdata starts and code addresses held in data (T34)
    starts = dict(data.get("starts") or {})
    starts["named"] = [e["rva"] for e in data["names"] if e["kind"] in ("func", "code")]
    report.append(create_functions(prog, starts, stats))

    def source_plate(e):
        if e["source"] in ("ng-reloc", "curated"):
            plates[e["rva"]].append(f"{'ng' if e['source'] == 'ng-reloc' else 'curated'}: {e['origin']}")
        elif e["source"] in ("addrlibdb", "addrlibdb-class", "po3"):
            plates[e["rva"]].append(f"name from {e['origin']}")

    # 1. NG names
    vt_check = collections.Counter()
    vt_mismatch = []
    for e in data["names"]:
        a = base.add(e["rva"])
        name = e["name"]
        kind = e["kind"]
        try:
            if kind in ("func", "code"):
                f = fm.getFunctionAt(a)
                if f is None:
                    cmd = CreateFunctionCmd(a)
                    if cmd.applyTo(prog):
                        stats["functions created"] += 1
                    f = fm.getFunctionAt(a)
                if f is None:
                    stats["no function, labelled"] += 1
                    ns, simple = split(name)
                    st.createLabel(a, simple, ns, SourceType.IMPORTED).setPrimary()
                    continue
                if f.getName(True) == name:
                    stats[f"same name ({e['source']})"] += 1
                    source_plate(e)
                    continue
                sym = f.getSymbol()
                src = sym.getSource()
                if src == SourceType.USER_DEFINED:
                    # curator's manual name wins over NG; listed in the report
                    renamed.append(f"0x{e['rva']:X}: kept manual {f.getName(True)}, NG says {name}")
                    continue
                vm = re.search(r"RTTI vftable 0x([0-9A-F]+) slot", e.get("origin", ""))                     if e["source"] == "rtti-vslot" else None
                if vm:  # the namespace of Ghidra's own vftable symbol of the owner class
                    vt_ns = [x.getParentNamespace() for x in st.getSymbols(base.add(int(vm.group(1), 16)))
                             if x.getName() == "vftable"]
                    if vt_ns:
                        name = vt_ns[0].getName(True) + "::" + name.rsplit("::", 1)[1]
                if f.getName(True) == name:
                    stats[f"same name ({e['source']})"] += 1
                    source_plate(e)
                    continue
                if src == SourceType.ANALYSIS:
                    renamed.append(f"0x{e['rva']:X}: {f.getName(True)} [{src}] -> {name} [{e['source']}]")
                    old_ns, old_name = f.getParentNamespace(), f.getName()
                    ns, simple = split(name)
                    sym.setNameAndNamespace(simple, ns, SourceType.IMPORTED)
                    st.createLabel(a, old_name, old_ns, SourceType.ANALYSIS)
                else:  # DEFAULT, or IMPORTED by an earlier run of this script
                    ns, simple = split(name)
                    sym.setNameAndNamespace(simple, ns, SourceType.IMPORTED)
                stats[f"function named ({e['source']})"] += 1
                source_plate(e)
            elif kind in ("vtable", "rtti"):
                have = [s.getName(True) for s in st.getSymbols(a)]
                key = "vftable" if kind == "vtable" else "RTTI"
                ghidra = [n for n in have if key.lower() in n.lower()]
                cls = name.rsplit("::", 1)[0] if kind == "vtable" else name[name.index("_") + 1:]
                if ghidra:
                    match = any(norm(cls) == norm(g.rsplit("::", 1)[0]) for g in ghidra) or \
                        any(norm(cls) in norm(g) for g in ghidra) or \
                        any("'::" in g and norm(cls).endswith(norm(g.rsplit("::", 2)[-2])) for g in ghidra)
                    # last rule: a class local to a function (`bool Script::ResetDialogueFlags(...)'::__l2::X)
                    vt_check[f"{kind}: ghidra has it, class {'matches' if match else 'differs'}"] += 1
                    if not match and len(vt_mismatch) < 60:
                        vt_mismatch.append(f"0x{e['rva']:X}: NG {name} vs Ghidra {', '.join(ghidra[:3])}")
                    if len(vt_mismatch) == 60:
                        vt_mismatch.append("... (first 60)")
                else:
                    vt_check[f"{kind}: missing in ghidra, NG label added"] += 1
                    ns, simple = split(name) if kind == "vtable" else (glob, name)
                    st.createLabel(a, simple, ns, SourceType.IMPORTED)
            else:
                ns, simple = split(name)
                existing = st.getSymbol(simple, a, ns)
                if existing is None:
                    existing = st.createLabel(a, simple, ns, SourceType.IMPORTED)
                existing.setPrimary()
                stats[f"data named ({e['source']})"] += 1
                if e["source"] in ("ng-reloc", "curated"):
                    plates[e["rva"]].append(f"{'ng' if e['source'] == 'ng-reloc' else 'curated'}: {e['origin']}")
        except Exception as ex:  # noqa: BLE001
            errors.append(f"0x{e['rva']:X} {name}: {ex}")

    # 2. Address Library labels
    for id_, rva in data["ids"].items():
        a = base.add(rva)
        lbl = f"{prefix}_{id_}"
        try:
            f = fm.getFunctionAt(a)
            if f is not None and f.getName() == lbl:
                continue
            if f is not None and f.getSymbol().getSource() == SourceType.DEFAULT:
                f.setName(lbl, SourceType.IMPORTED)
                stats["function named AE_<id>"] += 1
                continue
            if st.getGlobalSymbol(lbl, a) is None:
                st.createLabel(a, lbl, SourceType.IMPORTED)
                stats["id label"] += 1
        except Exception as ex:  # noqa: BLE001
            errors.append(f"{lbl}: {ex}")

    # 2b. strings that name Class::Method: comment on the single function that uses the string
    rm = prog.getReferenceManager()
    cm = collections.Counter()
    for rva, text in data.get("cm_strings", []):
        funcs = {fm.getFunctionContaining(r.getFromAddress()) for r in rm.getReferencesTo(base.add(rva))}
        funcs.discard(None)
        if len(funcs) == 1:
            f = funcs.pop()
            plates[f.getEntryPoint().getOffset() - base.getOffset()].append(f"string names {text}")
            cm["commented"] += 1
        else:
            cm[f"{min(len(funcs), 2)}{'+' if len(funcs) > 1 else ''} functions use it"] += 1
    stats["Class::Method strings"] = dict(cm)

    # 2c. Function ID names from MFC: the game does not link MFC, so they are false; back to <PREFIX>_<id>
    ids_at = collections.defaultdict(list)
    for id_, rva in data["ids"].items():
        ids_at[rva].append(id_)
    mfc = [f for f in fm.getFunctions(True)
           if f.getSymbol().getSource() == SourceType.ANALYSIS and MFC.search(f.getName(True))]
    stats["MFC Function ID names found"] = len(mfc)
    for f in mfc:
        sym = f.getSymbol()
        rva = f.getEntryPoint().getOffset() - base.getOffset()
        old = f.getName(True)
        plates[rva].append(f"Function ID name {old} is MFC, which the game does not link: not trusted")
        if ids_at.get(rva):
            try:
                old_ns, old_name = f.getParentNamespace(), f.getName()
                lbl = f"{prefix}_{ids_at[rva][0]}"
                dup = st.getGlobalSymbol(lbl, f.getEntryPoint())
                if dup is not None and not dup.equals(sym):
                    dup.delete()  # our id label; the function takes the name
                sym.setNameAndNamespace(lbl, glob, SourceType.IMPORTED)
                st.createLabel(f.getEntryPoint(), old_name, old_ns, SourceType.ANALYSIS)
                stats["MFC Function ID names demoted"] += 1
            except Exception as ex:  # noqa: BLE001
                errors.append(f"MFC 0x{rva:X} {old}: {ex}")
        else:
            stats["MFC Function ID names commented (no ID)"] += 1

    # 3. Comments: docs and NG origin, as "[clr] " plate lines
    for rva, lines in data["comments"].items():
        plates[int(rva)].extend(lines)
    for rva, lines in plates.items():
        a = base.add(rva)
        old = listing.getComment(PLATE, a) or ""
        kept = [ln for ln in str(old).splitlines() if not ln.startswith(TAG)]
        listing.setComment(a, PLATE, "\n".join(kept + [TAG + ln for ln in lines]))
        stats["plate comments"] += 1

    default_after = sum(1 for f in fm.getFunctions(True) if f.getSymbol().getSource() == SourceType.DEFAULT)
    report.append(f"functions: {total_funcs} before, {fm.getFunctionCount()} after; "
                  f"default-named {default_before} -> {default_after}")
    for k, v in sorted(stats.items()):
        report.append(f"  {k}: {v}")
    report.append("RTTI / vtable check (NG Offsets_*.h vs Ghidra RTTI analyzer):")
    for k, v in sorted(vt_check.items()):
        report.append(f"  {k}: {v}")
    report.extend("  " + m for m in vt_mismatch)
    report.append(player_vtable(prog, data))
    report.append(analysis_names(prog))
    report.append(f"non-default names replaced by NG ({len(renamed)}; old name kept as label):")
    report.extend("  " + r for r in renamed[:200])
    report.append(f"errors ({len(errors)}):")
    report.extend("  " + r for r in errors[:200])
    return report


def create_functions(prog, starts, stats):
    """Disassemble and create functions at .pdata starts and at code addresses held in data, unless the address
    is already inside a function (a pointer into the middle of a function is a label, not a start)."""
    from ghidra.app.cmd.disassemble import DisassembleCommand
    from ghidra.app.cmd.function import CreateFunctionCmd
    fm, listing, base = prog.getFunctionManager(), prog.getListing(), prog.getImageBase()
    pdata = set(starts.get("pdata", []))
    c = collections.Counter()
    # lea targets count only when Ghidra decoded a LEA at one of the sites (the byte scan can hit mid-instruction)
    leas = set()
    for t, sites in (starts.get("leas") or {}).items():
        for site in sites:
            ins = listing.getInstructionAt(base.add(site))
            if ins is not None and str(ins.getMnemonicString()).upper() == "LEA":
                leas.add(int(t))
                break
    leas -= pdata | set(starts.get("pointers", []))
    ids = set(starts.get("ids", [])) - pdata - leas - set(starts.get("pointers", []))
    named = set(starts.get("named", []))
    for rva in sorted(pdata | set(starts.get("pointers", [])) | leas | ids | named):
        a = base.add(rva)
        f = fm.getFunctionAt(a)
        if f is not None and listing.getInstructionAt(a) is None:
            # created on raw bytes by an older run (1-byte body): disassemble and create again, keep the name
            name, ns, src = f.getName(), f.getParentNamespace(), f.getSymbol().getSource()
            fm.removeFunction(a)
            DisassembleCommand(a, None, True).applyTo(prog)
            if CreateFunctionCmd(a).applyTo(prog) and fm.getFunctionAt(a) is not None:
                fm.getFunctionAt(a).getSymbol().setNameAndNamespace(name, ns, src)
                c["repaired (was created without code)"] += 1
            continue
        if f is not None:
            c["exists"] += 1
            continue
        if fm.getFunctionContaining(a) is not None and rva not in pdata:
            c["inside a function (label)"] += 1
            continue
        if listing.getInstructionAt(a) is None:
            if listing.getDefinedDataAt(a) is not None:
                c["defined data there (skipped)"] += 1
                continue
            DisassembleCommand(a, None, True).applyTo(prog)
            if listing.getInstructionAt(a) is None:
                c["not disassemblable"] += 1
                continue
        if CreateFunctionCmd(a).applyTo(prog) and fm.getFunctionAt(a) is not None:
            c["created from .pdata" if rva in pdata else "created from lea in code" if rva in leas
              else "created for a name" if rva in named and rva not in ids
              else "created from an ID after padding" if rva in ids else "created from a data pointer"] += 1
        else:
            c["create failed"] += 1
    stats["functions created (phase 0)"] = sum(v for k, v in c.items() if k.startswith("created"))
    return "phase 0, function starts: " + ", ".join(f"{k} {v}" for k, v in sorted(c.items()))


def analysis_names(prog):
    """Function names that still come from Ghidra analysis, by first namespace part."""
    from ghidra.program.model.symbol import SourceType
    c = collections.Counter()
    for f in prog.getFunctionManager().getFunctions(True):
        if f.getSymbol().getSource() == SourceType.ANALYSIS:
            n = f.getName(True)
            c["FID_conflict" if n.startswith("FID_conflict") else n.split("::")[0] if "::" in n else
              "Unwind/Catch funclets" if n.startswith(("Unwind@", "Catch@", "Catch_All@")) else "global"] += 1
    top = ", ".join(f"{k} {v}" for k, v in c.most_common(25))
    return f"analysis names left: {sum(c.values())}: {top}"


def player_vtable(prog, data):
    """Ghidra symbols at every VTABLE_PlayerCharacter address of NG."""
    st = prog.getSymbolTable()
    base = prog.getImageBase()
    lines = ["PlayerCharacter: NG VTABLE_PlayerCharacter vs Ghidra symbols"]
    for i, rva in enumerate(data["vtables"].get("VTABLE_PlayerCharacter", [])):
        names = [s.getName(True) for s in st.getSymbols(base.add(rva))]
        lines.append(f"  [{i}] 0x{rva:X}: {', '.join(names) or '-'}")
    return "\n".join(lines)


if __name__ == "__main__":
    main()
