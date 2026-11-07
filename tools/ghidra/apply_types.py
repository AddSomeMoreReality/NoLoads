#!/usr/bin/env python3
"""Curator: apply NG types (collect_types.py) to the MASTER Ghidra project. Ghidra must be closed.

  E:\\Ghidra\\pyghidra-venv\\Scripts\\python.exe tools/ghidra/apply_types.py [--version 1.6.1170] [--project-dir D]

The first run copies the project to <project>-pre-types (robocopy). Ghidra cannot remove /NG once functions
use it, so a rerun starts from that copy: --restore mirrors it back over the project, then applies. A new
analysis (analyze.ps1 + apply_names.py) needs a fresh copy: delete <project>-pre-types first.

Steps:
  1. NG.pdb -> Ghidra's PDB loader, "data types only", class-hierarchy layout -> category /NG (was /NG.pdb);
     the program's preferred namespace root becomes /NG/RE, so class namespaces (TES, BSScript::...) find their
     structures for `this`.
  2. Unnamed base-class components -> super_<Base>; NG's padding for runtime-specific blocks
     (_padPlayerRuntimeData) -> the matching nested *_RUNTIME_DATA structure when the sizes agree.
  3. Vtables: /NG/vftables/<Class>_vftable with one pointer per slot, named after MSVC's vftable report.
     Every class with its own primary vftable is flattened along its primary-base chain, so offset 0 is a
     `vfptr` typed <Class>_vftable* and calls through it get slot names in the pseudocode.
  4. `this`: __thiscall for functions in a class namespace that has a /NG/RE structure, when a vftable holds the
     function, or when the name is from NG/curated (plate comment "[clr] ng:"/"[clr] curated:") and not a static
     method of NG. Third-party names put functions into classes but give no `this`: they may be static.
  5. Singletons (data relocations of X::GetSingleton): X* or X at the address.
Then run snapshot.py. Report: STATE_DIR/apply-types-<version>.report.txt.
"""
import argparse
import collections
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import addrlib  # noqa: E402
import gconfig  # noqa: E402
import gstart  # noqa: E402

ROOT = "NG"
# NG's GetSingleton dereferences these, but the game keeps the object itself at the address (CLAUDE.md, section 7:
# RenderTargetManager at AE#411451 is reached with lea, not mov).
SINGLETON_DEPTH_FIX = {"BSGraphics::RenderTargetManager": 1}


def key(s):
    """Spelling-independent class key: MSVC 'RE::X<struct RE::Y>' vs Ghidra 'RE::X<RE::Y>' / 'unsigned_short'."""
    s = re.sub(r"\b(struct|class|union|enum)\s+", "", s)
    return re.sub(r"[\s_]", "", s)


def ident(s):
    s = re.sub(r"[^0-9A-Za-z_]", "_", s).strip("_")
    return s if s and not s[0].isdigit() else "_" + s


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default=None)
    ap.add_argument("--types", default=None, help="collect_types.py output dir (default: STATE_DIR/types/<ver>)")
    ap.add_argument("--project-dir", default=None, help="default: master project of the version")
    ap.add_argument("--heap", default="10G")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--restore", action="store_true", help="start from <project>-pre-types (rerun)")
    a = ap.parse_args()
    ver = gconfig.version(a.version)
    pdir = a.project_dir or ver["master_dir"]
    pdir = os.path.normpath(pdir)
    backup = pdir + "-pre-types"
    if a.restore:
        if not os.path.isdir(backup):
            raise SystemExit(f"no {backup}")
        robocopy(backup, pdir)
        print(f"restored {pdir} from {backup}")
    elif not os.path.isdir(backup):
        robocopy(pdir, backup)
        print(f"saved {pdir} to {backup} (rerun with --restore)")
    tdir = a.types or str(gconfig.STATE_DIR / "types" / ver["name"])
    data = json.load(open(os.path.join(tdir, "types.json"), encoding="utf-8"))
    report_path = str(gconfig.STATE_DIR / f"apply-types-{ver['name']}.report.txt")
    t0 = time.time()
    gstart.start_jvm(a.heap)
    with gstart.open_program(pdir, ver["project"], ver["program"], False) as (_, prog):
        t1 = time.time()
        tx = prog.startTransaction("CLR NG types")
        ok = False
        try:
            report = apply(prog, ver, data, os.path.join(tdir, "NG.pdb"))
            ok = not a.dry_run
        finally:
            prog.endTransaction(tx, ok)
    report.insert(0, f"jvm+open {t1 - t0:.0f} s, apply+save {time.time() - t1:.0f} s, committed={ok}")
    open(report_path, "w", encoding="utf-8").write("\n".join(report) + "\n")
    print("\n".join(report[:60]))
    print(f"report: {report_path}")


def robocopy(src, dst):
    import subprocess
    rc = subprocess.run(["robocopy", src, dst, "/MIR", "/XF", "*.lock", "*.lock~", "/NFL", "/NDL", "/NJH", "/NJS",
                         "/NP", "/R:2", "/W:2"]).returncode
    if rc >= 8:
        raise SystemExit(f"robocopy {src} -> {dst} failed with code {rc}")


def apply(prog, ver, data, pdb_path):
    from java.io import File
    from ghidra.app.util.bin.format.pdb2.pdbreader import PdbParser, PdbReaderOptions
    from ghidra.app.util.importer import MessageLog
    from ghidra.app.util.pdb.pdbapplicator import (DefaultPdbApplicator, ObjectOrientedClassLayout,
                                                   PdbApplicatorControl, PdbApplicatorOptions)
    from ghidra.program.model.data import (CategoryPath, DataTypeConflictHandler, PointerDataType, Structure,
                                           StructureDataType)
    from ghidra.util.task import TaskMonitor
    mon = TaskMonitor.DUMMY
    dtm = prog.getDataTypeManager()
    report = []
    T = time.time

    # 1. PDB -> /NG
    t = T()
    root = dtm.getRootCategory()
    for old in (ROOT, os.path.basename(pdb_path)):
        if root.getCategory(old) is not None:
            raise SystemExit(f"/{old} already exists: rerun with --restore (Ghidra cannot remove it once used)")
    aopts = PdbApplicatorOptions()
    aopts.setProcessingControl(PdbApplicatorControl.DATA_TYPES_ONLY)
    aopts.setCompositeLayout(ObjectOrientedClassLayout.CLASS_HIERARCHY)
    log = MessageLog()
    pdb = PdbParser.parse(File(pdb_path), PdbReaderOptions(), mon)
    try:
        pdb.deserialize()
        DefaultPdbApplicator(pdb, prog, dtm, prog.getImageBase(), aopts, mon, log).applyNoAnalysisState()
    finally:
        pdb.close()
    root.getCategory(os.path.basename(pdb_path)).setName(ROOT)
    prog.setPreferredRootNamespaceCategoryPath(f"/{ROOT}/RE")
    structs = {}
    n_fields = 0
    for d in dtm.getAllStructures():
        cp = str(d.getCategoryPath())
        if cp == f"/{ROOT}" or cp.startswith(f"/{ROOT}/"):
            parts = [p for p in cp.split("/")[2:] if p]
            structs[key("::".join(parts + [d.getName()]))] = d
            n_fields += d.getNumDefinedComponents()
    report.append(f"1. PDB: {len(structs)} structures, {n_fields} fields in /{ROOT} ({T() - t:.0f} s); "
                  f"loader messages: {len(str(log.toString()).splitlines())}")

    # 2. base names, runtime-data padding
    t = T()
    renamed = 0
    for s in structs.values():
        for c in s.getDefinedComponents():
            if c.getFieldName() is None and c.getComment() == "Base" and isinstance(c.getDataType(), Structure):
                c.setFieldName("super_" + ident(c.getDataType().getName()))
                renamed += 1
    overlays, pads = [], []
    for s in structs.values():
        for c in list(s.getDefinedComponents()):
            fn = c.getFieldName() or ""
            m = re.match(r"^_pad(\w*?)RuntimeData$", fn)
            if not m:
                continue
            cat = dtm.getCategory(s.getCategoryPath().extend(s.getName()))
            want = m.group(1).upper()
            cands = [d for d in (cat.getDataTypes() if cat else []) if d.getName().endswith("RUNTIME_DATA")
                     and want in d.getName()]
            fit = [d for d in cands if d.getLength() == c.getLength()]
            if fit:
                s.replaceAtOffset(c.getOffset(), fit[0], c.getLength(), "runtime", f"[clr] NG {fn} at "
                                  f"0x{c.getOffset():X} = {fit[0].getName()} ({ver['name']})")
                overlays.append(f"{s.getName()}+0x{c.getOffset():X} {fn} -> {fit[0].getName()}")
            else:
                pads.append(f"{s.getName()}+0x{c.getOffset():X} {fn} 0x{c.getLength():X}: candidates "
                            + ", ".join(f"{d.getName()} 0x{d.getLength():X}" for d in cands))
    report.append(f"2. super_* names: {renamed}; runtime-data overlays: {overlays}; left as padding: {pads} "
                  f"({T() - t:.0f} s)")

    # 3. vftables + flattening of the primary-base chain
    t = T()
    vcat = CategoryPath(f"/{ROOT}/vftables")
    vt_of = {}
    n_slots = 0
    for cname, c in data["classes"].items():
        prim = [v for v in c["vftables"] if v["offset"] == 0]
        s = structs.get(key(cname))
        if not prim or s is None or not cname.startswith("RE::"):
            continue
        short = ident(cname[4:].replace("::", "_"))
        vt = StructureDataType(vcat, short + "_vftable", 0, dtm)
        used = set()
        for i, slot in enumerate(prim[0]["slots"]):
            nm = slot.replace("RE::", "")
            fld = ident(nm.rsplit("::", 1)[-1].replace("{dtor}", "dtor")) or f"slot_{i:X}"
            if fld in used:
                fld = f"{fld}_{i:X}"
            used.add(fld)
            vt.add(PointerDataType(), 8, fld, f"slot 0x{i:X}: {nm}")
        vt_of[cname] = dtm.addDataType(vt, DataTypeConflictHandler.REPLACE_HANDLER)
        n_slots += len(prim[0]["slots"])
    flattened, failed = 0, []

    def expand(st, off):
        for c in st.getDefinedComponents():
            dt = c.getDataType()
            if c.getOffset() == 0 and c.getComment() == "Base" and isinstance(dt, Structure):
                yield from expand(dt, off)
            else:
                yield off + c.getOffset(), c

    for cname, vt in vt_of.items():
        s = structs[key(cname)]
        comps = list(s.getDefinedComponents())
        if not comps or comps[0].getOffset() != 0:
            continue
        first = comps[0]
        try:
            if s.isPackingEnabled():  # PDB structures are packed; keep offsets explicit before editing
                s.setPackingEnabled(False)
            if first.getComment() == "Base" and isinstance(first.getDataType(), Structure):
                items = list(expand(first.getDataType(), 0))
                length = s.getLength()
                names = {c.getFieldName() for c in comps[1:]}
                s.clearAtOffset(0)
                for off, c in items:
                    nm = c.getFieldName()
                    if nm in names:
                        nm = f"{nm}_{off:X}"
                    names.add(nm)
                    if c.isBitFieldComponent():
                        bf = c.getDataType()
                        s.insertBitFieldAt(off, c.getLength(), bf.getBitOffset(), bf.getBaseDataType(),
                                           bf.getDeclaredBitSize(), nm, c.getComment())
                    elif c.getLength() > 0:
                        s.replaceAtOffset(off, c.getDataType(), c.getLength(), nm, c.getComment())
                if s.getLength() != length:
                    failed.append(f"{cname}: size 0x{length:X} -> 0x{s.getLength():X}")
                flattened += 1
            c0 = s.getComponentAt(0)
            if c0 is not None and c0.getLength() == 8 and (c0.getFieldName() or "").startswith("vfptr"):
                s.replaceAtOffset(0, PointerDataType(vt, dtm), 8, "vfptr", f"{cname[4:]} primary vftable")
            else:
                failed.append(f"{cname}: no vfptr at 0 ({c0.getFieldName() if c0 else None})")
        except Exception as e:  # noqa: BLE001
            failed.append(f"{cname}: {type(e).__name__}: {e}")
    report.append(f"3. vftables: {len(vt_of)} structures, {n_slots} slots; flattened {flattened} classes; "
                  f"problems {len(failed)}: {failed[:15]} ({T() - t:.0f} s)")

    # 4. this
    t = T()
    from ghidra.program.model.listing import GhidraClass, VariableUtilities
    from ghidra.program.model.symbol import SourceType
    fm, st, listing = prog.getFunctionManager(), prog.getSymbolTable(), prog.getListing()
    virtual = set()
    for sym in st.getSymbols("vftable"):
        dat = listing.getDataAt(sym.getAddress())
        n = dat.getNumComponents() if dat is not None and dat.isArray() else 0
        for i in range(n):
            v = dat.getComponent(i).getValue()
            if v is not None:
                virtual.add(v.getOffset())
    statics = set(data["statics"])
    try:
        from ghidra.program.model.listing import CommentType
        plate_type = CommentType.PLATE
    except ImportError:
        from ghidra.program.model.listing import CodeUnit
        plate_type = CodeUnit.PLATE_COMMENT
    counts = collections.Counter()
    for f in fm.getFunctions(True):
        ns = f.getParentNamespace()
        if not isinstance(ns, GhidraClass):
            continue
        is_virtual = f.getEntryPoint().getOffset() in virtual
        src = f.getSymbol().getSource()
        qual = f"{ns.getName(True)}::{f.getName()}"
        if not is_virtual and (src not in (SourceType.IMPORTED, SourceType.USER_DEFINED) or qual in statics):
            counts["skipped: static or analysis name"] += 1
            continue
        if not is_virtual:
            # only NG/curated names are known to be non-static (third-party names may be static methods)
            plate = str(listing.getComment(plate_type, f.getEntryPoint()) or "")
            if "[clr] ng:" not in plate and "[clr] curated:" not in plate and src != SourceType.USER_DEFINED:
                counts["skipped: name not from NG (static unknown)"] += 1
                continue
        cs = VariableUtilities.findExistingClassStruct(ns, dtm)
        if cs is None or not str(cs.getCategoryPath()).startswith(f"/{ROOT}/RE"):
            counts["skipped: no NG structure for the class"] += 1
            continue
        if f.getCallingConventionName() == "__thiscall":
            counts["already __thiscall"] += 1
            continue
        try:
            f.setCallingConvention("__thiscall")
            counts["virtual" if is_virtual else "NG name"] += 1
        except Exception:  # noqa: BLE001
            counts["error"] += 1
    report.append(f"4. this: {dict(counts)} ({T() - t:.0f} s)")

    # 5. singletons
    t = T()
    from ghidra.program.model.data import DataUtilities
    _, id2rva = addrlib.load_addrlib(ver["addrlib"])
    done, missing = [], []
    for sg in data["singletons"]:
        if sg["cls"] in SINGLETON_DEPTH_FIX:
            sg = dict(sg, depth=SINGLETON_DEPTH_FIX[sg["cls"]])
        rva = id2rva.get(sg["id"])
        cands = [key(f"RE::{sg['cls']}::{sg['type']}"), key(f"RE::{sg['type']}")]
        if sg["cls"].rsplit("::", 1)[-1] == sg["type"]:
            cands.insert(0, key(f"RE::{sg['cls']}"))
        s = next((structs[k] for k in cands if k in structs), None)
        if rva is None or s is None:
            missing.append(f"{sg['cls']} ({sg['type']}, AE#{sg['id']})")
            continue
        dt = s
        for _ in range(sg["depth"] - 1):
            dt = PointerDataType(dt, dtm)
        addr = prog.getImageBase().add(rva)
        try:
            mode = DataUtilities.ClearDataMode.CLEAR_ALL_CONFLICT_DATA if sg["depth"] > 1 else                 DataUtilities.ClearDataMode.CLEAR_ALL_UNDEFINED_CONFLICT_DATA  # an object: only undefined bytes
            DataUtilities.createData(prog, addr, dt, -1, False, mode)
            done.append(f"{sg['cls']}={'*' * (sg['depth'] - 1)}{s.getName()}")
        except Exception as e:  # noqa: BLE001
            missing.append(f"{sg['cls']}: {e}")
    report.append(f"5. singletons typed: {len(done)}; not typed: {missing} ({T() - t:.0f} s)")
    report.append("   " + ", ".join(done))

    # 6. sizes: Ghidra structures vs MSVC's layout of the same build (its STATIC_ASSERT_SIZE all held)
    diff, checked, grown = [], 0, 0
    for cname, c in data["classes"].items():
        s = structs.get(key(cname))
        if s is None or not cname.startswith("RE::"):
            continue
        checked += 1
        if not s.isPackingEnabled() and 1 < s.getLength() < c["size"]:
            s.growStructure(c["size"] - s.getLength())  # tail padding lost when packing was turned off
            grown += 1
        if s.getLength() != c["size"] and not (c["size"] == 1 and s.getLength() <= 1):
            diff.append(f"{cname[4:]}: MSVC 0x{c['size']:X}, Ghidra 0x{s.getLength():X}")
    report.append(f"6. sizes: {checked} RE:: structures compared with MSVC layouts, {len(diff)} differ: {diff[:20]}; tail padding restored: {grown}; "
                  f"NG asserts compiled for this runtime: {data['asserted']}")
    return report


if __name__ == "__main__":
    main()
