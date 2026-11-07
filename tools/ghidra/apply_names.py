#!/usr/bin/env python3
"""Apply names from collect_names.py to the MASTER Ghidra project. Curator only; Ghidra must be closed.

Runs under the PyGhidra venv:
  E:\\Ghidra\\pyghidra-venv\\Scripts\\python.exe tools/ghidra/apply_names.py [--version 1.6.1170] [--names names.json]

Repeatable: existing names equal to ours are skipped, our plate-comment lines (prefix "[clr] ") are replaced,
other comments are kept. Names from Ghidra analysis are replaced (old name stays as a label), names made by hand
in the GUI (USER_DEFINED) are kept and listed; put lasting names into curated_names.txt instead. Order: NG names, then <PREFIX>_<id> labels (default-named functions take that name),
then comments. Ghidra's own RTTI symbols are compared with NG VTABLE_*/RTTI_*, not replaced.
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


def norm(s):
    """Class name without spelling differences of NG (flattened templates) and Ghidra (demangled)."""
    s = re.sub(r"_anon_[0-9A-Fa-f]+", "", s or "").replace("__ptr64", "")
    s = re.sub(r"[^0-9A-Za-z]", "", s).lower()
    return s.replace("unsignedint", "uint")


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
        sp = SymbolPath(full)
        parent = sp.getParent()
        ns = glob if parent is None else namespace_of(parent.getPath())
        if ns is None:
            return glob, full.replace("::", "__")
        return ns, sp.getName()

    total_funcs = fm.getFunctionCount()
    default_before = sum(1 for f in fm.getFunctions(True) if f.getSymbol().getSource() == SourceType.DEFAULT)

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
                    continue
                sym = f.getSymbol()
                src = sym.getSource()
                if src == SourceType.USER_DEFINED:
                    # curator's manual name wins over NG; listed in the report
                    renamed.append(f"0x{e['rva']:X}: kept manual {f.getName(True)}, NG says {name}")
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
                if e["source"] in ("ng-reloc", "curated"):
                    plates[e["rva"]].append(f"{'ng' if e['source'] == 'ng-reloc' else 'curated'}: {e['origin']}")
            elif kind in ("vtable", "rtti"):
                have = [s.getName(True) for s in st.getSymbols(a)]
                key = "vftable" if kind == "vtable" else "RTTI"
                ghidra = [n for n in have if key.lower() in n.lower()]
                cls = name.rsplit("::", 1)[0] if kind == "vtable" else name[name.index("_") + 1:]
                if ghidra:
                    match = any(norm(cls) == norm(g.rsplit("::", 1)[0]) for g in ghidra) or \
                        any(norm(cls) in norm(g) for g in ghidra)
                    vt_check[f"{kind}: ghidra has it, class {'matches' if match else 'differs'}"] += 1
                    if not match and len(vt_mismatch) < 60:
                        vt_mismatch.append(f"0x{e['rva']:X}: NG {name} vs Ghidra {', '.join(ghidra[:3])}")
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
    report.append(f"non-default names replaced by NG ({len(renamed)}; old name kept as label):")
    report.extend("  " + r for r in renamed[:200])
    report.append(f"errors ({len(errors)}):")
    report.extend("  " + r for r in errors[:200])
    return report


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
