#!/usr/bin/env python3
"""Automated LAN lockstep tests: real host + client processes on this machine,
each played by the command-only autopilot. Every peer must end with the same
state hash, with no invariant violations or desyncs.

  tools/test/lan.py --data ~/Netstorm [--bin build/netstorm] [--seconds 300]
"""
import argparse, json, os, re, subprocess, sys, time

ap = argparse.ArgumentParser()
ap.add_argument("--data", required=True)
ap.add_argument("--bin", default="build/netstorm")
ap.add_argument("--seconds", type=int, default=240)
a = ap.parse_args()
env = dict(os.environ, SDL_VIDEODRIVER="offscreen")
tmp = "/tmp" if os.name != "nt" else os.environ.get("TEMP", ".")


def peer(args, tag, port, seconds):
    return subprocess.Popen([a.bin, "--data", a.data, *args, "--lan-port", str(port), "--name", tag, "--autopilot",
                             "--simulate", str(seconds), "--validate", "--json", "--hash-every", "30",
                             "--screenshot", os.path.join(tmp, f"lan_{tag}.bmp"), "--size", "320x240"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)


def parse(out):
    res = next((json.loads(l[7:]) for l in out.splitlines() if l.startswith("RESULT ")), None)
    hashes = [l for l in out.splitlines() if l.startswith("HASH ")]
    lan = [l for l in out.splitlines() if l.startswith("LAN:")]
    return res, hashes, lan


def case(name, layout, clients, port, kill_after=None, seconds=None):
    seconds = seconds or a.seconds
    host = peer(["--host", layout], "host", port, seconds)
    time.sleep(0.7)
    cs = [peer(["--join", "127.0.0.1"], f"c{i}", port, seconds) for i in range(clients)]
    if kill_after is not None:
        time.sleep(kill_after)
        cs[-1].kill()  # a player's machine dies mid-battle
    outs = [p.communicate(timeout=seconds * 3 + 180)[0] for p in [host] + cs]
    parsed = [parse(o) for o in outs]
    live = parsed[:-1] if kill_after is not None else parsed
    ok = all(r is not None for r, _, _ in live)
    msgs = []
    if ok:
        finals = {r["hash"] for r, _, _ in live}
        ok &= len(finals) == 1
        ok &= all(r["violations"] == 0 for r, _, _ in live)
        ok &= all(h == live[0][1] for _, h, _ in live)
        if len(finals) != 1:
            msgs.append(f"final hashes differ: {finals}")
        if kill_after is not None:
            surrendered = [k for k, v in live[0][0]["players"].items() if v["out"]]
            ok &= bool(surrendered)
            msgs.append(f"left the battle: {surrendered or 'nobody (expected the killed client)'}")
    for (r, h, lan), who in zip(parsed, ["host"] + [f"c{i}" for i in range(clients)]):
        bad = [l for l in lan if "DESYNC" in l or "timed out" in l]
        if bad and not (kill_after is not None and who == f"c{clients - 1}"):
            ok = False
            msgs += [f"{who}: {l}" for l in bad]
    print(f"{name:<34} {'OK' if ok else 'FAIL'}  " + (f"{len(live[0][1])} matching checkpoints" if ok else ""),
          *msgs, sep="\n    " if msgs else "")
    if not ok:
        for o, who in zip(outs, ["host"] + [f"c{i}" for i in range(clients)]):
            print(f"--- {who} (tail)\n" + "\n".join(o.splitlines()[-8:]))
    return ok


results = [
    case("2 humans + 1 AI", "HHA", 1, 27310),
    case("3 humans", "HHH", 2, 27320),
    # Headless games run much faster than real time, so kill early in a long game.
    case("client drops mid-battle", "HHHA", 2, 27330, kill_after=1.5, seconds=900),
]
sys.exit(0 if all(results) else 1)
