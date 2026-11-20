#!/usr/bin/env python3
"""Short digest of one run folder: python tools/run_digest.py <run folder> [--full]

Reads CellLoadResearch.trace.jsonl (and .log, *.hang.txt) and writes <folder>/digest.md (UTF-8).
Only the path is printed: Cyrillic breaks the console. Every section is cut to LIMIT lines unless --full.
"""
import bisect
import collections
import glob
import json
import os
import re
import sys

LIMIT = 40
FULL = '--full' in sys.argv


def load_trace(path):
    out = []
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                r = json.loads(line)
            except ValueError:
                continue
            if isinstance(r, dict) and 'ev' in r:
                if not isinstance(r.get('args'), dict):
                    r['args'] = {}
                r.setdefault('t_us', 0)
                out.append(r)
    return out


def flat(obj, prefix=''):
    """Scalar leaves of nested dicts by dotted name (lists are skipped)."""
    for k, v in obj.items():
        if isinstance(v, dict):
            yield from flat(v, prefix + k + '.')
        elif not isinstance(v, list):
            yield prefix + k, v


def ts(t_us):
    return f'{t_us / 1e6:.2f}s'


def cell_name(s):
    return s.split(' [')[0] if s else '?'


class Doc:
    def __init__(self, full):
        self.full = full
        self.lines = []

    def section(self, title, rows, note=''):
        self.lines.append(f'## {title}')
        if note:
            self.lines.append(note)
        if not rows:
            self.lines.append('(нет)')
        else:
            shown = rows if self.full else rows[:LIMIT]
            self.lines.extend(shown)
            if len(shown) < len(rows):
                self.lines.append(f'… ещё {len(rows) - len(shown)} строк, см. --full')
        self.lines.append('')


def transitions(rs):
    """A transition is an event.ActorCell 'enter'. Durations come from the records that follow it."""
    ev = [r for r in rs if r['ev'] == 'event.ActorCell']
    enters = []
    last_leave = None
    for r in ev:
        a = r['args']
        if a.get('flag') == 'leave':
            last_leave = a.get('cell')
        elif a.get('flag') == 'enter':
            enters.append({'t': r['t_us'], 'from': last_leave, 'to': a.get('cell'), 'summary': None, 'visible': None})
            last_leave = None
    for kind, key in (('loading.summary', 'summary'), ('transition.visible', 'visible')):
        for r in rs:
            if r['ev'] != kind:
                continue
            # the latest enter that is not after this record
            i = bisect.bisect_right([e['t'] for e in enters], r['t_us']) - 1
            if i >= 0 and enters[i][key] is None:
                enters[i][key] = r['args']
    return enters


def sec_transitions(rs, trs):
    rows = ['| время | откуда → куда | дверь | загрузка, мс | всего до видимости, мс |', '|---|---|---|---|---|']
    for e in trs:
        s, v = e['summary'], e['visible']
        load = f"{s['total_us'] / 1000:.0f}" if s else '-'
        tot = f"{v['total_ms']}" if v else '-'
        door = ('да' if v.get('door') else 'нет') if v else '-'
        rows.append(f"| {ts(e['t'])} | {cell_name(e['from']) if e['from'] else '(старт)'} → {cell_name(e['to'])} | {door} | {load} | {tot} |")
    return rows


def sec_hotkeys(rs):
    return [f"- {ts(r['t_us'])} {r['args'].get('action')}" for r in rs if r['ev'] == 'input.hotkey']


KEYS = ('portal.passes', 'portal.soundsCarried', 'portal.subtitlesCarried', 'portal.distancesCarried',
        'portal.imageSpace.swaps', 'portal.imageSpace.held', 'blocker')


def sec_portal(rs, trs):
    rows = []
    statuses = [r for r in rs if r['ev'] == 'interior.sim' and r['args'].get('action') == 'status']
    st_t = [r['t_us'] for r in statuses]
    for r in rs:
        a = r['args']
        if r['ev'] == 'portal.start':
            rows.append(f"- {ts(r['t_us'])} portal.start")
        elif r['ev'] == 'interior.sim' and a.get('action') == 'detach':
            rows.append(f"- {ts(r['t_us'])} detach why={a.get('why')} mode={a.get('mode')} restored={a.get('restored')}")
        elif r['ev'] == 'portal.fault':
            rows.append(f"- {ts(r['t_us'])} **portal.fault** {json.dumps(a, ensure_ascii=False)[:200]}")
    rows.append('')
    rows.append('Последний status перед каждым переходом:')
    for e in trs:
        i = bisect.bisect_left(st_t, e['t']) - 1
        if i < 0:
            continue
        d = dict(flat(statuses[i]['args']))
        vals = ' '.join(f"{k.split('.', 1)[-1] if k != 'blocker' else k}={d[k]!r}" for k in KEYS if k in d)
        rows.append(f"- перед {ts(e['t'])} ({cell_name(e['to'])}), status {ts(statuses[i]['t_us'])}: {vals}")
    return rows


def stack_sig(stack):
    frames = [f for f in (stack or []) if not f.startswith('CellLoadResearch.dll')]
    return ' < '.join(frames[:3]) or '(нет стека)'


def sec_jumps(rs, trs):
    jumps = [r for r in rs if r['ev'] == 'actor.jump']
    if not jumps:
        return [], []
    by = collections.defaultdict(lambda: [0, 0.0, 0.0])
    for r in jumps:
        a = r['args']
        s = by[a.get('name') or a.get('actor')]
        d = float(a.get('distance') or 0)
        s[0] += 1
        s[1] += d
        s[2] = max(s[2], d)
    rows = ['| актёр | число | сумма | максимум |', '|---|---|---|---|']
    for n, (c, sm, mx) in sorted(by.items(), key=lambda kv: -kv[1][1]):
        rows.append(f'| {n} | {c} | {sm:.0f} | {mx:.0f} |')

    sigs = collections.defaultdict(lambda: [0, 0.0])
    for r in jumps:
        s = sigs[stack_sig(r['args'].get('stack'))]
        s[0] += 1
        s[1] += float(r['args'].get('distance') or 0)
    rows += ['', 'По подписи стека (3 кадра без CellLoadResearch.dll):']
    for sig, (c, sm) in sorted(sigs.items(), key=lambda kv: -kv[1][1]):
        rows.append(f'- {c} шт., сумма {sm:.0f}: {sig}')

    et = [e['t'] for e in trs]
    each = ['| время | актёр | kind | расстояние | относительно перехода |', '|---|---|---|---|---|']
    for r in jumps:
        a = r['args']
        i = bisect.bisect_right(et, r['t_us']) - 1
        cands = []
        if i >= 0:
            cands.append((r['t_us'] - et[i], 'после входа в', trs[i]['to']))
        if i + 1 < len(et):
            cands.append((et[i + 1] - r['t_us'], 'до входа в', trs[i + 1]['to']))
        rel = '-'
        if cands:
            dt, word, cell = min(cands, key=lambda c: c[0])
            rel = f'{dt / 1e6:.1f} с {word} {cell_name(cell)}'
        each.append(f"| {ts(r['t_us'])} | {a.get('name') or a.get('actor')} | {a.get('kind')} | {float(a.get('distance') or 0):.0f} | {rel} |")
    return rows, each


def sec_scenes(rs, trs):
    et = [e['t'] for e in trs]
    by = collections.OrderedDict()
    for r in rs:
        if r['ev'] != 'scene.change':
            continue
        a = r['args']
        q = by.setdefault(a.get('quest') or a.get('scene'), {'n': 0, 'first': r['t_us'], 'last': r['t_us'], 'play': 0})
        q['n'] += 1
        q['last'] = r['t_us']
        q['play'] += 1 if a.get('playing') else 0
    rows = []
    for name, q in by.items():
        i = bisect.bisect_right(et, q['first']) - 1
        rel = f"{(q['first'] - et[i]) / 1e6:+.1f} с от входа в {cell_name(trs[i]['to'])}" if i >= 0 else 'до первого перехода'
        rows.append(f"- {name}: {q['n']} изм. (playing={q['play']}), {ts(q['first'])}..{ts(q['last'])}, первое {rel}")
    return rows


def sec_faults(rs, folder):
    rows = []
    hitches = [r for r in rs if r['ev'] == 'frame.hitch']
    if hitches:
        worst = max(hitches, key=lambda r: r['args'].get('gap_ms', 0))
        rows.append(f"- frame.hitch: {len(hitches)} шт., худший {worst['args'].get('gap_ms')} мс на {ts(worst['t_us'])}")
        for r in hitches if FULL else ():
            rows.append(f"  - {ts(r['t_us'])} gap={r['args'].get('gap_ms')} мс afterLoading={r['args'].get('afterLoading_ms')} мс")
    for r in rs:
        if r['ev'] == 'portal.fault':
            rows.append(f"- {ts(r['t_us'])} portal.fault {json.dumps(r['args'], ensure_ascii=False)[:200]}")
    log = os.path.join(folder, 'CellLoadResearch.log')
    if os.path.exists(log):
        pat = re.compile(r'\[(error|warning|[EW])\]')
        with open(log, encoding='utf-8', errors='replace') as f:
            for line in f:
                if pat.search(line):
                    rows.append('- log: ' + line.strip()[:200])
    for h in glob.glob(os.path.join(folder, '*.hang.txt')):
        rows.append(f'- **зависание**: {os.path.basename(h)}')
    return rows


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args:
        print(__doc__)
        return 2
    folder = args[0]
    trace = os.path.join(folder, 'CellLoadResearch.trace.jsonl')
    if not os.path.exists(trace):
        print(f'нет {trace}')
        return 1
    rs = load_trace(trace)
    doc = Doc('--full' in sys.argv)
    trs = transitions(rs)
    info = next((r['args'] for r in rs if r['ev'] == 'info.plugin'), {})
    doc.lines.append(f"# Сводка прогона {os.path.basename(os.path.abspath(folder))}")
    doc.lines.append(f"Версия {info.get('version', '?')}, runtime {info.get('runtime', '?')}"
                     + (f", ветка {info['branch']}, коммит {info['commit']}" if info.get('branch') else '')
                     + f", записей {len(rs)}, длительность {rs[-1]['t_us'] / 1e6:.0f} с" if rs else '')
    doc.lines.append('')
    doc.section('Переходы', sec_transitions(rs, trs))
    doc.section('Клавиши', sec_hotkeys(rs))
    doc.section('Портал', sec_portal(rs, trs))
    by_actor, each = sec_jumps(rs, trs)
    doc.section('actor.jump: по актёрам и стекам', by_actor)
    doc.section('actor.jump: каждый прыжок', each)
    doc.section('scene.change', sec_scenes(rs, trs))
    doc.section('Сбои', sec_faults(rs, folder))
    out = os.path.join(folder, 'digest.md')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(doc.lines) + '\n')
    print(os.path.abspath(out))
    return 0


if __name__ == '__main__':
    sys.exit(main())
