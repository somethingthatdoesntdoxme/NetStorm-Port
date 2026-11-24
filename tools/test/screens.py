#!/usr/bin/env python3
"""Render every menu screen plus a game view headless and check none of them
is blank or broken (needs Pillow).  tools/test/screens.py --data ~/Netstorm"""
import argparse, os, subprocess, sys, tempfile
from PIL import Image, ImageStat

ap = argparse.ArgumentParser()
ap.add_argument("--data", required=True)
ap.add_argument("--bin", default="build/netstorm")
ap.add_argument("--out", default="test-results/screens")
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)
env = dict(os.environ, SDL_VIDEODRIVER="offscreen")
shots = {
    "title": ["--menu", "title"], "chapters": ["--menu", "chapters"], "missions": ["--menu", "missions"],
    "briefing": ["--menu", "brief"],
    "skirmish_setup": ["--menu", "skirmish"], "lan_browser": ["--menu", "lan"],
    "game_skirmish": ["--skirmish", "--ai-both", "--simulate", "300"], "game_thewarbegins": ["--campaign", "thewarbegins"],
    "game_note": ["--campaign", "thunderingpower", "--simulate", "130", "--stop-at-note"],
    "game_gift": ["--mission", "skirmish", "--test-gift"],
    "game_air": ["--campaign", "thewarbegins", "--ai-both", "--simulate", "900", "--stop-at-air", "2"],
    "sprites": ["--sprites"],
}
bad = 0
for name, args in shots.items():
    path = os.path.join(a.out, name + ".bmp")
    p = subprocess.run([a.bin, "--data", a.data, *args, "--screenshot", path, "--size", "1280x720"],
                       capture_output=True, text=True, env=env)
    ok = os.path.exists(path)
    detail = f"exit {p.returncode}"
    if ok:
        im = Image.open(path).convert("RGB")
        st = ImageStat.Stat(im)
        spread = sum(st.stddev) / 3
        colours = len(im.getcolors(1 << 20) or [])
        # The sprite viewer shows small frames on a plain background, so it gets lower bars.
        min_spread, min_colours = (4, 24) if name == "sprites" else (12, 64)
        ok = spread > min_spread and colours > min_colours and im.size == (1280, 720)
        detail = f"{im.size[0]}x{im.size[1]}, {colours} colours, contrast {spread:.0f}"
        im.save(os.path.join(a.out, name + ".png"))
        os.remove(path)
    print(f"{name:<20} {'OK' if ok else 'BROKEN'}  {detail}")
    bad += not ok
sys.exit(1 if bad else 0)
