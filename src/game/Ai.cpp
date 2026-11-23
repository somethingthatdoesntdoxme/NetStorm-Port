#include "game/Ai.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ns {

void SkirmishAi::step(Sim& sim, Island& world) {
  ++tick_;
  if (tick_ % bridgeEvery_ == 7 % bridgeEvery_) {  // a bridge piece every few seconds
    int x, y;
    if (bestBridgeMove(world, queue_, player_, x, y)) placeBridge(world, queue_.current(), x, y, player_);
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

void SkirmishAi::priestWork(Sim& sim, Island& world) {
  Entity* priest = nullptr;
  int workshops = 0, altars = 0, shooters = 0;
  std::string fury;
  for (auto& e : sim.entities()) {
    if (!e.alive || e.owner != player_) continue;
    if (e.role == Role::Priest && e.chainedAt < 0 && e.carriedBy < 0) priest = &e;
    if (e.role == Role::Workshop) ++workshops;
    if (e.role == Role::Altar) ++altars;
    if (e.role == Role::Shooter) ++shooters;
    if ((e.role == Role::Temple || e.role == Role::Workshop) && fury.empty()) fury = e.type->def->str("theme");
  }
  if (fury.empty() || fury == "Fire") fury = "sun";
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
    for (auto& g : sim.entities())
      if (g.alive && g.role == Role::Golem && g.owner == player_ && !g.carrying &&
          sim.orderCapture(g.id, pr.id))
        break;
  }
  if (!priest || priest->immobile || priest->job != Entity::Job::Idle) return;
  // 3. Construction: a workshop if we have none, an altar once we're fighting.
  const TypeInfo* want = nullptr;
  if (workshops == 0) want = sim.registryType(fury + "Factory");
  else if (altars == 0 && shooters >= 3) want = sim.registryType("altar");
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
    if (e.alive && e.owner == player_) altars += e.role == Role::Altar, shooters += e.role == Role::Shooter;
  float reserve = (altars == 0 && shooters >= 3) ? 600.0f : 0.0f;
  float frontX = 0, frontY = 0;
  bool haveFront = frontPoint(sim, player_, frontX, frontY);
  for (auto& w : sim.entities()) {
    if (!w.alive || w.role != Role::Workshop || w.owner != player_ || !w.building.empty()) continue;
    std::string theme = w.type->def->str("theme");
    auto list = sim.buildable(w.id);
    float px = haveFront ? frontX : w.x, py = haveFront ? frontY : w.y;
    // Generator first if this fury has no energy where the fighting is.
    if (sim.energyAt(player_, theme, int(px), int(py)) < 20) {
      for (auto* t : list)
        if (t->def->str("group") == "battery" && sim.player(player_).power - float(t->def->num("cost", 0)) >= reserve &&
            sim.startBuild(w.id, t->name))
          return;
      continue;
    }
    // Otherwise cycle through affordable shooters.
    std::vector<const TypeInfo*> shooterTypes;
    for (auto* t : list)
      if (t->def->num("range", 0) > 0) shooterTypes.push_back(t);
    if (shooterTypes.empty()) continue;
    size_t pick = (seed_ + uint32_t(tick_ / 20)) % shooterTypes.size();
    for (size_t k = 0; k < shooterTypes.size(); ++k) {
      const TypeInfo* t = shooterTypes[(pick + k) % shooterTypes.size()];
      if (sim.player(player_).power - float(t->def->num("cost", 0)) < reserve) continue;
      if (sim.startBuild(w.id, t->name)) return;
    }
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
    for (auto& e : sim.entities()) {
      if (!e.alive || e.owner == 0 || sim.allied(e.owner, player_) || e.role == Role::Geyser) continue;
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
  else ready.erase(ready.begin());  // nowhere to put it: drop it from the list
}

}  // namespace ns
