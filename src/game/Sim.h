#pragma once
// Deterministic game simulation (fixed 20 Hz tick, so it can later run in
// lockstep over the network).
//
// Economy, from the original manual (help.english):
//  * Storm Power is the currency; everything except bridges costs it.
//  * Geysers hold Storm Crystals. Golems (transports, type sunWalker) made at
//    the Temple (vortex) shuttle between a geyser on their owner's land and
//    the Temple, each trip delivering one crystal (nugget, 200 power).
//  * Generators (batteries) give energy of their fury in a radius; shooters
//    need energy of their fury (minUsage) to be built and to fire.
//  * Workshops (factories) build units for Storm Power; finished units are
//    dropped by the player onto the sky next to their own land, growing a
//    small island.
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "data/GameData.h"
#include "world/Island.h"

namespace ns {

enum class Role : uint8_t { Static, Golem, Priest, Geyser, Temple, Workshop, Generator, Shooter, Altar };

struct Entity {
  int id = 0;
  const TypeInfo* type = nullptr;
  Role role = Role::Static;
  int owner = 0;
  float x = 0, y = 0;  // anchor cell (bottom-right of footprint for buildings, feet for walkers)
  int hp = 0, maxHp = 0;
  float animOffset = 0;
  bool alive = true;

  // Geyser
  int crystals = 0;  // Storm Power left in it
  // Golem
  enum class Job : uint8_t { Idle, ToGeyser, Mining, ToTemple, ToCapture, ToAltar, Moving, ToBuild, Building } job = Job::Idle;
  int target = -1;    // geyser id
  int home = -1;      // temple id
  bool carrying = false;
  float timer = 0;
  std::vector<std::pair<int, int>> path;
  // Workshop
  std::string building;  // type being built ("" = idle)
  float progress = 0;    // seconds done
  // Buildings under construction (0..1); not functional until 1.
  float built = 1.0f;
  // Priest
  bool immobile = false;   // at <= 50% hp: force field, can't act, can be captured
  int carriedBy = -1;      // golem carrying this priest
  int chainedAt = -1;      // altar this priest is chained to
  int buildId = -1;        // building this priest is constructing
  // Golem carrying a captured priest
  int carryingPriest = -1;
  // Altar
  int captive = -1;        // chained enemy priest
  float runes = 0;         // 0..5 runes warded
  // Shooter
  float cooldown = 0;
  int lastTarget = -1;
  float flash = 0;  // >0 while showing damage flash
};

// A shot in flight. Damage is applied when it lands (deterministic: fixed
// flight time from distance).
struct Projectile {
  const TypeInfo* missile = nullptr;  // sprite (may be null: drawn as a spark)
  int owner = 0;
  float x0, y0, x1, y1;  // cells
  float t = 0, dur = 0.5f;
  bool arc = false;     // cannons lob shells
  int target = -1;      // entity id (or -1 for a bridge cell)
  int bridgeX = -1, bridgeY = -1;
  float damage = 0;
  std::string theme;
};

struct Effect {
  const TypeInfo* type = nullptr;  // explosion sprite set
  float x, y;                      // world cells (anchor)
  float t = 0, life = 1.0f;
};

struct PlayerState {
  std::vector<std::string> tech;  // lower-case unit names this player may build (empty = all)
  uint16_t allies = 0;            // bitmask of allied players
  float power = 0;  // Storm Power
  int golemTarget = 2;        // temple replaces lost golems up to this many
  float golemTimer = 0;
  int delivered = 0;
  std::vector<std::string> ready;  // finished units waiting to be placed
};

class Sim {
 public:
  static constexpr float kDt = 0.05f;  // 20 Hz

  Sim(const GameData& gd, Island& world) : gd_(gd), world_(world) {}

  int spawn(const TypeInfo* t, int owner, float x, float y);
  // Spawns a walker on a free walkable cell next to building `nearId`.
  int spawnWalkerNear(const TypeInfo* t, int owner, int nearId);
  void step();  // one fixed tick

  // Workshop production. Returns false if it can't start (cost/energy/busy).
  bool startBuild(int workshopId, const std::string& unitType, std::string* why = nullptr);
  // Units a workshop of this fury can build (starter knowledge + all, for now).
  std::vector<const TypeInfo*> buildable(int workshopId) const;
  // Drop a finished unit from `player`'s ready list at anchor cell (x, y).
  bool canDropUnit(const TypeInfo* t, int x, int y, int player) const;
  bool dropUnit(int player, int readyIndex, int x, int y);

  const std::vector<Projectile>& projectiles() const { return shots_; }
  const std::vector<Effect>& effects() const { return effects_; }
  // Damage an entity (applies kill + island collapse).
  void damage(Entity& e, float amount);
  int winner() const { return winner_; }  // >0 once only one player has buildings left

  // ---- priest + sacrifice ------------------------------------------------
  // Order a walker (priest or golem) to walk to a cell.
  bool orderMove(int walkerId, int x, int y);
  // Priest constructs `typeName` with anchor (x, y). Cost is paid up front.
  bool canConstruct(int priestId, const TypeInfo* t, int x, int y, std::string* why = nullptr) const;
  bool orderConstruct(int priestId, const std::string& typeName, int x, int y, std::string* why = nullptr);
  // Golem picks up an immobilised enemy priest and takes him to our altar.
  bool orderCapture(int golemId, int priestId, std::string* why = nullptr);
  // Types a priest can construct.
  std::vector<const TypeInfo*> constructable() const;
  const TypeInfo* registryType(const std::string& name) const { return gd_.registry.byName(name); }
  bool allied(int a, int b) const {
    return a == b || (a > 0 && b > 0 && a <= 8 && b <= 8 && ((players_[size_t(a)].allies >> b) & 1));
  }
  // +1 = player has won (no un-allied opponents left), -1 = lost, 0 = still going.
  int outcome(int player) const;
  bool eliminated(int player) const { return player >= 0 && player <= 8 && out_[size_t(player)]; }
  static constexpr float kRuneTime = 6.0f;  // seconds per rune, 5 runes

  // Energy of a fury available to `player` at a cell (sum of generators in range).
  int energyAt(int player, const std::string& theme, int x, int y) const;

  std::vector<Entity>& entities() { return ents_; }
  const std::vector<Entity>& entities() const { return ents_; }
  Entity* find(int id);
  PlayerState& player(int p) { return players_[size_t(std::clamp(p, 0, 8))]; }
  double time() const { return time_; }
  // Game event log ("[12s] P1 priest immobilised").
  const std::vector<std::string>& events() const { return events_; }
  void log(const std::string& m);
  // Debug: walking path length between two cells (-1 if unreachable).
  int pathLength(int sx, int sy, int tx, int ty) const { auto p = findPath(sx, sy, tx, ty, 0, 0); return p.empty() ? -1 : int(p.size()); }
  bool walkableAt(int x, int y) const { return walkable(x, y); }

 private:
  void stepGolem(Entity& g);
  void stepWorkshop(Entity& w);
  void stepShooter(Entity& s);
  void stepPriest(Entity& p);
  void stepAltar(Entity& a);
  bool walkStep(Entity& w);  // advance along path; true when finished
  bool occupied(int x, int y, int ignoreId = -1) const;
  void stepProjectiles();
  void kill(Entity& e);
  void collapseLand(const Entity& e);
  bool lineBlocked(float x0, float y0, float x1, float y1, int shooterOwner, int& blocker) const;
  bool walkable(int x, int y) const { return world_.filled(x, y) || world_.bridgeAt(x, y); }
  std::vector<std::pair<int, int>> findPath(int sx, int sy, int tx, int ty, int tw, int th) const;
  void approach(const Entity& b, int& cx, int& cy) const;  // cell next to a building

  const GameData& gd_;
  Island& world_;
  std::vector<Entity> ents_;
  std::array<PlayerState, 9> players_{};
  std::vector<Projectile> shots_;
  std::vector<Effect> effects_;
  int winner_ = 0;
  std::vector<std::string> events_;
  std::array<bool, 9> out_{};  // players whose priest was sacrificed
  int nextId_ = 1;
  double time_ = 0;
};

Role roleFor(const TypeInfo* t);

}  // namespace ns
