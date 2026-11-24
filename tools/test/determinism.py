#!/usr/bin/env python3
"""Lockstep determinism: run the same games twice, state hashes must match at
every checkpoint.  tools/test/determinism.py --data ~/Netstorm [--bin build/netstorm]"""
import argparse, os, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("--data", required=True)
ap.add_argument("--bin", default="build/netstorm")
ap.add_argument("--seconds", type=int, default=300)
a = ap.parse_args()
env = dict(os.environ, SDL_VIDEODRIVER="offscreen")
cases = [["--mission", "skirmish"], ["--campaign", "rainvsrain"], ["--campaign", "guardmyback"]]
bad = 0
for case in cases:
    runs = []
    for i in range(2):
        out = subprocess.run([a.bin, "--data", a.data, *case, "--ai-both", "--simulate", str(a.seconds),
                              "--hash-every", "15", "--screenshot", os.path.join("/tmp" if os.name != "nt" else ".", "det.bmp"),
                              "--size", "320x240"], capture_output=True, text=True, env=env).stdout
        runs.append([l for l in out.splitlines() if l.startswith("HASH ")])
    same = runs[0] == runs[1] and len(runs[0]) > 0
    first_diff = next((i for i, (x, y) in enumerate(zip(*runs)) if x != y), None)
    print(f"{' '.join(case):<28} {'OK' if same else 'DIVERGED'}  {len(runs[0])} checkpoints"
          + ("" if same else f", first difference at {runs[0][first_diff] if first_diff is not None else 'length'}"))
    bad += not same
sys.exit(1 if bad else 0)
