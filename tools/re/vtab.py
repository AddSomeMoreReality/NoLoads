# usage: vtab.py <Class> [<BaseClass>]: every vtable slot of a class as AE#id, marking slots that differ from the base
import sys, re
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
pe = nf.PE(EXE)
slots, _ = nf.scan_vtables(pe)
def table(cls):
    out = {}
    for rva, names in slots.items():
        for n in names:
            m = re.fullmatch(re.escape(cls) + r"::vfunc\[(0x[0-9a-f]+)\]", n)
            if m: out[int(m.group(1), 16)] = rva
    return out
t = table(sys.argv[1]); b = table(sys.argv[2]) if len(sys.argv) > 2 else {}
for i in sorted(t):
    mark = "" if b.get(i) == t[i] else " *"
    print(f"{i:#x} AE#{rva2id.get(t[i], '?')} {0x140000000 + t[i]:#x}{mark}")
