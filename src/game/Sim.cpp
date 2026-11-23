#include "game/Sim.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <cstdio>
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
  if (d.hasFlag("altar")) return Role::Altar;
  if (d.hasFlag("geyser")) return Role::Geyser;
  if (d.hasFlag("vortex")) return Role::Temple;
  if (d.hasFlag("factory")) return Role::Workshop;
  if (d.hasFlag("walker") && t->name == "sunWalker") return Role::Golem;
  if (d.hasFlag("flyer")) return Role::Flyer;
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

int Sim::spawnWalkerNear(const TypeInfo* t, int owner, int nearId) {
  Entity* b = find(nearId);
  if (!b) return -1;
  int cx, cy;
  approach(*b, cx, cy);
  if (!walkable(cx, cy)) {
    // Any walkable cell in a ring around the footprint.
    int fx = footX(b->type), fy = footY(b->type);
    for (int r = 1; r < 6 && !walkable(cx, cy); ++r)
      for (int y = int(b->y) - fy + 1 - r; y <= int(b->y) + r; ++y)
        for (int x = int(b->x) - fx + 1 - r; x <= int(b->x) + r; ++x)
          if (walkable(x, y)) cx = x, cy = y;
  }
  return spawn(t, owner, cx + 0.5f, cy + 0.5f);
}

void Sim::log(const std::string& m) {
  char t[16];
  std::snprintf(t, sizeof t, "[%ds] ", int(time_));
  events_.push_back(t + m);
  if (events_.size() > 200) events_.erase(events_.begin());
}

int Sim::outcome(int player) const {
  std::array<int, 9> strength{};
  for (auto& e : ents_)
    if (e.alive && e.owner > 0 && e.owner <= 8 && !out_[size_t(e.owner)] && e.role != Role::Golem &&
        e.role != Role::Geyser)
      strength[size_t(e.owner)]++;
  if (out_[size_t(player)] || strength[size_t(player)] == 0) return -1;
  for (int p = 1; p <= 8; ++p)
    if (strength[size_t(p)] && !allied(p, player)) return 0;
  return time_ > 1 ? 1 : 0;
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
    if (e.flash > 0) e.flash -= kDt;
    if (e.role == Role::Golem) stepGolem(e);
    else if (e.role == Role::Workshop) stepWorkshop(e);
    else if (e.role == Role::Shooter) stepShooter(e);
    else if (e.role == Role::Priest) stepPriest(e);
    else if (e.role == Role::Altar) stepAltar(e);
    else if (e.role == Role::Flyer) stepFlyer(ents_[i]);
  }
  stepProjectiles();
  // Temples slowly replace lost golems (approximation: one per 30 s, up to the
  // mission's collector count) so a player can't be starved out for good.
  {
    std::array<int, 9> golems{}, temple{};
    temple.fill(-1);
    for (auto& e : ents_) {
      if (!e.alive || e.owner <= 0 || e.owner > 8) continue;
      if (e.role == Role::Golem) golems[size_t(e.owner)]++;
      if (e.role == Role::Temple && e.built >= 1) temple[size_t(e.owner)] = e.id;
    }
    const TypeInfo* g = gd_.registry.byName("sunWalker");
    for (int p = 1; p <= 8 && g; ++p) {
      PlayerState& ps = players_[size_t(p)];
      if (temple[size_t(p)] < 0 || out_[size_t(p)] || golems[size_t(p)] >= ps.golemTarget) {
        ps.golemTimer = 0;
        continue;
      }
      ps.golemTimer += kDt;
      if (ps.golemTimer >= 30) {
        ps.golemTimer = 0;
        if (spawnWalkerNear(g, p, temple[size_t(p)]) >= 0) log("P" + std::to_string(p) + "'s temple raised a new golem");
      }
    }
  }
  for (auto& fx : effects_) fx.t += kDt;
  effects_.erase(std::remove_if(effects_.begin(), effects_.end(), [](const Effect& f) { return f.t >= f.life; }),
                 effects_.end());
  // Drop dead entities once in a while (ids stay unique).
  if (int(time_ / kDt) % 100 == 0)
    ents_.erase(std::remove_if(ents_.begin(), ents_.end(), [](const Entity& e) { return !e.alive; }), ents_.end());
  // Winner: the only player who still has a priest or buildings.
  if (!winner_) {
    std::array<int, 9> alive{};
    for (auto& e : ents_)
      if (e.alive && e.owner > 0 && !out_[size_t(e.owner)] && e.role != Role::Golem && e.role != Role::Geyser)
        alive[size_t(e.owner)]++;
    int count = 0, last = 0;
    for (int p = 1; p <= 8; ++p)
      if (alive[size_t(p)]) ++count, last = p;
    if (count == 1 && time_ > 1) winner_ = last, log("P" + std::to_string(last) + " WINS");
  }
}

// ---- combat --------------------------------------------------------------

namespace {
float centreX(const Entity& e) {
  int fx = footX(e.type);
  return e.role == Role::Golem || e.role == Role::Priest ? e.x : e.x - fx / 2.0f + 0.5f;
}
float centreY(const Entity& e) {
  int fy = footY(e.type);
  return e.role == Role::Golem || e.role == Role::Priest ? e.y : e.y - fy / 2.0f + 0.5f;
}
// Missile sprite per shooter (from the type list).
const char* missileFor(const std::string& shooter) {
  if (shooter == "sunArcher") return "sunDisc";
  if (shooter == "windArcher") return "bolt";
  if (shooter == "rainCannon") return "rainCannonMissile";
  if (shooter == "thunderCannon") return "thunderCannonMissile";
  if (shooter == "thunderArcher") return "manabolt";
  return "blankMissile";
}
}  // namespace

// Blockers (height >= 2, enemy-owned) on the straight line stop direct fire.
bool Sim::lineBlocked(float x0, float y0, float x1, float y1, int owner, int& blocker) const {
  float best = 2.0f;
  blocker = -1;
  for (auto& e : ents_) {
    if (!e.alive || allied(e.owner, owner) || !e.type || !e.type->def) continue;
    if (e.type->def->str("group") != "blocker" && e.type->def->num("height", 0) < 2) continue;
    float cx = centreX(e), cy = centreY(e);
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    if (len2 < 1e-4f) continue;
    float t = ((cx - x0) * dx + (cy - y0) * dy) / len2;
    if (t <= 0.05f || t >= 0.95f) continue;
    float px = x0 + dx * t - cx, py = y0 + dy * t - cy;
    if (px * px + py * py <= 2.0f && t < best) best = t, blocker = e.id;
  }
  return blocker >= 0;
}

void Sim::stepShooter(Entity& s) {
  if (s.cooldown > 0) {
    s.cooldown -= kDt;
    return;
  }
  const TypeDef& d = *s.type->def;
  float range = float(d.num("range", 0));
  if (range <= 0) return;  // blockers, generators...
  // Needs energy of its fury to fire.
  int need = int(d.num("minUsage", 0));
  std::string theme = d.str("theme");
  if (need > 0 && energyAt(s.owner, theme, int(s.x), int(s.y)) < need) return;

  bool cannon = d.str("group") == "cannon";
  bool aviary = d.str("group") == "aviary";
  bool antiAir = d.str("group") == "archer";  // archers shoot flyers; shells and bolts can't
  float sx = centreX(s), sy = centreY(s);
  // Target: nearest enemy thing in range (keep the last target if still valid).
  Entity* best = nullptr;
  float bestD = range * range;
  for (auto& e : ents_) {
    if (!e.alive || e.owner == 0 || allied(e.owner, s.owner) || e.role == Role::Geyser) continue;
    if (e.role == Role::Flyer && !antiAir && !aviary) continue;
    if (e.role == Role::Priest && (e.carriedBy >= 0 || e.chainedAt >= 0)) continue;
    float dx = centreX(e) - sx, dy = centreY(e) - sy, d2 = dx * dx + dy * dy;
    if (e.id == s.lastTarget) d2 *= 0.8f;  // stickiness
    if (d2 <= bestD) bestD = d2, best = &e;
  }
  if (aviary) {
    // Aviaries launch a flyer at whatever's in range, one airborne at a time.
    for (auto& f : ents_)
      if (f.alive && f.role == Role::Flyer && f.home == s.id) return;
    if (best && launchFlyer(s, *best)) s.cooldown = float(d.num("delayBetweenShots", 6));
    return;
  }
  float hps = float(d.num("hpPerSec", 10));
  float delay = float(d.num("delayBetweenShots", cannon ? 2.5 : 0.6));
  Projectile p;
  p.owner = s.owner;
  p.x0 = sx;
  p.y0 = sy;
  p.arc = cannon;
  p.damage = hps * delay;
  p.theme = theme;
  p.missile = gd_.registry.byName(missileFor(s.type->name));
  if (best) {
    p.target = best->id;
    p.x1 = centreX(*best);
    p.y1 = centreY(*best);
    if (!cannon) {
      int blk;
      if (lineBlocked(sx, sy, p.x1, p.y1, s.owner, blk)) {
        p.target = blk;
        Entity* b = find(blk);
        p.x1 = centreX(*b);
        p.y1 = centreY(*b);
      }
    }
    s.lastTarget = best->id;
  } else if (cannon) {
    // Cannons with nothing to shoot break enemy bridges in range.
    int bx = -1, by = -1;
    float bd = range * range;
    int r = int(range);
    for (int y = int(sy) - r; y <= int(sy) + r; ++y)
      for (int x = int(sx) - r; x <= int(sx) + r; ++x) {
        int b = world_.bridgeAt(x, y);
        if (!b || allied(b, s.owner) || b == 9) continue;
        float dx = x + 0.5f - sx, dy = y + 0.5f - sy, d2 = dx * dx + dy * dy;
        if (d2 < bd) bd = d2, bx = x, by = y;
      }
    if (bx < 0) return;
    p.bridgeX = bx;
    p.bridgeY = by;
    p.x1 = bx + 0.5f;
    p.y1 = by + 0.5f;
  } else {
    return;
  }
  float dist = std::sqrt((p.x1 - p.x0) * (p.x1 - p.x0) + (p.y1 - p.y0) * (p.y1 - p.y0));
  p.dur = std::max(0.15f, dist / (cannon ? 14.0f : 24.0f));
  shots_.push_back(p);
  s.cooldown = delay;
}

void Sim::stepProjectiles() {
  for (auto& p : shots_) {
    p.t += kDt;
    if (p.t < p.dur) continue;
    if (p.target >= 0) {
      if (Entity* e = find(p.target)) damage(*e, p.damage);
    } else if (p.bridgeX >= 0) {
      world_.setBridge(p.bridgeX, p.bridgeY, 0);
    }
    Effect fx;
    fx.type = gd_.registry.byName("bombExplodeSmall");
    fx.x = p.x1;
    fx.y = p.y1;
    fx.life = 0.5f;
    effects_.push_back(fx);
  }
  shots_.erase(std::remove_if(shots_.begin(), shots_.end(), [](const Projectile& p) { return p.t >= p.dur; }),
               shots_.end());
}

void Sim::damage(Entity& e, float amount) {
  if (!e.alive) return;
  e.hp -= int(std::ceil(amount));
  e.flash = 0.15f;
  if (e.role == Role::Priest) {
    // A priest is never killed by damage: at half health a force field forms.
    e.hp = std::max(e.hp, 1);
    if (e.hp * 2 <= e.maxHp && !e.immobile) {
      e.immobile = true;
      log("P" + std::to_string(e.owner) + " priest immobilised");
      e.path.clear();
      e.job = Entity::Job::Idle;
    }
    return;
  }
  if (e.hp <= 0) kill(e);
}

void Sim::kill(Entity& e) {
  e.alive = false;
  Effect fx;
  fx.type = gd_.registry.byName(footX(e.type) >= 4 ? "bombExplodeLarge" : "bombExplodeMedium");
  fx.x = centreX(e);
  fx.y = centreY(e);
  fx.life = 1.0f;
  effects_.push_back(fx);
  collapseLand(e);
}

// A destroyed unit's own little island falls out of the sky, along with
// anything standing on it (walkers) -- unless another building still holds
// that land up, or it's permanent (home islands, geyser islands).
void Sim::collapseLand(const Entity& dead) {
  if (!dead.type || !dead.type->def || !dead.type->def->hasFlag("createsisland")) return;
  int fx = footX(dead.type), fy = footY(dead.type);
  auto covered = [&](int x, int y) {
    for (auto& o : ents_) {
      if (!o.alive || o.id == dead.id || !o.type || !o.type->def || !o.type->def->hasFlag("createsisland")) continue;
      int ox = footX(o.type), oy = footY(o.type);
      if (x >= int(o.x) - ox && x <= int(o.x) + 1 && y >= int(o.y) - oy && y <= int(o.y) + 1) return true;
    }
    return false;
  };
  for (int y = int(dead.y) - fy; y <= int(dead.y) + 1; ++y)
    for (int x = int(dead.x) - fx; x <= int(dead.x) + 1; ++x) {
      if (!world_.filled(x, y) || world_.permanent(x, y) || covered(x, y)) continue;
      world_.set(x, y, Element::None);
      world_.setOwner(x, y, 0);
      for (auto& o : ents_)  // walkers standing there fall
        if (o.alive && (o.role == Role::Golem || o.role == Role::Priest) && int(o.x) == x && int(o.y) == y)
          o.alive = false;
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

bool Sim::walkStep(Entity& g) {
  const float speed = g.type && g.type->def ? float(g.type->def->num("speed", 1.8)) : 1.8f;
  {
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
    // Anything carried moves along.
    if (Entity* c = find(g.carryingPriest)) c->x = g.x, c->y = g.y - 0.3f;
    return g.path.empty();
  }
}

void Sim::stepGolem(Entity& g) {
  auto walk = [&]() { return walkStep(g); };
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
    case Entity::Job::ToBuild:
    case Entity::Job::Building:
      g.job = Entity::Job::Idle;
      break;
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
    case Entity::Job::ToCapture: {
      Entity* pr = find(g.target);
      if (!pr || !pr->immobile || pr->carriedBy >= 0 || pr->chainedAt >= 0) {
        g.job = Entity::Job::Idle;
        g.target = -1;
        break;
      }
      if (!walk()) break;
      if (std::abs(g.x - pr->x) + std::abs(g.y - pr->y) > 2.5f) {  // he moved: re-plan
        g.path = findPath(int(g.x), int(g.y), int(pr->x), int(pr->y), 0, 0);
        if (g.path.empty()) g.job = Entity::Job::Idle;
        break;
      }
      // Pick him up and head for our altar.
      Entity* altar = nullptr;
      for (auto& e : ents_)
        if (e.alive && e.role == Role::Altar && e.owner == g.owner && e.built >= 1 && e.captive < 0) altar = &e;
      if (!altar) {
        g.job = Entity::Job::Idle;
        break;
      }
      pr->carriedBy = g.id;
      g.carryingPriest = pr->id;
      log("P" + std::to_string(g.owner) + " golem captured the P" + std::to_string(pr->owner) + " priest");
      g.target = altar->id;
      int cx, cy;
      approach(*altar, cx, cy);
      g.path = findPath(int(g.x), int(g.y), cx, cy, 0, 0);
      g.job = Entity::Job::ToAltar;
      break;
    }
    case Entity::Job::ToAltar: {
      if (!walk()) break;
      Entity* pr = find(g.carryingPriest);
      Entity* altar = find(g.target);
      g.carryingPriest = -1;
      g.job = Entity::Job::Idle;
      if (!pr) break;
      pr->carriedBy = -1;
      if (altar && altar->captive < 0) {
        // Chained in the sacrificial circle (centre of the altar).
        altar->captive = pr->id;
        altar->runes = 0;
        pr->chainedAt = altar->id;
        log("P" + std::to_string(pr->owner) + " priest chained at the P" + std::to_string(altar->owner) + " altar");
        pr->x = altar->x - 3.0f;
        pr->y = altar->y - 3.0f;
        pr->hp = pr->maxHp;  // "draws new life force from the transport"
        pr->immobile = false;
      }
      break;
    }
    case Entity::Job::Moving:
      if (walk()) g.job = Entity::Job::Idle, g.timer = 3.0f;
      break;
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

static bool sameTheme(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}

int Sim::energyAt(int player, const std::string& theme, int x, int y) const {
  int total = 0;
  for (auto& e : ents_) {
    if (!e.alive || e.owner != player || !e.type || !e.type->def) continue;
    bool gen = (e.role == Role::Generator || e.role == Role::Temple) && e.built >= 1;
    if (!gen) continue;
    // Sun ("common Knowledge") units run on any fury's energy; fury units need their own.
    std::string gt = e.type->def->str("theme");
    if (!sameTheme(theme, "sun") && !sameTheme(gt, theme)) continue;
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
    const auto& tech = players_[size_t(w->owner)].tech;
    if (!tech.empty()) {
      std::string ln = t->name;
      std::transform(ln.begin(), ln.end(), ln.begin(), [](unsigned char c) { return std::tolower(c); });
      if (std::find(tech.begin(), tech.end(), ln) == tech.end()) continue;
    }
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
  if (w->built < 1) return fail("workshop still under construction");
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
  if (w.building.empty() || w.built < 1) return;
  const TypeInfo* t = gd_.registry.byName(w.building);
  float buildTime = t && t->def ? std::max(4.0f, float(t->def->num("cost", 400)) / 50.0f) : 8.0f;
  w.progress += kDt;
  if (w.progress >= buildTime) {
    if (t && t->def && t->def->hasFlag("walker") && roleFor(t) == Role::Golem) {
      // Golems walk out of the temple rather than being dropped.
      int temple = w.id;
      for (auto& e : ents_)
        if (e.alive && e.owner == w.owner && e.role == Role::Temple) temple = e.id;
      spawnWalkerNear(t, w.owner, temple);
      log("P" + std::to_string(w.owner) + " got a new golem");
    } else {
      players_[size_t(w.owner)].ready.push_back(w.building);
    }
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

// ---- priest, construction, capture, sacrifice -----------------------------

bool Sim::occupied(int x, int y, int ignoreId) const {
  for (auto& e : ents_) {
    if (!e.alive || e.id == ignoreId || !e.type || !e.type->def) continue;
    if (e.role == Role::Golem || e.role == Role::Priest || e.role == Role::Flyer) continue;
    int fx = footX(e.type), fy = footY(e.type);
    if (x > int(e.x) - fx && x <= int(e.x) && y > int(e.y) - fy && y <= int(e.y)) return true;
  }
  return false;
}

bool Sim::orderMove(int id, int x, int y) {
  Entity* w = find(id);
  if (!w || (w->role != Role::Priest && w->role != Role::Golem)) return false;
  if (w->immobile || w->carriedBy >= 0 || w->chainedAt >= 0) return false;
  if (int(w->x) == x && int(w->y) == y) return true;  // already there
  auto path = findPath(int(w->x), int(w->y), x, y, 0, 0);
  if (path.empty()) return false;
  w->path = path;
  w->job = Entity::Job::Moving;
  if (w->role == Role::Priest && w->buildId >= 0) w->buildId = -1;
  return true;
}

std::vector<const TypeInfo*> Sim::constructable() const {
  std::vector<const TypeInfo*> out;
  for (const char* n : {"sunFactory", "windFactory", "rainFactory", "thunderFactory", "windVortex", "rainVortex",
                        "thunderVortex", "altar"})
    if (const TypeInfo* t = gd_.registry.byName(n); t && t->def) out.push_back(t);
  return out;
}

bool Sim::canConstruct(int priestId, const TypeInfo* t, int ax, int ay, std::string* why) const {
  auto fail = [&](const char* m) {
    if (why) *why = m;
    return false;
  };
  const Entity* p = nullptr;
  for (auto& e : ents_)
    if (e.id == priestId && e.alive) p = &e;
  if (!p || p->role != Role::Priest || !t || !t->def) return fail("select your priest");
  if (p->immobile) return fail("priest is immobilised");
  int fx = footX(t), fy = footY(t);
  for (int y = ay - fy + 1; y <= ay; ++y)
    for (int x = ax - fx + 1; x <= ax; ++x) {
      if (!world_.filled(x, y) || world_.ownerAt(x, y) != p->owner) return fail("must be on your own large island");
      if (occupied(x, y)) return fail("something is in the way");
    }
  if (players_[size_t(p->owner)].power < float(t->def->num("cost", 0))) return fail("not enough storm power");
  return true;
}

bool Sim::orderConstruct(int priestId, const std::string& typeName, int ax, int ay, std::string* why) {
  const TypeInfo* t = gd_.registry.byName(typeName);
  if (!canConstruct(priestId, t, ax, ay, why)) return false;
  Entity* p = find(priestId);
  int id = spawn(t, p->owner, float(ax), float(ay));
  Entity* b = find(id);
  p = find(priestId);  // spawn may reallocate
  b->built = 0.0f;
  log("P" + std::to_string(b->owner) + " priest started building " + t->name);
  b->hp = std::max(1, b->maxHp / 10);
  players_[size_t(p->owner)].power -= float(t->def->num("cost", 0));
  int cx, cy;
  approach(*b, cx, cy);
  p->path = findPath(int(p->x), int(p->y), cx, cy, 0, 0);
  p->buildId = id;
  p->job = Entity::Job::ToBuild;
  return true;
}

bool Sim::orderResume(int priestId, int buildingId) {
  Entity* p = find(priestId);
  Entity* b = find(buildingId);
  if (!p || !b || p->role != Role::Priest || b->owner != p->owner || b->built >= 1 || p->immobile) return false;
  int cx, cy;
  approach(*b, cx, cy);
  p->path = findPath(int(p->x), int(p->y), cx, cy, 0, 0);
  if (p->path.empty() && (std::abs(p->x - cx) > 1 || std::abs(p->y - cy) > 1)) return false;
  p->buildId = b->id;
  p->job = Entity::Job::ToBuild;
  return true;
}

bool Sim::orderCapture(int golemId, int priestId, std::string* why) {
  auto fail = [&](const char* m) {
    if (why) *why = m;
    return false;
  };
  Entity* g = find(golemId);
  Entity* pr = find(priestId);
  if (!g || g->role != Role::Golem) return fail("select a golem");
  if (!pr || pr->role != Role::Priest || allied(pr->owner, g->owner)) return fail("target an enemy priest");
  if (!pr->immobile) return fail("damage him to half health first");
  bool haveAltar = false;
  for (auto& e : ents_)
    if (e.alive && e.role == Role::Altar && e.owner == g->owner && e.built >= 1 && e.captive < 0) haveAltar = true;
  if (!haveAltar) return fail("you need a finished altar");
  g->path = findPath(int(g->x), int(g->y), int(pr->x), int(pr->y), 0, 0);
  if (g->path.empty()) return fail("golem cant reach him");
  g->carrying = false;
  g->target = pr->id;
  g->job = Entity::Job::ToCapture;
  return true;
}

void Sim::stepPriest(Entity& p) {
  if (p.chainedAt >= 0 || p.carriedBy >= 0) return;  // captive
  // Constant regeneration; the force field drops above half health.
  if (p.hp < p.maxHp && int(time_ / kDt) % 10 == 0) p.hp = std::min(p.maxHp, p.hp + std::max(1, p.maxHp / 50));
  if (p.immobile) {
    if (p.hp * 2 > p.maxHp + 10) p.immobile = false;
    return;
  }
  switch (p.job) {
    case Entity::Job::Moving:
      if (walkStep(p)) p.job = Entity::Job::Idle;
      break;
    case Entity::Job::ToBuild:
      if (!find(p.buildId)) {
        p.job = Entity::Job::Idle;
        break;
      }
      if (walkStep(p)) p.job = Entity::Job::Building;
      break;
    case Entity::Job::Building: {
      Entity* b = find(p.buildId);
      if (!b) {
        p.job = Entity::Job::Idle;
        break;
      }
      float total = std::max(8.0f, float(b->type->def->num("cost", 600)) / 60.0f);
      b->built = std::min(1.0f, b->built + kDt / total);
      b->hp = std::max(b->hp, int(b->maxHp * b->built));
      if (b->built >= 1) {
        p.buildId = -1;
        p.job = Entity::Job::Idle;
      }
      break;
    }
    default:
      break;
  }
}

void Sim::stepAltar(Entity& a) {
  if (a.captive < 0) return;
  Entity* victim = find(a.captive);
  if (!victim) {
    a.captive = -1;
    a.runes = 0;
    return;
  }
  // Our own priest must be standing at the altar, and not immobilised.
  Entity* mine = nullptr;
  for (auto& e : ents_)
    if (e.alive && e.role == Role::Priest && e.owner == a.owner && e.chainedAt < 0 && e.carriedBy < 0) mine = &e;
  bool present = mine && !mine->immobile && std::abs(mine->x - (a.x - 3)) < 6 && std::abs(mine->y - (a.y - 3)) < 6;
  if (!present) {
    if (mine && mine->immobile && a.runes > 0) {  // spell broken: the captive escapes
      victim->chainedAt = -1;
      a.captive = -1;
      a.runes = 0;
    }
    return;
  }
  a.runes += kDt / kRuneTime;
  if (a.runes >= 5.0f) {
    // The enemy priest plunges into the abyss; his player is out.
    log("P" + std::to_string(victim->owner) + " priest SACRIFICED by P" + std::to_string(a.owner));
    out_[size_t(victim->owner)] = true;
    victim->alive = false;
    Effect fx;
    fx.type = gd_.registry.byName("bombExplodeLarge");
    fx.x = victim->x;
    fx.y = victim->y;
    fx.life = 1.5f;
    effects_.push_back(fx);
    a.captive = -1;
    a.runes = 0;
    if (knowledgeGifts) {
      players_[size_t(a.owner)].giftAltar = a.id;  // the altar stays until the gift is chosen
    } else {
      a.alive = false;  // campaign: the altar is spent
    }
    // Everything the sacrificed player owned stops working.
    for (auto& e : ents_)
      if (e.alive && e.owner == victim->owner) e.owner = 0;
  }
}

// ---- Air units -----------------------------------------------------------

bool Sim::launchFlyer(Entity& av, Entity& target) {
  std::string theme = av.type->def->str("theme");
  if (!theme.empty()) theme[0] = char(std::tolower(static_cast<unsigned char>(theme[0])));
  const TypeInfo* ft = gd_.registry.byName(theme + "Flyer");
  if (!ft || !ft->def) return false;
  int id = spawn(ft, av.owner, centreX(av), centreY(av));
  Entity* f = find(id);
  if (!f) return false;
  f->home = av.id;
  f->target = target.id;
  f->life = float(ft->def->num("lifeSpan", 60));
  if (ft->def->num("maxHitPoints", -1) < 0) f->maxHp = f->hp = 60;
  f->heading = std::atan2(centreY(target) - f->y, centreX(target) - f->x);
  log("P" + std::to_string(av.owner) + " launched a " + ft->name);
  return true;
}

void Sim::stepFlyer(Entity& f) {
  Entity* home = find(f.home);
  f.life -= kDt;
  if (f.life <= 0 || !home || !home->alive) {  // burnt out, or its aviary is gone
    f.alive = false;
    Effect fx;
    fx.type = gd_.registry.byName("bombExplodeSmall");
    fx.x = f.x;
    fx.y = f.y;
    fx.life = 0.6f;
    effects_.push_back(fx);
    return;
  }
  const TypeDef& d = *f.type->def;
  float range = float(home->type->def->num("range", d.num("range", 35)));
  float hx = centreX(*home), hy = centreY(*home);
  // Keep the current target while it's alive and within the aviary's reach; otherwise the nearest.
  Entity* t = find(f.target);
  auto inReach = [&](const Entity& e) {
    float dx = centreX(e) - hx, dy = centreY(e) - hy;
    return dx * dx + dy * dy <= range * range;
  };
  if (!t || !t->alive || !inReach(*t) || (t->role == Role::Priest && (t->carriedBy >= 0 || t->chainedAt >= 0))) {
    t = nullptr;
    float best = 1e18f;
    for (auto& e : ents_) {
      if (!e.alive || e.owner == 0 || allied(e.owner, f.owner) || e.role == Role::Geyser || !inReach(e)) continue;
      if (e.role == Role::Priest && (e.carriedBy >= 0 || e.chainedAt >= 0)) continue;
      float dx = centreX(e) - f.x, dy = centreY(e) - f.y, d2 = dx * dx + dy * dy;
      if (d2 < best) best = d2, t = &e;
    }
    f.target = t ? t->id : -1;
  }
  // Fly: towards the target, circling it once close; with no target, loiter over the aviary.
  float gx = t ? centreX(*t) : hx, gy = t ? centreY(*t) : hy;
  float dx = gx - f.x, dy = gy - f.y, dist = std::sqrt(dx * dx + dy * dy);
  float want = std::atan2(dy, dx);
  if (dist < 3.0f) want += 1.3f;  // circle
  float diff = std::remainder(want - f.heading, 6.2831853f);
  float turn = 4.0f * kDt;  // rad/s
  f.heading += std::clamp(diff, -turn, turn);
  float speed = float(d.num("speed", 2.0)) * 4.0f;  // cells/s (flyers are much faster than walkers)
  f.x += std::cos(f.heading) * speed * kDt;
  f.y += std::sin(f.heading) * speed * kDt;
  // Strafe: continuous damage while close.
  if (t && dist < 4.0f) {
    damage(*t, float(d.num("hpPerSec", 15)) * kDt);
    t->flash = 0.1f;
  }
}

// ---- Knowledge -------------------------------------------------------------

static std::string lowerName(std::string s) {
  for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

static bool isProduct(const TypeInfo* t) {
  if (!t || !t->def || t->def->num("cost", 0) <= 0) return false;
  const TypeDef& d = *t->def;
  return d.hasFlag("emplacement") || d.hasFlag("walker") || d.hasFlag("balloon") || d.str("group") == "battery";
}

std::vector<std::string> Sim::starterKnowledge() const {
  std::vector<std::string> out{"sunwalker"};
  for (int s = 0; s < gd_.registry.count(); ++s) {
    const TypeInfo* t = gd_.registry.bySet(s);
    if (isProduct(t) && t->def->num("techBit", -1) < 0) out.push_back(lowerName(t->name));
  }
  return out;
}

std::vector<const TypeInfo*> Sim::giftOptions(int player) const {
  std::vector<const TypeInfo*> out;
  const PlayerState& ps = players_[size_t(player)];
  const Entity* altar = nullptr;
  for (auto& e : ents_)
    if (e.alive && e.id == ps.giftAltar) altar = &e;
  if (!altar) return out;
  for (int s = 0; s < gd_.registry.count(); ++s) {
    const TypeInfo* t = gd_.registry.bySet(s);
    if (!isProduct(t) || t->def->num("techBit", -1) < 0) continue;
    if (int(t->def->num("level", 1)) > altar->altarLevel) continue;
    if (std::find(ps.tech.begin(), ps.tech.end(), lowerName(t->name)) != ps.tech.end()) continue;
    out.push_back(t);
  }
  return out;
}

bool Sim::canUpgradeAltar(int player) const {
  for (auto& e : ents_)
    if (e.alive && e.id == players_[size_t(player)].giftAltar) return e.altarLevel < 3;
  return false;
}

bool Sim::chooseGift(int player, const std::string& pick) {
  PlayerState& ps = players_[size_t(player)];
  Entity* altar = find(ps.giftAltar);
  if (!altar || !altar->alive) {
    ps.giftAltar = -1;
    return false;
  }
  if (pick == "upgrade") {
    if (altar->altarLevel >= 3) return false;
    altar->altarLevel++;
    log("P" + std::to_string(player) + " upgraded an altar to level " + std::to_string(altar->altarLevel));
  } else {
    bool ok = false;
    for (auto* t : giftOptions(player)) ok |= lowerName(t->name) == lowerName(pick);
    if (!ok) return false;
    ps.tech.push_back(lowerName(pick));
    altar->alive = false;  // "When new Knowledge is granted the Altar is consumed."
    log("P" + std::to_string(player) + " gained the Knowledge of " + pick);
  }
  ps.giftAltar = -1;
  return true;
}

}  // namespace ns
