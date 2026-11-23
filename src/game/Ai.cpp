#include "game/Ai.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ns {

static std::string lowerStr(std::string s);

void SkirmishAi::step(Sim& sim, Island& world) {
  ++tick_;
  world_ = &world;
  if (tick_ % bridgeEvery_ == 7 % bridgeEvery_) {  // a bridge piece every few seconds
    int x, y;
    bool moved = bestBridgeMove(world, queue_, player_, x, y);
    if (!moved) {  // nothing left to claim: get within shooting range of enemy buildings
      std::vector<std::pair<int, int>> goals;
      for (auto& e : sim.entities())
        if (e.alive && e.owner > 0 && e.owner != player_ && !sim.allied(e.owner, player_) &&
            e.role != Role::Golem && e.role != Role::Geyser && e.role != Role::Flyer && e.role != Role::Priest &&
            e.carriedBy < 0)
          goals.push_back({int(e.x), int(e.y)});
      int reach = 0;
      for (auto& w : sim.entities())
        if (w.alive && w.owner == player_ && w.role == Role::Workshop)
          for (auto* t : sim.buildable(w.id)) {
            int r = int(t->def->num("range", 0));
            if (r > reach) reach = r;  // our longest reach decides how close we must get
          }
      if (reach == 0) reach = 12;
      // Reached firing range but no room to place there? Keep pushing in with
      // tighter goals so the foothold gets wide enough for real units.
      for (int r = std::clamp(reach - 2, 5, 30); !goals.empty() && !moved && r >= 4; r -= 5)
        moved = bestBridgeMove(world, queue_, player_, x, y, 0, &goals, r);
    }
    if (moved) placeBridge(world, queue_.current(), x, y, player_);
    queue_.next();
  }
  if (tick_ % 20 == 3) produce(sim);
  if (tick_ % 10 == 5) dropReady(sim, world);
  if (tick_ % 40 == 11) priestWork(sim, world);
}

bool SkirmishAi::findSite(Sim& sim, Island& world, int priestId, const TypeInfo* t, int& bx, int& by) {
  const Entity* pr = sim.find(priestId);
  if (!pr) return false;
  float best = 1e18f;
  bool found = false;
  for (int y = 1; y < world.height() - 1; y += 2)
    for (int x = 1; x < world.width() - 1; x += 2) {
      if (!world.filled(x, y) || world.ownerAt(x, y) != player_) continue;
      float d = (x - pr->x) * (x - pr->x) + (y - pr->y) * (y - pr->y);
      if (d >= best) continue;
      if (sim.canConstruct(priestId, t, x, y)) best = d, bx = x, by = y, found = true;
    }
  return found;
}

static std::string lowerStr(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Which furies' workshops we need: one for our best attack unit, one for a
// generator that can power it (sun units take any energy). No tech list =
// everything allowed: our own fury plus sun.
std::vector<std::string> SkirmishAi::neededWorkshops(Sim& sim, const std::string& fury) {
  const auto& tech = sim.player(player_).tech;
  std::vector<std::string> out;
  auto add = [&](const std::string& t) {
    if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
  };
  if (tech.empty()) {
    add(lowerStr(fury));
    add("sun");
    return out;
  }
  std::string shooterTheme, batteryTheme;
  double bestCost = -1;
  for (auto& name : tech) {
    const TypeInfo* t = sim.registryType(name);
    if (!t || !t->def) continue;
    std::string th = lowerStr(t->def->str("theme"));
    if (t->def->num("range", 0) > 0 && t->def->num("cost", 0) > 0 && t->def->num("cost", 0) > bestCost &&
        !t->def->hasFlag("walker"))
      bestCost = t->def->num("cost", 0), shooterTheme = th;
  }
  for (auto& name : tech) {
    const TypeInfo* t = sim.registryType(name);
    if (!t || !t->def || t->def->str("group") != "battery") continue;
    std::string th = lowerStr(t->def->str("theme"));
    if (shooterTheme == "sun" || th == shooterTheme) {
      batteryTheme = th;
      if (th == shooterTheme) break;
    }
  }
  add("sun");  // common Knowledge: always worth a workshop
  add(shooterTheme);
  add(batteryTheme);
  return out;
}

void SkirmishAi::priestWork(Sim& sim, Island& world) {
  Entity* priest = nullptr;
  int workshops = 0, altars = 0, shooters = 0;
  std::string fury;
  for (auto& e : sim.entities()) {
    if (!e.alive || e.owner != player_) continue;
    if (e.role == Role::Priest && e.chainedAt < 0 && e.carriedBy < 0) priest = &e;
    if (e.role == Role::Workshop) ++workshops;
    if (e.role == Role::Altar && e.built >= 1) ++altars;  // half-built ones get resumed or replaced
    if (e.role == Role::Shooter) ++shooters;
    if ((e.role == Role::Temple || e.role == Role::Workshop) && fury.empty()) fury = e.type->def->str("theme");
  }
  if (fury.empty() || fury == "Fire") fury = "sun";
  // 0. A finished sacrifice: pick our gift.
  if (sim.player(player_).giftAltar >= 0) {
    auto opts = sim.giftOptions(player_);
    const TypeInfo* pick = nullptr;
    double best = -1;
    for (auto* t : opts) {  // best attack unit of our fury (or sun), by cost
      std::string th = lowerStr(t->def->str("theme"));
      if (t->def->num("range", 0) > 0 && (th == lowerStr(fury) || th == "sun") && t->def->num("cost", 0) > best)
        best = t->def->num("cost", 0), pick = t;
    }
    if (!pick)  // then that fury's generator
      for (auto* t : opts)
        if (t->def->str("group") == "battery" && lowerStr(t->def->str("theme")) == lowerStr(fury)) pick = t;
    if (pick) sim.chooseGift(player_, pick->name);
    else if (sim.canUpgradeAltar(player_)) sim.chooseGift(player_, "upgrade");
    else if (!opts.empty()) sim.chooseGift(player_, opts.front()->name);
  }
  // 1. Sacrifice: if we hold a captive, send our priest to the altar.
  for (auto& a : sim.entities()) {
    if (!a.alive || a.role != Role::Altar || a.owner != player_ || a.captive < 0 || !priest) continue;
    if (std::abs(priest->x - (a.x - 3)) > 4 || std::abs(priest->y - (a.y - 3)) > 4)
      if (priest->job == Entity::Job::Idle) sim.orderMove(priest->id, int(a.x) - 3, int(a.y) + 1);
    return;
  }
  // 2. Capture: any immobilised enemy priest -> send an idle golem.
  for (auto& pr : sim.entities()) {
    if (!pr.alive || pr.role != Role::Priest || sim.allied(pr.owner, player_) || pr.owner == 0 || !pr.immobile)
      continue;
    if (pr.carriedBy >= 0 || pr.chainedAt >= 0) continue;
    bool already = false;
    for (auto& g : sim.entities())
      if (g.alive && g.role == Role::Golem && g.owner == player_ && g.target == pr.id) already = true;
    if (already) continue;
    std::string why;
    bool sent = false;
    // Nearest golem first; a capture is worth more than the crystals it's holding.
    std::vector<std::pair<float, int>> golems;
    for (auto& g : sim.entities())
      if (g.alive && g.role == Role::Golem && g.owner == player_ && g.carryingPriest < 0)
        golems.push_back({(g.x - pr.x) * (g.x - pr.x) + (g.y - pr.y) * (g.y - pr.y), g.id});
    std::sort(golems.begin(), golems.end());
    for (auto& [d, id] : golems)
      if (sim.orderCapture(id, pr.id, &why)) {
        sent = true;
        break;
      }
    if (golems.empty()) why = "no golems";
    if (!sent && tick_ % 1200 == 11) sim.log("P" + std::to_string(player_) + " AI capture failed: " + why);
  }
  if (!priest || priest->immobile || priest->job != Entity::Job::Idle) return;
  // Finish anything we left half-built (e.g. after being knocked down) before starting more.
  for (auto& b : sim.entities())
    if (b.alive && b.owner == player_ && b.built < 1 && sim.orderResume(priest->id, b.id)) return;
  // 3. Construction: the workshops our Knowledge needs, then an altar once we're fighting.
  const TypeInfo* want = nullptr;
  for (auto& th : neededWorkshops(sim, fury)) {
    bool have = false;
    for (auto& e : sim.entities())
      if (e.alive && e.owner == player_ && e.role == Role::Workshop && lowerStr(e.type->def->str("theme")) == th)
        have = true;
    if (!have) {
      want = sim.registryType(th + "Factory");
      break;
    }
  }
  if (!want && altars == 0 && shooters >= 3) want = sim.registryType("altar");
  if (!want) return;
  int x, y;
  if (findSite(sim, world, priest->id, want, x, y)) sim.orderConstruct(priest->id, want->name, x, y);
}

// Our territory cell closest to the nearest enemy building: where new
// shooters go, so that's where energy is needed.
static bool frontPoint(Sim& sim, int player, float& fx, float& fy) {
  float best = 1e18f;
  bool any = false;
  for (auto& m : sim.entities()) {
    if (!m.alive || m.owner != player || !m.type || !m.type->def) continue;
    for (auto& e : sim.entities()) {
      if (!e.alive || e.owner == 0 || sim.allied(e.owner, player) || e.role == Role::Geyser || e.role == Role::Golem)
        continue;
      float d = (e.x - m.x) * (e.x - m.x) + (e.y - m.y) * (e.y - m.y);
      if (d < best) best = d, fx = m.x, fy = m.y, any = true;
    }
  }
  return any;
}

void SkirmishAi::produce(Sim& sim) {
  // Save up for an altar once we're fighting and don't have one.
  int altars = 0, shooters = 0;
  for (auto& e : sim.entities())
    if (e.alive && e.owner == player_) altars += e.role == Role::Altar && e.built >= 1, shooters += e.role == Role::Shooter;
  float reserve = (altars == 0 && shooters >= 3) ? 600.0f : 0.0f;
  float frontX = 0, frontY = 0;
  bool haveFront = frontPoint(sim, player_, frontX, frontY);
  // A balloon when free geysers lie out of reach of our golems.
  {
    int balloons = 0;
    for (auto& e : sim.entities()) balloons += e.alive && e.owner == player_ && e.air;
    bool freeGeyser = false;
    for (auto& g : sim.entities())
      if (g.alive && g.role == Role::Geyser && g.crystals > 0 && world_ && world_->ownerAt(int(g.x), int(g.y)) == 0)
        freeGeyser = true;
    if (balloons < 1 && freeGeyser)
      for (auto& w : sim.entities()) {
        if (!w.alive || w.role != Role::Workshop || w.owner != player_ || !w.building.empty()) continue;
        for (auto* t : sim.buildable(w.id))
          if (t->def->hasFlag("balloon") && sim.player(player_).power >= float(t->def->num("cost", 0)) + 300 &&
              sim.startBuild(w.id, t->name))
            return;
      }
  }
  // Upgrade a workshop whose slots are full when we know a better unit it could make.
  if (sim.knowledgeGifts)
    for (auto& w : sim.entities()) {
      if (!w.alive || w.role != Role::Workshop || w.owner != player_ || w.built < 1 || w.workshopLevel >= 4) continue;
      if (int(w.slots.size()) < sim.slotCapacity(w)) continue;
      std::string theme = lowerStr(w.type->def->str("theme"));
      bool wants = false;
      for (auto& name : sim.player(player_).tech) {
        const TypeInfo* t = sim.registryType(name);
        if (t && t->def && lowerStr(t->def->str("theme")) == theme && t->def->num("range", 0) > 0 &&
            std::find(w.slots.begin(), w.slots.end(), t->name) == w.slots.end())
          wants = true;
      }
      if (wants && sim.player(player_).power >= float(sim.upgradeCost(w)) + 400 && sim.upgradeWorkshop(w.id)) return;
    }
  if (sim.player(player_).ready.size() >= 2) return;  // place what we have first
  for (auto& w : sim.entities()) {
    if (!w.alive || w.role != Role::Workshop || w.owner != player_ || !w.building.empty()) continue;
    auto list = sim.buildable(w.id);
    float px = haveFront ? frontX : w.x, py = haveFront ? frontY : w.y;
    float power = sim.player(player_).power;
    // Shooters that would actually work at the front (enough energy of the right kind).
    std::vector<const TypeInfo*> shooterTypes;
    bool unpowered = false;
    for (auto* t : list) {
      if (t->def->num("range", 0) <= 0) continue;
      int need = int(t->def->num("minUsage", 0));
      if (need > 0 && sim.energyAt(player_, t->def->str("theme"), int(px), int(py)) < need) {
        unpowered = true;
        continue;
      }
      shooterTypes.push_back(t);
    }
    // Need energy first? Build a generator here if this workshop makes one.
    if (shooterTypes.empty() && unpowered) {
      for (auto* t : list)
        if (t->def->str("group") == "battery" && power - float(t->def->num("cost", 0)) >= reserve &&
            sim.startBuild(w.id, t->name))
          return;
      continue;  // another workshop may make the generator
    }
    // Also keep a generator coming every so often so the front stays powered.
    if (unpowered && (tick_ / 20) % 3 == 0)
      for (auto* t : list)
        if (t->def->str("group") == "battery" && power - float(t->def->num("cost", 0)) >= reserve &&
            sim.startBuild(w.id, t->name))
          return;
    if (shooterTypes.empty()) continue;
    size_t pick = (seed_ + uint32_t(tick_ / 20)) % shooterTypes.size();
    for (size_t k = 0; k < shooterTypes.size(); ++k) {
      const TypeInfo* t = shooterTypes[(pick + k) % shooterTypes.size()];
      if (power - float(t->def->num("cost", 0)) < reserve) continue;
      if (sim.startBuild(w.id, t->name)) return;
    }
  }
  // A generator-only workshop builds generators when another one is starved.
  for (auto& w : sim.entities()) {
    if (!w.alive || w.role != Role::Workshop || w.owner != player_ || !w.building.empty()) continue;
    bool starving = false;
    for (auto& o : sim.entities()) {
      if (!o.alive || o.role != Role::Workshop || o.owner != player_ || o.id == w.id) continue;
      for (auto* t : sim.buildable(o.id)) {
        int need = int(t->def->num("minUsage", 0));
        if (t->def->num("range", 0) > 0 && need > 0 &&
            sim.energyAt(player_, t->def->str("theme"), int(haveFront ? frontX : o.x), int(haveFront ? frontY : o.y)) < need)
          starving = true;
      }
    }
    if (!starving) continue;
    for (auto* t : sim.buildable(w.id))
      if (t->def->str("group") == "battery" && sim.player(player_).power - float(t->def->num("cost", 0)) >= reserve &&
          sim.startBuild(w.id, t->name))
        return;
  }
}

void SkirmishAi::dropReady(Sim& sim, Island& world) {
  auto& ready = sim.player(player_).ready;
  if (ready.empty()) return;
  const TypeInfo* t = nullptr;
  for (int s = 0; s < 1 && !ready.empty(); ++s) {
    // Resolve the type through any entity's registry (workshops know theirs).
    for (auto& e : sim.entities())
      if (e.type && e.type->def && e.type->name == ready[0]) t = e.type;
  }
  // Fall back: look up via a temporary spawn-free search of buildable lists.
  if (!t)
    for (auto& w : sim.entities())
      if (w.role == Role::Workshop && w.owner == player_)
        for (auto* c : sim.buildable(w.id))
          if (c->name == ready[0]) t = c;
  if (!t) return;
  // Aim point: nearest enemy building for shooters, own workshop for generators.
  float axp = 0, ayp = 0, myx = 0, myy = 0;
  int mine = 0;
  for (auto& e : sim.entities())
    if (e.alive && e.owner == player_ && e.role == Role::Workshop) myx += e.x, myy += e.y, ++mine;
  if (!mine) return;
  myx /= float(mine);
  myy /= float(mine);
  float best = std::numeric_limits<float>::max();
  {  // shooters and generators both go towards the enemy
    // Aim at enemy buildings (golems and flyers wander, so they make bad aim points).
    for (auto& e : sim.entities()) {
      if (!e.alive || e.owner == 0 || sim.allied(e.owner, player_) || e.role == Role::Geyser ||
          e.role == Role::Golem || e.role == Role::Flyer || e.role == Role::Priest)
        continue;
      float d = std::abs(e.x - myx) + std::abs(e.y - myy);
      if (d < best) best = d, axp = e.x, ayp = e.y;
    }
    if (best == std::numeric_limits<float>::max()) axp = myx, ayp = myy;
  }
  // Search legal drop spots near our territory, closest to the aim point.
  int bx = -1, by = -1;
  float bd = std::numeric_limits<float>::max();
  int x0 = world.width(), y0 = world.height(), x1 = 0, y1 = 0;
  for (int y = 0; y < world.height(); ++y)
    for (int x = 0; x < world.width(); ++x)
      if ((world.filled(x, y) && world.ownerAt(x, y) == player_) || world.bridgeAt(x, y) == player_)
        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
  for (int y = std::max(2, y0); y <= std::min(world.height() - 3, y1); ++y)
    for (int x = std::max(2, x0); x <= std::min(world.width() - 3, x1); ++x) {
      bool near = (world.filled(x, y) && world.ownerAt(x, y) == player_) || world.bridgeAt(x, y) == player_;
      if (!near) continue;
      // Try drop anchors around this cell.
      for (int dy = -4; dy <= 4; dy += 2)
        for (int dx = -4; dx <= 4; dx += 2) {
          int ax = x + dx, ay = y + dy;
          float d = (ax - axp) * (ax - axp) + (ay - ayp) * (ay - ayp);
          if (d >= bd) continue;
          if (sim.canDropUnit(t, ax, ay, player_)) bd = d, bx = ax, by = ay;
        }
    }
  if (bx >= 0) sim.dropUnit(player_, 0, bx, by);
  // Nowhere to put it yet: keep it (it's paid for) and try again once we've
  // bridged further. Rotate so one awkward unit doesn't block the rest.
  else if (ready.size() > 1) std::rotate(ready.begin(), ready.begin() + 1, ready.end());
}

}  // namespace ns
