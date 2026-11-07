"""Third-party ID -> name tables for collect_names.py (T34). Standard library + git.

Sources (only names and IDs are taken, as facts; no code is copied; data stay in STATE_DIR/thirdparty, not in git):
  addrlibdb  meh321/AddressLibraryDatabase, skyrimae.rename: community names of Address Library IDs (AE), from IDA
             databases. No license file; the README describes it as a shared name database for exactly this use.
  po3        powerof3/CommonLibSSE, branch dev-1.6.1170 (MIT): RELOCATION_ID and REL::ID(n) (AE IDs in that branch),
             read with collect_names' NG parser.
  tilted     tiltedphoques/TiltedEvolution (GPL-3.0): POINTER_SKYRIMSE(type, name, AE id); names only as comments
             and votes.
Priority (collect_names.put): NG, curated, Papyrus and commands win; po3 = NG level; addrlibdb names above NG Unk_
and RTTI slot names; addrlibdb "Class::sub" (method unknown) gives Class::AE_<id> only to functions nobody named.
Every third-party name that differs from the chosen one goes into the plate comment.
"""
import collections
import pathlib
import re
import subprocess

import gconfig

SOURCES = {
    "addrlibdb": dict(url="https://github.com/meh321/AddressLibraryDatabase", branch=None, dir="addrlibdb",
                      license="no license file; README: shared name database for Address Library IDs"),
    "po3": dict(url="https://github.com/powerof3/CommonLibSSE", branch="dev-1.6.1170", dir="po3-1170",
                license="MIT"),
    "tilted": dict(url="https://github.com/tiltedphoques/TiltedEvolution", branch=None, dir="tilted",
                   license="GPL-3.0 (names and IDs only)", filter="blob:limit=2m"),
}


def root():
    return gconfig.STATE_DIR / "thirdparty"


def fetch(update=False):
    """Shallow clones in STATE_DIR/thirdparty; {source: commit}."""
    root().mkdir(parents=True, exist_ok=True)
    commits = {}
    for name, s in SOURCES.items():
        d = root() / s["dir"]
        if not d.exists():
            cmd = ["git", "clone", "-q", "--depth", "1"] + (["-b", s["branch"]] if s["branch"] else []) + \
                (["--filter=" + s["filter"]] if s.get("filter") else []) + [s["url"], str(d)]
            subprocess.run(cmd, check=True)
        elif update:
            subprocess.run(["git", "-C", str(d), "fetch", "-q", "--depth", "1", "origin", s["branch"] or "HEAD"],
                           check=True)
            subprocess.run(["git", "-C", str(d), "reset", "-q", "--hard", "FETCH_HEAD"], check=True)
        commits[name] = subprocess.run(["git", "-C", str(d), "rev-parse", "--short", "HEAD"],
                                       capture_output=True, text=True).stdout.strip()
    return commits


IDA_AUTO = re.compile(r"^(j_)*(nullsub|sub|loc|locret|off|unk|byte|word|dword|qword|xmmword|stru|asc|flt|dbl|"
                      r"funcs|jpt|def)_[0-9A-Fa-f]+(_\d+)?$")


def addrlibdb():
    """{AE id: (name, class_only)} from skyrimae.rename ('<id> <name>_*'; _gs_/_ge_ encode < and >)."""
    out = {}
    path = root() / SOURCES["addrlibdb"]["dir"] / "skyrimae.rename"
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        parts = line.split(" ", 1)
        if len(parts) != 2 or not parts[0].isdigit():
            continue
        name = re.sub(r"_\*$", "", parts[1].strip())
        name = name.replace("_gs_", "<").replace("_ge_", ">")
        if not name:
            continue
        cls, _, last = name.rpartition("::")
        auto = IDA_AUTO.match(last) is not None  # IDA's placeholders (nullsub_12, j_sub_..., qword_...)
        if auto and not cls:
            continue
        if cls and last == "dtor":  # the database writes destructors as dtor
            name = f"{cls}::~{cls.rsplit('::', 1)[-1]}"
        # sub / FuncN (a virtual slot without a name) / IDA placeholder: only the class is known
        out[int(parts[0])] = (name, bool(cls) and (auto or re.fullmatch(r"sub|Func\d+", last) is not None))
    return out


def po3(pe):
    """{AE id: (name, kind)} from the po3 fork, named like ng-reloc."""
    import collect_names as cn
    base = root() / SOURCES["po3"]["dir"]
    parser = cn.NGParser("ae", single_id=True)
    for d in ("include", "src"):
        for f in sorted((base / d).rglob("*")):
            if f.suffix in (".h", ".cpp"):
                parser.parse_file(f, f.relative_to(base))
    by_id = collections.defaultdict(list)
    for r in parser.relocs:
        by_id[r["id"]].append(r)
    out = {}
    for id_, rs in by_id.items():
        rs.sort(key=lambda r: (r["how"] != "decltype", r["origin"]))
        r = rs[0]
        if not r["name"]:
            continue
        func_like = r["var"] in (None, "func") or "func" in (r["type"] or "") or "(" in (r["type"] or "")
        if func_like or r["how"] == "decltype":
            out[id_] = (r["name"], "func")
        else:
            cls, _, meth = (r["enclosing"] or "").rpartition("::")
            var = r["var"] or "data"
            name = r["name"] if r["enclosing"] is None else \
                (f"{cls}::{var}" if cls and (meth == "GetSingleton" or var == meth) else
                 f"{cls + '::' if cls else ''}{meth}_{var}")
            out[id_] = (name, "data")
    return out, sum(len(v) for v in by_id.values())


TILTED = re.compile(r"POINTER_SKYRIMSE\(\s*([^,]+?)\s*,\s*(\w+)\s*,\s*(\d+)\s*\)")


def tilted():
    """{AE id: 'File::variable'}."""
    out = {}
    base = root() / SOURCES["tilted"]["dir"] / "Code"
    for f in base.rglob("*"):
        if f.suffix not in (".cpp", ".h", ".hpp"):
            continue
        for m in TILTED.finditer(f.read_text(encoding="utf-8", errors="replace")):
            out.setdefault(int(m.group(3)), f"{f.stem}::{m.group(2)}")
    return out


def method_key(name):
    m = name.rsplit("::", 1)[-1]
    if m.startswith("~"):
        return "dtor"
    m = re.sub(r"[^0-9a-z]", "", m.lower())
    return "ctor" if m in ("ctor", "constructor") else m


def collect(ver, id2rva, pe, put, entries, update=False):
    """Put third-party names; return (report lines, {rva: [(source, name)]}) for comments."""
    if ver["runtime"] != "ae":
        return [f"third-party tables: none for runtime {ver['runtime']}"], {}
    commits = fetch(update)
    db = addrlibdb()
    p3, p3_relocs = po3(pe)
    tl = tilted()
    lines = [f"third-party sources: " + ", ".join(f"{k}@{v} ({SOURCES[k]['license']})" for k, v in commits.items())]
    cands = collections.defaultdict(list)
    for id_, (name, kind) in p3.items():
        if id_ in id2rva:
            cands[id2rva[id_]].append(("po3", name, kind))
    for id_, (name, class_only) in db.items():
        if id_ in id2rva:
            cands[id2rva[id_]].append(("addrlibdb-class" if class_only else "addrlibdb", name, None))
    for id_, name in tl.items():
        if id_ in id2rva:
            cands[id2rva[id_]].append(("tilted", name, None))
    # agreement with names we already trust (NG, curated)
    agree = collections.Counter()
    for rva, cs in cands.items():
        cur = entries.get(rva)
        if cur is None or cur["source"] not in ("ng-reloc", "ng-vfunc", "curated") or cur["name"].split("::")[-1].startswith("Unk_"):
            continue
        for src, name, _ in cs:
            if src == "addrlibdb-class":
                continue
            agree[(src, method_key(name) == method_key(cur["name"]))] += 1
    rid = {r: i for i, r in id2rva.items()}
    counts = collections.Counter()
    for rva, cs in cands.items():
        by = {s: n for s, n, _ in cs}
        p3k, dbk = by.get("po3"), by.get("addrlibdb")
        confirmed = p3k and dbk and method_key(p3k) == method_key(dbk)
        for src, name, kind in cs:
            code = pe.is_code(rva)
            k = kind or ("func" if code else "data")
            if src == "po3":
                put(rva, name, "po3", k, f"po3 CommonLibSSE{' (= addrlibdb)' if confirmed else ''}", [], 2)
                counts["po3"] += 1
            elif src == "addrlibdb" and re.fullmatch(r"[\w:<>,~*&\- ]+", name):
                put(rva, name, "addrlibdb", k, f"AddressLibraryDatabase{' (= po3)' if confirmed else ''}", [], 1.5)
                counts["addrlibdb"] += 1
            elif src == "addrlibdb-class" and code:
                cls = name.rsplit("::", 1)[0]
                put(rva, f"{cls}::{ver['prefix']}_{rid[rva]}", "addrlibdb-class", "func",
                    "AddressLibraryDatabase (class only)", [], -1)
                counts["addrlibdb class only"] += 1
        counts["confirmed by po3 and addrlibdb"] += bool(confirmed)
    lines.append(f"third-party candidates: addrlibdb {len(db)} IDs ({sum(1 for v in db.values() if v[1])} class only), "
                 f"po3 {len(p3)} IDs from {p3_relocs} relocations, tilted {len(tl)}; at addresses {len(cands)}; "
                 f"{dict(counts)}")
    lines.append("agreement of method names with NG/curated where both exist: " + ", ".join(
        f"{s} {agree[(s, True)]}/{agree[(s, True)] + agree[(s, False)]}" for s in ("po3", "addrlibdb", "tilted")))
    return lines, {rva: [(s, n) for s, n, _ in cs] for rva, cs in cands.items()}
