# usage: scan_player_access.py <full.asm> <out.json>
# Finds, in a full dumpbin disassembly, every function that reads the player's position or the camera's.
#
#   dumpbin /disasm:nobytes SkyrimSE.exe > full.asm
#
# It follows registers linearly through each function:
#   - player: mov r,[AE#403521]
#   - world camera: the result of call AE#36609
#   - player camera: mov r,[PlayerCamera singleton]
# A use is a memory operand at the position fields of that register, or the register passed on to a call.
# A site counts as a distance check when subss, then mulss or sqrtss, then comiss follow within 48 instructions.
# Arguments are followed too: a function that reads the position of its argument (directly or through a
# callee) is a helper, and a caller that passes the player to it reads the player's position indirectly.
# This is a linear scan, not data flow: branches can hide or invent a use, so the result is a map, not a proof.
import bisect, collections, json, re, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf

EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
starts = sorted(rva2id)
pe = nf.PE(EXE)
BASE = 0x140000000
PLAYER = f"{BASE + id2rva[403521]:016X}"
PLAYER_CAMERA = f"{BASE + id2rva[400802]:016X}" if 400802 in id2rva else None
WORLD_CAMERA = BASE + id2rva[36609]
VOLATILE = {"rax", "rcx", "rdx", "r8", "r9", "r10", "r11"}
ALIAS = {
    "eax": "rax", "ecx": "rcx", "edx": "rdx", "ebx": "rbx", "esi": "rsi", "edi": "rdi", "ebp": "rbp",
    **{f"r{i}d": f"r{i}" for i in range(8, 16)},
    "al": "rax", "cl": "rcx", "dl": "rdx", "bl": "rbx", "sil": "rsi", "dil": "rdi", "bpl": "rbp",
    **{f"r{i}b": f"r{i}" for i in range(8, 16)},
}
LOCATION = {0x54, 0x58, 0x5C}
TRANSLATE = {0xA0, 0xA4, 0xA8}
line_re = re.compile(r"^\s+([0-9A-F]{16}):\s+(\S+)\s*(.*)$")
mem_re = re.compile(r"\[(r[a-z0-9]+)\+([0-9A-Fa-f]+)h\]")
call_re = re.compile(r"([0-9A-F]{16})\s*$")


def owner(rva):
    i = pe.function_at(rva)
    return i[0] if i else None


def label(rva):
    i = bisect.bisect_right(starts, rva) - 1
    if rva in rva2id:
        return f"AE#{rva2id[rva]}"
    return f"AE#{rva2id[starts[i]]}+{rva - starts[i]:#x}"


sites = collections.defaultdict(list)       # function -> [(kind, offset, distance)]
passed = collections.Counter()              # (callee, register) with the player as an argument
playerCalls = collections.defaultdict(list) # (callee, register) -> [(caller, offset)]
argReads = collections.defaultdict(set)     # function -> arguments whose position it reads
argPassed = collections.defaultdict(set)    # (function, argument) -> (callee, argument) it is passed to
ARGS = {"rcx": 0, "rdx": 1, "r8": 2, "r9": 3}
current = None
tags = {}
pending = []                                # open sites waiting for the distance pattern


def close(pending_site):
    pending_site["site"]["distance"] = pending_site["sub"] and pending_site["sq"] and pending_site["cmp"]


for line in open(sys.argv[1], encoding="ascii", errors="replace"):
    m = line_re.match(line)
    if not m:
        continue
    va = int(m.group(1), 16)
    rva = va - BASE
    mnemonic = m.group(2)
    ops = m.group(3)
    fn = owner(rva)
    if fn != current:
        for p in pending:
            close(p)
        pending = []
        current = fn
        tags = {"rcx": "arg0", "rdx": "arg1", "r8": "arg2", "r9": "arg3"} if fn == rva else {}
    # distance pattern after an open site
    for p in pending[:]:
        p["left"] -= 1
        if mnemonic == "subss":
            p["sub"] = True
        elif mnemonic in ("mulss", "sqrtss", "dpps"):
            p["sq"] = True
        elif mnemonic in ("comiss", "ucomiss"):
            p["cmp"] = True
        if p["left"] <= 0 or (p["sub"] and p["sq"] and p["cmp"]):
            close(p)
            pending.remove(p)
    first = ops.split(",")[0].strip()
    source = ops.split(",", 1)[1].strip() if "," in ops else ""
    # memory uses of tagged registers
    for base, disp in mem_re.findall(ops):
        tag = tags.get(base)
        if not tag:
            continue
        d = int(disp, 16)
        kind = None
        if tag.startswith("arg") and d in LOCATION and fn is not None:
            argReads[fn].add(int(tag[3:]))
        if tag == "player" and d in LOCATION:
            kind = "player.location"
        elif tag == "playerRoot" and d in TRANSLATE:
            kind = "player.3D"
        elif tag == "worldCamera" and d in TRANSLATE:
            kind = "worldCamera.translate"
        elif tag == "playerCamera" and d in TRANSLATE:
            kind = "playerCamera.translate"
        if kind and fn is not None:
            site = {"kind": kind, "off": rva - fn, "distance": False}
            sites[fn].append(site)
            pending.append({"site": site, "left": 48, "sub": mnemonic == "subss", "sq": False, "cmp": False})
    # register tracking
    if mnemonic == "mov" and PLAYER in ops and first in ("rax", "rcx", "rdx", "rbx", "rsi", "rdi", "rbp") + tuple(f"r{i}" for i in range(8, 16)):
        tags[first] = "player"
        continue
    if PLAYER_CAMERA and mnemonic == "mov" and PLAYER_CAMERA in ops and first.startswith("r"):
        tags[first] = "playerCamera"
        continue
    if mnemonic == "mov" and source in tags and first.startswith("r") and "[" not in first:
        tags[first] = tags[source]
        continue
    if mnemonic == "call":
        target = call_re.search(ops)
        if target:
            t = int(target.group(1), 16)
            for reg in ("rcx", "rdx", "r8", "r9"):
                tag = tags.get(reg, "")
                if tag == "player" and "[" not in ops:
                    passed[(t - BASE, reg)] += 1
                    playerCalls[(t - BASE, ARGS[reg])].append((fn, rva - fn if fn is not None else 0))
                elif tag.startswith("arg") and fn is not None:
                    argPassed[(fn, int(tag[3:]))].add((t - BASE, ARGS[reg]))
        for reg in list(tags):
            if reg in VOLATILE:
                del tags[reg]
        if target and int(target.group(1), 16) == WORLD_CAMERA:
            tags["rax"] = "worldCamera"
        continue
    if mnemonic == "jmp":  # a tail call keeps the argument registers
        target = call_re.search(ops)
        if target and fn is not None and owner(int(target.group(1), 16) - BASE) != fn:
            t = int(target.group(1), 16) - BASE
            for reg in ("rcx", "rdx", "r8", "r9"):
                tag = tags.get(reg, "")
                if tag.startswith("arg"):
                    argPassed[(fn, int(tag[3:]))].add((t, ARGS[reg]))
                elif tag == "player":
                    playerCalls[(t, ARGS[reg])].append((fn, rva - fn))
        continue
    if mnemonic == "ret":
        tags = {}
        continue
    written = ALIAS.get(first, first)
    if written in tags and "[" not in first and mnemonic not in ("cmp", "test", "comiss", "ucomiss", "push", "nop", "bt"):
        del tags[written]

for p in pending:
    close(p)

# Which argument positions end up reading a position, through any number of calls.
reads = {(fn, a) for fn, args in argReads.items() for a in args}
changed = True
while changed:
    changed = False
    for key, targets in argPassed.items():
        if key not in reads and targets & reads:
            reads.add(key)
            changed = True

indirect = collections.defaultdict(list)
for (callee, arg), callers in playerCalls.items():
    if (callee, arg) in reads:
        for caller, off in callers:
            if caller is not None:
                indirect[caller].append({"helper": label(callee), "arg": arg, "off": off})

result = {"functions": [], "playerArgument": [], "helpers": [], "indirect": []}
for (fn, arg) in sorted(reads):
    if (fn, arg) in {(k[0], k[1]) for k in playerCalls}:
        result["helpers"].append({"fn": label(fn), "rva": fn, "arg": arg, "callersWithPlayer": len(playerCalls[(fn, arg)])})
for fn, items in sorted(indirect.items()):
    result["indirect"].append({"fn": label(fn), "rva": fn, "size": (lambda f: f[1] - f[0])(pe.function_at(fn)), "calls": items[:8], "sites": len(items)})
for fn, items in sorted(sites.items()):
    counts = collections.Counter(i["kind"] for i in items)
    result["functions"].append({
        "fn": label(fn),
        "rva": fn,
        "size": (lambda f: f[1] - f[0])(pe.function_at(fn)),
        "kinds": dict(counts),
        "distanceSites": sum(1 for i in items if i["distance"]),
        "offsets": sorted({i["off"] for i in items})[:12],
    })
for (callee, reg), n in passed.most_common(300):
    result["playerArgument"].append({"callee": label(callee), "reg": reg, "sites": n})
json.dump(result, open(sys.argv[2], "w", encoding="utf-8"), indent=0)
total = collections.Counter()
for f in result["functions"]:
    for k, v in f["kinds"].items():
        total[k] += v
print("functions", len(result["functions"]), "sites", dict(total), "with distance pattern", sum(1 for f in result["functions"] if f["distanceSites"]))
print("helpers that read the position of an argument given the player", len(result["helpers"]), "functions reading it through them", len(result["indirect"]), "sites", sum(f["sites"] for f in result["indirect"]))
