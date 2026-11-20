#!/usr/bin/env python3
"""Curator: refresh the read-only copy of the Ghidra project that gq.py/gserver.py read.

Close the Ghidra window (or at least save) first. Stops the query server, mirrors the master project
into <master>-ro without lock files, done; the next gq.py call starts the server on the new copy.

  python tools/ghidra/snapshot.py [--version 1.6.1170]
"""
import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gconfig  # noqa: E402
import gq  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", default=None)
    a = ap.parse_args()
    ver = gconfig.version(a.version)
    if gq.running():
        print("stopping the query server")
        gq.call({"cmd": "stop"})
        for _ in range(60):
            if not gq.running():
                break
            time.sleep(1)
        time.sleep(3)
    src, dst = ver["master_dir"], ver["ro_dir"]
    t0 = time.time()
    # robocopy: exit codes below 8 mean success
    rc = subprocess.run(["robocopy", src, dst, "/MIR", "/XF", "*.lock", "*.lock~", "tmp*.ps",
                         "/NFL", "/NDL", "/NJH", "/NJS", "/NP", "/R:2", "/W:2"]).returncode
    if rc >= 8:
        sys.exit(f"robocopy failed with code {rc}")
    print(f"copied {src} -> {dst} in {time.time() - t0:.0f} s")


if __name__ == "__main__":
    main()
