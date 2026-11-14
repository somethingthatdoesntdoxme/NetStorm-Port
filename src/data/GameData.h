#pragma once
// Everything loaded from the user's own NetStorm install. Nothing from the
// original game is stored in this repository.
#include <memory>

#include "data/Palette.h"
#include "data/ShapeDb.h"
#include "data/Taff.h"
#include "data/TypeFile.h"

namespace ns {

struct GameData {
  std::filesystem::path root;  // folder containing Netstorm.exe / netstorm.game
  TaffArchive archive;
  ShapeDb shapes;
  Palette gamePalette;
  RemapTables remaps;
  std::vector<TypeDef> types;

  // Loads all of the above. On failure returns false and fills `err`.
  bool load(const std::filesystem::path& installDir, std::string* err);
};

}  // namespace ns
