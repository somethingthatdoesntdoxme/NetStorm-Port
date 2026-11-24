#pragma once
// Skirmish AI v1. Plays the same moves a human can: bridge pieces, workshop
// production, dropping finished units. Deterministic (driven by sim ticks).
#include <algorithm>
#include <string>
#include <vector>

#include "game/BridgePieces.h"
#include "game/Sim.h"

namespace ns {

class SkirmishAi {
 public:
  SkirmishAi(int player, uint32_t seed) : player_(player), queue_(seed), seed_(seed) {}
  void step(Sim& sim, Island& world);  // call every sim tick
  int player() const { return player_; }
  // Mission setting "timeBetweenMoves" (seconds between bridge moves).
  void setMoveInterval(float seconds) { bridgeEvery_ = std::max(10, int(seconds * 20 / 2)); }

 private:
  void produce(Sim& sim);
  void dropReady(Sim& sim, Island& world);
  void priestWork(Sim& sim, Island& world);
  std::vector<std::string> neededWorkshops(Sim& sim, const std::string& fury);
  bool findSite(Sim& sim, Island& world, int priestId, const TypeInfo* t, int& x, int& y);
  int player_;
  BridgeQueue queue_;
  uint32_t seed_;
  int tick_ = 0;
  int bridgeEvery_ = 30;
  const Island* world_ = nullptr;
  int reachFor_ = -1;      // downed enemy priest our golems couldn't reach: bridge towards him
  long reachSince_ = 0;
  long holdSince_ = -1;
  int stuck_ = 0;
  long nextSiteSearch_ = 0;          // bridge attempts in a row that found no forward move   // tick we started holding an out-of-range shooter
};

}  // namespace ns
