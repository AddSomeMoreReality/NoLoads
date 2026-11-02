# usage: distance_report.py <player_access.json> <out.md>
# Turns the result of scan_player_access.py into a Markdown table with the names the executable itself gives:
# vtable slots, strings in the function and direct callers.
import json, sys
sys.path.insert(0, r"E:\Games\Skyrim\ModCreation\My attempts\NoLoads\CellLoadResearch\tools")
import name_functions as nf

EXE = r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\SkyrimSE.exe"
_, id2rva = nf.load_idb(r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin")
rva2id = {v: k for k, v in id2rva.items()}
pe = nf.PE(EXE)
slots, _ = nf.scan_vtables(pe)
data = json.load(open(sys.argv[1], encoding="utf-8"))
rvas = {f["rva"] for f in data["functions"]} | {f["rva"] for f in data["indirect"]}
calls = nf.build_call_index(pe, rvas)


def describe(rva):
    names = [n.replace("::vfunc", " ") for n in slots.get(rva, [])][:2]
    bounds = pe.function_at(rva)
    strings = [s for s in nf.strings_in(pe, bounds[0], bounds[1]) if len(s) > 3][:3] if bounds else []
    callers = []
    for site in calls.get(rva, [])[:40]:
        f = pe.function_at(site)
        if f and f[0] in rva2id:
            label = f"AE#{rva2id[f[0]]}"
            if label not in callers:
                callers.append(label)
    return names, strings, callers[:4]


def row(f, how):
    names, strings, callers = describe(f["rva"])
    text = ", ".join(names + [f'"{s[:36]}"' for s in strings])
    return f"| {f['fn']} | {how} | {text} | {' '.join(callers)} |"


out = [
    "# Приложение: обращения к позиции игрока и камеры",
    "",
    "Файл создан `tools/re/distance_report.py` из результата `tools/re/scan_player_access.py` для 1.6.1170, руками не правится. Разбор и статус — в `docs/DISTANCE_CHECKS.md`.",
    "",
    f"Прямых читателей: {len(data['functions'])}. Читателей через вспомогательные функции: {len(data['indirect'])}.",
    "",
    "`дист.` — после чтения идут вычитание, квадрат или корень и сравнение, то есть это почти наверняка проверка расстояния. `Что это`: слоты таблиц виртуальных функций и строки из самой функции.",
    "",
    "## Прямые чтения",
    "",
    "| Функция | Что читает | Что это | Кто вызывает |",
    "|---|---|---|---|",
]
for f in sorted(data["functions"], key=lambda x: (-x["distanceSites"], x["rva"])):
    kinds = ", ".join(f"{k} ×{v}" for k, v in f["kinds"].items())
    how = f"{kinds}{', дист.' if f['distanceSites'] else ''}"
    out.append(row(f, how))
out += ["", "## Чтения через вспомогательные функции", "", "| Функция | Через что | Что это | Кто вызывает |", "|---|---|---|---|"]
for f in sorted(data["indirect"], key=lambda x: x["rva"]):
    helpers = sorted({c["helper"].split("+")[0] for c in f["calls"]})
    out.append(row(f, " ".join(helpers[:3])))
out += ["", "## Вспомогательные функции", "", "| Функция | Аргумент с позицией | Мест, где туда передан игрок |", "|---|---:|---:|"]
for h in sorted(data["helpers"], key=lambda x: -x["callersWithPlayer"]):
    out.append(f"| {h['fn']} | {h['arg'] + 1} | {h['callersWithPlayer']} |")
open(sys.argv[2], "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")
print("rows", len(data["functions"]), len(data["indirect"]), len(data["helpers"]))
