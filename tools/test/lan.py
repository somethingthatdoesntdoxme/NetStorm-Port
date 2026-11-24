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
    return subprocess.Popen([a.bin, "--data", a.data, *args, *(["--lan-port", str(port)] if port else []),
                             "--name", tag, "--autopilot",
                             "--simulate", str(seconds), "--validate", "--json", "--hash-every", "30",
                             "--screenshot", os.path.join(tmp, f"lan_{tag}.bmp"), "--size", "320x240"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)


def parse(out):
    res = next((json.loads(l[7:]) for l in out.splitlines() if l.startswith("RESULT ")), None)
    # tick -> hash. A rejoining peer replays from tick 0 and prints some ticks twice;
    # every printing of a tick must agree, so keep them all and check consistency.
    hashes = {}
    for l in out.splitlines():
        if l.startswith("HASH "):
            _, tick, h = l.split()
            if hashes.get(tick, h) != h:
                hashes[tick] = "INCONSISTENT"
            else:
                hashes[tick] = h
    lan = [l for l in out.splitlines() if l.startswith("LAN:")]
    return res, hashes, lan


def case(name, layout, clients, port, kill_after=None, seconds=None, extra_last=(), expect_chat=None):
    seconds = seconds or a.seconds
    host = peer(["--host", layout], "host", port, seconds)
    time.sleep(0.7)
    cs = [peer(["--join", "127.0.0.1", *(extra_last if i == clients - 1 else ())], f"c{i}", port, seconds)
          for i in range(clients)]
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
            # The killed player's seat is kept (they could reconnect) until the rejoin window ends.
            msgs.append("killed client's army idles while its seat is kept")
    if expect_chat:
        for o, who in zip(outs, ["host"] + [f"c{i}" for i in range(clients)]):
            if expect_chat not in o:
                ok = False
                msgs.append(f"{who} never got the chat line")
    if "--drop-at" in extra_last:
        rejoined = "rejoined" in outs[-1]
        ok &= rejoined
        msgs.append("dropped client rejoined and replayed" if rejoined else "dropped client did NOT rejoin")
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


def relay_case(name, port):
    """Host and client meet through a netstorm-relay (no port forwarding); the client
    also loses its connection mid-battle and must rejoin the room and replay."""
    relay_bin = os.path.join(os.path.dirname(a.bin), "netstorm-relay" + (".exe" if os.name == "nt" else ""))
    relay = subprocess.Popen([relay_bin, "--port", str(port)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             text=True)
    time.sleep(0.5)
    host = peer(["--host", "HHA", "--relay", "127.0.0.1", "--relay-port", str(port), "--say", "relay works"],
                "host", 0, a.seconds)
    code, lines = None, []
    t0 = time.time()
    while code is None and time.time() - t0 < 20:
        line = host.stdout.readline()
        lines.append(line)
        m = re.search(r"room code ([A-Z]+)", line)
        if m:
            code = m.group(1)
    if not code:
        host.kill(), relay.kill()
        print(f"{name:<34} FAIL  no room code from the relay")
        return False
    client = peer(["--room", code, "--relay", "127.0.0.1", "--relay-port", str(port), "--drop-at", "50"], "c0", 0,
                  a.seconds)
    outs = ["".join(lines) + host.communicate(timeout=a.seconds * 3 + 180)[0],
            client.communicate(timeout=a.seconds * 3 + 180)[0]]
    relay.kill()
    (rh, hh, _), (rc, hc, _) = parse(outs[0]), parse(outs[1])
    ok = rh and rc and rh["hash"] == rc["hash"] and hh == hc and "rejoined" in outs[1] and "CHAT host: relay works" in outs[1]
    print(f"{name:<34} {'OK' if ok else 'FAIL'}  room {code}" + (f", {len(hh)} matching checkpoints, rejoin + chat" if ok else ""))
    if not ok:
        for o, who in zip(outs, ["host", "client"]):
            print(f"--- {who} (tail)\n" + "\n".join(o.splitlines()[-8:]))
    return bool(ok)


results = [
    case("2 humans + 1 AI", "HHA", 1, 27310),
    case("3 humans", "HHH", 2, 27320),
    # Headless games run much faster than real time, so kill early in a long game.
    case("client drops mid-battle", "HHHA", 2, 27330, kill_after=1.5, seconds=900),
    case("connection lost, rejoin + replay", "HHA", 1, 27340, extra_last=("--drop-at", "60")),
    case("chat reaches everyone", "HHH", 2, 27350, extra_last=("--say", "gg all"), expect_chat="CHAT c1: gg all"),
    relay_case("through a relay (no port forwarding)", 27360),
]
sys.exit(0 if all(results) else 1)
