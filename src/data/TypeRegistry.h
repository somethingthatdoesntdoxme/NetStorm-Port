#pragma once
// Links sprite sets in _shapes.shp to the .type definitions.
//
// The engine keeps a fixed table of 119 type names (netstorm.game, pointer
// table at 0x60dae0). Sprite set N belongs to entry N of that table. Within a
// set, frame k is the image of the k-th frame line of the .type file; for
// types with the `shadow`/`flyershadow` flag a second block follows with the
// shadow of each frame line (frame lines + k).
#include <string>
#include <unordered_map>

#include "data/TypeFile.h"

namespace ns {

struct GameData;

struct TypeInfo {
  int set = -1;                 // sprite set index, -1 if none
  const TypeDef* def = nullptr;  // parsed .type, nullptr if no script
  std::string name;
  bool hasShadows = false;
  int imageFrames = 0;  // number of image frames (shadow block starts here)

  int frame(int line) const { return line; }
  int shadowFrame(int line) const { return hasShadows ? imageFrames + line : -1; }
  // Index of the first frame line in a cluster ("L") with a given variation.
  int findLine(const std::string& cluster, int variation = 0) const;
};

// The engine's fixed type-name table (sprite set order).
const std::vector<std::string>& engineTypeNames();

class TypeRegistry {
 public:
  void build(const GameData& gd);

  const TypeInfo* bySet(int set) const;
  const TypeInfo* byName(const std::string& name) const;  // case-insensitive
  int count() const { return int(sets_.size()); }

 private:
  std::vector<TypeInfo> sets_;
  std::unordered_map<std::string, int> byName_;
};

}  // namespace ns
