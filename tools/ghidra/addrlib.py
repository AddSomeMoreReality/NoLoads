"""Address Library (SE format 1, AE format 2) and minimal PE metadata. Standard library only."""
import bisect
import struct


def load_addrlib(path):
    """Return (version tuple, {id: rva}). Both formats share the delta encoding."""
    data = open(path, "rb").read()
    pos = 0

    def read(fmt):
        nonlocal pos
        values = struct.unpack_from(fmt, data, pos)
        pos += struct.calcsize(fmt)
        return values[0] if len(values) == 1 else values

    fmt = read("<i")
    if fmt not in (1, 2):
        raise SystemExit(f"{path}: Address Library format {fmt} is not supported")
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
    return tuple(version), id2rva


LEA_OP = bytes([0x8D])


class PE:
    """Sections, image base and .pdata function starts of a PE32+ file."""

    def __init__(self, path):
        self.data = open(path, "rb").read()
        d = self.data
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        opt_size = struct.unpack_from("<H", d, pe + 20)[0]
        opt = pe + 24
        self.image_base = struct.unpack_from("<Q", d, opt + 24)[0]
        exc_rva, exc_size = struct.unpack_from("<II", d, opt + 112 + 3 * 8)
        self.sections = []
        for i in range(nsec):
            s = pe + 24 + opt_size + 40 * i
            name = d[s:s + 8].rstrip(b"\0").decode("ascii", "replace")
            vsize, va, rsize, raw = struct.unpack_from("<IIII", d, s + 8)
            chars = struct.unpack_from("<I", d, s + 36)[0]
            self.sections.append((name, va, max(vsize, rsize), raw, chars))
        self.func_begin = []
        self.func_end = []
        self.chained = set()  # .pdata entries that continue a function starting elsewhere (UNW_FLAG_CHAININFO)
        off = self.rva2off(exc_rva)
        for i in range(exc_size // 12):
            b, e, u = struct.unpack_from("<III", d, off + 12 * i)
            self.func_begin.append(b)
            self.func_end.append(e)
            try:
                if u & 1 or (d[self.rva2off(u)] >> 3) & 0x4:
                    self.chained.add(b)
            except ValueError:
                pass
        order = sorted(range(len(self.func_begin)), key=self.func_begin.__getitem__)
        self.func_begin = [self.func_begin[i] for i in order]
        self.func_end = [self.func_end[i] for i in order]
        self.func_starts = set(self.func_begin)
        self.primary_starts = self.func_starts - self.chained  # real function starts

    def code_leas(self):
        """{target rva: [site rva]} of `lea r64,[rip+disp32]` in code whose target is code (callbacks passed as
        arguments). Byte scan: REX.W (48/4C) 8D ModRM mod=00 rm=101; targets inside functions are filtered later."""
        out = {}
        for name, va, size, raw, chars in self.sections:
            if not chars & 0x20000000:
                continue
            code = self.data[raw:raw + size]
            i = code.find(LEA_OP)
            while 0 < i < len(code) - 5:
                if code[i - 1] in (0x48, 0x4C) and (code[i + 1] & 0xC7) == 0x05:
                    t = va + i + 6 + struct.unpack_from("<i", code, i + 2)[0]
                    if self.is_code(t):
                        out.setdefault(t, []).append(va + i - 1)  # site: the REX byte
                i = code.find(LEA_OP, i + 1)
        return out

    def code_pointers(self):
        """{target rva: [rva of the qword]} for 8-aligned qwords in non-executable sections pointing into code."""
        out = {}
        lo, hi = self.image_base, self.image_base + max(va + size for _, va, size, _, _ in self.sections)
        for name, va, size, raw, chars in self.sections:
            if chars & 0x20000000 or name in (".pdata", ".reloc", ".rsrc"):
                continue
            buf = self.data[raw:raw + min(size, len(self.data) - raw)]
            for i in range(0, len(buf) - 7 - (va % 8), 8):
                v = struct.unpack_from("<Q", buf, i)[0]
                if lo <= v < hi and self.is_code(v - self.image_base):
                    out.setdefault(v - self.image_base, []).append(va + i)
        return out

    def section_of(self, rva):
        for name, va, size, raw, chars in self.sections:
            if va <= rva < va + size:
                return name
        return None

    def is_code(self, rva):
        for name, va, size, raw, chars in self.sections:
            if va <= rva < va + size:
                return bool(chars & 0x20000000)  # IMAGE_SCN_MEM_EXECUTE
        return False

    def rva2off(self, rva):
        for name, va, size, raw, chars in self.sections:
            if va <= rva < va + size:
                return rva - va + raw
        raise ValueError(hex(rva))

    def pdata_function(self, rva):
        """(begin, end) of the .pdata entry that contains rva, or None. Chained entries are separate."""
        i = bisect.bisect_right(self.func_begin, rva) - 1
        if i >= 0 and rva < self.func_end[i]:
            return self.func_begin[i], self.func_end[i]
        return None
