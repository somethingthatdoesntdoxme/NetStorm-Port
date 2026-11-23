// Scripted check of the full priest chain, driven through the Sim API:
// damage -> immobilise -> golem capture -> chained at altar -> 5 runes ->
// enemy eliminated. Needs a NetStorm install (path as argv[1]).
#include <cstdio>

#include "data/GameData.h"
#include "game/Sim.h"

using namespace ns;

static int fails = 0;
#define CHECK(c, msg)                                   \
  do {                                                  \
    if (!(c)) {                                         \
      std::printf("FAIL: %s\n", msg);                  \
      ++fails;                                          \
    } else {                                            \
      std::printf("ok:   %s\n", msg);                  \
    }                                                   \
  } while (0)

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  GameData gd;
  std::string err;
  if (!gd.load(argv[1], &err)) {
    std::printf("load failed: %s\n", err.c_str());
    return 2;
  }
  // One flat island: player 1 on the left half, player 2 on the right.
  Island world(60, 30);
  for (int y = 2; y < 28; ++y)
    for (int x = 2; x < 58; ++x) {
      world.set(x, y, Element::Sun);
      world.setOwner(x, y, x < 30 ? 1 : 2);
    }
  world.markAllPermanent();
  Sim sim(gd, world);
  sim.player(1).power = 5000;
  int myPriest = sim.spawn(gd.registry.byName("priest"), 1, 10.5f, 20.5f);
  int enemyPriest = sim.spawn(gd.registry.byName("priest"), 2, 40.5f, 15.5f);
  int golem = sim.spawn(gd.registry.byName("sunWalker"), 1, 12.5f, 22.5f);
  auto run = [&](float seconds) {
    for (int i = 0; i < int(seconds / Sim::kDt); ++i) sim.step();
  };

  // 1. Our priest builds an altar.
  std::string why;
  bool ordered = sim.orderConstruct(myPriest, "altar", 20, 12, &why);
  CHECK(ordered, ("priest can be ordered to build an altar " + why).c_str());
  run(30);
  int altarId = -1;
  for (auto& e : sim.entities())
    if (e.alive && e.role == Role::Altar && e.owner == 1) altarId = e.id;
  CHECK(altarId >= 0 && sim.find(altarId)->built >= 1, "altar finished");

  // 2. Damage the enemy priest: never dies, immobilised at half health.
  sim.damage(*sim.find(enemyPriest), 70);
  CHECK(sim.find(enemyPriest) && sim.find(enemyPriest)->immobile, "enemy priest immobilised at <= 50% hp");
  sim.damage(*sim.find(enemyPriest), 1000);
  CHECK(sim.find(enemyPriest) && sim.find(enemyPriest)->hp >= 1, "force field keeps the priest alive");

  // 3. Golem captures him and brings him to the altar.
  CHECK(sim.orderCapture(golem, enemyPriest, &why), ("golem ordered to capture " + why).c_str());
  for (int i = 0; i < 60 && sim.find(enemyPriest) && sim.find(enemyPriest)->chainedAt < 0; ++i) {
    // Keep him down while the golem walks over (he regenerates).
    if (Entity* p = sim.find(enemyPriest); p && p->carriedBy < 0) sim.damage(*p, 5);
    run(1);
  }
  CHECK(sim.find(enemyPriest) && sim.find(enemyPriest)->chainedAt == altarId, "enemy priest chained at our altar");

  // 4. Our priest goes to the altar and wards the five runes.
  Entity* a = sim.find(altarId);
  CHECK(sim.orderMove(myPriest, int(a->x) - 3, int(a->y) + 1), "our priest walks to the altar");
  run(60);
  CHECK(sim.eliminated(2), "player 2 eliminated by the sacrifice");
  CHECK(sim.winner() == 1, "player 1 wins");
  for (auto& ev : sim.events()) std::printf("   %s\n", ev.c_str());
  std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
  return fails ? 1 : 0;
}
