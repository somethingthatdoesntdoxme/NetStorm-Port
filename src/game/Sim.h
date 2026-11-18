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

enum class Role : uint8_t { Static, Golem, Priest, Geyser, Temple, Workshop, Generator, Shooter };

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
  enum class Job : uint8_t { Idle, ToGeyser, Mining, ToTemple } job = Job::Idle;
  int target = -1;    // geyser id
  int home = -1;      // temple id
  bool carrying = false;
  float timer = 0;
  std::vector<std::pair<int, int>> path;
  // Workshop
  std::string building;  // type being built ("" = idle)
  float progress = 0;    // seconds done
};

struct PlayerState {
  float power = 0;  // Storm Power
  int delivered = 0;
  std::vector<std::string> ready;  // finished units waiting to be placed
};

class Sim {
 public:
  static constexpr float kDt = 0.05f;  // 20 Hz

  Sim(const GameData& gd, Island& world) : gd_(gd), world_(world) {}

  int spawn(const TypeInfo* t, int owner, float x, float y);
  void step();  // one fixed tick

  // Workshop production. Returns false if it can't start (cost/energy/busy).
  bool startBuild(int workshopId, const std::string& unitType, std::string* why = nullptr);
  // Units a workshop of this fury can build (starter knowledge + all, for now).
  std::vector<const TypeInfo*> buildable(int workshopId) const;
  // Drop a finished unit from `player`'s ready list at anchor cell (x, y).
  bool canDropUnit(const TypeInfo* t, int x, int y, int player) const;
  bool dropUnit(int player, int readyIndex, int x, int y);

  // Energy of a fury available to `player` at a cell (sum of generators in range).
  int energyAt(int player, const std::string& theme, int x, int y) const;

  std::vector<Entity>& entities() { return ents_; }
  const std::vector<Entity>& entities() const { return ents_; }
  Entity* find(int id);
  PlayerState& player(int p) { return players_[size_t(std::clamp(p, 0, 8))]; }
  double time() const { return time_; }

 private:
  void stepGolem(Entity& g);
  void stepWorkshop(Entity& w);
  bool walkable(int x, int y) const { return world_.filled(x, y) || world_.bridgeAt(x, y); }
  std::vector<std::pair<int, int>> findPath(int sx, int sy, int tx, int ty, int tw, int th) const;
  void approach(const Entity& b, int& cx, int& cy) const;  // cell next to a building

  const GameData& gd_;
  Island& world_;
  std::vector<Entity> ents_;
  std::array<PlayerState, 9> players_{};
  int nextId_ = 1;
  double time_ = 0;
};

Role roleFor(const TypeInfo* t);

}  // namespace ns
