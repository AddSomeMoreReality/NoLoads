# usage: scanflag.py <disp hex> <bit>: functions that load dword [reg+disp] and then test the bit through a register
# scanbits.py only sees forms with an immediate on the memory operand. This one also finds
# "mov ecx,[rax+F4h]; shr ecx,14h; test cl,1", "bt ecx,14h" and "test ecx,100000h" within 32 bytes.
import bisect, collections, re, struct, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
starts = sorted(rva2id)
pe = nf.PE(EXE)
disp = int(sys.argv[1], 16)
bit = int(sys.argv[2])
mask = 1 << bit
_, vaddr, vsize, raw, rawsize = pe.text
code = pe.data[raw:raw + rawsize]


def name(rva):
    i = bisect.bisect_right(starts, rva) - 1
    return f"AE#{rva2id[starts[i]]}", rva - starts[i]


needle = struct.pack("<I", disp)
hits = collections.defaultdict(set)
for m in re.finditer(re.escape(needle), code):
    p = m.start()
    if code[p - 2] != 0x8B:  # mov r32, [r+disp32]
        continue
    modrm = code[p - 1]
    if (modrm >> 6) != 2 or (modrm & 7) == 4:
        continue
    window = code[p + 4:p + 36]
    found = None
    for i in range(len(window) - 3):
        w = window[i:]
        if w[0] == 0xC1 and (w[1] & 0xF8) == 0xE8 and w[2] == bit:  # shr r32, imm8
            found = "shr"
            break
        if w[0] == 0x0F and w[1] == 0xBA and (w[2] & 0xF8) == 0xE0 and w[3] == bit:  # bt r32, imm8
            found = "bt"
            break
        if len(w) >= 6 and w[0] == 0xF7 and (w[1] & 0xF8) == 0xC0 and struct.unpack_from("<I", w, 2)[0] & mask:  # test r32, imm32
            found = "test"
            break
        if len(w) >= 5 and w[0] == 0xA9 and struct.unpack_from("<I", w, 1)[0] & mask:  # test eax, imm32
            found = "test"
            break
        if len(w) >= 6 and w[0] == 0x81 and (w[1] & 0xF8) == 0xE0 and struct.unpack_from("<I", w, 2)[0] == mask:  # and r32, imm32
            found = "and"
            break
        if len(w) >= 5 and w[0] == 0x25 and struct.unpack_from("<I", w, 1)[0] == mask:  # and eax, imm32
            found = "and"
            break
    if found:
        fn, off = name(vaddr + p)
        hits[fn].add((off, found))

for fn, items in sorted(hits.items(), key=lambda x: int(x[0][3:])):
    print(fn, " ".join(f"{k}+{o:#x}" for o, k in sorted(items)))
print("functions", len(hits))
