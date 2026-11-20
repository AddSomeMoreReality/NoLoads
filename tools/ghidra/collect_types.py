#!/usr/bin/env python3
"""Curator: NG types for the Ghidra base -> PDB + JSON for apply_types.py. Standard library + MSVC.

CommonLibSSE-NG headers are compiled for ONE runtime only (ENABLE_SKYRIM_AE for 1.6.x, ENABLE_SKYRIM_SE for
1.5.97): then NG lays every class out exactly for that runtime (TES = 0x2C0 on AE, with the fields that the
flat SE+AE build hides behind GetRuntimeData()), and every STATIC_ASSERT_SIZE checks that runtime's size.
The compiler is the parser: no C++ is parsed here.

  pass 1  tu_layout.cpp (headers only) + /d1reportAllClassLayout -> layout.txt: sizes, bases, vftables with
          method names (MSVC's own report);
  pass 2  tu_types.cpp (a dllexport function taking a pointer to every RE:: class of pass 1) -> NG.dll + NG.pdb:
          full field types, read by Ghidra's PDB loader in "data types only" mode (apply_types.py).

Output: STATE_DIR/types/<version>/ (layout.txt, NG.pdb, types.json). Flags (include paths, defines) come from the
main checkout's xmake build of commonlibsse-ng (build/.deps/.../*.obj.d): build the plugin once first.

  python tools/ghidra/collect_types.py [--version 1.6.1170] [--vcvars <vcvars64.bat>]
"""
import argparse
import collections
import json
import os
import pathlib
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gconfig  # noqa: E402

RUNTIME_DEFINES = {"ae": ["ENABLE_SKYRIM_AE=1"], "se": ["ENABLE_SKYRIM_SE=1"]}
DROP_DEFINES = ("ENABLE_SKYRIM_", "HAS_SKYRIM_MULTI_TARGETING", "SKSE_SUPPORT_PATCH_SAFETY")


def ng_flags(main):
    """-D and include flags of an NG source file from the xmake dependency cache of the main checkout."""
    deps = sorted((main / "build" / ".deps" / "commonlibsse-ng").rglob("Actor.cpp.obj.d"))
    if not deps:
        raise SystemExit(f"no xmake build of commonlibsse-ng under {main / 'build'}; build the plugin once")
    text = deps[0].read_text(encoding="utf-8", errors="replace")
    values = text[text.index("values"):text.index("files =")]
    flags = re.findall(r'"((?:[^"\\]|\\.)*)"|\[\[(.*?)\]\]', values)
    out = []
    for q, b in flags:
        f = (q or b).replace('\\"', '"')
        if f.startswith(("-D", "/D")) and not f[2:].startswith(DROP_DEFINES):
            out.append("/D" + f[2:])
        elif f.startswith(("-I", "/I")):
            p = pathlib.Path(f[2:])
            out.append("/I" + str(p if p.is_absolute() else main / p))
        elif f.startswith(("-external:I", "/external:I")):
            out.append("/I" + f.split(":I", 1)[1])
        elif f.startswith(("-std:", "/std:")):
            out.append("/" + f[1:])
    return out


def run_cl(vcvars, workdir, args, log):
    """Run cl.exe inside the VS environment; stdout+stderr -> log file. Returns the exit code."""
    bat = workdir / "run.bat"
    q = lambda a: f'"{a}"' if (" " in a or '"' in a) and not a.startswith('"') else a  # noqa: E731
    bat.write_text("@echo off\r\n"
                   f'call "{vcvars}" >nul 2>nul\r\n'
                   f'cd /d "{workdir}"\r\n'
                   "cl " + " ".join(q(a) for a in args) + f' > "{log}" 2>&1\r\n'
                   "exit /b %ERRORLEVEL%\r\n", encoding="mbcs")
    return subprocess.run(["cmd", "/c", str(bat)]).returncode


BASE_ARGS = ["/nologo", "/EHsc", "/permissive-", "/bigobj", "/utf-8", "/Zc:preprocessor", "/Zc:enumTypes",
             "/Zc:templateScope", "/W0", "/Od", "/MD", "/DNDEBUG"]
HEAD = '#include "SKSE/Impl/PCH.h"\n#include "RE/Skyrim.h"\n'


def parse_layout(path):
    """MSVC /d1reportAllClassLayout -> {class: {size, bases, vftables}}."""
    classes = {}
    cur = None
    vt = None
    for line in open(path, encoding="mbcs", errors="replace"):
        line = line.rstrip("\r\n")
        m = re.match(r"^(?:class|struct|union) (.+?)\tsize\((\d+)\):$", line)
        if m:
            cur = classes.setdefault(m.group(1), dict(size=int(m.group(2)), bases=[], vftables=[]))
            vt = None
            continue
        m = re.match(r"^(.+)::\$vftable@(.*):$", line)
        if m:
            owner = classes.get(m.group(1))
            vt = None
            if owner is not None:
                vt = dict(for_=m.group(2), offset=None, slots=[])
                owner["vftables"].append(vt)
            cur = None
            continue
        if vt is not None:
            m = re.match(r"^\s*\|\s*(-?\d+)\s*$", line)
            if m and vt["offset"] is None:
                vt["offset"] = -int(m.group(1)) if int(m.group(1)) <= 0 else int(m.group(1))
                continue
            m = re.match(r"^\s*(\d+)\s*\|\s*&(.*?)\s*$", line)
            if m:
                name = m.group(2)
                t = re.match(r"thunk: this-=\d+; goto (.*)", name)
                vt["slots"].append(t.group(1) if t else name)
                continue
            if not line.strip():
                vt = None
            continue
        if cur is not None:
            m = re.match(r"^\s*(\d+)\s*\| \+--- \(base class (.+)\)$", line)
            if m:
                cur["bases"].append((int(m.group(1)), m.group(2), line.index("+---")))
    for c in classes.values():  # direct bases only: the shallowest "+---" indentation
        if c["bases"]:
            top = min(ind for _, _, ind in c["bases"])
            c["bases"] = [(off, b) for off, b, ind in c["bases"] if ind == top]
        for v in c["vftables"]:
            v["offset"] = v["offset"] or 0
    return classes


def exportable(name):
    """Non-template RE:: classes that a free function can name (no lambdas, anonymous or private-looking)."""
    return (name.startswith("RE::") and "<" not in name and "`" not in name and "$" not in name
            and " " not in name)


def ng_methods(runtime):
    """From the NG headers (collect_names' parser): static member functions per class, and the singletons
    (data relocations inside X::GetSingleton) with how the address holds the object."""
    import collect_names as cn
    ng = gconfig.ng_dir()
    statics = set()
    parser = cn.NGParser(runtime)
    for d in ("include", "src"):
        for f in sorted((ng / d).rglob("*")):
            if f.suffix not in (".h", ".cpp"):
                continue
            parser.parse_file(f, f.relative_to(ng))
            if f.suffix != ".h":
                continue
            code, _ = cn.strip_comments("\n".join(cn.preprocess(f.read_text(encoding="utf-8",
                                                                              errors="replace").splitlines())))
            for m in re.finditer(r"\b(?:class|struct)\s+(?:\[\[[^\]]*\]\]\s*)?(\w+)\s*(?:final\s*)?(?::[^;{]*)?\{",
                                 code):
                depth, i, top = 1, m.end(), []
                while i < len(code) and depth:
                    ch = code[i]
                    if ch == "{":
                        depth += 1
                    elif ch == "}":
                        depth -= 1
                    elif depth == 1:
                        top.append(ch)
                    if depth > 1 and ch == "{" and top and top[-1] != ";":
                        top.append(";")  # an inline body ends the declaration
                    i += 1
                for sm in re.finditer(r"\bstatic\b[^;(){}]*?\b(~?\w+)\s*\(", "".join(top)):
                    statics.add(f"{m.group(1)}::{sm.group(1)}")
    singletons = []
    for r in parser.relocs:
        enc = r["enclosing"] or ""
        t = (r["type"] or "").strip()
        if not enc.endswith("::GetSingleton") or not t.endswith("*") or "(" in t:
            continue
        cls = enc[:-len("::GetSingleton")]
        depth = len(t) - len(t.rstrip("*"))
        base = t.rstrip("*").strip()
        nm = re.match(r"^NiPointer<(.+)>$", base)
        if nm:  # NiPointer<X> stored at the address == X* stored there
            base, depth = nm.group(1), depth + 1
        singletons.append(dict(id=r["id"], cls=cls, type=base, depth=depth, origin=r["origin"]))
    return sorted(statics), singletons


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default=None)
    ap.add_argument("--vcvars", default=gconfig.VCVARS)
    ap.add_argument("--out", default=None, help="default: STATE_DIR/types/<version>")
    a = ap.parse_args()
    ver = gconfig.version(a.version)
    out = pathlib.Path(a.out) if a.out else gconfig.STATE_DIR / "types" / ver["name"]
    out.mkdir(parents=True, exist_ok=True)
    flags = ng_flags(gconfig.main_checkout())
    defines = ["/D" + d for d in RUNTIME_DEFINES[ver["runtime"]]]
    t0 = time.time()

    # pass 1: layouts
    (out / "tu_layout.cpp").write_text(HEAD, encoding="utf-8")
    rc = run_cl(a.vcvars, out, BASE_ARGS + flags + defines + ["/c", "/d1reportAllClassLayout", "tu_layout.cpp"],
                out / "layout.txt")
    if rc:
        raise SystemExit(f"pass 1 failed ({rc}); see {out / 'layout.txt'} (search 'error')")
    classes = parse_layout(out / "layout.txt")
    t1 = time.time()
    print(f"pass 1: {len(classes)} class layouts in {t1 - t0:.0f} s")

    # pass 2: PDB with every RE:: class
    names = sorted(n for n in classes if exportable(n))
    body = [HEAD]
    for i, n in enumerate(names):
        body.append(f'extern "C" __declspec(dllexport) const void* clr_t{i}(const {n}* p) {{ return p; }}')
    (out / "tu_types.cpp").write_text("\n".join(body) + "\n", encoding="utf-8")
    pdb_args = BASE_ARGS + flags + defines + ["/LD", "/Zi", "tu_types.cpp", "/FeNG.dll", "/FdNG_compile.pdb",
                                              "/link", "/DEBUG:FULL", "/NOENTRY", "/PDB:NG.pdb",
                                              "/FORCE:UNRESOLVED", "/IGNORE:4006,4088"]
    rc = run_cl(a.vcvars, out, pdb_args, out / "pdb.txt")
    log = (out / "pdb.txt").read_text(encoding="mbcs", errors="replace")
    errors = [ln for ln in log.splitlines() if re.search(r"\berror C\d+", ln)]
    if errors:
        # names the compiler refused (private nested types and the like): drop them and retry once
        bad = set()
        for ln in errors:
            for m in re.finditer(r"clr_t(\d+)", ln):
                bad.add(int(m.group(1)))
            m = re.search(r"tu_types\.cpp\((\d+)\)", ln)
            if m:
                bad.add(int(m.group(1)) - 4)  # HEAD has 2 lines + 1 for the join, lines are 1-based
        print(f"pass 2: {len(errors)} compile errors, dropping {len(bad)} classes and retrying")
        names = [n for i, n in enumerate(names) if i not in bad]
        body = [HEAD] + [f'extern "C" __declspec(dllexport) const void* clr_t{i}(const {n}* p) {{ return p; }}'
                         for i, n in enumerate(names)]
        (out / "tu_types.cpp").write_text("\n".join(body) + "\n", encoding="utf-8")
        rc = run_cl(a.vcvars, out, pdb_args, out / "pdb.txt")
    if not (out / "NG.pdb").exists():
        raise SystemExit(f"pass 2 failed ({rc}); see {out / 'pdb.txt'}")
    t2 = time.time()
    print(f"pass 2: NG.pdb {(out / 'NG.pdb').stat().st_size / 2**20:.1f} MB with {len(names)} RE:: classes "
          f"in {t2 - t1:.0f} s (link exit {rc}; unresolved externals are expected)")

    # sizes asserted in NG (compiled: they all hold for this runtime)
    ng = gconfig.ng_dir()
    asserted = collections.Counter()
    for p in (ng / "include" / "RE").rglob("*.h"):
        t = p.read_text(encoding="utf-8", errors="replace")
        asserted["STATIC_ASSERT_SIZE"] += len(re.findall(r"^\s*STATIC_ASSERT_SIZE\(", t, re.M))
        asserted["static_assert(sizeof"] += len(re.findall(r"static_assert\(sizeof\(", t))
    statics, singletons = ng_methods(ver["runtime"])
    print(f"NG headers: {len(statics)} static member functions, {len(singletons)} singletons")
    data = dict(version=ver["name"], runtime=ver["runtime"], defines=RUNTIME_DEFINES[ver["runtime"]],
                pdb=str(out / "NG.pdb"), asserted=asserted, exported=len(names),
                seconds=round(time.time() - t0), statics=statics, singletons=singletons, classes=classes)
    (out / "types.json").write_text(json.dumps(data, indent=0), encoding="utf-8")
    print(f"types.json: {len(classes)} classes, "
          f"{sum(len(c['vftables']) for c in classes.values())} vftables; asserts in NG headers: {dict(asserted)}")


if __name__ == "__main__":
    main()
