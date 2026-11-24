#!/usr/bin/env bash
# Find the commit that broke an AI-vs-AI mission result, automatically:
#   git bisect start <bad> <good>
#   git bisect run tools/test/bisect_mission.sh <netstorm-install> <mission> [seconds]
# Exit 0 = mission still WON (good), 1 = not won (bad), 125 = can't build (skip).
DATA="$1"; MISSION="$2"; SECS="${3:-1500}"
B=/tmp/ns_bisect_build
cmake -S . -B "$B" -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 && cmake --build "$B" --target netstorm --parallel >/dev/null 2>&1 || exit 125
out=$(SDL_VIDEODRIVER=offscreen "$B/netstorm" --data "$DATA" --campaign "$MISSION" --ai-both --simulate "$SECS" \
      --screenshot /tmp/bisect.bmp --size 320x240 2>&1)
echo "$(git rev-parse --short HEAD): $(echo "$out" | grep -E 'game over' || echo 'no result')"
echo "$out" | grep -q "game over.*WON"
