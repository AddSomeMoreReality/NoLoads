"""Name sources of T34 for collect_names.py (standard library only). Each returns names for put() and a report line.

  rtti-vslot  unnamed functions in vftable slots of RTTI classes -> Owner::vf_0xNN (owner: the most base class
              with the same function in that slot); several unrelated owners -> comment only
  papyrus     native functions of Papyrus (papyrus.py) -> Papyrus::<Class>::<Function>
  console     SCRIPT_FUNCTION tables (NG CommandTable.h): console and script commands -> ConsoleCommand::<Name>,
              ScriptCommand::<Name>, Condition::<Name>; handlers shared by several commands -> comment only
  strings     C strings in data that name "Class::Method": candidates for comments (apply_names.py keeps them
              when exactly one function references the string)
"""
import collections
import re
import struct

import papyrus
import rtti

# classes whose slot functions are runtime/library code: their Function ID names are trusted, no vf_ names
LIBRARY = re.compile(r"^(std::|Concurrency::|type_info$|stdext::|`|<lambda)")
# SCRIPT_FUNCTION (NG include/RE/C/CommandTable.h): size 0x50, functionName +0x00, executeFunction +0x30,
# conditionFunction +0x40; tables: console AE#365650 / SE#501797, script AE#361120 / SE#501789
CMD_SIZE, CMD_NAME, CMD_EXEC, CMD_COND = 0x50, 0x00, 0x30, 0x40
# entry counts = Commands::kConsoleCommandsEnd / kScriptCommandsEnd (NG's Locate*Command loops); the script table
# is followed directly by the console table, so a count is needed, not an end marker
CMD_TABLES = {"ae": [("ConsoleCommand", 365650, 0x1B4), ("ScriptCommand", 361120, 0x2E0)],
              "se": [("ConsoleCommand", 501797, 0x1B4), ("ScriptCommand", 501789, 0x2E0)]}


def vslots(pe, put, comments):
    tabs = rtti.RTTI(pe).tables()
    owners = rtti.slot_owners(tabs)
    primary_vt = {}
    for t in tabs:
        if t["offset"] == 0:
            primary_vt.setdefault(t["cls"], t["vftable"])
    c = collections.Counter()
    for f, os_ in owners.items():
        own = {o for o, _ in os_}
        lib = {o for o in own if LIBRARY.match(o) or o.startswith(".?")}
        if own == lib:
            c["library or unparsed classes only"] += 1
            continue
        own -= lib
        if len(own) == 1 and len({s for o, s in os_ if o in own}) == 1:
            o, s = next((o, s) for o, s in os_ if o in own)
            # apply_names.py puts the name into the namespace of Ghidra's vftable symbol at that address
            put(f, f"{o}::vf_0x{s:X}", "rtti-vslot", "func", f"RTTI vftable 0x{primary_vt.get(o, 0):X} slot 0x{s:X}", [], 0)
            c["named"] += 1
        else:
            slots = sorted(f"{o}[0x{s:X}]" for o, s in os_)
            comments.setdefault(f, []).append("vftable slots: " + ", ".join(slots[:8]) +
                                              (f" (+{len(slots) - 8})" if len(slots) > 8 else ""))
            c["several owners (comment)"] += 1
    return f"rtti-vslot: {len(tabs)} vftables, {len(owners)} slot functions; {dict(c)}", owners


def natives(pe, put):
    nat = papyrus.natives(pe)
    by_cb = collections.defaultdict(list)
    for cb, cls, fn, site, pat in nat:
        by_cb[cb].append((cls, fn, site))
    n = 0
    for cb, regs in by_cb.items():
        if len(regs) == 1:
            cls, fn, site = regs[0]
            put(cb, f"Papyrus::{cls}::{fn}", "papyrus", "func", f"registered at RVA 0x{site:X}", [], 2)
            n += 1
    return f"papyrus: {len(nat)} registrations, {len(by_cb)} callbacks, {n} named, " \
           f"{len({c for _, c, _, _, _ in nat})} classes"


def commands(pe, id2rva, runtime, put, comments):
    uses = collections.defaultdict(list)  # rva -> [(kind, name)]
    total = collections.Counter()
    for kind, id_, count in CMD_TABLES.get(runtime, []):
        rva = id2rva.get(id_)
        if rva is None:
            continue
        for i in range(count):
            off = pe.rva2off(rva + CMD_SIZE * i)
            name_va = struct.unpack_from("<Q", pe.data, off + CMD_NAME)[0]
            if not name_va:
                break
            try:
                so = pe.rva2off(name_va - pe.image_base)
            except ValueError:
                break
            name = pe.data[so:pe.data.index(b"\0", so)].decode("ascii", "replace")
            if not re.fullmatch(r"[A-Za-z_][\w]*", name):
                total["bad name"] += 1
                continue
            total[kind] += 1
            for field, label in ((CMD_EXEC, kind), (CMD_COND, "Condition")):
                v = struct.unpack_from("<Q", pe.data, off + field)[0]
                if v and pe.is_code(v - pe.image_base):
                    uses[v - pe.image_base].append((label, name))
    named = 0
    for f, us in uses.items():
        if len(us) == 1:
            put(f, f"{us[0][0]}::{us[0][1]}", "console", "func", "SCRIPT_FUNCTION table", [], 2)
            named += 1
        else:
            comments.setdefault(f, []).append(
                "handler of " + ", ".join(f"{k}::{n}" for k, n in us[:6]) + (f" (+{len(us) - 6})" if len(us) > 6 else ""))
    return f"console/script commands: {dict(total)}, {len(uses)} handlers, {named} named, " \
           f"{len(uses) - named} shared (comment)"


STR_RE = re.compile(rb"[\x20-\x7e]{6,200}\x00")
CM_RE = re.compile(r"\b((?:[A-Z][A-Za-z0-9_]*::)+~?[A-Za-z_][A-Za-z0-9_]*)\b")


def class_method_strings(pe, known_classes):
    """[(string rva, 'Class::Method')] for printable strings in data that contain Class::Method of a known class."""
    out = []
    for name, va, size, raw, chars in pe.sections:
        if chars & 0x20000000 or name in (".pdata", ".reloc", ".rsrc"):
            continue
        buf = pe.data[raw:raw + size]
        for m in STR_RE.finditer(buf):
            text = m.group(0)[:-1].decode("ascii")
            for cm in CM_RE.findall(text):
                if cm.split("::")[-2] in known_classes or cm.split("::")[0] in known_classes:
                    out.append((va + m.start(), cm))
                    break
    return out
