#pragma once
// Skirmish / LAN battle setup and its map generator. Pure game-layer code: every
// lockstep peer runs generateSkirmish() with the same config and gets the same map.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "data/GameData.h"
#include "world/Island.h"

namespace ns {

struct SkirmishSlot {
  enum Kind : uint8_t { Closed, Human, AI } kind = Closed;
  std::string fury = "rain";  // wind / rain / thunder (sun is common Knowledge, it has no temple)
  uint8_t team = 0;           // 0 = on its own; same non-zero team = allies
  std::string name;
};

struct SkirmishConfig {
  std::array<SkirmishSlot, 8> slots;  // slot i = player i+1
  int mapSize = 1;                    // 0 small, 1 medium, 2 large
  int startPower = 3000;
  int neutralIslands = 6;
  uint32_t seed = 1;
  int players() const {
    int n = 0;
    for (auto& s : slots) n += s.kind != SkirmishSlot::Closed;
    return n;
  }
  // Compact text form, sent from the host to every client before the start.
  std::string encode() const;
  static bool decode(const std::string& text, SkirmishConfig& out);
};

struct Placement {
  std::string type;
  int owner;
  float x, y;  // sim anchor (cells)
};

// Builds the world grid and lists what to spawn. Deterministic in `cfg`.
void generateSkirmish(const GameData& gd, const SkirmishConfig& cfg, Island& world, std::vector<Placement>& spawns);

const char* templeFor(const std::string& fury);  // "rainVortex" etc.

}  // namespace ns
