# usage: scanfield.py <disp hex> [read|write|all] [xmm|gpr|all]: instructions accessing [reg+disp], grouped by function
# Handles disp8 and disp32, with or without SIB, for mov, movss, movups, movq, movd and movsd.
import bisect, collections, struct, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
starts = sorted(rva2id)
pe = nf.PE(EXE)
disp = int(sys.argv[1], 16)
mode = sys.argv[2] if len(sys.argv) > 2 else "all"
kind = sys.argv[3] if len(sys.argv) > 3 else "all"
_, vaddr, vsize, raw, rawsize = pe.text
code = pe.data[raw:raw + rawsize]

# (prefix bytes, opcode bytes, write?, xmm?)
OPS = [
    (b"\xf3", b"\x0f\x11", True, True), (b"\xf3", b"\x0f\x10", False, True),    # movss
    (b"\xf2", b"\x0f\x11", True, True), (b"\xf2", b"\x0f\x10", False, True),    # movsd
    (b"", b"\x0f\x11", True, True), (b"", b"\x0f\x10", False, True),            # movups
    (b"\x66", b"\x0f\x7e", True, True), (b"\x66", b"\x0f\x6e", False, True),    # movd
    (b"\xf3", b"\x0f\x7e", False, True), (b"\x66", b"\x0f\xd6", True, True),    # movq
    (b"", b"\x89", True, False), (b"", b"\x8b", False, False),                  # mov
]


def fn(rva):
    j = bisect.bisect_right(starts, rva) - 1
    return f"AE#{rva2id[starts[j]]}", rva - starts[j]


hits = collections.defaultdict(list)
data = code
n = len(data)
for prefix, op, write, xmm in OPS:
    if mode == "read" and write or mode == "write" and not write:
        continue
    if kind == "xmm" and not xmm or kind == "gpr" and xmm:
        continue
    start = 0
    while True:
        i = data.find(op, start)
        if i < 0:
            break
        start = i + 1
        p = i + len(op)
        if p + 6 >= n:
            continue
        modrm = data[p]
        mod = modrm >> 6
        rm = modrm & 7
        q = p + 1
        if mod == 3 or (mod == 0):
            continue
        if rm == 4:
            q += 1
        if mod == 1:
            if disp > 0x7f or data[q] != disp:
                continue
        else:
            if struct.unpack_from("<I", data, q)[0] != disp:
                continue
        # prefix check: the byte before the opcode, skipping one optional REX
        b = i - 1
        if 0x40 <= data[b] <= 0x4f:
            b -= 1
        if prefix:
            if data[b] != prefix[0]:
                continue
        elif data[b] in (0xf3, 0xf2, 0x66):
            continue
        rva = vaddr + i
        name, off = fn(rva)
        hits[name].append((off, "W" if write else "R"))

for name, items in sorted(hits.items(), key=lambda x: int(x[0][3:])):
    print(name, " ".join(f"{k}+{o:#x}" for o, k in items[:8]), f"({len(items)})")
print("total functions", len(hits), "sites", sum(len(v) for v in hits.values()))
