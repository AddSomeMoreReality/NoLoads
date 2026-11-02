# usage: settingname.py <RVA hex of the Setting value or object>... : name of a static Setting, from the code that registers it
# Static initialisers load the object with lea and the name with lea next to each other.
import re, struct, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf
EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
pe = nf.PE(EXE)
_, vaddr, vsize, raw, rawsize = pe.text
code = pe.data[raw:raw + rawsize]


def text_at(rva):
    off = pe.rva2off(rva)
    if off is None:
        return None
    end = pe.data.find(b"\0", off, off + 200)
    if end < 0:
        return None
    raw_text = pe.data[off:end]
    if len(raw_text) < 3 or not all(32 <= c < 127 for c in raw_text):
        return None
    return raw_text.decode("ascii")


def leas():
    # 48/4C 8D modrm(rip) disp32
    for m in re.finditer(rb"[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", code):
        i = m.start()
        disp = struct.unpack_from("<i", code, i + 3)[0]
        yield i, vaddr + i + 7 + disp


all_leas = list(leas())
for arg in sys.argv[1:]:
    target = int(arg, 16)
    names = []
    for n, (i, t) in enumerate(all_leas):
        if t in (target, target - 8, target + 8) or t == target:
            for k in range(max(0, n - 4), min(len(all_leas), n + 5)):
                s = text_at(all_leas[k][1])
                if s and abs(all_leas[k][0] - i) < 48 and re.fullmatch(r"[A-Za-z0-9_ :]+", s):
                    names.append((all_leas[k][0] - i, s, t - target))
    print(arg, sorted(set(names), key=lambda x: abs(x[0]))[:6])
