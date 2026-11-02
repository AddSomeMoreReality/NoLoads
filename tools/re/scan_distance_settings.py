# usage: scan_distance_settings.py <CellLoadResearch.settings.txt> <full.asm> <out.md>
# Every distance check compares against a number, and most of those numbers are settings. For each setting
# whose name speaks of distance, radius or range, finds its Setting object in the executable (the pointer to
# its name sits at +0x10, the value at +0x08) and the functions that read the value.
# A setting whose name pointer is written by code at start-up has no such object here: it is listed as unresolved.
import collections, re, struct, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf

EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
pe = nf.PE(EXE)
BASE = 0x140000000
INTEREST = re.compile(r"dist|radius|range|nearby|proximity", re.I)
data = pe.data


def off2rva(offset):
    for _, vaddr, _, raw, size in pe.sections:
        if raw <= offset < raw + size:
            return vaddr + offset - raw
    return None


settings = []
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    parts = line.rstrip("\n").split("\t")
    if len(parts) >= 3 and INTEREST.search(parts[1]):
        settings.append((parts[0], parts[1], parts[2]))

pointers = collections.defaultdict(list)
for match in re.finditer(rb"(?=[\x00-\xff]{7}\x00)", b""):  # placeholder keeps the scan below readable
    pass
values = {}
for kind, name, value in settings:
    needle = name.encode("ascii", "replace") + b"\0"
    start = 0
    while True:
        at = data.find(needle, start)
        if at < 0:
            break
        start = at + 1
        if at and data[at - 1] != 0:
            continue
        rva = off2rva(at)
        if rva is None:
            continue
        pointer = struct.pack("<Q", BASE + rva)
        p = 0
        while True:
            hit = data.find(pointer, p)
            if hit < 0:
                break
            p = hit + 8
            holder = off2rva(hit)
            if holder:
                values[BASE + holder - 8] = name

readers = collections.defaultdict(collections.Counter)
ref = re.compile(r"\[([0-9A-F]{16})h\]")
line_re = re.compile(r"^\s+([0-9A-F]{16}):")
for line in open(sys.argv[2], encoding="ascii", errors="replace"):
    found = ref.search(line)
    if not found:
        continue
    address = int(found.group(1), 16)
    if address not in values:
        continue
    site = int(line_re.match(line).group(1), 16) - BASE
    bounds = pe.function_at(site)
    fn = bounds[0] if bounds else site
    readers[values[address]][f"AE#{rva2id.get(fn, hex(fn))}"] += 1

out = [
    "",
    "## Настройки расстояния и кто их читает",
    "",
    "Создано `tools/re/scan_distance_settings.py`. `нет объекта`: имя настройки пишется в объект кодом при старте, адрес этим способом не найден.",
    "",
    "| Настройка | Значение | Кто читает |",
    "|---|---:|---|",
]
resolved = set(values.values())
for kind, name, value in sorted(settings, key=lambda s: s[1].lower()):
    if name in readers:
        who = " ".join(k for k, _ in readers[name].most_common(6))
        more = len(readers[name]) - 6
        out.append(f"| {name} | {value} | {who}{f' и ещё {more}' if more > 0 else ''} |")
    elif name in resolved:
        out.append(f"| {name} | {value} | не читается напрямую |")
    else:
        out.append(f"| {name} | {value} | нет объекта |")
open(sys.argv[3], "a", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
print("settings", len(settings), "with object", len(resolved), "with readers", len(readers))
for name in ("fAIDialogueDistance", "fAIInDialogueModeWithPlayerDistance", "fVisibleNavmeshMoveDist", "fNotVisibleNavmeshMoveDist", "fLowPerfCombatantVoiceDistance", "fDeathSoundMaxDistance", "fMaxHighlightRadius"):
    print(name, dict(readers.get(name, {})))
