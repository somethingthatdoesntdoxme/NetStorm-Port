#pragma once
// Bridge building: the core NetStorm mechanic. Players get a queue of
// Tetris-style bridge pieces, rotate them, and drop them into open sky to
// link islands. A piece is legal if every cell is open sky and at least one
// cell touches the player's own land or bridge.
#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include "world/Island.h"

namespace ns {

struct BridgeShape {
  std::vector<std::pair<int, int>> cells;  // relative cells
  BridgeShape rotated() const;             // 90 degrees clockwise, normalised
};

class BridgeQueue {
 public:
  explicit BridgeQueue(uint32_t seed = 1);
  const BridgeShape& current() const { return queue_[0]; }
  const BridgeShape& upcoming(int i) const { return queue_[size_t(i)]; }
  static constexpr int kVisible = 3;
  void rotate();
  void next();  // current piece used: shift the queue

 private:
  BridgeShape random();
  std::mt19937 rng_;
  std::array<BridgeShape, kVisible> queue_;
};

// True if `shape` placed with its origin at (x, y) is a legal drop for `player`.
bool canPlaceBridge(const Island& world, const BridgeShape& shape, int x, int y, int player);
// Places the piece; returns how many cells of unowned land it claimed.
int placeBridge(Island& world, const BridgeShape& shape, int x, int y, int player);

// Simple bridge AI: finds the best legal drop of `queue`'s current piece
// (any rotation) that brings `player` closer to unowned land. Returns false if
// nothing useful fits. Applies the rotation to the queue.
bool bestBridgeMove(const Island& world, BridgeQueue& queue, int player, int& outX, int& outY);

}  // namespace ns
