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
    const TypeInfo* t = gd.registry.byName(o.type);
    if (!t || t->set < 0 || !t->def) {
      ++out.unplaced;
      return;
    }
    std::string ln = lower(o.type);
    if (ln == "noisland") return;
    int fx, fy;
    footprint(t, fx, fy);
    int owner = ownerOverride >= 0 ? ownerOverride : std::max(0, o.owner);
    if (t->def->hasFlag("islandThreeByThree") || ln == "island") {
      fillRect(ax - fx + 1, ay - fy + 1, ax, ay, Element::Sun, 0);
    } else if (t->def->hasFlag("createsisland")) {
      Element e = elementFromName(o.type);
      if (e == Element::None) e = Element::Sun;
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
    int cols = nChunks > 6 ? 3 : 2;
    int rows = (nChunks + cols - 1) / cols;
    int w = cols * 16, h = rows * 16;
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

    // Home island: the territory's chunks, with organic edges.
    for (int c = 0; c < nChunks; ++c) {
      int gx = ox + (c % cols) * 16, gy = oy + (c / cols) * 16;
      for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
          int wx = gx + x, wy = gy + y;
          float u = float(wx - ox) / w, v = float(wy - oy) / h;  // 0..1 across the fort
          float edge = std::min({u, v, 1 - u, 1 - v}) * std::min(w, h);  // distance to fort edge, cells
          float n = noise(wx * 0.18f, wy * 0.18f, seed + uint32_t(ti) * 7) * 5.0f;
          if (edge + 0.5f > n) {
            land.set(wx, wy, el);
            land.setOwner(wx, wy, owner);
          }
        }
    }
    for (auto& o : terr.objects) {
      int c = o.chunk;
      int ax = ox + (c % cols) * 16 + o.x, ay = oy + (c / cols) * 16 + o.y;
      addObject(o, ax, ay, o.owner > 0 ? o.owner : (o.type == "geyser" ? 0 : owner));
      // Make sure every building of the fort stands on land.
      const TypeInfo* t = gd.registry.byName(o.type);
      int fx, fy;
      footprint(t, fx, fy);
      fillRect(ax - fx, ay - fy, ax + 1, ay + 1, el, owner);
    }
  }
  land.tidy();

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
