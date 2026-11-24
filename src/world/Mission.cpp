#include "world/Mission.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace ns {

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

Element elementFromName(const std::string& n) {
  std::string l = lower(n);
  if (l.rfind("rain", 0) == 0) return Element::Rain;
  if (l.rfind("wind", 0) == 0) return Element::Wind;
  if (l.rfind("thunder", 0) == 0) return Element::Thunder;
  if (l.rfind("sun", 0) == 0) return Element::Sun;
  return Element::None;
}

uint32_t hash2(int x, int y, uint32_t s) {
  uint32_t a = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + s * 2246822519u;
  a = (a ^ (a >> 13)) * 1274126177u;
  return a ^ (a >> 16);
}

// Smooth value noise in [0,1].
float noise(float x, float y, uint32_t s) {
  int xi = int(std::floor(x)), yi = int(std::floor(y));
  float fx = x - xi, fy = y - yi;
  auto v = [&](int a, int b) { return (hash2(a, b, s) & 0xffff) / 65535.0f; };
  float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
  float a = v(xi, yi) + (v(xi + 1, yi) - v(xi, yi)) * sx;
  float b = v(xi, yi + 1) + (v(xi + 1, yi + 1) - v(xi, yi + 1)) * sx;
  return a + (b - a) * sy;
}

}  // namespace

bool buildMission(const GameData& gd, const FortFile& fort, MissionWorld& out, uint32_t seed) {
  out = MissionWorld{};
  out.name = fort.name;
  constexpr int kWorld = 512;  // cells; the original's 256x256 world sits in the middle
  constexpr int kOff = 128;
  out.land = Island(kWorld, kWorld);
  Island& land = out.land;

  auto footprint = [&](const TypeInfo* t, int& fx, int& fy) {
    fx = fy = 1;
    if (t && t->def) {
      fx = std::max(1, int(t->def->num("foot_x", 1)));
      fy = std::max(1, int(t->def->num("foot_y", 1)));
    }
  };
  std::vector<uint8_t> unitGrown(size_t(kWorld) * kWorld, 0);  // land grown by (destroyable) units
  auto markGrown = [&](int x0, int y0, int x1, int y1) {
    for (int y = std::max(0, y0); y <= std::min(kWorld - 1, y1); ++y)
      for (int x = std::max(0, x0); x <= std::min(kWorld - 1, x1); ++x)
        if (!land.filled(x, y)) unitGrown[size_t(y) * kWorld + x] = 1;
  };
  auto fillRect = [&](int x0, int y0, int x1, int y1, Element e, int owner) {
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        if (!land.filled(x, y)) land.set(x, y, e);
        if (owner > 0) land.setOwner(x, y, owner);
      }
  };
  auto addObject = [&](const FortObject& o, int ax, int ay, int ownerOverride) {
    if (o.landMarker) {
      fillRect(ax - 1, ay - 1, ax, ay, Element::Sun, o.player);
      return;
    }
    if (o.type.empty()) return;  // type id not in the fort's name table (a quirk in RunForIt's data)
    const TypeInfo* t = gd.registry.byName(o.type);
    if (!t || t->set < 0 || !t->def) {
      ++out.unplaced;
      out.unplacedTypes.push_back(o.type);
      return;
    }
    std::string ln = lower(o.type);
    if (ln == "noisland") return;
    if (ln == "bridge") {
      int owner = ownerOverride >= 0 ? ownerOverride : std::max(0, o.owner);
      land.setBridge(ax, ay, owner > 0 ? owner : 9);
      return;
    }
    int fx, fy;
    footprint(t, fx, fy);
    int owner = ownerOverride >= 0 ? ownerOverride : std::max(0, o.owner);
    if (t->def->hasFlag("islandThreeByThree") || ln == "island") {
      fillRect(ax - fx + 1, ay - fy + 1, ax, ay, Element::Sun, 0);
    } else if (t->def->hasFlag("createsisland")) {
      Element e = elementFromName(o.type);
      if (e == Element::None) e = Element::Sun;
      if (ln != "geyser") markGrown(ax - fx, ay - fy, ax + 1, ay + 1);
      fillRect(ax - fx, ay - fy, ax + 1, ay + 1, e, owner);  // footprint + 1 cell apron
    }
    // Islands themselves are drawn as terrain, not as sprites.
    if (ln == "island" || ln == "geyser" || t->def->hasFlag("notreal")) {
      if (ln != "geyser") return;
    }
    MissionObject mo;
    mo.type = t;
    mo.cellX = ax;
    mo.cellY = ay;
    mo.owner = owner;
    out.objects.push_back(mo);
  };

  // Neutral world: absolute chunk positions.
  for (auto& o : fort.chaff.objects) {
    int cx = o.chunk % 16, cy = o.chunk / 16;
    addObject(o, kOff + cx * 16 + o.x, kOff + cy * 16 + o.y, -1);
  }

  // Bounds of the neutral layout, so player forts can be placed around it.
  int bx0 = kOff + 96, by0 = kOff + 96, bx1 = kOff + 160, by1 = kOff + 160;
  {
    bool any = false;
    for (int y = 0; y < kWorld; ++y)
      for (int x = 0; x < kWorld; ++x)
        if (land.filled(x, y)) {
          if (!any) bx0 = bx1 = x, by0 = by1 = y, any = true;
          bx0 = std::min(bx0, x);
          bx1 = std::max(bx1, x);
          by0 = std::min(by0, y);
          by1 = std::max(by1, y);
        }
    for (auto& mo : out.objects) {
      bx0 = std::min(bx0, mo.cellX);
      bx1 = std::max(bx1, mo.cellX);
      by0 = std::min(by0, mo.cellY);
      by1 = std::max(by1, mo.cellY);
    }
  }

  // Player forts: each territory's chunks laid out 2 wide, placed around the
  // neutral area (north, south, west, east, then corners).
  int placed = 0;
  for (size_t ti = 0; ti < fort.territories.size(); ++ti) {
    const FortSection& terr = fort.territories[ti];
    if (terr.objects.empty()) continue;
    int nChunks = std::max(1, terr.chunkCount);
    // Chunk layout: a random connected polyomino (the original builds the
    // chunk-to-world map at runtime; a polyomino gives its T/L-shaped arms).
    std::vector<std::pair<int, int>> cpos{{0, 0}};
    auto taken = [&](int x, int y) {
      for (auto& p : cpos) if (p.first == x && p.second == y) return true;
      return false;
    };
    // Never grow towards the middle of the map: arms poking into the
    // neutral area put home buildings in enemy range.
    const int side = placed % 6;  // 0 N, 1 S, 2 W, 3 E, 4 NW, 5 SE (see placement below)
    auto inward = [&](int d) {
      return ((side == 0 || side == 4) && d == 2) || (side == 1 && d == 3) || ((side == 2 || side == 4) && d == 0) ||
             ((side == 3 || side == 5) && d == 1) || (side == 5 && d == 3);
    };
    for (int c = 1; c < nChunks; ++c) {
      for (int tries = 0; tries < 64; ++tries) {
        uint32_t h = hash2(c, tries, seed + uint32_t(ti) * 31);
        // Mostly grow from the newest chunk, so arms form.
        const auto& from = (h & 3) ? cpos.back() : cpos[(h >> 2) % cpos.size()];
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        int d = int((h >> 8) & 3);
        if (inward(d) && tries < 48) continue;
        int nx = from.first + dx[d], ny = from.second + dy[d];
        if (!taken(nx, ny)) { cpos.push_back({nx, ny}); break; }
      }
      if (int(cpos.size()) <= c) cpos.push_back({c, 99});  // shouldn't happen
    }
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    for (auto& p : cpos) {
      minX = std::min(minX, p.first), maxX = std::max(maxX, p.first);
      minY = std::min(minY, p.second), maxY = std::max(maxY, p.second);
    }
    for (auto& p : cpos) p.first -= minX, p.second -= minY;
    int w = (maxX - minX + 1) * 16, h = (maxY - minY + 1) * 16;
    int cxm = (bx0 + bx1) / 2, cym = (by0 + by1) / 2;
    int ox, oy;
    switch (placed % 6) {
      case 0: ox = cxm - w / 2; oy = by0 - h - 10; break;
      case 1: ox = cxm - w / 2; oy = by1 + 10; break;
      case 2: ox = bx0 - w - 10; oy = cym - h / 2; break;
      case 3: ox = bx1 + 10; oy = cym - h / 2; break;
      case 4: ox = bx0 - w - 10; oy = by0 - h - 10; break;
      default: ox = bx1 + 10; oy = by1 + 10; break;
    }
    ++placed;

    // Owner + element of this fort from its buildings.
    std::map<int, int> owners;
    std::map<Element, int> els;
    for (auto& o : terr.objects) {
      if (o.owner > 0) owners[o.owner]++;
      Element e = elementFromName(o.type);
      if (e != Element::None) els[e]++;
    }
    int owner = owners.empty() ? int(placed) : std::max_element(owners.begin(), owners.end(), [](auto& a, auto& b) {
                                                  return a.second < b.second;
                                                })->first;
    Element el = els.empty() ? Element::Sun : std::max_element(els.begin(), els.end(), [](auto& a, auto& b) {
                                                 return a.second < b.second;
                                               })->first;
    // Wind/rain/thunder forts in the original mostly sit on their own grass;
    // keep that, but sun stays green.

    // Home island: each chunk's land inset from the sides that have no
    // neighbouring chunk, with ragged edges.
    for (int c = 0; c < nChunks; ++c) {
      int px = cpos[size_t(c)].first, py = cpos[size_t(c)].second;
      bool L = taken(px - 1, py), R = taken(px + 1, py), U = taken(px, py - 1), D = taken(px, py + 1);
      int gx = ox + px * 16, gy = oy + py * 16;
      for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
          int wx = gx + x, wy = gy + y;
          float edge = 99;
          if (!L) edge = std::min(edge, float(x));
          if (!R) edge = std::min(edge, float(15 - x));
          if (!U) edge = std::min(edge, float(y));
          if (!D) edge = std::min(edge, float(15 - y));
          float n = 0.8f + noise(wx * 0.16f, wy * 0.16f, seed + uint32_t(ti) * 7) * 3.5f +
                    noise(wx * 0.5f, wy * 0.5f, seed + uint32_t(ti) * 13) * 1.6f;
          if (edge > n) {
            land.set(wx, wy, el);
            land.setOwner(wx, wy, owner);
          }
        }
    }
    for (auto& o : terr.objects) {
      int c = o.chunk;
      if (c < 0 || c >= nChunks) c = 0;
      int ax = ox + cpos[size_t(c)].first * 16 + o.x, ay = oy + cpos[size_t(c)].second * 16 + o.y;
      addObject(o, ax, ay, o.owner > 0 ? o.owner : (o.type == "geyser" ? 0 : owner));
      // Make sure every building of the fort stands on land.
      const TypeInfo* t = gd.registry.byName(o.type);
      int fx, fy;
      footprint(t, fx, fy);
      if (o.type != "bridge") fillRect(ax - fx, ay - fy, ax + 1, ay + 1, el, owner);
    }
  }
  land.tidy();
  for (int y = 0; y < kWorld; ++y)
    for (int x = 0; x < kWorld; ++x)
      land.setPermanent(x, y, land.filled(x, y) && !unitGrown[size_t(y) * kWorld + x]);

  // Final bounds.
  out.minX = out.minY = kWorld;
  out.maxX = out.maxY = 0;
  for (int y = 0; y < kWorld; ++y)
    for (int x = 0; x < kWorld; ++x)
      if (land.filled(x, y)) {
        out.minX = std::min(out.minX, x);
        out.maxX = std::max(out.maxX, x);
        out.minY = std::min(out.minY, y);
        out.maxY = std::max(out.maxY, y);
      }
  if (out.maxX < out.minX) out.minX = out.minY = 0, out.maxX = out.maxY = 64;
  return true;
}

}  // namespace ns
