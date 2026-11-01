# usage: vslot.py <Class> <slot hex>...: the function in a class's vtable slot, as AE#id
import sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
pe = nf.PE(EXE)
slots, _ = nf.scan_vtables(pe)
cls = sys.argv[1]
for s in sys.argv[2:]:
    want = f"{cls}::vfunc[{int(s, 16):#x}]"
    for rva, names in slots.items():
        if want in names:
            print(want, f"AE#{rva2id.get(rva, '?')}", hex(0x140000000 + rva))
