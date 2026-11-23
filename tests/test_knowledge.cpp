// Knowledge rules (skirmish/multiplayer): starting Knowledge is every unit
// with no techBit; a finished sacrifice offers Knowledge up to the altar's
// level (consuming the altar) or an altar upgrade. Needs a NetStorm install.
#include <algorithm>
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

static bool has(const std::vector<std::string>& v, const char* s) { return std::find(v.begin(), v.end(), s) != v.end(); }
static bool offers(const std::vector<const TypeInfo*>& v, const char* s) {
  for (auto* t : v) {
    std::string n = t->name;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return std::tolower(c); });
    if (n == s) return true;
  }
  return false;
}

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  GameData gd;
  std::string err;
  if (!gd.load(argv[1], &err)) return 2;
  Island world(40, 30);
  for (int y = 2; y < 28; ++y)
    for (int x = 2; x < 38; ++x) world.set(x, y, Element::Sun), world.setOwner(x, y, 1);
  world.markAllPermanent();
  Sim sim(gd, world);
  sim.knowledgeGifts = true;

  auto starter = sim.starterKnowledge();
  CHECK(has(starter, "sunarcher") && has(starter, "suncannon"), "start with Disc Throwers and Sun Cannons");
  CHECK(!has(starter, "thundercannon") && !has(starter, "windarcher"), "fury units are not starting Knowledge");
  sim.player(1).tech = starter;

  int altar = sim.spawn(gd.registry.byName("altar"), 1, 20, 20);
  CHECK(sim.giftOptions(1).empty(), "no gift without a finished sacrifice");
  sim.player(1).giftAltar = altar;
  auto lv1 = sim.giftOptions(1);
  CHECK(offers(lv1, "thunderbattery") && !offers(lv1, "thundercannon"), "level-1 altar offers only level-1 Knowledge");
  CHECK(!sim.chooseGift(1, "thundercannon"), "can't take Knowledge above the altar's level");

  CHECK(sim.chooseGift(1, "upgrade") && sim.find(altar)->altarLevel == 2, "upgrade raises the altar to level 2");
  CHECK(sim.player(1).giftAltar < 0 && sim.find(altar)->alive, "upgraded altar stays, gift used");

  sim.player(1).giftAltar = altar;  // next sacrifice on the same altar
  CHECK(offers(sim.giftOptions(1), "thundercannon"), "level-2 altar offers Thunder Cannons");
  CHECK(sim.chooseGift(1, "thunderCannon"), "take Thunder Cannon Knowledge");
  CHECK(has(sim.player(1).tech, "thundercannon"), "Thunder Cannons are now known");
  CHECK(!sim.find(altar) || !sim.find(altar)->alive, "Knowledge consumes the altar");

  // Workshop production slots: 2 at level 1, +1 per upgrade.
  sim.player(1).power = 20000;
  sim.player(1).tech = {"sunarcher", "suncannon", "sunblocker", "sunbattery"};
  int ws = sim.spawn(gd.registry.byName("sunFactory"), 1, 10, 10);
  sim.spawn(gd.registry.byName("rainVortex"), 1, 14, 18);  // temple: energy for the build checks
  auto finish = [&]() {
    for (int i = 0; i < 400 && !sim.find(ws)->building.empty(); ++i) sim.step();
    sim.player(1).ready.clear();
  };
  CHECK(sim.startBuild(ws, "sunArcher"), "first unit type takes slot 1");
  finish();
  CHECK(sim.startBuild(ws, "sunCannon"), "second unit type takes slot 2");
  finish();
  std::string why;
  CHECK(!sim.startBuild(ws, "sunBlocker", &why), "third type refused: both slots committed");
  CHECK(sim.buildable(ws).size() == 2, "a full workshop only offers its committed units");
  CHECK(sim.startBuild(ws, "sunArcher"), "committed types can be rebuilt");
  finish();
  CHECK(sim.upgradeWorkshop(ws) && sim.find(ws)->workshopLevel == 2, "upgrade adds a level");
  CHECK(sim.startBuild(ws, "sunBlocker"), "upgrade opens a third slot");
  finish();
  sim.upgradeWorkshop(ws);
  sim.upgradeWorkshop(ws);
  CHECK(!sim.upgradeWorkshop(ws), "only three upgrades per workshop");

  std::printf("%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
  return fails ? 1 : 0;
}
