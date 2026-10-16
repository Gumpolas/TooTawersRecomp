#!/usr/bin/env python3
"""Generate the recompiled game code from YOUR copy of The Two Towers.

    python generate.py <path to default.xbe, or the extracted disc folder>

Runs the xboxrecomp pipeline (xbe_parser -> disasm -> func_id -> abi_analysis -> names ->
recomp) on the game's executable and writes the C sources to
twotowers/src/recomp/gen/. Nothing from the game is in this repository; this
step needs the USA retail default.xbe (SHA-256 checked below).

Then build with CMake (see README.md).
"""
import hashlib
import os
import shutil
import subprocess
import sys

XBE_SHA256 = "ae2d1b5304b3e9572339b693a6a45ad0869bdaeec89aae374b5c14feebbb8d30"
ROOT = os.path.dirname(os.path.abspath(__file__))
TK = os.path.join(ROOT, "xboxrecomp-0.12.0")
GAME = os.path.join(ROOT, "twotowers")


def find_xbe(arg):
    if os.path.isdir(arg):
        for name in os.listdir(arg):
            if name.lower() == "default.xbe":
                return os.path.join(arg, name)
        sys.exit("No default.xbe in %s" % arg)
    if os.path.isfile(arg):
        return arg
    sys.exit("Not found: %s" % arg)


def run(args, quiet_filter=None):
    print("  >", " ".join(args[1:] if args[0] == sys.executable else args), flush=True)
    p = subprocess.run(args, cwd=TK, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    out = p.stdout.splitlines()
    shown = [l for l in out if quiet_filter is None or any(k in l for k in quiet_filter)]
    for l in (shown or out)[-4:]:
        print("    " + l)
    if p.returncode != 0:
        print("\n".join(out[-40:]))
        sys.exit("Step failed (exit %d)" % p.returncode)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    xbe = find_xbe(sys.argv[1])
    data = open(xbe, "rb").read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != XBE_SHA256:
        sys.exit("That default.xbe is not the USA retail release this port is built from\n"
                 "(SHA-256 %s, expected %s)." % (digest, XBE_SHA256))
    os.makedirs(os.path.join(TK, "game_files"), exist_ok=True)
    dst = os.path.join(TK, "game_files", "default.xbe")
    if os.path.abspath(xbe) != os.path.abspath(dst):
        shutil.copyfile(xbe, dst)
    print("default.xbe OK (USA retail). Generating code -- this takes several minutes.")
    py = sys.executable
    seeds = os.path.join(GAME, "config", "seed_functions.json")
    gen = os.path.join(GAME, "src", "recomp", "gen")
    run([py, "-m", "tools.xbe_parser", "game_files/default.xbe", "--json", "game_files/default_analysis.json"])
    run([py, "-m", "tools.disasm", "game_files/default.xbe", "--force", "--seed-functions", seeds])
    run([py, "-m", "tools.func_id", "game_files/default.xbe"])
    run([py, "-m", "tools.abi_analysis", "game_files/default.xbe"])
    if os.path.isdir(os.path.join(TK, "tools", "ghidra_naming", "export")):
        run([py, os.path.join("tools", "ghidra_naming", "merge_names.py"), "--apply"])
    shutil.rmtree(gen, ignore_errors=True)
    run([py, "-m", "tools.recomp", "game_files/default.xbe", "--all", "--split", "250", "--gen-dir", gen,
         "--exclude-manual", os.path.join(GAME, "src", "twotowers_manual.c")],
        quiet_filter=["functions (", "unresolved"])
    run([py, os.path.join(GAME, "postproc_apu.py"), gen], quiet_filter=["rewritten"])
    n = len([f for f in os.listdir(gen) if f.endswith(".c")])
    print("Done: %d source files in twotowers/src/recomp/gen. Now build with CMake (README.md)." % n)


if __name__ == "__main__":
    main()
