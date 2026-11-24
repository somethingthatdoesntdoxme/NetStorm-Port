#!/usr/bin/env python3
"""Campaign sweep: play every mission AI-vs-AI headless with invariant checks.

  tools/test/sweep.py --data ~/Netstorm [--bin build/netstorm] [--seconds 1800]
                      [--jobs 2] [--filter rain] [--out test-results]

Writes test-results/sweep.json and sweep.md. Exit code 1 if any run crashed,
timed out, or broke an invariant. Missions that end without a winner aren't
failures (that's AI strength, tracked in the report), unless --require-finish.
"""
import argparse, concurrent.futures as cf, json, os, pathlib, subprocess, sys, tempfile, time


def run(binary, data, mission, seconds, timeout):
    cmd = [binary, "--data", data, "--campaign", mission, "--ai-both", "--simulate", str(seconds),
           "--validate", "--json", "--screenshot", os.path.join(tempfile.gettempdir(), f"sweep_{mission}.bmp"),
           "--size", "320x240"]
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen")
    t0 = time.time()
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return {"mission": mission, "status": "TIMEOUT", "wall": round(time.time() - t0)}
    res = None
    violations = []
    for line in p.stdout.splitlines():
        if line.startswith("RESULT "):
            res = json.loads(line[7:])
        elif line.startswith("VIOLATION "):
            violations.append(line[10:])
    if res is None:
        return {"mission": mission, "status": "CRASH", "code": p.returncode, "wall": round(time.time() - t0),
                "stderr": p.stderr[-800:]}
    res["status"] = "INVARIANT" if res["violations"] else ("WON" if res["outcome"] > 0 else
                                                           "LOST" if res["outcome"] < 0 else "UNFINISHED")
    if res["status"] == "UNFINISHED":
        # The community build's AIs only sacrifice priests at top difficulty, so a side
        # reduced to its priest and transports is a stalemate, not an AI bug.
        crippled = [k for k, v in res.get("players", {}).items() if not v["out"] and v.get("buildings", 1) == 0]
        if crippled:
            res["status"] = "STALEMATE"
            res["crippled"] = crippled
    res["first_violations"] = violations
    res["wall"] = round(time.time() - t0)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--bin", default="build/netstorm")
    ap.add_argument("--seconds", type=int, default=1800)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--filter", default="")
    ap.add_argument("--missions", default="", help="comma-separated mission ids (overrides --filter)")
    ap.add_argument("--normal-only", action="store_true", help="skip Easy/Hard variants")
    ap.add_argument("--out", default="test-results")
    ap.add_argument("--require-finish", action="store_true")
    a = ap.parse_args()

    env = dict(os.environ, SDL_VIDEODRIVER="offscreen")
    listing = subprocess.run([a.bin, "--data", a.data, "--list-missions"], capture_output=True, text=True, env=env)
    missions = [l.split("\t")[0] for l in listing.stdout.splitlines() if "\t" in l]
    missions = [m for m in missions if a.filter in m and not m.startswith("demo")]
    if a.missions:
        missions = [m for m in a.missions.split(",") if m]
    if a.normal_only:
        missions = [m for m in missions if not (m.endswith("easy") or m.endswith("hard"))]
    missions.sort()
    print(f"{len(missions)} missions, {a.seconds}s each, {a.jobs} at a time", flush=True)

    results = []
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        futs = {ex.submit(run, a.bin, a.data, m, a.seconds, a.seconds * 2 + 120): m for m in missions}
        for f in cf.as_completed(futs):
            r = f.result()
            results.append(r)
            print(f"  {r['mission']:<26} {r['status']:<10} {r.get('seconds', '-')!s:>5}s  wall {r['wall']}s", flush=True)

    results.sort(key=lambda r: r["mission"])
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "sweep.json").write_text(json.dumps(results, indent=1))
    md = ["| mission | result | game time | invariant breaks |", "|---|---|---|---|"]
    for r in results:
        st = r['status'] + (f" ({', '.join(r['crippled'])} has no buildings)" if r.get('crippled') else "")
        md.append(f"| {r['mission']} | {st} | {r.get('seconds', '-')} | {r.get('violations', '-')} |")
    for r in results:
        for v in r.get("first_violations", [])[:3]:
            md.append(f"\n- `{r['mission']}`: {v}")
        if r["status"] == "CRASH":
            md.append(f"\n- `{r['mission']}` crashed (code {r.get('code')}): {r.get('stderr', '')[-300:]}")
    (out / "sweep.md").write_text("\n".join(md) + "\n")
    counts = {}
    for r in results:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    print("summary:", counts)
    bad = [r for r in results if r["status"] in ("CRASH", "TIMEOUT", "INVARIANT")]
    if a.require_finish:
        bad += [r for r in results if r["status"] == "UNFINISHED"]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
