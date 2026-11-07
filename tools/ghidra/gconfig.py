"""Paths and per-version settings for the Ghidra tools (tools/ghidra/).

Every path can be overridden by an environment variable of the same name (CLR_GHIDRA_DIR, ...).
Machine-specific defaults live here so that executor worktrees need no local setup.
"""
import os
import pathlib
import subprocess
import tempfile


def _env(name, default):
    return os.environ.get(name, default)


GHIDRA_DIR = _env("CLR_GHIDRA_DIR", r"E:\Ghidra\ghidra_12.2_PUBLIC")
JAVA_HOME = _env("CLR_GHIDRA_JAVA", r"D:\Java")  # JDK 21+; Ghidra itself remembers it in java_home.save
VENV_PYTHON = _env("CLR_GHIDRA_PYTHON", r"E:\Ghidra\pyghidra-venv\Scripts\python.exe")
GAME_DIR = _env("CLR_GAME_DIR", r"E:\Games\Skyrim\The Elder Scrolls V - Skyrim")
PROJECTS_DIR = _env("CLR_GHIDRA_PROJECTS", r"E:\Games\Skyrim\ModCreation\My attempts")
SERVER_HOST = "127.0.0.1"
SERVER_PORT = int(_env("CLR_GHIDRA_PORT", "47333"))
SERVER_HEAP = _env("CLR_GHIDRA_HEAP", "4G")
SERVER_IDLE_MINUTES = float(_env("CLR_GHIDRA_IDLE_MINUTES", "20"))
SERVER_DECOMPILERS = int(_env("CLR_GHIDRA_DECOMPILERS", "3"))  # decompile.exe processes for parallel requests
STATE_DIR = pathlib.Path(_env("CLR_GHIDRA_STATE", os.path.join(tempfile.gettempdir(), "clr-ghidra")))

# One entry per game build. "runtime" picks the SE or AE half of RELOCATION_ID(se, ae) and the label prefix.
VERSIONS = {
    "1.6.1170": {
        "runtime": "ae",
        "exe": os.path.join(GAME_DIR, "SkyrimSE.exe"),
        "addrlib": os.path.join(GAME_DIR, r"Data\SKSE\Plugins\versionlib-1-6-1170-0.bin"),
        "project": "SkyrimSE-1.6.1170",
        "program": "/SkyrimSE.exe",
    },
}
DEFAULT_VERSION = _env("CLR_GHIDRA_VERSION", "1.6.1170")


def version(name=None):
    name = name or DEFAULT_VERSION
    if name not in VERSIONS:
        raise SystemExit(f"unknown game version {name}; known: {', '.join(VERSIONS)}")
    v = dict(VERSIONS[name])
    v["name"] = name
    v["prefix"] = v["runtime"].upper()
    # Master project: the curator edits it. Read-only copy: the query server opens it.
    v["master_dir"] = os.path.join(PROJECTS_DIR, v["project"])
    v["ro_dir"] = os.path.join(PROJECTS_DIR, v["project"] + "-ro")
    return v


def main_checkout():
    """Main checkout of the repository (worktrees share it); it holds lib/commonlibsse-ng."""
    here = pathlib.Path(__file__).resolve().parent
    try:
        common = subprocess.run(["git", "-C", str(here), "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                capture_output=True, text=True, check=True).stdout.strip()
        return pathlib.Path(common).parent
    except (OSError, subprocess.CalledProcessError):
        return here.parent.parent


def ng_dir():
    return pathlib.Path(_env("CLR_NG_DIR", str(main_checkout() / "lib" / "commonlibsse-ng")))


def repo_dir():
    return pathlib.Path(__file__).resolve().parent.parent.parent
