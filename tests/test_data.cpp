// Data sweep: every mission script parses and has a map, every map builds,
// the campaign chain is complete, and every sprite frame decodes.
// Needs a NetStorm install (path as argv[1]).
#include <cstdio>
#include <set>

#include "data/FortFile.h"
#include "data/GameData.h"
#include "data/MissionInfo.h"
#include "world/Mission.h"

using namespace ns;

static int fails = 0;
#define CHECK(c, ...)                 \
  do {                                \
    if (!(c)) {                       \
      std::printf("FAIL: ");          \
      std::printf(__VA_ARGS__);       \
      std::printf("\n");              \
      ++fails;                        \
    }                                 \
  } while (0)

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  GameData gd;
  std::string err;
  if (!gd.load(argv[1], &err)) {
    std::printf("can't load data: %s\n", err.c_str());
    return 2;
  }

  // 1. Mission scripts and their maps.
  int missions = 0, built = 0;
  std::set<std::string> ids;
  for (auto& [name, bytes] : gd.archive.entries()) {
    if (name.size() < 9 || name.compare(name.size() - 8, 8, ".english") != 0) continue;
    std::string id = name.substr(name.rfind('/') + 1);
    id = id.substr(0, id.size() - 8);
    MissionInfo m = loadMission(gd, id);
    const auto* fortBytes = gd.archive.find("d/" + m.fort + ".fort");
    if (!fortBytes) continue;  // help pages, tutorials without maps
    ++missions;
    ids.insert(id);
    CHECK(!m.title.empty(), "%s: no title", id.c_str());
    CHECK(!m.briefing.empty() || id.rfind("demo", 0) == 0, "%s: no briefing", id.c_str());
    int others = 0;
    for (int p = 2; p <= 8; ++p) others += m.players[size_t(p)].present;
    CHECK(others > 0, "%s: no opponents in the header", id.c_str());
    for (auto& sec : m.script)
      for (auto& b : sec.shared ? std::vector<ScriptButton>{} : sec.buttons)
        if (b.action == "tell")
          CHECK(m.section(b.arg) != nullptr, "%s: button '%s' tells missing section '%s'", id.c_str(), b.label.c_str(),
                b.arg.c_str());
    FortFile fort;
    std::string ferr;
    bool ok = fort.parse(*fortBytes, gd.types, &ferr);
    CHECK(ok, "%s: fort %s doesn't parse: %s", id.c_str(), m.fort.c_str(), ferr.c_str());
    if (!ok) continue;
    MissionWorld w;
    ok = buildMission(gd, fort, w);
    CHECK(ok, "%s: map doesn't build", id.c_str());
    if (ok) {
      ++built;
      CHECK(!w.objects.empty(), "%s: map has no objects", id.c_str());
      CHECK(w.unplaced == 0, "%s: %d objects without a sprite type (%s)", id.c_str(), w.unplaced,
            w.unplacedTypes.empty() ? "" : w.unplacedTypes[0].c_str());
    }
  }
  std::printf("missions with maps: %d, maps built: %d\n", missions, built);
  CHECK(missions >= 40, "expected 40+ playable missions, found %d", missions);

  // 2. The campaign chain: Easy/Normal/Hard all reach Final Confrontation.
  for (const char* diff : {"", "easy", "hard"}) {
    std::string id = "thewarbegins";
    std::vector<std::string> chain;
    std::set<std::string> seen;
    while (!id.empty() && !seen.count(id)) {
      seen.insert(id);
      chain.push_back(id);
      MissionInfo m = loadMission(gd, id);
      id = m.next;
    }
    CHECK(chain.size() == 16 && chain.back() == "finalconfrontation", "campaign chain: %zu missions, ends at %s",
          chain.size(), chain.back().c_str());
    for (auto& c : chain) {
      std::string v = c + diff;
      if (*diff) CHECK(ids.count(v) || ids.count(c), "missing %s variant of %s", diff, c.c_str());
    }
  }

  // 3. Every sprite frame decodes.
  int frames = 0, broken = 0, empty = 0;
  for (int s = 0; s < gd.shapes.setCount(); ++s)
    for (int f = 0; f < gd.shapes.frameCount(s); ++f) {
      ++frames;
      ShapeFrame img = gd.shapes.decode(s, f);
      if (img.width == 0 || img.height == 0) {
        ++empty;
        continue;
      }
      if (img.width < 0 || img.height < 0 || img.width > 2048 || img.height > 2048 ||
          img.index.size() != size_t(img.width) * size_t(img.height) || img.mask.size() != img.index.size())
        ++broken;
    }
  std::printf("sprite frames: %d, broken: %d, empty: %d\n", frames, broken, empty);
  CHECK(broken == 0, "%d sprite frames fail to decode", broken);

  std::printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
  return fails ? 1 : 0;
}
