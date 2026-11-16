#pragma once
// Island terrain: a rectangular grid of 16x11-pixel cells.
//
// The art (type "isle", sprite set 27) is drawn as auto-tiles chosen from the
// 8 neighbours of each filled cell, using the clusters in isle.type:
//   AA filled                (6 variants per element)
//   BF left edge  CG top edge  DH right edge  EI bottom edge
//   FL/GM/HN/IO   outside corners: top-left / top-right / bottom-right / bottom-left
//   AB/AC/AD/AE   inside corners:  missing SW / NW / NE / SE diagonal
// Edge/corner clusters have 2 variants per element. Element order in the
// file: sun, thunder, wind, rain.
#include <cstdint>
#include <string>
#include <vector>

namespace ns {

enum class Element : uint8_t { None = 0, Sun, Thunder, Wind, Rain };

constexpr int kCellW = 16;  // pixels per grid cell (world units)
constexpr int kCellH = 11;

struct TileChoice {
  std::string cluster;  // "AA", "BF", ...
  int variation;        // 1-based variation as written in isle.type
  bool fringe;          // bottom-facing piece: hang rock underneath
};

class Island {
 public:
  Island(int w, int h) : w_(w), h_(h), cells_(size_t(w) * h, Element::None), owners_(size_t(w) * h, 0) {}

  int width() const { return w_; }
  int height() const { return h_; }
  Element at(int x, int y) const {
    return (x < 0 || y < 0 || x >= w_ || y >= h_) ? Element::None : cells_[size_t(y) * w_ + x];
  }
  void set(int x, int y, Element e) {
    if (x >= 0 && y >= 0 && x < w_ && y < h_) cells_[size_t(y) * w_ + x] = e;
  }
  bool filled(int x, int y) const { return at(x, y) != Element::None; }
  int ownerAt(int x, int y) const {
    return (x < 0 || y < 0 || x >= w_ || y >= h_) ? 0 : owners_[size_t(y) * w_ + x];
  }
  void setOwner(int x, int y, int o) {
    if (x >= 0 && y >= 0 && x < w_ && y < h_) owners_[size_t(y) * w_ + x] = uint8_t(o);
  }

  // Picks the tile for a filled cell. `seed` varies the grass texture.
  TileChoice tileFor(int x, int y, uint32_t seed = 0) const;

  // Removes cells the tile set can't draw (single-cell spurs/necks), the way
  // the original only allows "legal" island shapes.
  void tidy();

  // World position of the cell's anchor (bottom-right pixel of its tile).
  int worldX = 0, worldY = 0;
  float bob = 0;  // vertical float offset (islands drift gently)
  int owner = 0;  // 0 = nobody, 1..8 = player (edge glow colour)

 private:
  int w_, h_;
  std::vector<Element> cells_;
  std::vector<uint8_t> owners_;
};

// Simple generator for test scenes: an organic blob of the given element.
Island makeBlobIsland(int w, int h, Element e, uint32_t seed);

}  // namespace ns
