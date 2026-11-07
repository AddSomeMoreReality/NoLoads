#!/usr/bin/env python3
"""Collect names for SkyrimSE.exe addresses -> JSON for apply_names.py. Standard library only.

Sources, by priority:
  ng-reloc   RELOCATION_ID / REL::RelocationID / REL::VariantID inside NG functions -> Class::Method
             (decltype(&X::Y) of the relocation's func_t wins over the enclosing function's name);
  ng-vfunc   virtual methods of NG headers ("// 98" slot comments) resolved through VTABLE_* of the exe;
  ng-table   VTABLE_*, RTTI_*, NiRTTI_* of Offsets_*.h (labels; Ghidra's own RTTI names are compared, not replaced);
  docs       `Class::Method` next to `AE#id` in docs/*.md and src/ comments -> comments only.
Every Address Library ID becomes a label <PREFIX>_<id> in apply_names.py; this script only resolves the rest.

Usage:
  python tools/ghidra/collect_names.py [--version 1.6.1170] [--out names.json] [--report report.md]
"""
import argparse
import collections
import json
import os
import pathlib
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import addrlib  # noqa: E402
import gconfig  # noqa: E402

# Flat (SE + AE, no VR) build of NG, as configured by xmake.lua of the project.
DEFINED = {"ENABLE_SKYRIM_SE", "ENABLE_SKYRIM_AE", "EXCLUSIVE_SKYRIM_FLAT", "HAS_SKYRIM_MULTI_TARGETING",
           "SKSE_SUPPORT_XBYAK", "__cpp_lib_format", "FMT_VERSION"}


# ----------------------------------------------------------------------------
# Lexing: preprocessor, comments, strings
# ----------------------------------------------------------------------------
def _cond(kw, expr):
    expr = re.sub(r"//.*|/\*.*?\*/", "", expr).strip()
    if kw == "ifdef":
        return bool(expr) and expr.split()[0] in DEFINED
    if kw == "ifndef":
        return not expr or expr.split()[0] not in DEFINED
    e = re.sub(r"defined\s*\(\s*(\w+)\s*\)|defined\s+(\w+)",
               lambda m: " 1 " if (m.group(1) or m.group(2)) in DEFINED else " 0 ", expr)
    e = e.replace("&&", " and ").replace("||", " or ")
    e = re.sub(r"!(?!=)", " not ", e)
    e = re.sub(r"\b[A-Za-z_]\w*\b", lambda m: m.group(0) if m.group(0) in ("and", "or", "not") else "0", e)
    try:
        return bool(eval(e, {"__builtins__": {}}))  # expression is built from 0/1/operators only
    except Exception:
        return False


def preprocess(lines):
    """Blank out inactive #if branches and all directive lines (with continuations)."""
    out, stack, active, cont = [], [], True, False
    for line in lines:
        s = line.strip()
        if cont:
            cont = s.endswith("\\")
            out.append("")
            continue
        if s.startswith("#"):
            cont = s.endswith("\\")
            m = re.match(r"#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", s)
            if m:
                kw, rest = m.groups()
                if kw in ("if", "ifdef", "ifndef"):
                    c = _cond(kw, rest)
                    stack.append([active, c])
                    active = active and c
                elif kw == "elif" and stack:
                    parent, taken = stack[-1]
                    c = not taken and _cond("if", rest)
                    stack[-1][1] = taken or c
                    active = parent and c
                elif kw == "else" and stack:
                    parent, taken = stack[-1]
                    stack[-1][1] = True
                    active = parent and not taken
                elif kw == "endif" and stack:
                    active = stack.pop()[0]
            out.append("")
            continue
        out.append(line if active else "")
    return out


_LEX = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\\n])*\"|(?<![0-9A-Za-z_])'(?:\\.|[^'\\\n]){1,4}'", re.S)


def strip_comments(text):
    """Return (code with comments and string contents blanked, {line: comment text})."""
    comments = collections.defaultdict(str)
    parts, last = [], 0
    for m in _LEX.finditer(text):
        tok = m.group(0)
        parts.append(text[last:m.start()])
        if tok.startswith("//") or tok.startswith("/*"):
            line = text.count("\n", 0, m.start())
            if tok.startswith("//"):
                comments[line] += tok[2:]
            parts.append(re.sub(r"[^\n]", " ", tok))
        else:
            parts.append(tok[0] + " " * (len(tok) - 2) + tok[-1])
        last = m.end()
    parts.append(text[last:])
    return "".join(parts), comments


# ----------------------------------------------------------------------------
# Scopes
# ----------------------------------------------------------------------------
_CONTROL = re.compile(r"^(if|for|while|switch|catch|else|do|try|return)\b|\b(else|do|try)$")
_ACCESS = re.compile(r"^(?:\s*(?:public|private|protected)\s*:)+")


def _top_level(s, opens="(<[", closes=")>]"):
    """Characters of s that are outside any brackets, others replaced by spaces."""
    depth, out = 0, []
    for ch in s:
        if ch in opens:
            depth += 1
            out.append(" ")
        elif ch in closes:
            depth = max(0, depth - 1)
            out.append(" ")
        else:
            out.append(ch if depth == 0 else " ")
    return "".join(out)


def clean_name(name):
    name = re.sub(r"\s+", "", name)
    name = re.sub(r"^(::)?RE::", "", name)
    return name


def classify(header):
    """Kind and name of the scope opened by '{' after header text."""
    h = _ACCESS.sub("", header).strip()
    if not h:
        return ("block", None)
    m = re.match(r"^(?:inline\s+)?namespace\b\s*([\w:]*)", h)
    if m and "(" not in h:
        return ("ns", m.group(1) or "")
    if re.search(r"\benum\b", h) or h.startswith("extern"):
        return ("block", None)
    flat = _top_level(h, "(", ")")
    if _CONTROL.search(h) or re.search(r"(?<!operator)\[[^\[\]]*\]\s*(\(|mutable|->|$)", h):
        return ("block", None)
    cls = None
    for m in re.finditer(r"\b(class|struct|union)\s+(?:\[\[[^\]]*\]\]\s*)?(?:alignas\s*\([^)]*\)\s*)?"
                         r"([A-Za-z_]\w*)\s*(?:final\s*)?(:(?!:)(.*))?$", h, re.S):
        cls = m
    if cls and "(" not in _top_level(h[:cls.start()], "<", ">"):
        bases = []
        if cls.group(4):
            for b in _split_top(cls.group(4)):
                b = re.sub(r"\b(public|private|protected|virtual)\b", "", b).strip()
                if b:
                    bases.append(clean_name(b))
        return ("class", cls.group(2), bases)
    if "(" not in flat and "(" not in h:
        return ("block", None)
    no_paren = _top_level(h, "(", ")")
    if "=" in no_paren.replace("==", "").replace("operator", "") and "operator" not in h:
        return ("block", None)
    m = re.search(r"((?:[A-Za-z_]\w*(?:<[^()]*?>)?\s*::\s*)*operator\s*(?:\(\)|\[\]|[^\s(]+))\s*\(", h)
    if not m:
        idx = h.find("(")
        m = re.search(r"((?:[A-Za-z_]\w*(?:<[^()]*?>)?\s*::\s*)*~?[A-Za-z_]\w*)\s*$", h[:idx])
    if not m:
        return ("block", None)
    name = m.group(1)
    if name in ("decltype", "sizeof", "alignof", "static_assert", "requires"):
        return ("block", None)
    return ("func", clean_name(name))


def _split_top(s):
    out, depth, cur = [], 0, []
    for ch in s:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    out.append("".join(cur))
    return out


def qualname(stack, own=None):
    """Namespace + class path (RE dropped) for the scope stack, plus own name."""
    parts = []
    for sc in stack:
        if sc[0] == "ns" and sc[1]:
            parts.extend(p for p in sc[1].split("::") if p)
        elif sc[0] == "class":
            parts.append(sc[1])
        elif sc[0] == "func":
            parts.append(sc[1])
    if parts and parts[0] == "RE":
        parts = parts[1:]
    if own:
        parts.append(own)
    return "::".join(parts)


# ----------------------------------------------------------------------------
# NG
# ----------------------------------------------------------------------------
_RELOC = re.compile(r"\b(RELOCATION_ID|REL::RelocationID|REL::VariantID|REL::ID)\s*\(")
_USING = re.compile(r"\busing\s+(\w+)\s*=\s*decltype\s*\(\s*&\s*([\w:~<>, \[\]]+?)\s*\)\s*;")
_DECL = re.compile(r"REL::Relocation\s*<(.*)>\s*(\w+)\s*$", re.S)
_TOKENS = re.compile(r"[{};]|" + _RELOC.pattern + r"|" + _USING.pattern)


def _args(code, pos):
    """Arguments of the call whose '(' is just before pos; returns (list of str, end index)."""
    depth, i = 1, pos
    while i < len(code) and depth:
        if code[i] == "(":
            depth += 1
        elif code[i] == ")":
            depth -= 1
        i += 1
    return [a.strip() for a in _split_top(code[pos:i - 1])], i


def _int(s):
    try:
        return int(s.replace("'", ""), 0)
    except ValueError:
        return None


class NGParser:
    def __init__(self, runtime):
        self.idx = 0 if runtime == "se" else 1
        self.relocs = []        # dict(id, name, kind_hint, var, origin, how)
        self.skipped = collections.Counter()
        self.class_bases = {}   # qualname -> [bases]
        self.class_vtable = {}  # qualname -> VTABLE_* symbol
        self.vdecls = []        # (class qualname, method, slot, section, origin)
        self.tables = {}        # symbol -> [variant ids]

    def parse_file(self, path, rel):
        raw = path.read_text(encoding="utf-8", errors="replace").splitlines()
        text = "\n".join(preprocess(raw))
        code, comments = strip_comments(text)
        if rel.name.startswith("Offsets_"):
            self._tables(code)
            return
        line_starts = [0]
        for m in re.finditer("\n", code):
            line_starts.append(m.end())
        stack, last, usings = [], 0, []
        section = {}
        self._last_header = ""
        for m in _TOKENS.finditer(code):
            tok = m.group(0)
            if tok == "{":
                self._last_header = code[last:m.start()]
                sc = classify(self._last_header)
                if sc[0] == "class":
                    q = qualname(stack, sc[1])
                    self.class_bases.setdefault(q, sc[2])
                stack.append(sc)
                last = m.end()
            elif tok == "}":
                if stack:
                    stack.pop()
                last = m.end()
            elif tok == ";":
                stmt = code[last:m.start()]
                if stack and stack[-1][0] == "class":
                    self._class_stmt(stack, stmt, code, last, line_starts, comments, section, rel)
                last = m.end()
            elif m.group(m.lastindex) and tok.startswith("using"):
                usings.append((len(stack), m.group(m.lastindex - 1), clean_name(m.group(m.lastindex))))
            else:
                self._reloc(m, code, stack, last, usings, line_starts, rel)
            # forget usings of closed scopes
            usings = [u for u in usings if u[0] <= len(stack)]

    def _class_stmt(self, stack, stmt, code, start, line_starts, comments, section, rel):
        cls = qualname(stack)
        m = re.search(r"\bVTABLE\s*=\s*(VTABLE_\w+)", stmt)
        if m:
            self.class_vtable[cls] = m.group(1)
        # section comments between previous statement and this one
        first = _line_of(line_starts, start)
        last = _line_of(line_starts, start + len(stmt))
        for ln in range(first, last + 1):
            c = comments.get(ln, "").strip()
            sm = re.match(r"(override|add)\b\s*(?:\((.*)\))?", c)
            if sm and not stmt[line_starts[ln] - start if ln < len(line_starts) else 0:].strip().startswith(c):
                section[cls] = (sm.group(1), clean_name(sm.group(2) or ""))
        if not re.search(r"\b(virtual|SKYRIM_REL_VR_VIRTUAL)\b|\boverride\b", stmt) or "(" not in stmt:
            return
        flat = re.sub(r"\s+", " ", stmt)
        mm = re.search(r"(operator\s*(?:\(\)|\[\]|[^\s(]+)|~?[A-Za-z_]\w*)\s*\(", flat)
        if not mm:
            return
        method = mm.group(1).replace(" ", "")
        c = comments.get(last, "")
        slot = _slot(c, self.idx)
        if slot is None:
            self.skipped["vfunc without slot comment"] += 1
            return
        self.vdecls.append((cls, method, slot, section.get(cls, ("add", "")), f"{rel}:{last + 1}"))

    def _reloc(self, m, code, stack, last, usings, line_starts, rel):
        kind = m.group(1)
        args, end = _args(code, m.end())
        line = _line_of(line_starts, m.start()) + 1
        origin = f"{rel}:{line}"
        if kind == "REL::ID":
            self.skipped["REL::ID without SE/AE pair"] += 1
            return
        ids = [_int(a) for a in args]
        if len(ids) < 2 or ids[self.idx] in (None, 0):
            self.skipped["no ID for this runtime"] += 1
            return
        id_ = ids[self.idx]
        # declaration: innermost init-block header, or current statement
        if stack and stack[-1][0] == "block":
            decl_text = self._last_header
        else:
            decl_text = code[last:m.start()]
        tail = code[end:end + 40]
        if re.match(r"\s*,", tail):
            self.skipped["ID + offset (not a name)"] += 1
            return
        dm = _DECL.search(decl_text.strip().rstrip("{(").strip())
        var = dm.group(2) if dm else None
        rtype = dm.group(1).strip() if dm else ""
        func_scope = [sc for sc in stack if sc[0] == "func"]
        enclosing = qualname([sc for sc in stack if sc[0] != "block"]) if func_scope else None
        how, name = "enclosing", enclosing
        for depth, alias, target in reversed(usings):
            if alias == rtype and target != var:
                if "::" not in target and enclosing:
                    # unqualified &Func inside namespace RE::X { ... } -> X::Func
                    parent = enclosing.rpartition("::")[0]
                    target = f"{parent}::{target}" if parent else target
                how, name = "decltype", target
                break
        if name is None:
            name = qualname(stack, var or "")
            how = "namespace"
        self.relocs.append(dict(id=id_, name=name, var=var, type=rtype, enclosing=enclosing,
                                how=how, origin=origin))

    def _tables(self, code):
        for m in re.finditer(r"\b((?:VTABLE|RTTI|NiRTTI)_\w+)\s*[\({]", code):
            sym = m.group(1)
            end = code.find(";", m.end())
            ids = []
            for v in re.finditer(r"VariantID\s*\(([^)]*)\)", code[m.start():end]):
                parts = [_int(a.strip()) for a in v.group(1).split(",")]
                ids.append(parts[self.idx] if len(parts) > self.idx else None)
            if not ids:
                vals = [_int(a.strip()) for a in code[m.end():end].strip(" ()}{").split(",")]
                ids = [vals[self.idx]] if len(vals) > self.idx else []
            self.tables[sym] = ids


def _line_of(starts, pos):
    lo, hi = 0, len(starts)
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if starts[mid] <= pos:
            lo = mid
        else:
            hi = mid
    return lo


def _slot(comment, idx):
    c = comment.strip()
    key = "AE" if idx == 1 else "SE"
    m = re.search(rf"\b{key}\s*(?:0x)?([0-9A-Fa-f]{{1,3}})\b", c)
    if not m:
        m = re.search(rf"\b(?:SE/AE|SE,\s*AE|{key}[/,][A-Z/,]*)\s*(?:0x)?([0-9A-Fa-f]{{1,3}})\b", c)
    if not m:
        m = re.match(r"(?:0x)?([0-9A-Fa-f]{2,3})\b", c)
    return int(m.group(1), 16) if m else None


# ----------------------------------------------------------------------------
# Docs
# ----------------------------------------------------------------------------
_NAME = r"(?:RE::)?([A-Za-z_]\w*(?:<[^`<>|]*>)?(?:::~?[A-Za-z_]\w*)+)(?:\(\))?"
_GAP = r"[\s(,:|—-]{0,6}(?:[a-z]\w*\s+)?"  # "(", " | ", " — ", or one lowercase word like "detour"
# `Name` (`AE#id`   |   `AE#id` (`Name`)   (md);   Name (AE#id   |   AE#id Name   (code comments)
_DOC_MD = [re.compile(r"`" + _NAME + r"`" + _GAP + r"`?AE#(\d+)\b(?![\(\w])"),
           re.compile(r"\bAE#(\d+)`?(?![\(\w])" + _GAP + r"`" + _NAME + r"`")]
_DOC_SRC = [re.compile(r"\b" + _NAME + r"\b" + _GAP + r"AE#(\d+)\b(?![\(\w])"),
            re.compile(r"\bAE#(\d+)(?![\(\w])[\s(,:—-]{0,4}\b" + _NAME + r"\b")]


def own_namespaces(src_dir):
    names = set()
    for p in pathlib.Path(src_dir).rglob("*"):
        if p.suffix in (".h", ".cpp"):
            t = p.read_text(encoding="utf-8", errors="replace")
            names.update(re.findall(r"\bnamespace\s+(\w+)", t))
            names.update(re.findall(r"^\s*(?:class|struct)\s+(\w+)", t, re.M))
    return names - {"RE", "REL", "SKSE", "std", "detail"}


def collect_docs(repo, ng_classes):
    own = own_namespaces(repo / "src") - ng_classes
    found = collections.defaultdict(lambda: collections.defaultdict(set))  # id -> name -> files
    files = sorted((repo / "docs").rglob("*.md")) + sorted(p for p in (repo / "src").rglob("*") if p.suffix in (".h", ".cpp"))
    for p in files:
        is_md = p.suffix == ".md"
        rel = p.relative_to(repo).as_posix()
        for line in p.read_text(encoding="utf-8", errors="replace").splitlines():
            if "AE#" not in line:
                continue
            if not is_md:
                ci = line.find("//")
                if ci < 0:
                    continue
                line = line[ci:]
            name_first, id_first = _DOC_MD if is_md else _DOC_SRC
            pairs = [(m.group(1), int(m.group(2))) for m in name_first.finditer(line)]
            pairs += [(m.group(2), int(m.group(1))) for m in id_first.finditer(line)]
            for name, id_ in pairs:
                if name.split("::")[0] in own or name.startswith(("std::", "REL::", "SKSE::")):
                    continue
                found[id_][name].add(rel)
    return found


# ----------------------------------------------------------------------------
# Resolution
# ----------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default=None, help="game build, a key of gconfig.VERSIONS")
    ap.add_argument("--ng", default=None, help="CommonLibSSE-NG checkout (default: main checkout lib/commonlibsse-ng)")
    ap.add_argument("--out", default=None, help="names JSON (default: state dir)")
    ap.add_argument("--report", default=None, help="text report (default: next to --out)")
    a = ap.parse_args()

    ver = gconfig.version(a.version)
    ng = pathlib.Path(a.ng) if a.ng else gconfig.ng_dir()
    out = pathlib.Path(a.out) if a.out else gconfig.STATE_DIR / f"names-{ver['name']}.json"
    report_path = pathlib.Path(a.report) if a.report else out.with_suffix(".report.txt")
    out.parent.mkdir(parents=True, exist_ok=True)

    _, id2rva = addrlib.load_addrlib(ver["addrlib"])
    pe = addrlib.PE(ver["exe"])
    rva2ids = collections.defaultdict(list)
    for i, r in id2rva.items():
        rva2ids[r].append(i)

    parser = NGParser(ver["runtime"])
    files = [p for d in ("include", "src") for p in (ng / d).rglob("*") if p.suffix in (".h", ".cpp")]
    for p in sorted(files):
        parser.parse_file(p, p.relative_to(ng))

    report = []
    entries = {}      # rva -> dict(name, source, ...)
    conflicts = []

    def put(rva, name, source, kind, origin, ids, prio):
        cur = entries.get(rva)
        if cur is None or prio > cur["prio"]:
            if cur is not None and cur["name"] != name:
                conflicts.append((rva, cur["name"], cur["source"], name, source, "replaced"))
            entries[rva] = dict(rva=rva, name=name, source=source, kind=kind, origin=origin, ids=ids, prio=prio)
        elif cur["name"] != name:
            conflicts.append((rva, cur["name"], cur["source"], name, source, "kept"))

    # ng-reloc
    by_id = collections.defaultdict(list)
    for r in parser.relocs:
        by_id[r["id"]].append(r)
    missing = 0
    reloc_counts = collections.Counter()
    for id_, rs in sorted(by_id.items()):
        rva = id2rva.get(id_)
        if rva is None:
            missing += 1
            continue
        code = pe.is_code(rva)
        rs.sort(key=lambda r: (r["how"] != "decltype", r["origin"]))
        r = rs[0]
        if code:
            func_like = r["var"] in (None, "func") or "func" in (r["type"] or "") or "(" in (r["type"] or "")
            name = r["name"] if func_like or r["how"] == "decltype" else f"{r['name']}_{r['var']}"
            kind = "func" if rva in pe.func_starts else "code"
        else:
            cls, _, meth = (r["enclosing"] or "").rpartition("::")
            var = r["var"] or "data"
            if r["enclosing"] is None:
                name = r["name"]
            elif meth == "GetSingleton" or var == meth:
                name = f"{cls}::{var}" if cls else var
            else:
                name = f"{cls + '::' if cls else ''}{meth}_{var}"
            kind = "data"
        for other in rs[1:]:
            if other["name"] != r["name"] and other["how"] == r["how"]:
                conflicts.append((rva, r["name"], "ng-reloc", other["name"], "ng-reloc", f"same ID {id_}"))
        reloc_counts[kind] += 1
        put(rva, name, "ng-reloc", kind, r["origin"], rva2ids[rva], 3 if r["how"] == "decltype" else 2)

    # ng-table
    table_counts = collections.Counter()
    vt_rva = {}
    for sym, ids in parser.tables.items():
        for i, id_ in enumerate(ids):
            if not id_ or id_ not in id2rva:
                continue
            rva = id2rva[id_]
            if sym.startswith("VTABLE_"):
                cls = sym[len("VTABLE_"):]
                vt_rva.setdefault(sym, []).append(rva)
                name = f"{cls}::vftable" + (f"_{i}" if i else "")
            else:
                name = sym
            table_counts[sym.split("_")[0]] += 1
            put(rva, name, "ng-table", "vtable" if sym.startswith("VTABLE_") else "rtti", sym, rva2ids[rva], 1)

    # ng-vfunc
    vfunc_counts, vf_conflicts = _vfuncs(parser, pe, id2rva, vt_rva, rva2ids, put)

    # curated (tools/ghidra/curated_names.txt)
    curated = 0
    cur_path = pathlib.Path(__file__).with_name("curated_names.txt")
    for n, line in enumerate(cur_path.read_text(encoding="utf-8").splitlines() if cur_path.exists() else [], 1):
        text, _, why = line.partition(";")
        parts = text.split()
        if not parts or parts[0].startswith("#") or len(parts) < 2:
            continue
        key, name = parts[0], parts[1]
        if key.upper().startswith(ver["prefix"] + "#"):
            rva = id2rva.get(int(key[len(ver["prefix"]) + 1:]))
        elif key.lower().startswith("0x"):
            rva = int(key, 16)
        else:
            rva = None
        if rva is None:
            report.append(f"curated_names.txt:{n}: {key} does not resolve for {ver['name']}")
            continue
        kind = "func" if pe.is_code(rva) else "data"
        put(rva, name, "curated", kind, f"curated_names.txt:{n}: {why.strip()}", rva2ids[rva], 9)
        curated += 1
    report.append(f"curated: {curated}")

    # docs
    ng_classes ={q.split("::")[-1] for q in parser.class_bases}
    docs = collect_docs(gconfig.repo_dir(), ng_classes)
    comments = {}
    doc_missing = 0
    for id_, names in docs.items():
        rva = id2rva.get(id_) if ver["runtime"] == "ae" else None
        if rva is None:
            doc_missing += 1
            continue
        text = "; ".join(f"{n} ({', '.join(sorted(fs))})" for n, fs in sorted(names.items()))
        comments.setdefault(rva, []).append(f"docs AE#{id_}: {text}")

    result = dict(version=ver["name"], runtime=ver["runtime"], prefix=ver["prefix"], image_base=pe.image_base,
                  ids={str(k): v for k, v in id2rva.items()},
                  names=sorted(entries.values(), key=lambda e: e["rva"]),
                  comments={str(k): v for k, v in comments.items()},
                  vtables={k: v for k, v in vt_rva.items()})
    out.write_text(json.dumps(result), encoding="utf-8")

    by_src = collections.Counter(e["source"] for e in entries.values())
    report.append(f"version {ver['name']} runtime {ver['runtime']}: {len(id2rva)} Address Library IDs, "
                  f"{len(rva2ids)} distinct addresses, {sum(1 for r in rva2ids if pe.is_code(r))} in code")
    report.append(f"NG files parsed: {len(files)}; relocations found: {len(parser.relocs)} "
                  f"({len(by_id)} distinct IDs, {missing} not in Address Library); by kind {dict(reloc_counts)}")
    report.append(f"skipped: {dict(parser.skipped)}")
    report.append(f"tables: {dict(table_counts)}")
    report.append(f"vfuncs: {dict(vfunc_counts)}")
    report.append(f"names by source (after priority): {dict(by_src)}")
    report.append(f"docs: {len(docs)} IDs with names, {doc_missing} not resolvable, {len(comments)} addresses commented")
    report.append(f"conflicts: {len(conflicts)} (+{len(vf_conflicts)} vfunc folds)")
    for c in conflicts:
        report.append(f"  0x{c[0]:X}: {c[1]} [{c[2]}] vs {c[3]} [{c[4]}] {c[5]}")
    report.append("vfunc folds (one function in several unrelated slots, left unnamed):")
    for rva, names in vf_conflicts[:400]:
        report.append(f"  0x{rva:X}: {', '.join(names[:6])}{' ...' if len(names) > 6 else ''}")
    report_path.write_text("\n".join(report) + "\n", encoding="utf-8")
    print("\n".join(report[:9]))
    print(f"names: {out}\nreport: {report_path}")


def _vfuncs(parser, pe, id2rva, vt_rva, rva2ids, put):
    counts = collections.Counter()
    by_simple = {}
    for q in parser.class_bases:
        by_simple.setdefault(q.split("::")[-1], q)

    def chain(cls):
        out, seen = [], set()
        while cls and cls not in seen:
            seen.add(cls)
            out.append(cls)
            bases = parser.class_bases.get(cls) or []
            if not bases:
                break
            b = bases[0]
            cls = b if b in parser.class_bases else by_simple.get(re.sub(r"<.*", "", b).split("::")[-1])
        return out

    # declarations per class, primary vtable only
    decls = collections.defaultdict(dict)  # cls -> slot -> method
    for cls, method, slot, (sec, base), origin in parser.vdecls:
        ch = chain(cls)
        if sec == "override" and base and base not in ch and by_simple.get(re.sub(r"<.*", "", base)) not in ch:
            counts["secondary-base slot (skipped)"] += 1
            continue
        prev = decls[cls].get(slot)
        if prev is None or prev.startswith("Unk_"):
            decls[cls][slot] = method

    def vt_entry(cls, slot):
        sym = parser.class_vtable.get(cls)
        if not sym or sym not in vt_rva:
            return None
        addr = vt_rva[sym][0] + 8 * slot
        try:
            va = struct.unpack_from("<Q", pe.data, pe.rva2off(addr))[0]
        except (ValueError, struct.error):
            return None
        rva = va - pe.image_base
        # leaf functions have no .pdata entry; a code address is enough
        return rva if pe.is_code(rva) else None

    cand = collections.defaultdict(set)  # rva -> {(defining class, method)}
    for cls in parser.class_bases:
        if cls not in parser.class_vtable:
            continue
        ch = chain(cls)
        slots = {}
        for c in reversed(ch):  # base first, derived overrides the name unless it is Unk_
            for s, mth in decls.get(c, {}).items():
                if s not in slots or (slots[s].startswith("Unk_") and not mth.startswith("Unk_")) or not mth.startswith("Unk_"):
                    slots[s] = mth if not (s in slots and mth.startswith("Unk_")) else slots[s]
        for s, mth in slots.items():
            if s > 0x200:
                continue
            target = vt_entry(cls, s)
            if target is None:
                counts["slot not a function start"] += 1
                continue
            owner = cls
            for c in ch[1:]:
                if vt_entry(c, s) == target:
                    owner = c
            cand[target].add((owner, "~" + owner.split("::")[-1] if mth.startswith("~") else mth))
    folds = []
    for rva, cs in cand.items():
        methods = {m for _, m in cs}
        owners = {o for o, _ in cs}
        if len(methods) > 1:
            known = {m for m in methods if not m.startswith("Unk_")}
            if len(known) != 1:
                folds.append((rva, sorted(f"{o}::{m}" for o, m in cs)))
                counts["folded (unnamed)"] += 1
                continue
            methods = known
            cs = {(o, m) for o, m in cs if m in known}
            owners = {o for o, _ in cs}
        mth = next(iter(methods))
        if len(owners) == 1:
            name = f"{next(iter(owners))}::{mth}"
        else:
            # owners on one chain: keep the most base one; otherwise shared implementation
            base = [o for o in owners if all(o in chain(p) for p in owners)]
            name = f"{base[0]}::{mth}" if base else None
            if name is None:
                folds.append((rva, sorted(f"{o}::{m}" for o, m in cs)))
                counts["shared by unrelated classes (unnamed)"] += 1
                continue
        counts["named" if not mth.startswith("Unk_") else "named Unk_"] += 1
        put(rva, name, "ng-vfunc", "func", "vtable", rva2ids.get(rva, []), 1 if mth.startswith("Unk_") else 2)
    return counts, folds


if __name__ == "__main__":
    main()
