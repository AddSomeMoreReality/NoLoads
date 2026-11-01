# usage: riprefs.py <AE id> : functions with a RIP-relative operand pointing at the ID's address (any instruction)
import bisect, collections, struct, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
starts = sorted(rva2id)
pe = nf.PE(EXE)
target = id2rva[int(sys.argv[1])]
_, vaddr, vsize, raw, rawsize = pe.text
code = pe.data[raw:raw + rawsize]
hits = collections.Counter()
# disp32 at offset i means next-instruction address = i + 4 + (0..4 immediate bytes); try imm sizes 0, 1, 4
for i in range(len(code) - 4):
    disp = struct.unpack_from("<i", code, i)[0]
    for imm in (0, 1, 4):
        if vaddr + i + 4 + imm + disp == target:
            # require a ModRM with mod=00 rm=101 right before
            if (code[i - 1] & 0xC7) == 0x05:
                rva = vaddr + i
                j = bisect.bisect_right(starts, rva) - 1
                hits[f"AE#{rva2id[starts[j]]}"] += 1
for name, n in hits.most_common(40):
    print(name, n)
