#!/usr/bin/env python3
"""Summarize a CellLoadResearch trace (JSONL) as Markdown for docs/FINDINGS.md.

Usage:
    python tools/analyze_trace.py logs/CellLoadResearch.trace.jsonl [-o report.md] [--timeline]

Standard library only.
"""
import argparse
import collections
import json
import sys

PLAYER = "[00000014]"
LOADING = "Loading Menu"
GRID_ITEM_OPS = (
    "hook.GridCellArray.Detach.",
    "hook.GridCellArray.ClearItem.",
    "hook.GridCellArray.MoveItem.",
    "hook.GridCellArray.SwapItem.",
)
WALK_WINDOW_US = 5_000_000  # look for loads this long after a grid shift
DOOR_LEAD_US = 5_000_000  # a player door activation this close before a load triggered it


# ----------------------------------------------------------------------------
# helpers
# ----------------------------------------------------------------------------
def load(path):
    records = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError as e:
                print(f"warning: line {number}: {e}", file=sys.stderr)
    records.sort(key=lambda r: r["seq"])
    return records


def a(r):
    return r.get("args") or {}


def ms(us):
    return "" if us is None else f"{us / 1000:.1f}"


def is_menu(r, name, opening):
    return r["ev"] == "event.Menu" and a(r).get("menu") == name and a(r).get("opening") == opening


def is_phase(r, name):
    return r["ev"] == "event.PositionPlayer" and a(r).get("name") == name


def by_player(r):
    return (a(r).get("activator") or "").endswith(PLAYER)


def is_noise(r):
    if r["ev"].startswith(GRID_ITEM_OPS):
        return True
    if r["ev"].startswith("hook.TESObjectDOOR.Activate") and not by_player(r) and "queuedValid" not in a(r):
        return True
    return r["ev"] == "event.Menu" and a(r).get("menu") == "LoadWaitSpinner"


def short(r):
    args = a(r)
    keys = {
        "event.PositionPlayer": ("name", "parentCell", "target_valid", "target_interior", "target_world"),
        "event.ActorCell": ("flag", "cell"),
        "event.Menu": ("menu", "opening"),
        "event.CellFullyLoaded": ("cell", "cell_xy"),
        "event.ActorLocationChange": ("oldLoc", "newLoc"),
        "hook.TESObjectDOOR.Activate.enter": ("doorCell", "destCell"),
        "hook.TESObjectDOOR.Activate.leave": ("result", "queuedValid", "dur_us"),
        "hook.GridCellArray.SetCenter.enter": ("world", "xy", "grid", "queued"),
        "hook.GridCellArray.SetCenter.leave": ("result", "dur_us"),
        "hook.GridCellArray.Shift.enter": ("xy",),
        "hook.GridCellArray.DetachAll.enter": ("world",),
        "hook.TES.SetCurrentCell.enter": ("cell", "pos", "unk4", "grid", "queued"),
        "hook.ExteriorCellLoader.QueueCellLoad.enter": ("xy", "unk", "grid", "queued", "playerCell_xy"),
        "hook.PlayerCharacter.CenterOnCell.enter": ("name", "cell"),
        "hook.TESObjectREFR.MoveTo.enter": ("target", "cell", "world", "pos"),
        "snapshot": ("reason", "interiorCell", "world", "grid", "queued", "queuedCellLoads", "parentCell"),
        "preload.change": ("where", "cells"),
        "preload.queue": ("xy",),
    }.get(r["ev"])
    if keys:
        args = {k: args[k] for k in keys if k in args}
    text = json.dumps(args, ensure_ascii=False) if args else ""
    return text if len(text) <= 160 else text[:157] + "..."


def table(headers, rows):
    out = ["| " + " | ".join(headers) + " |", "|" + "---|" * len(headers)]
    for row in rows:
        out.append("| " + " | ".join(str(c).replace("|", "\\|") for c in row) + " |")
    return "\n".join(out)


def first(records, pred, start=0):
    for i in range(start, len(records)):
        if pred(records[i]):
            return i
    return None


# ----------------------------------------------------------------------------
# model
# ----------------------------------------------------------------------------
class Transition:
    def __init__(self, records, lo, hi, loading):
        self.records = records[lo : hi + 1]
        self.loading = loading
        self.start = records[lo]
        self.end = records[hi]

    def find(self, pred):
        for r in self.records:
            if pred(r):
                return r
        return None

    def find_all(self, pred):
        return [r for r in self.records if pred(r)]

    @property
    def t0(self):
        return self.start["t_us"]

    def dt(self, r):
        return None if r is None else r["t_us"] - self.t0


def find_transitions(records):
    result = []
    covered = []
    open_idx = None
    for i, r in enumerate(records):
        if is_menu(r, LOADING, True):
            open_idx = i
        elif is_menu(r, LOADING, False) and open_idx is not None:
            result.append(Transition(records, open_idx, i, True))
            covered.append((records[open_idx]["seq"], r["seq"]))
            open_idx = None

    # kPre..kFinish without a loading screen (e.g. coc into the current cell)
    pre_idx = None
    for i, r in enumerate(records):
        if is_phase(r, "kPre"):
            pre_idx = i
        elif is_phase(r, "kFinish") and pre_idx is not None:
            seq = records[pre_idx]["seq"]
            if not any(lo <= seq <= hi for lo, hi in covered):
                result.append(Transition(records, pre_idx, i, False))
            pre_idx = None

    result.sort(key=lambda t: t.start["seq"])
    return result


def trigger_of(records, transition):
    """Player door activation shortly before the transition, if any."""
    start = transition.start["seq"]
    door = None
    for r in records:
        if r["seq"] >= start:
            break
        if r["ev"] == "hook.TESObjectDOOR.Activate.enter" and by_player(r):
            door = r
    if door and transition.t0 - door["t_us"] <= DOOR_LEAD_US:
        return "door", door
    if transition.find(lambda r: r["ev"] == "event.LoadGame"):
        return "load game", None
    return "other", None


def tasks(records):
    """CellLoaderTask calls grouped per task instance (pointers can be reused, so split on a new Run)."""
    current = {}
    done = []
    for r in records:
        ev = r["ev"]
        if not ev.startswith("hook.CellLoaderTask.") or not ev.endswith(".enter"):
            continue
        method = ev.split(".")[2]
        ptr = a(r).get("task")
        task = current.get(ptr)
        if task is None or (method == "Run" and "Run" in task["calls"]):
            task = {"ptr": ptr, "args": a(r), "calls": {}, "order": []}
            current[ptr] = task
            done.append(task)
        task["calls"].setdefault(method, r)
        task["order"].append(method)
    # leave records carry dur_us; match the first leave after each enter
    for task in done:
        for method, enter in task["calls"].items():
            leave = None
            for r in records:
                if r["seq"] > enter["seq"] and r["ev"] == f"hook.CellLoaderTask.{method}.leave" and r["tid"] == enter["tid"]:
                    leave = r
                    break
            task.setdefault("dur", {})[method] = a(leave).get("dur_us") if leave else None
    return done


# ----------------------------------------------------------------------------
# report sections
# ----------------------------------------------------------------------------
def section_run(records, out):
    out.append("## Run\n")
    info = next((r for r in records if r["ev"] == "info.plugin"), None)
    if info:
        out.append(f"- plugin {a(info).get('version')}, runtime {a(info).get('runtime')}")
    mains = collections.Counter(r["tid"] for r in records if r["main"])
    if mains:
        out.append(f"- main thread: {mains.most_common(1)[0][0]}")
    out.append(f"- records: {len(records)}, frames: {max((r['frame'] for r in records), default=0)}, "
               f"duration: {records[-1]['t_us'] / 1e6:.1f} s" if records else "- empty trace")
    ini = [r for r in records if r["ev"] == "info.ini"]
    if ini:
        out.append("\nINI settings (H10):\n")
        out.append(table(["setting", "found", "value", "collection"],
                         [(a(r).get("name"), a(r).get("found"), a(r).get("value", ""), a(r).get("collection", "")) for r in ini]))
    out.append("")


def section_threads(records, out):
    out.append("## Threads\n")
    counts = collections.OrderedDict()
    for r in records:
        if r["ev"].endswith(".leave"):
            continue
        c = counts.setdefault(r["ev"], {"n": 0, "main": 0, "tids": set()})
        c["n"] += 1
        c["main"] += 1 if r["main"] else 0
        c["tids"].add(r["tid"])
    rows = [(ev, c["n"], c["main"], c["n"] - c["main"], len(c["tids"])) for ev, c in sorted(counts.items())]
    out.append(table(["ev", "count", "main", "other", "threads"], rows))
    out.append("")


def section_transitions(records, transitions, out, timeline):
    out.append("## Transitions\n")
    rows = []
    for n, t in enumerate(transitions, 1):
        kind, door = trigger_of(records, t)
        pre = t.find(lambda r: is_phase(r, "kPre"))
        fin = t.find(lambda r: is_phase(r, "kFinish"))
        post = t.find(lambda r: is_phase(r, "kPost"))
        enter = None
        if pre:
            enter = next((r for r in t.records if r["seq"] > pre["seq"] and r["ev"] == "event.ActorCell" and a(r).get("flag") == "enter"), None)
        cfl = t.find_all(lambda r: r["ev"] == "event.CellFullyLoaded")
        runs = t.find_all(lambda r: r["ev"] == "hook.CellLoaderTask.Run.enter")
        phases = "→".join(a(r)["name"].lstrip("k") for r in t.find_all(lambda r: r["ev"] == "event.PositionPlayer"))
        scc = t.find_all(lambda r: r["ev"] == "hook.TES.SetCurrentCell.leave")
        scc_enter = t.find(lambda r: r["ev"] == "hook.TES.SetCurrentCell.enter")
        rows.append((
            n,
            f"{t.t0 / 1e6:.1f}",
            kind,
            a(pre).get("parentCell", "") if pre else "",
            a(fin).get("parentCell", "") if fin else "",
            ms(t.end["t_us"] - t.t0) if t.loading else "no menu",
            ms(t.dt(pre)) if t.loading else "",
            ms(enter["t_us"] - pre["t_us"]) if enter and pre else "",
            ms(post["t_us"] - enter["t_us"]) if post and enter else "",
            ms(fin["t_us"] - post["t_us"]) if fin and post else "",
            ms(sum(a(r).get("dur_us", 0) for r in scc)) if scc else "",
            a(scc_enter).get("unk4", "") if scc_enter else "",
            f"{len(cfl)} ({sum(1 for r in cfl if r['main'])} main)",
            len(runs),
            len(t.find_all(lambda r: r["ev"] == "hook.GridCellArray.SetCenter.enter")),
            phases,
        ))
    out.append(table(["#", "t, s", "trigger", "from (kPre)", "to (kFinish)", "LoadingMenu, ms", "open→kPre",
                      "kPre→enter", "enter→kPost", "kPost→kFinish", "SetCurrentCell, ms", "unk4", "CellFullyLoaded",
                      "CellLoaderTask", "SetCenter", "phases"], rows))
    out.append("")

    if timeline:
        for n, t in enumerate(transitions, 1):
            out.append(f"### Transition {n} timeline\n")
            out.append("```")
            out.extend(timeline_lines(records, t))
            out.append("```\n")


def timeline_lines(records, t):
    kind, door = trigger_of(records, t)
    lines = []
    lo = door["seq"] if door else t.start["seq"]
    selected = [r for r in records if lo <= r["seq"] <= t.end["seq"]]
    noise = collections.Counter(r["ev"] for r in selected if is_noise(r) and r["ev"].endswith(".enter"))
    selected = [r for r in selected if not is_noise(r)]
    i = 0
    while i < len(selected):
        r = selected[i]
        j = i
        while j + 1 < len(selected) and selected[j + 1]["ev"] == r["ev"] and r["ev"] in (
                "event.CellFullyLoaded", "hook.CellLoaderTask.Run.enter", "hook.CellLoaderTask.Run.leave"):
            j += 1
        count = f" ×{j - i + 1}" if j > i else ""
        lines.append(f"{(r['t_us'] - t.t0) / 1000:+9.1f} ms  f{r['frame']:<6} {'M' if r['main'] else '-'} {r['tid']:>6}  "
                     f"{r['ev']}{count}  {short(r)}")
        i = j + 1
    if noise:
        lines.append("  noise: " + ", ".join(f"{ev.split('.')[2]}×{n}" for ev, n in noise.items()))
    return lines


def section_doors(records, out):
    out.append("## Player door activations (H1)\n")
    rows = []
    for i, r in enumerate(records):
        if r["ev"] != "hook.TESObjectDOOR.Activate.enter" or not by_player(r):
            continue
        leave = next((x for x in records[i + 1:] if x["ev"] == "hook.TESObjectDOOR.Activate.leave" and x["tid"] == r["tid"]), None)
        fader = next((x for x in records[i + 1:] if is_menu(x, "Fader Menu", True)), None)
        loading = next((x for x in records[i + 1:] if is_menu(x, LOADING, True)), None)
        pre = next((x for x in records[i + 1:] if is_phase(x, "kPre")), None)
        rows.append((
            f"{r['t_us'] / 1e6:.1f}",
            a(r).get("doorCell"),
            a(r).get("destCell") or "(exterior)",
            r["main"],
            a(leave).get("queuedValid") if leave else "",
            ms(fader["t_us"] - r["t_us"]) if fader else "",
            ms(loading["t_us"] - r["t_us"]) if loading else "",
            (pre["frame"] - r["frame"]) if pre else "",
            a(pre).get("target_valid") if pre else "",
        ))
    out.append(table(["t, s", "door cell", "dest cell", "main", "queuedValid at leave", "→Fader open, ms",
                      "→Loading open, ms", "frames →kPre", "kPre target_valid"], rows))
    out.append("")


def section_walking(records, transitions, out):
    out.append("## Exterior grid shifts outside loading screens (H3, H5)\n")
    covered = [(t.start["seq"], t.end["seq"]) for t in transitions]
    rows = []
    last_enter = None
    for i, r in enumerate(records):
        if r["ev"] == "event.ActorCell" and a(r).get("flag") == "enter" and "cell_xy" in a(r):
            last_enter = r
        if r["ev"] != "hook.GridCellArray.SetCenter.enter" or any(lo <= r["seq"] <= hi for lo, hi in covered):
            continue
        leave = next((x for x in records[i + 1:] if x["ev"] == "hook.GridCellArray.SetCenter.leave"), None)
        window = [x for x in records[i + 1:] if x["t_us"] - r["t_us"] <= WALK_WINDOW_US]
        nxt = next((k for k, x in enumerate(window) if x["ev"] == "hook.GridCellArray.SetCenter.enter"), None)
        if nxt is not None:
            window = window[:nxt]
        cfl = [x for x in window if x["ev"] == "event.CellFullyLoaded"]
        runs = [x for x in window if x["ev"] == "hook.CellLoaderTask.Run.enter"]
        rows.append((
            f"{r['t_us'] / 1e6:.1f}",
            a(r).get("world"),
            a(r).get("xy"),
            a(r).get("grid"),
            a(r).get("queued"),
            a(leave).get("result") if leave else "",
            a(leave).get("dur_us") if leave else "",
            ms(r["t_us"] - last_enter["t_us"]) if last_enter else "",
            a(last_enter).get("cell_xy") if last_enter else "",
            len(runs),
            len(cfl),
            ms(cfl[0]["t_us"] - r["t_us"]) if cfl else "",
            ms(cfl[-1]["t_us"] - r["t_us"]) if cfl else "",
            len({x["tid"] for x in cfl}),
        ))
    out.append(table(["t, s", "world", "SetCenter xy", "grid before", "queued before", "result", "dur, us",
                      "after cell enter, ms", "entered cell xy", "CellLoaderTask", "CellFullyLoaded", "first CFL, ms",
                      "last CFL, ms", "CFL threads"], rows))
    out.append("")


def section_tasks(records, out):
    out.append("## CellLoaderTask (H3, H7)\n")
    all_tasks = tasks(records)
    if not all_tasks:
        out.append("No CellLoaderTask calls.\n")
        return
    orders = collections.Counter("→".join(t["order"]) for t in all_tasks)
    out.append(f"- tasks: {len(all_tasks)}, interior=true: {sum(1 for t in all_tasks if t['args'].get('interior'))}")
    out.append("- call order: " + ", ".join(f"{k} ×{v}" for k, v in orders.most_common()))
    for method in ("Run", "Finish", "PostProcess", "Cancel", "GetDescription"):
        calls = [t["calls"][method] for t in all_tasks if method in t["calls"]]
        if calls:
            durs = [t["dur"][method] for t in all_tasks if t.get("dur", {}).get(method) is not None]
            out.append(f"- {method}: {len(calls)} calls, main thread {sum(1 for c in calls if c['main'])}, "
                       f"threads {len({c['tid'] for c in calls})}, "
                       f"dur total {ms(sum(durs))} ms, max {ms(max(durs)) if durs else ''} ms")
    out.append("")
    rows = []
    for t in all_tasks:
        run = t["calls"].get("Run")
        fin = t["calls"].get("Finish")
        post = t["calls"].get("PostProcess")
        rows.append((
            f"{run['t_us'] / 1e6:.3f}" if run else "",
            t["args"].get("world"),
            t["args"].get("cell"),
            t["args"].get("xy"),
            t["args"].get("interior"),
            t["args"].get("state"),
            f"{run['tid']}{'M' if run['main'] else ''}" if run else "",
            ms(t["dur"].get("Run")),
            f"{fin['tid']}{'M' if fin['main'] else ''}" if fin else "",
            ms(fin["t_us"] - run["t_us"]) if fin and run else "",
            f"{post['tid']}{'M' if post['main'] else ''}" if post else "",
            ms(post["t_us"] - run["t_us"]) if post and run else "",
        ))
    out.append(table(["Run t, s", "world", "cell", "xy", "interior", "state", "Run tid", "Run, ms", "Finish tid",
                      "Run→Finish, ms", "PostProcess tid", "Run→PostProcess, ms"], rows))
    out.append("")


def section_queue(records, out):
    out.append("## ExteriorCellLoader::QueueCellLoad (H5, H8)\n")
    calls = [r for r in records if r["ev"] == "hook.ExteriorCellLoader.QueueCellLoad.enter"]
    if not calls:
        out.append("No QueueCellLoad calls.\n")
        return
    unk = collections.Counter(a(r).get("unk") for r in calls)
    out.append(f"- calls: {len(calls)}, main thread {sum(1 for r in calls if r['main'])}, "
               f"unk: " + ", ".join(f"{k} ×{v}" for k, v in unk.items()))
    out.append("")
    out.append(table(["t, s", "main", "world", "xy", "unk", "grid", "queued", "player cell xy", "caller"],
                     [(f"{r['t_us'] / 1e6:.3f}", r["main"], a(r).get("world"), a(r).get("xy"), a(r).get("unk"),
                       a(r).get("grid", ""), a(r).get("queued", ""), a(r).get("playerCell_xy", ""), r.get("caller", ""))
                      for r in calls]))
    out.append("")


def section_snapshots(records, out):
    out.append("## Snapshots\n")
    snaps = [r for r in records if r["ev"] == "snapshot"]
    if not snaps:
        out.append("No snapshots.\n")
        return
    rows = []
    for r in snaps:
        cells = a(r).get("cells") or []
        states = collections.Counter(c.get("state", "null") for c in cells if isinstance(c, dict))
        rows.append((
            f"{r['t_us'] / 1e6:.3f}",
            a(r).get("reason"),
            a(r).get("interiorCell") or "",
            a(r).get("world") or "",
            a(r).get("grid"),
            a(r).get("queued"),
            a(r).get("parentCell"),
            a(r).get("playerPos"),
            a(r).get("queuedCellLoads"),
            ", ".join(f"{k.lstrip('k')} {v}" for k, v in states.most_common()),
        ))
    out.append(table(["t, s", "reason", "interiorCell", "world", "grid", "queued", "parentCell", "playerPos",
                      "queuedCellLoads", "grid cell states"], rows))
    out.append("")


def section_preload(records, out):
    out.append("## Preload experiment\n")
    init = next((r for r in records if r["ev"] == "preload.init"), None)
    if not init:
        out.append("Not active.\n")
        return
    out.append("- init: " + json.dumps(a(init), ensure_ascii=False))
    queues = [r for r in records if r["ev"] == "preload.queue"]
    out.append(f"- QueueCellLoad batches by the plugin: {len(queues)}, cells: {sum(len(a(r).get('xy') or []) for r in queues)}")
    out.append("")
    rows = []
    for r in records:
        if r["ev"] == "preload.queue":
            rows.append((f"{r['t_us'] / 1e6:.3f}", "queue", "", json.dumps(a(r).get("xy"))))
        elif r["ev"] == "preload.change":
            for c in a(r).get("cells") or []:
                rows.append((f"{r['t_us'] / 1e6:.3f}", a(r).get("where"), c.get("cell") or c.get("xy"),
                             f"{c.get('from')} → {c.get('to')}"))
    out.append(table(["t, s", "seen at", "cell", "state"], rows))
    out.append("")


def section_loading(records, out):
    out.append("## Loading screen breakdown and profile\n")
    summaries = [r for r in records if r["ev"] == "loading.summary"]
    if not summaries:
        out.append("No loading.summary records.\n")
        return
    keys = ("total_us", "openToPre_us", "cellLoad_us", "enterToPost_us", "postToFinish_us", "finishToClose_us")
    rows = []
    for r in summaries:
        x = a(r)
        rows.append((f"{r['t_us'] / 1e6:.1f}", *(ms(x.get(k)) if (x.get(k) or -1) >= 0 else "" for k in keys),
                     x.get("postToFinish_physicsSteps", ""),
                     f"{x['postToFinish_physicsSimSeconds']:.2f}" if "postToFinish_physicsSimSeconds" in x else "",
                     ms(x.get("postToFinish_physicsStep_us")) if "postToFinish_physicsStep_us" in x else "",
                     x.get("warmStart", ""), x.get("overrideSet", "")))
    out.append(table(["t, s", "total", "open→kPre", "kPre→enter (cell)", "enter→kPost", "kPost→kFinish", "kFinish→close",
                      "warm start steps", "simulated, s", "in steps, ms", "fWarmStartMaxTime", "override set"], rows))
    warm = [r for r in records if r["ev"] == "hook.TES.ProcessEvent.PositionPlayer.leave"]
    if warm:
        out.append("\nTES as PositionPlayerEvent sink (`hook.TES.ProcessEvent.PositionPlayer`):\n")
        enters = {r["seq"]: r for r in records if r["ev"] == "hook.TES.ProcessEvent.PositionPlayer.enter"}
        wrows = []
        for r in warm:
            enter = max((e for s, e in enters.items() if s < r["seq"]), key=lambda e: e["seq"], default=None)
            wrows.append((f"{r['t_us'] / 1e6:.3f}", a(enter).get("type") if enter else "", a(enter).get("warmStart") if enter else "",
                          ms(a(r).get("dur_us")), a(r).get("steps"), f"{a(r).get('simSeconds', 0):.2f}", ms(a(r).get("stepUs"))))
        out.append(table(["t, s", "event type", "fWarmStartMaxTime", "duration, ms", "physics steps", "simulated, s", "in steps, ms"], wrows))
    out.append("")
    for r in records:
        if r["ev"] != "profile":
            continue
        out.append(f"### Profile at {r['t_us'] / 1e6:.1f} s: {a(r).get('samples')} samples over {ms(a(r).get('window_us'))} ms\n")
        for phase in a(r).get("phases") or []:
            out.append(f"**{phase.get('phase')}** — {phase.get('samples')} samples")
            for name in ("leafModules", "self", "inclusive", "functions", "chains"):
                if phase.get(name):
                    out.append(f"- {name}: " + ", ".join(f"{k} ×{n}" for k, n in phase[name]))
            if phase.get("tree"):
                out.append("\n```")
                out.extend(tree_lines(phase["tree"], phase.get("samples") or 1))
                out.append("```")
            out.append("")


def section_visible(records, out):
    """What the player sees: door -> fade-out -> loading screen -> fade-in (Fader Menu close).

    Rebuilt from the menu events, so it also works for traces older than the transition.visible record.
    """
    out.append("## Door → picture (fades around the loading screen)\n")
    sets = [r for r in records if r["ev"] in ("overrides.init", "overrides.set")]
    for r in sets:
        if r["ev"] == "overrides.init":
            out.append("- override sets: " + "; ".join(f"{k}: {v}" for k, v in a(r).items() if k.startswith("set")))
        else:
            out.append(f"- {r['t_us'] / 1e6:.1f} s: set {a(r).get('set')} ({a(r).get('values')})")
    if sets:
        out.append("")
    # Long frames in the first seconds after the loading screen (autosave etc.), since 0.7.2
    hitches = [(r["t_us"] / 1e3, a(r).get("gap_ms"), a(r).get("afterLoading_ms")) for r in records if r["ev"] == "frame.hitch"]

    def hitches_after(close):
        return ", ".join(f"{gap} @+{after}" for t, gap, after in hitches if close <= t <= close + 5000)

    active = 0
    rows, cur, door, fade_out = [], None, None, None
    for r in records:
        ev, x, t = r["ev"], a(r), r["t_us"] / 1e3
        if ev == "overrides.set":
            active = x.get("set", 0)
        elif ev == "hook.TESObjectDOOR.Activate.leave" and x.get("result") and "queuedValid" in x:
            door, fade_out = t, None  # only the player's activations carry queuedValid
        elif ev == "event.Menu":
            menu, opening = x.get("menu"), x.get("opening")
            if menu == "Fader Menu" and opening and door and fade_out is None and cur is None:
                fade_out = t
            elif menu == "Loading Menu" and opening:
                cur = {"open": t, "set": active}
                if door and t - door < 10000:
                    cur["door"] = door
                    if fade_out:
                        cur["fadeOut"] = fade_out
                door, fade_out = None, None
            elif menu == "Loading Menu" and cur:
                cur["close"] = t
            elif menu == "Fader Menu" and not opening and cur and "close" in cur:
                cur["end"] = t
                g = lambda p, q: round(cur[q] - cur[p]) if p in cur and q in cur else ""
                rows.append((f"{cur['open'] / 1e3:.1f}", "door" if "door" in cur else "other",
                             g("door" if "door" in cur else "open", "end"),
                             g("door", "fadeOut"), g("fadeOut", "open"), g("open", "close"), g("close", "end"), cur["set"],
                             hitches_after(cur["close"])))
                cur = None
    if not rows:
        out.append("No complete transitions.\n")
        return
    out.append(table(["t, s", "start", "total, ms", "door→fade-out", "fade-out", "loading screen", "fade-in", "override set",
                      "long frames after, ms @+ms"], rows))
    out.append("")


def tree_lines(node, total, depth=0, max_depth=40):
    """[label, count, [children]] -> indented lines with share of the phase."""
    label, count, children = node
    lines = [f"{'  ' * depth}{100 * count / total:5.1f}%  {label}"]
    if depth < max_depth:
        for child in children:
            lines.extend(tree_lines(child, total, depth + 1, max_depth))
    return lines


def section_probe(records, out):
    out.append("## Simulation probe (H11, H12)\n")
    probes = [r for r in records if r["ev"] == "probe"]
    if not probes:
        out.append("No probe records.\n")
        return

    def dist(p, q):
        return sum((x - y) ** 2 for x, y in zip(p or (0, 0, 0), q or (0, 0, 0))) ** 0.5

    prev_actor = {}
    summary = collections.defaultdict(collections.Counter)
    rows = []
    prev_cell = None
    for p in probes:
        args = a(p)
        player_cell = args.get("playerCell")
        where = "outside" if args.get("playerCell_xy") else "inside"
        # Steps and moves are counted since the previous probe: a probe right after a transition
        # would credit this side with what happened on the other one.
        steady = prev_cell == player_cell and args.get("reason") != "afterTransition"
        prev_cell = player_cell
        stepped = {w[0]: w[1] for w in args.get("worlds") or []}  # hkpWorld -> steps since the previous probe
        category = {}
        for c in args.get("cells") or []:
            category[c.get("cell")] = c.get("cat")
            s = summary[(where, "cells: " + c.get("cat", "?"))]
            s["n"] += 1
            s["attached"] += c.get("state") == "kAttached"
            s["inScene"] += bool(c.get("inScene"))
            if c.get("hkpWorld"):
                s["physics"] += 1
                if steady:
                    s["physicsCompared"] += 1
                    s["physicsAdvanced"] += stepped.get(c["hkpWorld"], 0) > 0
        for act in args.get("actors") or []:
            cell = act.get("cell")
            cat = "player" if cell == player_cell else category.get(cell, "other")
            s = summary[(where, "actors: " + cat)]
            s["n"] += 1
            s[act.get("level", "?")] += 1
            s["3D"] += bool(act.get("3D"))
            old = prev_actor.get(act.get("actor"))
            if steady and old and old[0] == cell:
                s["compared"] += 1
                s["moved"] += dist(old[1], act.get("pos")) > 20
            prev_actor[act.get("actor")] = (cell, act.get("pos"))
        rows.append((f"{p['t_us'] / 1e6:.1f}", args.get("reason"), where, player_cell, args.get("highTotal"),
                     args.get("middleHighTotal"), args.get("middleLowTotal"), args.get("lowTotal"),
                     len(args.get("actors") or [])))
    out.append(table(["t, s", "reason", "player", "player cell", "high", "middleHigh", "middleLow", "low", "actors traced"], rows))
    out.append("\nSummary over all probes. `world stepped` and `moved` (position changed > 20 units in the same cell) "
               "count only steady intervals: the previous probe was in the same player cell and no transition in between.\n")
    srows = []
    for (where, what), s in sorted(summary.items()):
        if what.startswith("cells"):
            srows.append((where, what, s["n"], s["attached"], s["inScene"], s["physics"],
                          f"{s['physicsAdvanced']}/{s['physicsCompared']}", "", "", ""))
        else:
            levels = ", ".join(f"{k} {s[k]}" for k in ("high", "middleHigh", "middleLow", "low") if s[k])
            srows.append((where, what, s["n"], "", "", "", "", levels, s["3D"], f"{s['moved']}/{s['compared']}"))
    out.append(table(["player", "what", "n", "attached", "inScene", "with Havok world", "world stepped",
                      "AI levels", "3D loaded", "moved"], srows))
    worlds = collections.Counter()
    for p in probes:
        for w in a(p).get("worlds") or []:
            worlds[w[0]] += w[1]
    if worlds:
        out.append("\nHavok worlds stepped over the run: " + ", ".join(f"{k} ×{n}" for k, n in worlds.most_common()))
    out.append("")


def section_detectors(records, out):
    """Load doors and windows near the player (detector.doors / detector.windows, since 0.8.0)."""
    doors = [r for r in records if r["ev"] == "detector.doors"]
    windows = [r for r in records if r["ev"] == "detector.windows"]
    if not doors and not windows:
        return
    out.append("## Detectors\n")
    if doors:
        last = next((r for r in reversed(doors) if a(r).get("reason") == "hotkey"), doors[-1])
        x = a(last)
        out.append(f"- detector.doors records: {len(doors)}; shown: {x.get('reason')} at {last['t_us'] / 1e6:.1f} s, "
                   f"player in {x.get('playerCell')}, {x.get('count')} doors in the loaded cells\n")
        rows = []
        for d in (x.get("doors") or [])[:12]:
            ang = d.get("angleDeg") or [0, 0, 0]
            rows.append((d.get("ref"), d.get("base"), f"{d.get('dist', 0) / 70:.1f}", f"{ang[2]:.0f}",
                         f"{d['meshYawDeg']:.0f}" if "meshYawDeg" in d else "", d.get("destCell"), d.get("destState"),
                         d.get("automatic"), d.get("locked"), d.get("linked")))
        out.append(table(["door", "base", "dist, m", "angle z", "mesh yaw", "destination", "dest state", "auto", "locked", "linked door"], rows))
        out.append("")
    if windows:
        last = next((r for r in reversed(windows) if a(r).get("reason") == "hotkey"), windows[-1])
        x = a(last)
        out.append(f"- detector.windows records: {len(windows)}; shown: {x.get('reason')} at {last['t_us'] / 1e6:.1f} s: "
                   f"{x.get('windows')} windows from {x.get('refsCached')} references ({x.get('refsScanned')} scanned); "
                   f"window shapes {x.get('shapes')}: split into pieces {x.get('shapesSplit')}, bound only {x.get('shapesNoVertices')}\n")
        rows = []
        for w in (x.get("nearest") or [])[:12]:
            rows.append((w.get("ref"), w.get("index"), w.get("texture"), f"{w.get('dist', 0) / 70:.1f}", f"{w.get('size', 0):.0f}",
                         f"{w['facingYawDeg']:.0f}" if "facingYawDeg" in w else "", w.get("triangles"), w.get("piece")))
        out.append(table(["reference", "#", "texture", "dist, m", "size", "facing yaw", "triangles", "piece"], rows))
        out.append("")
        for sample in x.get("rawSamples") or []:
            out.append(f"- raw vertices of {sample.get('shape')} ({sample.get('texture')}): desc {sample.get('desc')}, stride {sample.get('stride')}, "
                       f"full precision {sample.get('fullPrecision')}, {sample.get('vertices')} vertices: `{sample.get('bytes')}`")
        if x.get("rawSamples"):
            out.append("")
    for r in (x for x in records if x["ev"] == "detector.look"):
        x = a(r)
        picked = (f"ray hit {x.get('hitRef')} at {x.get('hitDistance', 0) / 70:.1f} m, normal {x.get('hitNormal')}"
                  if x.get("picked") else "no ray hit, bounding spheres")
        out.append(f"- F6 look at {r['t_us'] / 1e6:.1f} s ({x.get('direction')}, camera·aim {x.get('cameraDotAim', 0):.2f}; {picked}):\n")
        rows = [(h.get("ref"), h.get("model"), h.get("shape"), h.get("texture"), f"{h.get('enter', 0) / 70:.1f}",
                 f"{h.get('radius', 0):.0f}", h.get("window")) for h in x.get("hits") or []]
        out.append(table(["reference", "model", "shape", "texture", "enter, m", "radius", "window"], rows))
        out.append("")


def section_interior_sim(records, out):
    """Keeping the interior loaded (preload.keep) and simulating it from outside (interior.sim, since 0.8.0)."""
    sims = [r for r in records if r["ev"] == "interior.sim"]
    keeps = [r for r in records if r["ev"] == "preload.keep"]
    if not sims and not keeps:
        return
    out.append("## Interior simulation\n")
    if keeps:
        out.append(f"- preload.keep (kept interior queued for loading): {len(keeps)} at "
                   + ", ".join(f"{r['t_us'] / 1e6:.1f} s" for r in keeps[:10]))
    rows = []
    for r in sims:
        x = a(r)
        what = x.get("blocker") or x.get("why") or x.get("name") or ""
        rows.append((f"{r['t_us'] / 1e6:.1f}", x.get("action"), x.get("stage", x.get("mode", "")), x.get("state", ""), what,
                     x.get("steps", ""), x.get("worldActiveBefore", ""),
                     f"{x.get('highActors', x.get('highAfter', ''))}/{x.get('actors', '')}" if "actors" in x else "",
                     x.get("promoted", x.get("promotedTotal", "")), x.get("renderMode", ""),
                     f"{x['frameMs']:.1f}" if "frameMs" in x else "",
                     x.get("attached", x.get("cellAttached", "")),
                     f"{x.get('cellUpdates')}/{x.get('graphUpdates')}" if "cellUpdates" in x else "",
                     x.get("gameInteriorStepsTotal", ""), x.get("updatesBusyTotal", "")))
    out.append(table(["t, s", "action", "stage/mode", "state", "blocker / why", "steps", "world active",
                      "NPC high", "promoted", "render", "frame, ms", "attached", "cell/graph updates", "game interior steps",
                      "updates busy"], rows))
    out.append("")
    # 0.8.16: the interior attached like a grid cell
    for r in (s for s in sims if a(s).get("action") in ("cellAttach", "cellDetach", "cellLost")):
        out.append(f"- {a(r).get('action')} at {r['t_us'] / 1e6:.1f} s: {json.dumps(a(r))}")
    for r in (s for s in sims if a(s).get("action") == "attach" and a(s).get("controls")):
        x = a(r)
        where = (lambda c: f"#{c['number']}") if "number" in x["controls"][0] else (lambda c: f"{c.get('heightMeters', 0):.1f} m")
        out.append(f"- attach at {r['t_us'] / 1e6:.1f} s, mode {x.get('mode')}{' (copy)' if x.get('copy') else ''}: "
                   f"{x.get('geometries')} geometries, hidden {x.get('hidden')} (doorway {x.get('doorwayHidden', 0)}), control doors: "
                   + "; ".join(f"{where(c)} {c.get('what')} ({c.get('parent')})" for c in x["controls"]))
    # 0.8.6: the portal (stage 6)
    for r in (s for s in sims if a(s).get("action") == "attach" and a(s).get("mode") in (6, "portal")):
        x = a(r)
        out.append(f"- portal attach at {r['t_us'] / 1e6:.1f} s: door rectangle {x.get('rectWidth', 0):.0f} x {x.get('rectHeight', 0):.0f} "
                   f"({x.get('rectPoints')} vertices) at {x.get('rectCenter')}, inward {x.get('inward')}, main camera frustum {x.get('mainFrustum')}, "
                   f"quad under {x.get('quadParent')}")
    portal_rows = []
    for r in sims:
        x = a(r)
        p = x.get("portal") if x.get("action") == "status" else None
        if p and (p.get("active") or p.get("passes") or p.get("skipped")):
            lights = p.get("lights") or []
            portal_rows.append((f"{r['t_us'] / 1e6:.1f}", p.get("passes"), p.get("skipped"), p.get("skipWhy"), f"{p.get('cpuMsAverage', 0):.2f}",
                                f"{p.get('cpuMsMax', 0):.2f}", f"{p.get('eyeDistance', 0) / 70:.1f}",
                                f"{p.get('viewportWidth', '')}x{p.get('viewportHeight', '')}" if "viewportWidth" in p else "",
                                f"{sum(1 for l in lights if not l.get('mirror'))}+{sum(1 for l in lights if l.get('mirror'))}",
                                ",".join(str(l.get("litShapes", "-")) for l in lights[:8]),
                                ",".join(f"{s.get('lights')}" + (f"/{s.get('passLights')}" if "passLights" in s else "") for s in p.get("sampledShapes") or []),
                                f"{p.get('actorsShown', '')}/{p.get('actors', '')}" if "actors" in p else "",
                                p.get("textureSwapped"), f"{x['frameMs']:.1f}" if "frameMs" in x else "",
                                f"{p.get('effectSetups', '')}/{p.get('softEffectSetups', '')}" if "effectSetups" in p else "", p.get("depthMode", ""),
                                f"{p.get('poseChanged', '')}/{p.get('poseChecks', '')}" if "poseChecks" in p else "",
                                f"{p.get('animatedByGame', '')}/{p.get('animated', '')}/{p.get('forcedAnimationUpdates', '')}" if "animatedByGame" in p else "",
                                f"{(p.get('controllers') or {}).get('mode', '')} {p.get('controllerMsAverage', 0):.2f}" if "controllers" in p else "",
                                f"{(p.get('controllers') or {}).get('particles', '')}" if "controllers" in p else "",
                                p.get("fullProcessingGranted", ""),
                                f"{p.get('softEffects', '')} {p.get('softEffectsDropped', '')}" if "softEffects" in p else "",
                                f"{(p.get('hiddenEffects') or {}).get('drawn', '')}/{(p.get('hiddenEffects') or {}).get('effects', '')}" if p.get("hiddenEffects") else "",
                                p.get("gameAnimationAdded", ""), p.get("pointsCarried", ""), p.get("fadesCarried", ""), p.get("actorLightsLent", ""),
                                p.get("maxPassesPerFrame", ""), p.get("quadMaterialOurs", ""),
                                f"{p['quadPreviousOffset']:.2f}" if "quadPreviousOffset" in p else "",
                                f"{p['emissionScale']:.2f}" if "emissionScale" in p else "", p.get("emissionsWithoutBudget", "")))
    if portal_rows:
        out.append("\nPortal passes per status:\n")
        out.append(table(["t, s", "passes", "skipped", "why", "CPU ms", "max", "eye, m", "viewport", "lights own+mirrored", "lit shapes per light",
                          "lights of sampled shapes (list/pass)", "NPC shown", "texture swapped", "frame, ms", "effects/soft", "depth mode",
                          "pose moved/checks", "anim game/portal/forced", "controllers mode ms", "particles", "full AI granted",
                          "soft effects / dropped", "effects drawn/all", "added to game animation", "points carried in", "fades carried in",
                          "actor lights lent", "max passes a frame", "quad material ours", "quad previous offset",
                          "emission scale", "emissions without budget"], portal_rows))
        # 0.8.10: what the culling made of the actors' shapes, and the nodes above them
        last = next((a(r)["portal"] for r in reversed(sims) if a(r).get("action") == "status" and (a(r).get("portal") or {}).get("actorStates")), None)
        if last:
            out.append("\nInterior actors (last status with actors):\n")
            rows = []
            for s in last["actorStates"]:
                shape = s.get("firstShape") or {}
                rows.append((s.get("name"), s.get("culled"), f"{s.get('currentFade', '')}", f"{s.get('culledNodes', '')}/{s.get('nodes', '')}",
                             " < ".join(f"{n.get('name') or '-'}({n.get('type')}){'[culled]' if n.get('culled') else ''}" for n in s.get("ancestors") or []),
                             shape.get("shape", ""), shape.get("alpha", ""), shape.get("passes", ""), shape.get("passLights", ""), shape.get("lastAccumulated", "")))
            out.append(table(["root", "culled", "currentFade", "culled nodes", "ancestors", "first shape", "alpha", "passes", "pass lights",
                              "last accumulated"], rows))
            flags = ", ".join(f"{s.get('name')} {s.get('flags')}" for s in last["actorStates"] if s.get("flags"))
            if flags:
                out.append(f"\nActor root flags (0x1000 kPreProcessedNode, 0x100000 kNotVisible): {flags}")
            if last.get("interiorPreprocessed"):
                out.append(f"\nInterior nodes preprocessed by the game's culling: {last['interiorPreprocessed']}")
            moves = ", ".join(f"{s.get('name')} processMe={s.get('processMe')} offset {s.get('rootOffset', -1):.0f}" for s in last["actorStates"] if "processMe" in s)
            movement = "; ".join(f"{s.get('actor') or s.get('name')} moves {s.get('movementUpdates')} controller {s.get('charController')} at {s.get('controllerPos')} "
                                 f"velocity {s.get('velocity')} package {s.get('package')} level {s.get('processLevel', '?')} pos {s.get('actorPos')}"
                                 + (f" lod {s.get('lodDistance', 0):.0f} state {s.get('controllerState')} fall {s.get('fallTime', 0):.2f} "
                                    f"interior world {s.get('controllerInInteriorWorld')} z {s.get('zMin', 0):.0f}..{s.get('zMax', 0):.0f}" if "lodDistance" in s else "")
                                 + (f" flags {s.get('controllerFlags')} supported {s.get('supportedState')} support normal z {s.get('supportNormalZ', 0):.2f}"
                                    if "controllerFlags" in s else "")
                                 for s in last["actorStates"] if "movementUpdates" in s)
            if movement:
                out.append(f"\nActor movement (last status): {movement}")
            # 0.8.18: NPCs stuck below a step or on stairs: the controller climbs (velocity z > 0, the controller
            # above the actor by more than its usual 64), the actor's height does not change
            stuck = []
            for r in sims:
                for s in (a(r).get("portal") or {}).get("actorStates") or [] if a(r).get("action") == "status" else []:
                    if "controllerPos" not in s or not s.get("charController"):
                        continue
                    offset = s["controllerPos"][2] - s["actorPos"][2]
                    if s["velocity"][2] > 15 or offset - 64 > 8:
                        stuck.append(f"{r['t_us'] / 1e6:.1f} s {s.get('actor') or s.get('name')} at {[round(v) for v in s['actorPos']]} "
                                     f"controller +{offset:.0f} velocity z {s['velocity'][2]:.0f} z {s.get('zMin', 0):.0f}..{s.get('zMax', 0):.0f}"
                                     + (f" flags {s['controllerFlags']} supported {s.get('supportedState')}" if "controllerFlags" in s else ""))
            if stuck:
                out.append("\nNPCs climbing in place (velocity z > 15 or the controller > 72 above the actor):\n")
                out.extend(f"- {line}" for line in stuck)
            if moves:
                out.append(f"\nActors: {moves}; animated by the portal {last.get('animated')}, by the game {last.get('animatedByGame', '-')}, "
                           f"forced full updates {last.get('forcedAnimationUpdates', '-')}, pose moved {last.get('poseChanged')}/{last.get('poseChecks', '-')}, "
                           f"positions synced {last.get('positionsSynced')}")
        last = next((a(r)["portal"] for r in reversed(sims) if a(r).get("action") == "status" and (a(r).get("portal") or {}).get("effects")), None)
        if last:
            fx = last["effects"]
            out.append(f"\nEffects (last status): {fx.get('count')} effect shapes, lit by our lights: {last.get('effectsLit')}, "
                       f"registrations with our lights held asleep: {last.get('effectLightsHeld')}\n")
            out.append(table(["shape", "type", "from hearth", "alpha", "material alpha", "fade", "base alpha", "lit", "passes", "pass lights",
                              "technique", "last accumulated", "culled", "parent flags", "particles", "last update"],
                             [(e.get("shape"), e.get("type"), f"{e.get('fromHearth', 0):.0f}", e.get("alpha"), e.get("materialAlpha"), e.get("fade"),
                               e.get("baseAlpha"), e.get("lit"), e.get("passes"), e.get("passLights"), e.get("technique"), e.get("lastAccumulated"),
                               e.get("culled"), e.get("parentFlags", ""), e.get("particles", ""), e.get("lastUpdate", "")) for e in fx.get("nearest") or []]))
            if last.get("controllers"):
                out.append(f"\nControllers (last status): {json.dumps(last['controllers'])}")
            watched = last.get("watchedEffects")
            if watched:
                out.append(f"\nWatched effect shapes (not hidden by a node) in the last status interval: {len(watched)}\n")
                out.append(table(["shape", "type", "model", "flags", "radius", "visits", "parent visits", "pass calls", "no passes", "last accumulated",
                                  "material alpha", "fade", "fade distance", "particles", "emit scale", "size scale", "parents"],
                                 [(e.get("shape"), e.get("type"), e.get("model"), e.get("flags"), f"{e.get('radius', 0):.1f}", e.get("visits"),
                                   e.get("parentVisits"), e.get("passCalls"), e.get("noPasses"), e.get("lastAccumulated"), e.get("materialAlpha"),
                                   e.get("fade"), f"{e['fadeDistance']:.0f}" if "fadeDistance" in e else "", e.get("particles", ""),
                                   e.get("emitScale", ""), e.get("sizeScale", ""), e.get("parents")) for e in watched]))
            # 0.8.20: the same flames through the portal (status) and from inside (probe)
            inside = next((a(r)["fires"] for r in reversed(records) if r["ev"] == "probe" and a(r).get("fires")), None)
            for title, fires in (("through the portal (last status)", last.get("fires")), ("from inside (last probe inside)", inside)):
                if not fires:
                    continue
                out.append(f"\nFlames {title}:\n")
                rows = []
                for model in fires:
                    for shape in model.get("shapes") or []:
                        first = shape.get("first") or []
                        rows.append((model.get("model"), shape.get("shape"), shape.get("type"), shape.get("hidden"), f"{shape.get('radius', 0):.0f}",
                                     shape.get("alpha", ""), shape.get("fade", ""), shape.get("lastAccumulated", ""), shape.get("particles", ""),
                                     shape.get("emitScale", ""), shape.get("sizeScale", ""),
                                     "; ".join(f"r {p.get('radius', 0):.1f} s {p.get('size', 0):.2f} a {p.get('alpha', 0):.2f} at {p.get('offset')}" for p in first),
                                     shape.get("texture", ""), shape.get("shaderFlags", "")))
                out.append(table(["model", "shape", "type", "hidden", "radius", "alpha", "fade", "last accumulated", "particles", "emit", "size",
                                  "first particles (radius, size, alpha, from the model)", "texture", "shader flags"], rows))
            # 0.8.21: master particle systems (candle flames): emitters of the interior, particles under the game's manager
            insideMasters = next((a(r)["masters"] for r in reversed(records) if r["ev"] == "probe" and a(r).get("masters")), None)
            for title, masters in (("through the portal (last status)", last.get("masters")), ("from inside (last probe inside)", insideMasters)):
                if not masters:
                    continue
                out.append(f"\nMaster particle systems {title}, manager under {masters.get('parents')}, culled {masters.get('culled')}:\n")
                out.append(table(["system", "texture", "index", "emitters", "active", "max", "per frame", "in the interior", "first emitter there",
                                  "particles", "first particle", "culled"],
                                 [(m.get("system"), m.get("texture"), m.get("index"), m.get("emitters"), m.get("activeEmitters"), m.get("maxEmitters"),
                                   m.get("maxParticlesPerFrame"), m.get("emittersInScene"), m.get("firstEmitterInScene"), m.get("particles"),
                                   m.get("firstParticle"), m.get("culled")) for m in masters.get("systems") or []]))
            hidden = last.get("hiddenEffects")
            if hidden:
                out.append(f"\nEffect shapes not drawn by the last pass: {hidden.get('effects', 0) - hidden.get('drawn', 0)} of {hidden.get('effects')} "
                           f"(never drawn {hidden.get('neverDrawn')}, latest frame {hidden.get('latestFrame')})\n")
                out.append(table(["shape", "type", "model", "last accumulated", "radius", "alpha", "material alpha", "fade", "hidden by", "depth",
                                  "parent flags", "particles"],
                                 [(e.get("shape"), e.get("type"), e.get("model"), e.get("lastAccumulated"), f"{e.get('radius', 0):.1f}", e.get("alpha"),
                                   e.get("materialAlpha"), e.get("fade"), e.get("hiddenBy"), e.get("depth"), e.get("parentFlags"), e.get("particles", ""))
                                  for e in hidden.get("notDrawn") or []]))
            sampled = last.get("sampledShapes") or []
            if sampled:
                out.append("\nSampled interior shapes: " + "; ".join(
                    f"{s.get('name')}: lights {s.get('lights')}, pass lights {s.get('passLights', '?')}, alpha {s.get('alpha', '?')}, "
                    f"last accumulated {s.get('lastAccumulated', '?')}" for s in sampled))
        last = next((a(r)["portal"] for r in reversed(sims) if a(r).get("action") == "status" and (a(r).get("portal") or {}).get("nearPlayerLights")), None)
        if last:
            out.append("\nLights near the player (last status): "
                       + "; ".join(f"{l.get('name')} {l.get('distance', 0) / 70:.1f} m dynamic={l.get('dynamic')} restricted={l.get('restricted')}"
                                   for l in last["nearPlayerLights"]))
    for r in (s for s in records if s["ev"] in ("window.focus", "portal.depthMode", "portal.effectLights", "portal.controllers", "portal.softEffects", "portal.taa")):
        out.append(f"- {r['ev']} at {r['t_us'] / 1e6:.1f} s: {json.dumps(a(r))}")
    for r in (s for s in records if s["ev"] in ("portal.init", "portal.start", "portal.dump")):
        out.append(f"- {r['ev']} at {r['t_us'] / 1e6:.1f} s: {json.dumps(a(r))[:1500]}")
    # 0.8.16: the interior's own lights once attached, and what the light sources' references hold
    for r in (s for s in records if s["ev"] == "portal.start"):
        x = a(r)
        if "originalLights" in x:
            out.append(f"\nportal.start at {r['t_us'] / 1e6:.1f} s: cell attached {x.get('cellAttached')}, the interior's own lights {json.dumps(x['originalLights'])}\n")
        if x.get("lightSourceRefs"):
            out.append(table(["model", "refs", "LIGH", "with 3D", "effect shapes", "lit shapes", "other shapes", "hidden shapes"],
                             [(m.get("model"), m.get("refs"), m.get("lights"), m.get("with3D"), m.get("effectShapes"), m.get("litShapes"),
                               m.get("otherShapes"), m.get("hiddenShapes")) for m in x["lightSourceRefs"]]))
            addons = [(m.get("model"), n) for m in x["lightSourceRefs"] for n in m.get("addonNodes") or []]
            if addons:
                out.append("\nAddon nodes (BSValueNode) of one reference per model:\n")
                out.append(table(["model", "node", "value", "master particle system", "children", "hidden"],
                                 [(m, n.get("name"), n.get("value"), n.get("masterParticleSystem"), n.get("children"), n.get("hidden")) for m, n in addons]))
            samples = [(m.get("model"), s) for m in x["lightSourceRefs"] for s in m.get("shapes") or []]
            if samples:
                out.append("\nShapes of one reference per model:\n")
                out.append(table(["model", "shape", "shader", "hidden", "alpha property", "own emit", "external emittance", "glow map", "emit",
                                  "emit mult", "alpha", "last accumulated"],
                                 [(m, s.get("shape"), s.get("shader"), s.get("hidden"), s.get("alphaProperty"), s.get("ownEmit", ""),
                                   s.get("externalEmittance", ""), s.get("glowMap", ""), s.get("emit", ""), s.get("emitMult", ""), s.get("alpha", ""),
                                   s.get("lastAccumulated", "")) for m, s in samples]))
    for r in sims:
        p = a(r).get("portal") if a(r).get("action") == "status" else None
        if p and "originalLights" in p:
            out.append(f"- {r['t_us'] / 1e6:.1f} s: cell attached {p.get('cellAttached')}, the interior's own lights {json.dumps(p['originalLights'])}")
    # 0.8.4: what stood behind the exterior door, and the culling counters of every watched object
    attach = next((r for r in reversed(sims) if a(r).get("action") == "attach" and a(r).get("doorway")), None)
    if attach:
        x = a(attach)
        out.append(f"\nAttach at {attach['t_us'] / 1e6:.1f} s: container under {x.get('parent')}")
        if x.get("parentChildren"):
            out.append(f"- that node's children before: {x['parentChildren']}")
        if "nearbyRefs" in x:
            out.append(f"- references searched near the door: {x['nearbyRefs']}; rays into the doorway hit (ref@ahead): {x.get('doorwayRayHits') or '-'}")
        out.append(f"\nDoorway (ahead < 0: behind the door plane, units; street {x.get('street')}):\n")
        rows = [(d.get("ref"), d.get("model"), d.get("shape"), d.get("texture"), f"{d.get('ahead', 0):.0f}", f"{d.get('side', 0):.0f}",
                 f"{d.get('up', 0):.0f}", f"{d.get('radius', 0):.0f}", d.get("hidden")) for d in x["doorway"]]
        out.append(table(["reference", "model", "shape", "texture", "ahead", "side", "up", "radius", "hidden"], rows))
    statuses = [r for r in sims if a(r).get("action") == "status" and a(r).get("visible")][:16]
    if statuses:
        names = []
        for r in statuses:
            for v in a(r)["visible"]:
                if v["what"] not in names:
                    names.append(v["what"])
        types = {v["what"]: v.get("type", "") for r in statuses for v in a(r)["visible"]}
        out.append("\nOnVisible calls per second (the culling process reached the object), per status:\n")
        rows = []
        for name in names:
            values = []
            for r in statuses:
                v = next((v for v in a(r)["visible"] if v["what"] == name), None)
                values.append(f"{v['perSecond']:.0f}" if v else "")
            rows.append((name, types.get(name, ""), *values))
        out.append(table(["object", "class", *(f"{r['t_us'] / 1e6:.0f} s" for r in statuses)], rows))
    attach = next((r for r in reversed(sims) if a(r).get("action") == "attach" and a(r).get("tree")), None)
    if attach:
        x = a(attach)
        out.append(f"Last attach at {attach['t_us'] / 1e6:.1f} s: parent {x.get('parent')}, {x.get('geometries')} geometries, "
                   f"unculled {x.get('unculled')}, fades raised {x.get('fadesRaised')}; references {x.get('refs')}, "
                   f"with 3D {x.get('refsWith3D')}, under the cell node {x.get('refs3DUnderCell')}, no parent {x.get('refs3DNoParent')}\n")
        rows = [(n.get("name"), n.get("type"), n.get("culled"), n.get("children"), n.get("nodes"), n.get("geometries"),
                 n.get("culledInside"), f"{n.get('fadeBelowOne')}/{n.get('fadeNodes')}", f"{n.get('boundRadius', 0):.0f}")
                for n in x.get("tree") or []]
        out.append(table(["cell node", "type", "culled", "children", "nodes", "geometries", "culled inside", "fade < 1", "bound radius"], rows))
        out.append("")


def section_callers(records, out):
    out.append("## Callers\n")
    callers = collections.defaultdict(collections.Counter)
    for r in records:
        if r["ev"].endswith(".enter") and r.get("caller"):
            callers[r["ev"][: -len(".enter")]][r["caller"]] += 1
    rows = [(ev, ", ".join(f"{c} ×{n}" for c, n in counter.most_common(6))) for ev, counter in sorted(callers.items())]
    out.append(table(["hook", "callers"], rows))
    out.append("")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("trace")
    parser.add_argument("-o", "--output", help="write Markdown here instead of stdout")
    parser.add_argument("--timeline", action="store_true", help="add a per-transition event timeline")
    options = parser.parse_args()

    records = load(options.trace)
    transitions = find_transitions(records)

    out = [f"# Trace report: {options.trace}\n"]
    section_run(records, out)
    section_transitions(records, transitions, out, options.timeline)
    section_loading(records, out)
    section_visible(records, out)
    section_doors(records, out)
    section_walking(records, transitions, out)
    section_queue(records, out)
    section_tasks(records, out)
    section_snapshots(records, out)
    section_preload(records, out)
    section_probe(records, out)
    section_detectors(records, out)
    section_interior_sim(records, out)
    section_threads(records, out)
    section_callers(records, out)

    text = "\n".join(out) + "\n"
    if options.output:
        with open(options.output, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    else:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
