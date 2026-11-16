#pragma once
// Milestone-1 world view: sky, floating islands auto-tiled from the real
// terrain art, rock undersides, and buildings/units placed on the grid with
// shadows and depth sorting. Uses a generated test layout until the .fort
// mission format is decoded.
#include "render/Renderer.h"
#include "render/SpriteCache.h"
#include "world/Island.h"

namespace ns {

struct PlacedObject {
  const TypeInfo* type = nullptr;
  int island = 0;
  int cellX = 0, cellY = 0;  // footprint top-left cell on the island
  std::string cluster;       // animation cluster to play ("" = first line)
  int team = 0;              // 0 = none, 1..9 = remap table
  float animOffset = 0;
};

class WorldView {
 public:
  WorldView(Renderer& r, const GameData& gd);
  void handle(const SDL_Event& e);
  void update(double dt);
  void draw();
  bool wantsQuit() const { return quit_; }
  void buildTestScene(uint32_t seed);

 private:
  void drawSky();
  void drawIsland(const Island& isl);
  void drawObjects();
  void anchorOf(const PlacedObject& o, float& wx, float& wy) const;
  void blit(const SpriteTex& st, float wx, float wy);

  Renderer& r_;
  const GameData& gd_;
  SpriteCache cache_;
  Camera cam_;
  std::vector<Island> islands_;
  std::vector<PlacedObject> objects_;
  const TypeInfo* isle_ = nullptr;
  const TypeInfo* fringe_ = nullptr;
  const TypeInfo* stalag_ = nullptr;
  const TypeInfo* cloud_ = nullptr;
  double time_ = 0;
  bool dragging_ = false, quit_ = false, showGrid_ = false;
  uint32_t seed_ = 1;
  std::array<bool, 256> rimMask_{};  // palette indices used only by island rims
};

}  // namespace ns
