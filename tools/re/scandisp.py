# usage: scandisp.py <disp hex> [kind]: functions containing ModRM disp32 == disp (any register base), kind 'all' or 'store' (movss/mov to mem)
import bisect, collections, re, struct, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
starts = sorted(rva2id)
pe = nf.PE(EXE)
disp = int(sys.argv[1], 16)
_, vaddr, vsize, raw, rawsize = pe.text
code = pe.data[raw:raw + rawsize]
needle = struct.pack("<I", disp)
hits = collections.Counter()
for m in re.finditer(re.escape(needle), code):
    i = m.start()
    modrm = code[i - 1]
    if (modrm >> 6) != 2 or (modrm & 7) == 4:  # mod=10 (disp32), no SIB
        continue
    op = code[i - 4:i - 1]
    store = (op[-2:] == b"\x0f\x11") or code[i - 2] in (0x89,)
    if len(sys.argv) > 2 and sys.argv[2] == "store" and not store:
        continue
    rva = vaddr + i
    j = bisect.bisect_right(starts, rva) - 1
    hits[(f"AE#{rva2id[starts[j]]}", "store" if store else "")] += 1
for (name, kind), n in sorted(hits.items(), key=lambda x: -x[1])[:60]:
    print(name, kind, n)
