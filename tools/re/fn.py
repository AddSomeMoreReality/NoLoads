# usage: fn.py <AE id> [full] : disassemble the whole function (pdata bounds), tag call/jmp/lea targets with AE IDs
import bisect, re, subprocess, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
DUMP = r"D:\VS\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\dumpbin.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
starts = sorted(rva2id)
pe = nf.PE(EXE)


def name(rva):
    if rva in rva2id:
        return f"AE#{rva2id[rva]}"
    i = bisect.bisect_right(starts, rva) - 1
    return f"AE#{rva2id[starts[i]]}+{rva - starts[i]:#x}" if i >= 0 else hex(rva)


full = "full" in sys.argv[2:]
for arg in sys.argv[1:]:
    if arg == "full":
        continue
    if arg.startswith("0x"):
        rva = int(arg, 16) - 0x140000000
    else:
        rva = id2rva[int(arg)]
    b, e = pe.function_at(rva)
    print(f"== {name(rva)} rva {rva:#x} VA {0x140000000 + b:#x}..{0x140000000 + e:#x}")
    out = subprocess.run([DUMP, "/disasm:nobytes", f"/range:{0x140000000 + b:#x},{0x140000000 + e - 1:#x}", EXE],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        if not re.match(r"\s+[0-9A-F]{16}:", line):
            continue
        tag = ""
        m = re.search(r"([0-9A-F]{16})(h?\])?\s*$", line)
        if m and line.strip()[:16] != m.group(1):
            t = int(m.group(1), 16) - 0x140000000
            if 0 <= t < 0x8000000:
                tag = "   ; " + name(t)
        if full or re.search(r"\b(call|jmp)\b", line):
            print(line.strip() + tag)
