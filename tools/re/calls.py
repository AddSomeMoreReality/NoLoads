# usage: calls.py <start VA hex> <end VA hex> : dumpbin the range and print calls/leas with AE IDs
import re, subprocess, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
DUMP = r"D:\VS\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe"
EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
full = len(sys.argv) > 3 and sys.argv[3] == "full"
out = subprocess.run([DUMP, "/disasm:nobytes", f"/range:0x{sys.argv[1]},0x{sys.argv[2]}", EXE], capture_output=True, text=True).stdout
for line in out.splitlines():
    if not re.match(r"\s+[0-9A-F]{16}:", line):
        continue
    m = re.search(r"([0-9A-F]{16})(h?\])?\s*$", line)
    tag = ""
    if m and line.strip()[:16] != m.group(1):
        rva = int(m.group(1), 16) - 0x140000000
        if rva in rva2id:
            tag = f"   ; AE#{rva2id[rva]}"
    if full or re.search(r"\b(call|jmp)\b", line) or tag:
        print(line.strip() + tag)
