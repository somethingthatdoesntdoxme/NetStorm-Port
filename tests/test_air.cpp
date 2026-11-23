// Balloons: aerial transports gather Storm Power across open sky with no
// bridges; golems can't. Needs a NetStorm install (path as argv[1]).
#include <cstdio>

#include "data/GameData.h"
#include "game/Sim.h"

using namespace ns;

static int fails = 0;
#define CHECK(c, msg)                     \
  do {                                    \
    if (!(c)) {                           \
      std::printf("FAIL: %s\n", msg);    \
      ++fails;                            \
    } else {                              \
      std::printf("ok:   %s\n", msg);    \
    }                                     \
  } while (0)

static int runWith(const GameData& gd, const char* transport) {
  // Home island (left) and a geyser island (right), 12 cells of sky between.
  Island world(60, 24);
  for (int y = 4; y < 20; ++y)
    for (int x = 2; x < 22; ++x) world.set(x, y, Element::Sun), world.setOwner(x, y, 1);
  for (int y = 8; y < 16; ++y)
    for (int x = 34; x < 44; ++x) world.set(x, y, Element::Sun);
  world.markAllPermanent();
  Sim sim(gd, world);
  sim.player(1).golemTarget = 0;  // no free replacements in this test
  int temple = sim.spawn(gd.registry.byName("rainVortex"), 1, 12, 12);
  sim.spawn(gd.registry.byName("geyser"), 0, 40, 12);
  sim.spawnWalkerNear(gd.registry.byName(transport), 1, temple);
  for (int i = 0; i < int(120 / Sim::kDt); ++i) sim.step();
  return sim.player(1).delivered;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  GameData gd;
  std::string err;
  if (!gd.load(argv[1], &err)) return 2;
  int byGolem = runWith(gd, "sunWalker");
  int byBalloon = runWith(gd, "sunBalloon");
  std::printf("delivered: golem %d, balloon %d\n", byGolem, byBalloon);
  CHECK(byGolem == 0, "a golem can't reach a geyser across open sky");
  CHECK(byBalloon > 0, "a balloon flies over and brings back Storm Crystals");
  std::printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
  return fails ? 1 : 0;
}
