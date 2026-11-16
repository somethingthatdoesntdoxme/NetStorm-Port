#include "world/Island.h"

#include <cmath>

namespace ns {

namespace {
uint32_t hash(uint32_t a) {
  a ^= a >> 16;
  a *= 0x7feb352d;
  a ^= a >> 15;
  a *= 0x846ca68b;
  a ^= a >> 16;
  return a;
}
int elemIndex(Element e) { return std::max(0, int(e) - 1); }  // sun 0, thunder 1, wind 2, rain 3
}  // namespace

TileChoice Island::tileFor(int x, int y, uint32_t seed) const {
  Element e = at(x, y);
  int ei = elemIndex(e);
  bool n = filled(x, y - 1), s = filled(x, y + 1), w = filled(x - 1, y), east = filled(x + 1, y);
  bool nw = filled(x - 1, y - 1), ne = filled(x + 1, y - 1), sw = filled(x - 1, y + 1), se = filled(x + 1, y + 1);
  uint32_t r = hash(uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ seed);
  int rimVar = 2 * ei + 1 + int(r & 1);

  if (!n && !w) return {"FL", rimVar, false};
  if (!n && !east) return {"GM", rimVar, false};
  if (!s && !east) return {"HN", rimVar, true};
  if (!s && !w) return {"IO", rimVar, true};
  if (!w) return {"BF", rimVar, false};
  if (!n) return {"CG", rimVar, false};
  if (!east) return {"DH", rimVar, false};
  if (!s) return {"EI", rimVar, true};
  if (!sw) return {"AB", rimVar, true};
  if (!nw) return {"AC", rimVar, false};
  if (!ne) return {"AD", rimVar, false};
  if (!se) return {"AE", rimVar, true};
  return {"AA", 6 * ei + 1 + int(r % 6), false};
}

void Island::tidy() {
  // A filled cell needs at least one horizontal and one vertical neighbour,
  // otherwise there's no tile for it (1-wide spurs).
  bool changed = true;
  while (changed) {
    changed = false;
    for (int y = 0; y < h_; ++y)
      for (int x = 0; x < w_; ++x) {
        if (!filled(x, y)) continue;
        bool hor = filled(x - 1, y) || filled(x + 1, y);
        bool ver = filled(x, y - 1) || filled(x, y + 1);
        bool diagOnly = (filled(x - 1, y) && filled(x + 1, y) && !filled(x, y - 1) && !filled(x, y + 1)) ||
                        (filled(x, y - 1) && filled(x, y + 1) && !filled(x - 1, y) && !filled(x + 1, y));
        if (!hor || !ver || diagOnly) {
          set(x, y, Element::None);
          changed = true;
        }
      }
  }
}

Island makeBlobIsland(int w, int h, Element e, uint32_t seed) {
  Island isl(w, h);
  float cx = (w - 1) / 2.0f, cy = (h - 1) / 2.0f;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      float dx = (x - cx) / (w / 2.0f), dy = (y - cy) / (h / 2.0f);
      float a = std::atan2(dy, dx);
      float wobble = 0.82f + 0.12f * std::sin(a * 3 + seed) + 0.06f * std::sin(a * 7 + seed * 1.7f);
      if (dx * dx + dy * dy < wobble * wobble) isl.set(x, y, e);
    }
  isl.tidy();
  return isl;
}

}  // namespace ns
