#include "data/TypeRegistry.h"

#include "data/GameData.h"

#include <algorithm>
#include <cctype>

namespace ns {

namespace {
// Order of the engine's type table == order of sprite sets in _shapes.shp.
const char* const kSetTypeNames[] = {
    "dude", "sunArcher", "sunAviary", "sunFlyer", "banner", "windBattery",
    "rainBattery", "thunderBattery", "sunBlocker", "thunderBlocker", "blankMissile", "bolt",
    "bridge", "sunDisc", "emptyGeyser", "sunFactory", "windFactory", "rainFactory",
    "thunderFactory", "residence", "fencemark", "flag", "fortGump", "icon",
    "island", "islandStalag", "playerBanner", "isle", "isleBig", "geyserBrightener",
    "treeTwo", "treeThree", "fringe", "mana", "range", "manabolt",
    "flare", "puzzlePiece", "playerIsland", "battleIsland", "particlePlaceHolder", "sunBalloon",
    "buried", "bombExplodeSmall", "bombExplodeMedium", "bombExplodeLarge", "bombHeal", "bombInvisible",
    "bombParalyze", "bombHardener", "bombTreason", "edgeFarm", "geyser", "windVortex",
    "rainVortex", "thunderVortex", "outpost", "mcloud", "sunCannon", "rainCannon",
    "rainCannonMissile", "thunderCannon", "thunderCannonMissile", "sunWalker", "teleportEffect", "bulf",
    "sunFence", "windWalker", "windFlyer", "windAviary", "windArcher", "windBalloon",
    "windBlocker", "rainAviary", "rainFlyer", "rainFence", "rainBalloon", "rainBlocker",
    "thunderArcher", "thunderFence", "growingRainBlocker", "platform", "player", "mog",
    "nugget", "bridgeConnector", "rainWalker", "noIsland", "priest", "windWhirl",
    "thunderaviary", "RUIN", "Monument", "sunBattery", "lightning", "anim",
    "challengeIsland", "fakeThreeByThreeSurface", "sunFlyerBomb", "altar", "dais", "rune",
    "forceField", "bombSpecialOne", "daisExtraFrames", "fenceShield", "bombTwister", "bombIITwister",
    "bombIIITwister", "GravitationEffect", "bombGraviton", "HealEffect", "MeteorEffect", "BOMBmeteor",
    "bombIMano", "bombIIMano", "bombIIIMano", "bombLightingZap", "bombLightingwave",
};
constexpr int kNames = int(sizeof(kSetTypeNames) / sizeof(kSetTypeNames[0]));

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}
}  // namespace

int TypeInfo::findLine(const std::string& cluster, int variation) const {
  if (!def) return -1;
  for (size_t i = 0; i < def->frames.size(); ++i)
    if (def->frames[i].cluster == cluster && def->frames[i].variation == variation) return int(i);
  return -1;
}

void TypeRegistry::build(const GameData& gd) {
  sets_.clear();
  byName_.clear();
  std::unordered_map<std::string, const TypeDef*> defs;
  for (auto& t : gd.types) defs[lower(t.name)] = &t;

  int n = std::min(kNames, gd.shapes.setCount());
  for (int i = 0; i < gd.shapes.setCount(); ++i) {
    TypeInfo ti;
    ti.set = i;
    ti.name = i < n ? kSetTypeNames[i] : "set" + std::to_string(i);
    auto it = defs.find(lower(ti.name));
    if (it != defs.end()) {
      ti.def = it->second;
      int lines = int(ti.def->frames.size());
      int frames = gd.shapes.frameCount(i);
      ti.hasShadows = (ti.def->hasFlag("shadow") || ti.def->hasFlag("flyershadow")) && frames >= lines * 2;
      ti.imageFrames = ti.hasShadows ? lines : frames;
    } else {
      ti.imageFrames = gd.shapes.frameCount(i);
    }
    byName_[lower(ti.name)] = i;
    sets_.push_back(std::move(ti));
  }
}

const TypeInfo* TypeRegistry::bySet(int set) const {
  return set >= 0 && set < int(sets_.size()) ? &sets_[set] : nullptr;
}

const TypeInfo* TypeRegistry::byName(const std::string& name) const {
  auto it = byName_.find(lower(name));
  return it == byName_.end() ? nullptr : &sets_[it->second];
}

}  // namespace ns
