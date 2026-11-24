#include "game/Skirmish.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "core/DetMath.h"

namespace ns {

const char* templeFor(const std::string& fury) {
  if (fury == "wind") return "windVortex";
  if (fury == "thunder") return "thunderVortex";
  return "rainVortex";
}

static Element elementFor(const std::string& fury) {
  if (fury == "wind") return Element::Wind;
  if (fury == "thunder") return Element::Thunder;
  return Element::Rain;
}

std::string SkirmishConfig::encode() const {
  std::ostringstream o;
  o << "v1 " << mapSize << ' ' << startPower << ' ' << neutralIslands << ' ' << seed;
  for (auto& s : slots) o << ' ' << int(s.kind) << ' ' << (s.fury.empty() ? "rain" : s.fury) << ' ' << int(s.team);
  return o.str();
}

bool SkirmishConfig::decode(const std::string& text, SkirmishConfig& out) {
  std::istringstream in(text);
  std::string v;
  SkirmishConfig c;
  if (!(in >> v) || v != "v1") return false;
  if (!(in >> c.mapSize >> c.startPower >> c.neutralIslands >> c.seed)) return false;
  for (auto& s : c.slots) {
    int kind, team;
    if (!(in >> kind >> s.fury >> team)) return false;
    if (kind < 0 || kind > 2 || team < 0 || team > 8) return false;
    if (s.fury != "wind" && s.fury != "rain" && s.fury != "thunder") return false;
    s.kind = SkirmishSlot::Kind(kind), s.team = uint8_t(team);
  }
  if (c.mapSize < 0 || c.mapSize > 2 || c.startPower < 0 || c.startPower > 100000 || c.neutralIslands < 0 ||
      c.neutralIslands > 20)
    return false;
  out = c;
  return true;
}

namespace {
struct Rng {  // tiny portable PRNG (xorshift32)
  uint32_t s;
  uint32_t next() {
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return s;
  }
  int range(int lo, int hi) { return lo + int(next() % uint32_t(hi - lo + 1)); }
};

bool clear(const Island& w, int x0, int y0, int bw, int bh, int margin) {
  for (int y = y0 - margin; y < y0 + bh + margin; ++y)
    for (int x = x0 - margin; x < x0 + bw + margin; ++x) {
      if (x < 1 || y < 1 || x >= w.width() - 1 || y >= w.height() - 1) return false;
      if (w.filled(x, y)) return false;
    }
  return true;
}

void stamp(Island& w, const Island& blob, int x0, int y0, int owner) {
  for (int y = 0; y < blob.height(); ++y)
    for (int x = 0; x < blob.width(); ++x)
      if (blob.filled(x, y)) {
        w.set(x0 + x, y0 + y, blob.at(x, y));
        w.setOwner(x0 + x, y0 + y, owner);
      }
}
}  // namespace

void generateSkirmish(const GameData& gd, const SkirmishConfig& cfg, Island& world, std::vector<Placement>& spawns) {
  static const int sizes[3][2] = {{120, 90}, {160, 120}, {210, 160}};
  int W = sizes[cfg.mapSize][0], H = sizes[cfg.mapSize][1];
  world = Island(W, H);
  spawns.clear();
  Rng rng{cfg.seed * 2654435761u + 1};

  auto place = [&](const char* type, int owner, int cx, int cy) {
    // cx, cy = footprint top-left; converts to the sim's anchor convention.
    const TypeInfo* t = gd.registry.byName(type);
    if (!t || !t->def) return;
    int fx = std::max(1, int(t->def->num("foot_x", 1))), fy = std::max(1, int(t->def->num("foot_y", 1)));
    bool mobile = t->def->hasFlag("walker");
    spawns.push_back({type, owner, mobile ? cx + 0.5f : float(cx + fx - 1), mobile ? cy + 0.5f : float(cy + fy - 1)});
  };

  // Home islands evenly round an ellipse, slight jitter.
  std::vector<int> players;
  for (int i = 0; i < 8; ++i)
    if (cfg.slots[size_t(i)].kind != SkirmishSlot::Closed) players.push_back(i + 1);
  const int hw = 36, hh = 28;  // room for a temple, workshops, an altar and three geysers
  float cx = W / 2.0f, cy = H / 2.0f, rx = W / 2.0f - hw / 2.0f - 4, ry = H / 2.0f - hh / 2.0f - 4;
  float start = float(rng.range(0, 359)) * det::kPi / 180;
  for (size_t k = 0; k < players.size(); ++k) {
    int p = players[k];
    const SkirmishSlot& s = cfg.slots[size_t(p - 1)];
    float a = start + det::kTwoPi * float(k) / float(players.size());
    int x0 = int(cx + rx * det::cos(a)) - hw / 2, y0 = int(cy + ry * det::sin(a)) - hh / 2;
    x0 = std::clamp(x0, 2, W - hw - 2), y0 = std::clamp(y0, 2, H - hh - 2);
    Island blob = makeBlobIsland(hw, hh, elementFor(s.fury), cfg.seed * 31 + uint32_t(p) * 7);
    stamp(world, blob, x0, y0, p);
    // Base: temple in the middle, priest beside it, a geyser on the home island.
    place(templeFor(s.fury), p, x0 + hw / 2 - 4, y0 + hh / 2 - 3);
    place("priest", p, x0 + hw / 2 + 6, y0 + hh / 2 + 4);
    // Three geysers at home (one runs dry after 2000 Storm Power).
    place("geyser", 0, x0 + 5, y0 + hh / 2 - 1);
    place("geyser", 0, x0 + hw / 2 - 1, y0 + 3);
    place("geyser", 0, x0 + hw - 8, y0 + hh - 7);
  }
  // Neutral islands with geysers between the homes.
  int made = 0;
  for (int tries = 0; tries < 400 && made < cfg.neutralIslands; ++tries) {
    int bw = rng.range(12, 18), bh = rng.range(10, 14);
    int x0 = rng.range(2, W - bw - 2), y0 = rng.range(2, H - bh - 2);
    if (!clear(world, x0, y0, bw, bh, 5)) continue;
    static const Element els[3] = {Element::Wind, Element::Rain, Element::Thunder};
    Island blob = makeBlobIsland(bw, bh, els[rng.range(0, 2)], cfg.seed * 97 + uint32_t(made));
    stamp(world, blob, x0, y0, 0);
    place("geyser", 0, x0 + bw / 2 - 1, y0 + bh / 2 - 1);
    ++made;
  }
  world.markAllPermanent();
}

}  // namespace ns
