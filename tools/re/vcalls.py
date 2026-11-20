# usage: vcalls.py <vfunc index hex> [more indexes]: functions with call [reg+index*8], grouped by function
# Without type information this also matches other classes with the same slot.
import bisect, collections, re, struct, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
starts = sorted(rva2id)
pe = nf.PE(EXE)
_, vaddr, vsize, raw, rawsize = pe.text
code = pe.data[raw:raw + rawsize]
for arg in sys.argv[1:]:
    index = int(arg, 16)
    needle = struct.pack("<I", index * 8)
    hits = collections.Counter()
    for m in re.finditer(re.escape(needle), code):
        i = m.start()
        modrm = code[i - 1]
        if (modrm >> 6) != 2 or ((modrm >> 3) & 7) != 2 or (modrm & 7) == 4:
            continue
        if code[i - 2] != 0xFF:
            continue
        rva = vaddr + i
        j = bisect.bisect_right(starts, rva) - 1
        hits[f"AE#{rva2id[starts[j]]}"] += 1
    print(f"vfunc {index:#x}:", " ".join(f"{k}({v})" for k, v in sorted(hits.items(), key=lambda x: int(x[0][3:]))))
