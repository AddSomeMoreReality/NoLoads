import sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
IDB = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin"
_, id2rva = nf.load_idb(IDB)
rva2id = {v: k for k, v in id2rva.items()}
for a in sys.argv[1:]:
    va = int(a, 16)
    print(a, rva2id.get(va - 0x140000000, "?"))
