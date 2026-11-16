#include "game/BridgePieces.h"

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
  static const int weights[] = {6, 10, 8, 4, 8, 5, 5, 5, 4, 4, 2};
  std::discrete_distribution<int> d(std::begin(weights), std::end(weights));
  BridgeShape s = allShapes()[size_t(d(rng_))];
  int rot = std::uniform_int_distribution<int>(0, 3)(rng_);
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

bool bestBridgeMove(const Island& w, BridgeQueue& queue, int player, int& outX, int& outY) {
  const int W = w.width(), H = w.height();
  // Distance (through open sky) to the nearest unowned land.
  std::vector<int> dist(size_t(W) * H, std::numeric_limits<int>::max());
  std::deque<int> q;
  int minX = W, minY = H, maxX = 0, maxY = 0;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      if (w.filled(x, y) && w.ownerAt(x, y) == 0) {
        dist[size_t(y) * W + x] = 0;
        q.push_back(y * W + x);
      }
      if ((w.filled(x, y) && w.ownerAt(x, y) == player) || w.bridgeAt(x, y) == player) {
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
  int best = std::numeric_limits<int>::max(), bx = 0, by = 0, brot = 0;
  BridgeShape s = queue.current();
  for (int rot = 0; rot < 4; ++rot, s = s.rotated()) {
    for (int y = std::max(1, minY - 5); y <= std::min(H - 2, maxY + 5); ++y)
      for (int x = std::max(1, minX - 5); x <= std::min(W - 2, maxX + 5); ++x) {
        if (!canPlaceBridge(w, s, x, y, player)) continue;
        int score = std::numeric_limits<int>::max();
        for (auto [dx, dy] : s.cells) score = std::min(score, dist[size_t(y + dy) * W + x + dx]);
        if (score == std::numeric_limits<int>::max()) continue;
        score = score * 10 - int(s.cells.size());  // prefer progress, then bigger pieces
        if (score < best) best = score, bx = x, by = y, brot = rot;
      }
  }
  if (best == std::numeric_limits<int>::max()) return false;
  for (int i = 0; i < brot; ++i) queue.rotate();
  outX = bx;
  outY = by;
  return true;
}

}  // namespace ns
