#pragma once
// Skirmish AI v1. Plays the same moves a human can: bridge pieces, workshop
// production, dropping finished units. Deterministic (driven by sim ticks).
#include "game/BridgePieces.h"
#include "game/Sim.h"

namespace ns {

class SkirmishAi {
 public:
  SkirmishAi(int player, uint32_t seed) : player_(player), queue_(seed), seed_(seed) {}
  void step(Sim& sim, Island& world);  // call every sim tick
  int player() const { return player_; }

 private:
  void produce(Sim& sim);
  void dropReady(Sim& sim, Island& world);
  int player_;
  BridgeQueue queue_;
  uint32_t seed_;
  int tick_ = 0;
};

}  // namespace ns
