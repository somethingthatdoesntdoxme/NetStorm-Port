#include "game/Sim.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace ns {

namespace {
int footX(const TypeInfo* t) { return t && t->def ? std::max(1, int(t->def->num("foot_x", 1))) : 1; }
int footY(const TypeInfo* t) { return t && t->def ? std::max(1, int(t->def->num("foot_y", 1))) : 1; }
constexpr int kCrystal = 200;     // one nugget
constexpr float kMineTime = 2.0f;  // seconds to fill up at a geyser
}  // namespace

Role roleFor(const TypeInfo* t) {
  if (!t || !t->def) return Role::Static;
  const TypeDef& d = *t->def;
  if (d.hasFlag("priest")) return Role::Priest;
  if (d.hasFlag("geyser")) return Role::Geyser;
  if (d.hasFlag("vortex")) return Role::Temple;
  if (d.hasFlag("factory")) return Role::Workshop;
  if (d.hasFlag("walker") && t->name == "sunWalker") return Role::Golem;
  if (d.str("group") == "battery") return Role::Generator;
  if (d.hasFlag("emplacement")) return Role::Shooter;
  return Role::Static;
}

int Sim::spawn(const TypeInfo* t, int owner, float x, float y) {
  Entity e;
  e.id = nextId_++;
  e.type = t;
  e.role = roleFor(t);
  e.owner = owner;
  e.x = x;
  e.y = y;
  e.maxHp = e.hp = t && t->def ? int(t->def->num("maxHitPoints", 100)) : 100;
  e.animOffset = float((int(x) * 7 + int(y) * 13) % 10) * 0.1f;
  if (e.role == Role::Geyser) e.crystals = t->def ? int(t->def->num("cost", 2000)) : 2000;
  ents_.push_back(e);
  return e.id;
}

Entity* Sim::find(int id) {
  for (auto& e : ents_)
    if (e.id == id && e.alive) return &e;
  return nullptr;
}

void Sim::step() {
  time_ += kDt;
  for (size_t i = 0; i < ents_.size(); ++i) {
    Entity& e = ents_[i];
    if (!e.alive) continue;
    if (e.role == Role::Golem) stepGolem(e);
    else if (e.role == Role::Workshop) stepWorkshop(e);
  }
}

// Cell next to a building's footprint that a walker can stand on.
void Sim::approach(const Entity& b, int& cx, int& cy) const {
  int fx = footX(b.type), fy = footY(b.type);
  int x0 = int(b.x) - fx + 1, y0 = int(b.y) - fy + 1;
  // Prefer the cell just below the middle of the footprint, then any edge cell.
  cx = x0 + fx / 2;
  cy = int(b.y) + 1;
  if (walkable(cx, cy)) return;
  for (int y = y0 - 1; y <= int(b.y) + 1; ++y)
    for (int x = x0 - 1; x <= int(b.x) + 1; ++x)
      if ((x < x0 || x > int(b.x) || y < y0 || y > int(b.y)) && walkable(x, y)) {
        cx = x;
        cy = y;
        return;
      }
  cx = x0 + fx / 2;
  cy = y0 + fy / 2;  // inside (golems can stand on the building's island)
}

// BFS over land + bridges from (sx,sy) to the cell (tx,ty).
std::vector<std::pair<int, int>> Sim::findPath(int sx, int sy, int tx, int ty, int, int) const {
  const int W = world_.width(), H = world_.height();
  auto idx = [&](int x, int y) { return size_t(y) * W + x; };
  if (sx < 0 || sy < 0 || sx >= W || sy >= H || tx < 0 || ty < 0 || tx >= W || ty >= H) return {};
  std::vector<int> prev(size_t(W) * H, -2);
  std::deque<std::pair<int, int>> q;
  q.push_back({sx, sy});
  prev[idx(sx, sy)] = -1;
  bool found = false;
  // Bound the search to a window around start/target to keep it cheap.
  int bx0 = std::min(sx, tx) - 40, bx1 = std::max(sx, tx) + 40;
  int by0 = std::min(sy, ty) - 40, by1 = std::max(sy, ty) + 40;
  while (!q.empty()) {
    auto [x, y] = q.front();
    q.pop_front();
    if (x == tx && y == ty) {
      found = true;
      break;
    }
    const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (auto& n : nb) {
      int nx = x + n[0], ny = y + n[1];
      if (nx < bx0 || ny < by0 || nx > bx1 || ny > by1 || nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
      if (prev[idx(nx, ny)] != -2 || (!walkable(nx, ny) && !(nx == tx && ny == ty))) continue;
      prev[idx(nx, ny)] = int(idx(x, y));
      q.push_back({nx, ny});
    }
  }
  if (!found) return {};
  std::vector<std::pair<int, int>> path;
  for (int i = int(idx(tx, ty)); i >= 0 && i != int(idx(sx, sy)); i = prev[size_t(i)]) path.push_back({i % W, i / W});
  std::reverse(path.begin(), path.end());
  return path;
}

void Sim::stepGolem(Entity& g) {
  const float speed = g.type && g.type->def ? float(g.type->def->num("speed", 1.8)) : 1.8f;
  auto walk = [&]() -> bool {  // returns true when the path is finished
    float budget = speed * kDt;
    while (budget > 0 && !g.path.empty()) {
      float tx = g.path.front().first + 0.5f, ty = g.path.front().second + 0.5f;
      float dx = tx - g.x, dy = ty - g.y, d = std::sqrt(dx * dx + dy * dy);
      if (d <= budget) {
        g.x = tx;
        g.y = ty;
        budget -= d;
        g.path.erase(g.path.begin());
      } else {
        g.x += dx / d * budget;
        g.y += dy / d * budget;
        budget = 0;
      }
    }
    return g.path.empty();
  };
  auto planTo = [&](const Entity& b) {
    int cx, cy;
    approach(b, cx, cy);
    g.path = findPath(int(g.x), int(g.y), cx, cy, 0, 0);
    return !g.path.empty() || (int(g.x) == cx && int(g.y) == cy);
  };

  switch (g.job) {
    case Entity::Job::Idle: {
      // Home temple + nearest reachable geyser with crystals on our land.
      if (!find(g.home)) {
        g.home = -1;
        for (auto& e : ents_)
          if (e.alive && e.role == Role::Temple && e.owner == g.owner) g.home = e.id;
      }
      if (g.home < 0) return;
      g.timer -= kDt;
      if (g.timer > 0) return;
      g.timer = 1.0f;  // re-plan at most once a second when idle
      float bestD = std::numeric_limits<float>::max();
      int best = -1;
      for (auto& e : ents_) {
        if (!e.alive || e.role != Role::Geyser || e.crystals <= 0) continue;
        if (world_.ownerAt(int(e.x), int(e.y)) != g.owner) continue;  // only geysers we hold
        float d = std::abs(e.x - g.x) + std::abs(e.y - g.y);
        if (d < bestD) bestD = d, best = e.id;
      }
      if (best < 0) return;
      g.target = best;
      if (planTo(*find(best))) g.job = Entity::Job::ToGeyser;
      break;
    }
    case Entity::Job::ToGeyser:
      if (walk()) {
        g.job = Entity::Job::Mining;
        g.timer = kMineTime;
      }
      break;
    case Entity::Job::Mining: {
      g.timer -= kDt;
      if (g.timer > 0) return;
      Entity* gy = find(g.target);
      Entity* home = find(g.home);
      if (!gy || !home || gy->crystals <= 0) {
        g.job = Entity::Job::Idle;
        return;
      }
      gy->crystals -= kCrystal;
      g.carrying = true;
      if (planTo(*home)) g.job = Entity::Job::ToTemple;
      else g.job = Entity::Job::Idle;
      break;
    }
    case Entity::Job::ToTemple:
      if (walk()) {
        if (g.carrying) {
          players_[size_t(g.owner)].power += kCrystal;
          players_[size_t(g.owner)].delivered++;
        }
        g.carrying = false;
        Entity* gy = find(g.target);
        if (gy && gy->crystals > 0 && planTo(*gy)) g.job = Entity::Job::ToGeyser;
        else g.job = Entity::Job::Idle;
      }
      break;
  }
}

int Sim::energyAt(int player, const std::string& theme, int x, int y) const {
  int total = 0;
  for (auto& e : ents_) {
    if (!e.alive || e.owner != player || !e.type || !e.type->def) continue;
    bool gen = e.role == Role::Generator || e.role == Role::Temple;
    if (!gen || e.type->def->str("theme") != theme) continue;
    // Generators: -minUsage energy within ~20 cells; temples power their fury too.
    int amount = e.role == Role::Temple ? 100 : -int(e.type->def->num("minUsage", -100));
    float dx = e.x - x, dy = e.y - y;
    if (dx * dx + dy * dy <= 22.0f * 22.0f) total += amount;
  }
  return total;
}

std::vector<const TypeInfo*> Sim::buildable(int workshopId) const {
  std::vector<const TypeInfo*> out;
  const Entity* w = nullptr;
  for (auto& e : ents_)
    if (e.id == workshopId) w = &e;
  if (!w || !w->type->def) return out;
  std::string theme = w->type->def->str("theme");
  for (int s = 0; s < gd_.registry.count(); ++s) {
    const TypeInfo* t = gd_.registry.bySet(s);
    if (!t || !t->def || t->def->str("theme") != theme) continue;
    if (t->def->num("cost", 0) <= 0 || t->name == "bulf") continue;
    bool unit = t->def->hasFlag("emplacement") || (t->def->hasFlag("walker") && t->name != "sunWalker") ||
                t->def->hasFlag("flyer") || t->def->str("group") == "battery";
    if (unit) out.push_back(t);
  }
  return out;
}

bool Sim::startBuild(int workshopId, const std::string& unitType, std::string* why) {
  Entity* w = find(workshopId);
  const TypeInfo* t = gd_.registry.byName(unitType);
  auto fail = [&](const char* m) {
    if (why) *why = m;
    return false;
  };
  if (!w || w->role != Role::Workshop || !t || !t->def) return fail("not a workshop");
  if (!w->building.empty()) return fail("workshop busy");
  float cost = float(t->def->num("cost", 0));
  PlayerState& p = players_[size_t(w->owner)];
  if (p.power < cost) return fail("not enough Storm Power");
  int need = int(t->def->num("minUsage", 0));
  if (need > 0 && energyAt(w->owner, t->def->str("theme"), int(w->x), int(w->y)) < need)
    return fail("not enough energy");
  p.power -= cost;
  w->building = t->name;
  w->progress = 0;
  return true;
}

void Sim::stepWorkshop(Entity& w) {
  if (w.building.empty()) return;
  const TypeInfo* t = gd_.registry.byName(w.building);
  float buildTime = t && t->def ? std::max(4.0f, float(t->def->num("cost", 400)) / 50.0f) : 8.0f;
  w.progress += kDt;
  if (w.progress >= buildTime) {
    players_[size_t(w.owner)].ready.push_back(w.building);
    w.building.clear();
    w.progress = 0;
  }
}

bool Sim::canDropUnit(const TypeInfo* t, int ax, int ay, int player) const {
  if (!t || !t->def) return false;
  int fx = footX(t), fy = footY(t);
  bool touches = false;
  for (int y = ay - fy + 1; y <= ay; ++y)
    for (int x = ax - fx + 1; x <= ax; ++x) {
      if (world_.filled(x, y) || world_.bridgeAt(x, y)) return false;  // must drop into open sky
      const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
      for (auto& n : nb) {
        int nx = x + n[0], ny = y + n[1];
        if ((world_.filled(nx, ny) && world_.ownerAt(nx, ny) == player) || world_.bridgeAt(nx, ny) == player)
          touches = true;
      }
    }
  return touches;
}

bool Sim::dropUnit(int player, int readyIndex, int ax, int ay) {
  PlayerState& p = players_[size_t(player)];
  if (readyIndex < 0 || readyIndex >= int(p.ready.size())) return false;
  const TypeInfo* t = gd_.registry.byName(p.ready[size_t(readyIndex)]);
  if (!canDropUnit(t, ax, ay, player)) return false;
  int fx = footX(t), fy = footY(t);
  Element el = Element::Sun;
  std::string th = t->def->str("theme");
  if (th == "wind") el = Element::Wind;
  else if (th == "rain") el = Element::Rain;
  else if (th == "thunder") el = Element::Thunder;
  for (int y = ay - fy + 1; y <= ay; ++y)
    for (int x = ax - fx + 1; x <= ax; ++x) {
      world_.set(x, y, el);
      world_.setOwner(x, y, player);
    }
  spawn(t, player, float(ax), float(ay));
  p.ready.erase(p.ready.begin() + readyIndex);
  return true;
}

}  // namespace ns
