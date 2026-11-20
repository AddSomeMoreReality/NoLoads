# usage: scanbits.py <disp hex> <bit>: functions that set/clear/test a bit of dword [reg+disp] (or/and/bts/btr/bt/test imm)
import bisect, re, struct, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
starts = sorted(rva2id)
pe = nf.PE(EXE)
disp = int(sys.argv[1], 16); bit = int(sys.argv[2]); mask = 1 << bit
_, vaddr, vsize, raw, rawsize = pe.text
code = pe.data[raw:raw + rawsize]
d = struct.pack("<I", disp)
def name(rva):
    i = bisect.bisect_right(starts, rva) - 1
    return f"AE#{rva2id[starts[i]]}+{rva - starts[i]:#x}"
hits = []
for m in re.finditer(re.escape(d), code):
    p = m.start()
    # 81 modrm disp32 imm32  (or=/1 and=/4 test: F7 /0)
    for op, kind in ((0x81, None), (0xF7, "test")):
        q = p - 2
        if q >= 0 and code[q] == op:
            modrm = code[q + 1]
            if modrm >> 6 != 2 or (modrm & 7) == 4: continue
            reg = (modrm >> 3) & 7
            imm = struct.unpack_from("<I", code, p + 4)[0]
            k = kind or {1: "or", 4: "and"}.get(reg)
            if not k: continue
            if (k == "and" and not (imm & mask)) or (k in ("or", "test") and imm & mask):
                hits.append((vaddr + q - (1 if code[q - 1] in (0x41, 0x40) else 0), k, imm))
    # 0F BA modrm disp32 ib (bt=/4 bts=/5 btr=/6)
    q = p - 3
    if q >= 0 and code[q] == 0x0F and code[q + 1] == 0xBA:
        modrm = code[q + 2]
        if modrm >> 6 == 2 and (modrm & 7) != 4 and code[p + 4] == bit:
            k = {4: "bt", 5: "bts", 6: "btr", 7: "btc"}.get((modrm >> 3) & 7)
            if k: hits.append((vaddr + q, k, bit))
for rva, k, imm in hits:
    print(f"{0x140000000 + rva:#x} {name(rva)} {k} {imm:#x}")
