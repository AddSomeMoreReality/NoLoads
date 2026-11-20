"""MSVC x64 RTTI of a PE file, standard library only: classes, bases, vftables and slot owners.

CompleteObjectLocator (COL): signature 1, subobject offset, -, TypeDescriptor RVA, ClassHierarchyDescriptor RVA,
own RVA. A vftable starts right after a qword that holds the COL's address. The ClassHierarchyDescriptor lists
all bases (BaseClassDescriptor: TypeDescriptor RVA, contained bases, mdisp, pdisp, vdisp, attributes) in
depth-first order, the class itself first.
"""
import re
import struct


def demangle_type(m):
    """'.?AVTESForm@@' -> 'TESForm'; '.?AV?$BSTEventSink@VTESActivateEvent@@@@' -> 'BSTEventSink<TESActivateEvent>'.
    Covers class/struct names, namespaces and class-typed template arguments; anything else stays mangled."""
    if not m.startswith(".?A"):
        return m
    try:
        name, rest = _name(m[4:])
        return name
    except (IndexError, ValueError, AttributeError, KeyError):
        return m


_BASIC = {"D": "char", "E": "uchar", "F": "short", "G": "ushort", "H": "int", "I": "uint", "J": "long",
          "K": "ulong", "M": "float", "N": "double", "_N": "bool", "_J": "longlong", "_K": "ulonglong", "X": "void",
          "_W": "wchar_t"}


def _name(s, back=None):
    """Qualified name 'A@B@@' (innermost first) -> ('B::A', rest)."""
    back = back if back is not None else []
    parts = []
    while not s.startswith("@"):
        if s.startswith("?$"):
            tname, s = _template(s[2:], back)
            parts.append(tname)
        elif s[0].isdigit():
            parts.append(back[int(s[0])])
            s = s[1:]
            continue
        else:
            i = s.index("@")
            parts.append(s[:i])
            if s[:i] not in back:
                back.append(s[:i])
            s = s[i + 1:]
            continue
    return "::".join(reversed(parts)), s[1:]


def _arg(s, inner):
    """One template argument -> (text, rest)."""
    if s[0] in "VU":
        return _name(s[1:], inner)
    if s[:2] in _BASIC:
        return _BASIC[s[:2]], s[2:]
    if s[0] in _BASIC:
        return _BASIC[s[0]], s[1:]
    if s.startswith("$0"):  # integer: 0-9 means 1-10, else hex digits A-P then '@'
        if s[2].isdigit():
            return str(int(s[2]) + 1), s[3:]
        if s[2] == "A" and s[3] == "@":
            return "0", s[4:]
        i = s.index("@", 2)
        return str(int("".join("0123456789ABCDEF"["ABCDEFGHIJKLMNOP".index(c)] for c in s[2:i]), 16)), s[i + 1:]
    if s[0] == "P":  # pointer: P [E] [A|B] type
        j = 1 + (s[1] == "E")
        const = s[j] == "B"
        a, rest = _arg(s[j + 1:], inner)
        return ("const " if const else "") + a + " *", rest
    raise ValueError(s)


def _template(s, back):
    i = s.index("@")
    tname = s[:i]
    s = s[i + 1:]
    args, inner = [], []
    while not s.startswith("@"):
        a, s = _arg(s, inner)
        args.append(a)
    if tname not in back:
        back.append(tname)
    return f"{tname}<{','.join(args)}>", s[1:]


class RTTI:
    def __init__(self, pe):
        self.pe = pe
        d, base = pe.data, pe.image_base
        self.cols = {}  # col rva -> (offset, td rva, chd rva)
        data_secs = [(n, va, size, raw) for n, va, size, raw, ch in pe.sections
                     if not ch & 0x20000000 and n not in (".pdata", ".reloc", ".rsrc")]
        for n, va, size, raw in data_secs:
            end = min(raw + size, len(d))
            for off in range(raw, end - 24, 4):
                if d[off] != 1 or d[off + 1:off + 4] != b"\0\0\0":
                    continue
                sig, sub, _, td, chd, own = struct.unpack_from("<IiIIII", d, off)
                if own == va + (off - raw) and td and chd:
                    self.cols[own] = (sub, td, chd)
        self.names = {}
        self.vftables = []  # (vftable rva, col rva)
        col_va = {base + c: c for c in self.cols}
        for n, va, size, raw in data_secs:
            end = min(raw + size, len(d))
            for off in range(raw, end - 8, 8):
                v = struct.unpack_from("<Q", d, off)[0]
                c = col_va.get(v)
                if c is not None:
                    self.vftables.append((va + (off - raw) + 8, c))
        self.vftables.sort()
        self._starts = {v for v, _ in self.vftables}

    def td_name(self, td):
        if td not in self.names:
            off = self.pe.rva2off(td + 16)
            end = self.pe.data.index(b"\0", off)
            self.names[td] = demangle_type(self.pe.data[off:end].decode("ascii", "replace"))
        return self.names[td]

    def bases(self, chd):
        """[(class name, mdisp, pdisp)] with the class itself first."""
        d, r2o = self.pe.data, self.pe.rva2off
        _, _, n, bca = struct.unpack_from("<IIII", d, r2o(chd))
        out = []
        for i in range(n):
            bcd = struct.unpack_from("<I", d, r2o(bca) + 4 * i)[0]
            td, _, mdisp, pdisp = struct.unpack_from("<IIii", d, r2o(bcd))
            out.append((self.td_name(td), mdisp, pdisp))
        return out

    def slots(self, vft):
        """Code RVAs of a vftable: until a non-code value or the next vftable's COL pointer."""
        pe, out = self.pe, []
        rva = vft
        while len(out) < 2000:
            if out and rva + 8 in self._starts:  # rva holds the next table's COL pointer
                break
            try:
                v = struct.unpack_from("<Q", pe.data, pe.rva2off(rva))[0] - pe.image_base
            except (ValueError, struct.error):
                break
            if not pe.is_code(v):
                break
            out.append(v)
            rva += 8
        return out

    def tables(self):
        """[dict(cls, offset, vftable, slots, chain)] where chain = classes sharing the subobject (derived first)."""
        out = []
        for vft, col in self.vftables:
            sub, td, chd = self.cols[col]
            try:
                bl = self.bases(chd)
            except (ValueError, struct.error):
                continue
            chain = [bl[0][0]] + [b for b, md, pd in bl[1:] if md == sub and pd == -1]
            out.append(dict(cls=self.td_name(td), offset=sub, vftable=vft, slots=self.slots(vft), chain=chain))
        return out


def slot_owners(tables):
    """{function rva: {(owner class, slot)}}: owner = the most base class of the chain whose primary vftable has
    the same function in that slot."""
    primary = {}
    for t in tables:
        if t["offset"] == 0:
            primary.setdefault(t["cls"], t["slots"])
    owners = {}
    for t in tables:
        for i, f in enumerate(t["slots"]):
            owner = t["cls"]
            for b in t["chain"][1:]:
                ps = primary.get(b)
                if ps is not None and i < len(ps) and ps[i] == f:
                    owner = b
            owners.setdefault(f, set()).add((owner, i))
    return owners
