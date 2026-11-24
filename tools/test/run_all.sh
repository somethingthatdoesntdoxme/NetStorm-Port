#!/usr/bin/env bash
# Full local test run (needs a NetStorm install):
#   tools/test/run_all.sh ~/Netstorm            quick: unit/data tests, determinism, screens, short sweep
#   tools/test/run_all.sh ~/Netstorm --full     plus every mission (all difficulties) for 30 game-minutes
set -u
DATA="${1:?usage: run_all.sh <netstorm-install> [--full]}"
cd "$(dirname "$0")/../.."
cmake -S . -B build -DNETSTORM_DATA="$DATA" >/dev/null && cmake --build build --parallel >/dev/null || exit 1
fail=0
step() { echo; echo "== $1"; shift; "$@" || fail=1; }
step "unit + data tests"  ctest --test-dir build --output-on-failure
step "determinism"        python3 tools/test/determinism.py --data "$DATA"
step "screens"            python3 tools/test/screens.py --data "$DATA"
step "LAN lockstep"       python3 tools/test/lan.py --data "$DATA"
if [ "${2:-}" = "--full" ]; then
  step "campaign sweep (all)"   python3 tools/test/sweep.py --data "$DATA" --seconds 1800
else
  step "campaign sweep (quick)" python3 tools/test/sweep.py --data "$DATA" --seconds 300 --normal-only
fi
echo; [ $fail -eq 0 ] && echo "ALL GREEN" || echo "SOMETHING FAILED (see above; reports in test-results/)"
exit $fail
