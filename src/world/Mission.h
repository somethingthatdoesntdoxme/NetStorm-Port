#pragma once
// Builds a playable-looking world from a decoded .fort mission.
//
// World = 16x16 chunks of 16x16 cells. The neutral layer ("Chaff") has
// absolute chunk positions. Player forts (Terr sections) are placed into the
// world at load time -- the original does this at runtime too (its chunk map
// isn't stored in the file), so the layout here is our own choice.
//
// Land: home islands are grown from each territory's chunks (approximation
// until the original terrain builder is ported); small islands come from
// objects flagged `createsisland`, `island` pieces and land markers.
#include <string>
#include <vector>

#include "data/FortFile.h"
#include "data/GameData.h"
#include "world/Island.h"

namespace ns {

struct MissionObject {
  const TypeInfo* type = nullptr;
  int cellX = 0, cellY = 0;  // anchor cell (bottom-right of footprint)
  int owner = 0;             // 0 none, 1..8 player
};

struct MissionWorld {
  std::string name;
  Island land{256, 256};
  std::vector<MissionObject> objects;
  int minX = 0, minY = 0, maxX = 0, maxY = 0;  // used cell bounds
  int unplaced = 0;                            // objects with no sprite type
};

bool buildMission(const GameData& gd, const FortFile& fort, MissionWorld& out, uint32_t seed = 1);

}  // namespace ns
