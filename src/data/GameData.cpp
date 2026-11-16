#include "data/GameData.h"

#include "data/PeResources.h"

namespace ns {

bool GameData::load(const std::filesystem::path& installDir, std::string* err) {
  auto fail = [&](const std::string& m) {
    if (err) *err = m;
    return false;
  };
  root = installDir;

  auto tarc = findFileCI(root, "netstorm.tarc");
  if (!tarc) return fail("netstorm.tarc not found in " + root.string());
  std::string e;
  if (!archive.open(*tarc, &e)) return fail("netstorm.tarc: " + e);

  auto dDir = findFileCI(root, "d");
  if (!dDir) return fail("data folder 'd' not found in " + root.string());
  auto shp = findFileCI(*dDir, "_shapes.shp");
  if (!shp) return fail("d/_shapes.shp not found");
  if (!shapes.open(*shp, &e)) return fail("_shapes.shp: " + e);

  // Game palette: the 8-bit bitmap resource #139 in netstorm.game carries the
  // in-game palette. (Provisional -- verify against real screenshots.)
  bool havePal = false;
  if (auto game = findFileCI(root, "netstorm.game")) {
    if (auto pe = readFile(*game)) {
      if (auto dib = peResource(*pe, 2 /*RT_BITMAP*/, 139)) {
        if (auto p = Palette::fromDib(*dib)) {
          gamePalette = *p;
          havePal = true;
        }
      }
    }
  }
  if (!havePal) return fail("could not read the game palette from netstorm.game");

  if (auto cd = findFileCI(*dDir, "!color.dat")) {
    if (auto b = readFile(*cd)) remaps = RemapTables::fromColorDat(*b);
  }

  for (auto& [name, bytes] : archive.entries()) {
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".type") == 0)
      types.push_back(parseTypeFile(std::string(bytes.begin(), bytes.end()), name));
  }
  registry.build(*this);
  return true;
}

}  // namespace ns
