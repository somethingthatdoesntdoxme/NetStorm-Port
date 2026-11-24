#include "game/BridgePieces.h"
#include <string>
#include <cstdlib>
#include <cstdio>

#include <algorithm>
#include <deque>
#include <limits>

namespace ns {

namespace {
const std::vector<BridgeShape>& allShapes() {
  // The piece set: straights, corners and tetrominoes, weighted towards the
  // smaller pieces like the original's bar.
  static const std::vector<BridgeShape> s = {
      {{{0, 0}}},                                  // single
      {{{0, 0}, {1, 0}}},                          // 2 straight
      {{{0, 0}, {1, 0}, {2, 0}}},                  // 3 straight
      {{{0, 0}, {1, 0}, {2, 0}, {3, 0}}},          // I
      {{{0, 0}, {0, 1}, {1, 1}}},                  // small corner
      {{{0, 0}, {0, 1}, {0, 2}, {1, 2}}},          // L
      {{{1, 0}, {1, 1}, {1, 2}, {0, 2}}},          // J
      {{{0, 0}, {1, 0}, {2, 0}, {1, 1}}},          // T
      {{{1, 0}, {2, 0}, {0, 1}, {1, 1}}},          // S
      {{{0, 0}, {1, 0}, {1, 1}, {2, 1}}},          // Z
      {{{1, 0}, {0, 1}, {1, 1}, {2, 1}, {1, 2}}},  // plus
  };
  return s;
}
}  // namespace

BridgeShape BridgeShape::rotated() const {
  BridgeShape r;
  for (auto [x, y] : cells) r.cells.push_back({-y, x});
  int mx = 1 << 20, my = 1 << 20;
  for (auto [x, y] : r.cells) mx = std::min(mx, x), my = std::min(my, y);
  for (auto& c : r.cells) c.first -= mx, c.second -= my;
  return r;
}

BridgeQueue::BridgeQueue(uint32_t seed) : rng_(seed) {
  for (auto& q : queue_) q = random();
}

BridgeShape BridgeQueue::random() {
  // Plain integer maths on the generator's raw output: std::*_distribution differs
  // between standard libraries, which would desync a mixed-platform LAN game.
  static const uint32_t weights[] = {6, 10, 8, 4, 8, 5, 5, 5, 4, 4, 2};
  uint32_t total = 0;
  for (uint32_t w : weights) total += w;
  uint32_t r = uint32_t(rng_()) % total;
  size_t pick = 0;
  while (r >= weights[pick]) r -= weights[pick++];
  BridgeShape s = allShapes()[pick];
  int rot = int(uint32_t(rng_()) % 4);
  for (int i = 0; i < rot; ++i) s = s.rotated();
  return s;
}

void BridgeQueue::rotate() { queue_[0] = queue_[0].rotated(); }

void BridgeQueue::next() {
  for (int i = 0; i + 1 < kVisible; ++i) queue_[size_t(i)] = queue_[size_t(i + 1)];
  queue_[kVisible - 1] = random();
}

bool canPlaceBridge(const Island& w, const BridgeShape& shape, int x, int y, int player) {
  bool touches = false;
  for (auto [dx, dy] : shape.cells) {
    int cx = x + dx, cy = y + dy;
    if (cx < 1 || cy < 1 || cx >= w.width() - 1 || cy >= w.height() - 1) return false;
    if (w.filled(cx, cy) || w.bridgeAt(cx, cy)) return false;
    const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& n : nb) {
      int nx = cx + n[0], ny = cy + n[1];
      if ((w.filled(nx, ny) && w.ownerAt(nx, ny) == player) || w.bridgeAt(nx, ny) == player)
        touches = true;
    }
  }
  return touches;
}

int placeBridge(Island& w, const BridgeShape& shape, int x, int y, int player) {
  for (auto [dx, dy] : shape.cells) w.setBridge(x + dx, y + dy, player);
  // Reaching an unowned island claims it (flood fill over connected land).
  int claimed = 0;
  std::vector<std::pair<int, int>> stack;
  for (auto [dx, dy] : shape.cells) {
    const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& n : nb) {
      int nx = x + dx + n[0], ny = y + dy + n[1];
      if (w.filled(nx, ny) && w.ownerAt(nx, ny) == 0) stack.push_back({nx, ny});
    }
  }
  while (!stack.empty()) {
    auto [cx, cy] = stack.back();
    stack.pop_back();
    if (!w.filled(cx, cy) || w.ownerAt(cx, cy) != 0) continue;
    w.setOwner(cx, cy, player);
    ++claimed;
    stack.push_back({cx + 1, cy});
    stack.push_back({cx - 1, cy});
    stack.push_back({cx, cy + 1});
    stack.push_back({cx, cy - 1});
  }
  return claimed;
}

bool bestBridgeMove(const Island& w, BridgeQueue& queue, int player, int& outX, int& outY, uint16_t enemyMask,
                    const std::vector<std::pair<int, int>>* goals, int goalRadius,
                    const std::vector<uint8_t>* connected, int slack) {
  auto mine = [&](int mx, int my) {
    if (!((w.filled(mx, my) && w.ownerAt(mx, my) == player) || w.bridgeAt(mx, my) == player)) return false;
    return !connected || (mx >= 0 && my >= 0 && mx < w.width() && my < w.height() &&
                          (*connected)[size_t(my) * size_t(w.width()) + size_t(mx)]);
  };
  const int W = w.width(), H = w.height();
  // Distance (through open sky) to the nearest unowned land.
  std::vector<int> dist(size_t(W) * H, std::numeric_limits<int>::max());
  std::deque<int> q;
  int minX = W, minY = H, maxX = 0, maxY = 0;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      bool target = false;
      if (goals) {
        if (!w.filled(x, y) && !w.bridgeAt(x, y))
          for (auto [gx, gy] : *goals)
            if ((gx - x) * (gx - x) + (gy - y) * (gy - y) <= goalRadius * goalRadius) {
              target = true;
              break;
            }
      } else
      target = enemyMask ? ((w.filled(x, y) && ((enemyMask >> w.ownerAt(x, y)) & 1)) ||
                                 (w.bridgeAt(x, y) && ((enemyMask >> w.bridgeAt(x, y)) & 1)))
                              : (w.filled(x, y) && w.ownerAt(x, y) == 0);  // unclaimed land only:
      // counting enemy land here made expansion stop for good once our territory
      // touched theirs, leaving free geyser islands unclaimed (attack mode handles enemies)
      if (target) {
        dist[size_t(y) * W + x] = 0;
        q.push_back(y * W + x);
      }
      if (mine(x, y)) {
        minX = std::min(minX, x), maxX = std::max(maxX, x), minY = std::min(minY, y), maxY = std::max(maxY, y);
      }
    }
  if (q.empty() || minX > maxX) return false;
  while (!q.empty()) {
    int i = q.front();
    q.pop_front();
    int x = i % W, y = i / W;
    const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& n : nb) {
      int nx = x + n[0], ny = y + n[1];
      if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
      size_t j = size_t(ny) * W + nx;
      if (w.filled(nx, ny) || w.bridgeAt(nx, ny)) continue;
      if (dist[j] > dist[size_t(i)] + 1) {
        dist[j] = dist[size_t(i)] + 1;
        q.push_back(int(j));
      }
    }
  }
  // Current frontier: best distance already reachable from our territory.
  int current = std::numeric_limits<int>::max();
  for (int y = std::max(1, minY - 1); y <= std::min(H - 2, maxY + 1); ++y)
    for (int x = std::max(1, minX - 1); x <= std::min(W - 2, maxX + 1); ++x) {
      if (w.filled(x, y) || w.bridgeAt(x, y)) continue;
      const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (auto& n : nb)
        if (mine(x + n[0], y + n[1])) current = std::min(current, dist[size_t(y) * W + x]);
    }
  if (current == 0) return false;  // already touching the target: nothing to bridge
  if (current == std::numeric_limits<int>::max() && slack == 0) return false;
  int best = std::numeric_limits<int>::max(), bx = 0, by = 0, brot = 0;
  int dbgPlace = 0, dbgJoined = 0, dbgFinite = 0, dbgBestScore = std::numeric_limits<int>::max();
  BridgeShape s = queue.current();
  for (int rot = 0; rot < 4; ++rot, s = s.rotated()) {
    for (int y = std::max(1, minY - 5); y <= std::min(H - 2, maxY + 5); ++y)
      for (int x = std::max(1, minX - 5); x <= std::min(W - 2, maxX + 5); ++x) {
        if (!canPlaceBridge(w, s, x, y, player)) continue;
        ++dbgPlace;
        if (connected) {  // must grow from the part of our territory we can walk to
          bool joined = false;
          for (auto [dx, dy] : s.cells)
            for (auto [nx, ny] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) joined |= mine(x + dx + nx, y + dy + ny);
          if (!joined) continue;
        }
        ++dbgJoined;
        int score = std::numeric_limits<int>::max();
        for (auto [dx, dy] : s.cells) score = std::min(score, dist[size_t(y + dy) * W + x + dx]);
        // Must make progress (or, with slack, at least not lose much). No int overflow
        // when our whole front is cut off from the goal (current == max).
        if (score == std::numeric_limits<int>::max()) continue;
        ++dbgFinite;
        dbgBestScore = std::min(dbgBestScore, score);
        if (current != std::numeric_limits<int>::max() && score >= current + slack) continue;
        score = score * 10 - int(s.cells.size());  // prefer progress, then bigger pieces
        if (score < best) best = score, bx = x, by = y, brot = rot;
      }
  }
  if (goals && slack > 0 && std::getenv("NS_DEBUG_BRIDGE") && std::string(std::getenv("NS_DEBUG_BRIDGE")) == "2")
    std::fprintf(stderr, "bridge P%d: current %d slack %d | placeable %d joined %d finite %d best %d\n", player,
                 current == std::numeric_limits<int>::max() ? -1 : current, slack, dbgPlace, dbgJoined, dbgFinite,
                 dbgBestScore == std::numeric_limits<int>::max() ? -1 : dbgBestScore);
  if (best == std::numeric_limits<int>::max()) return false;
  for (int i = 0; i < brot; ++i) queue.rotate();
  outX = bx;
  outY = by;
  return true;
}

}  // namespace ns

namespace ns {
std::vector<uint8_t> walkableFrom(const Island& w, int fx, int fy) {
  std::vector<uint8_t> seen(size_t(w.width()) * size_t(w.height()), 0);
  std::vector<std::pair<int, int>> stack{{fx, fy}};
  while (!stack.empty()) {
    auto [x, y] = stack.back();
    stack.pop_back();
    if (x < 0 || y < 0 || x >= w.width() || y >= w.height()) continue;
    size_t k = size_t(y) * size_t(w.width()) + size_t(x);
    if (seen[k] || !(w.filled(x, y) || w.bridgeAt(x, y))) continue;
    seen[k] = 1;
    stack.push_back({x + 1, y}), stack.push_back({x - 1, y}), stack.push_back({x, y + 1}), stack.push_back({x, y - 1});
  }
  return seen;
}
}  // namespace ns
