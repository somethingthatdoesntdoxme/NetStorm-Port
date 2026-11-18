#pragma once
// Milestone-1 world view: sky, floating islands auto-tiled from the real
// terrain art, rock undersides, and buildings/units placed on the grid with
// shadows and depth sorting. Uses a generated test layout until the .fort
// mission format is decoded.
#include "render/Renderer.h"
#include "render/SpriteCache.h"
#include "world/Island.h"
#include "world/Mission.h"
#include "game/BridgePieces.h"
#include "game/Sim.h"

#include <memory>

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
  // Loads mission `index` from the script archive (sorted list of .fort files).
  bool loadMission(int index);
  bool loadMissionByName(const std::string& part);
  int autoBuild(int pieces);
  // Test hook: open the first own workshop and start building its first affordable unit.
  std::string demoProduction();
  Sim* sim() { return sim_.get(); }
  int player() const { return player_; }  // let the bridge AI play `pieces` pieces for the player
  const std::vector<std::string>& missions() const { return missions_; }

 private:
  void drawSky();
  void drawIsland(const Island& isl);
  void drawObjects();
  void drawBridges(const Island& isl, int cx0, int cy0, int cx1, int cy1);
  void drawBuildUi();
  void hoverCell(int sx, int sy);
  void anchorOf(const Entity& e, float& wx, float& wy) const;
  void startSim(float startPower);
  void drawHud();
  bool handleUiClick(int sx, int sy);
  const Entity* entityAtScreen(int sx, int sy) const;
  void blit(const SpriteTex& st, float wx, float wy);

  Renderer& r_;
  const GameData& gd_;
  SpriteCache cache_;
  Camera cam_;
  std::vector<Island> islands_;
  std::unique_ptr<Sim> sim_;
  double simAcc_ = 0;
  int selectedWorkshop_ = -1;  // production menu open for this workshop
  int dropIndex_ = -1;         // placing this ready unit
  std::string message_;
  double messageTime_ = 0;
  struct Button {
    SDL_FRect r;
    std::string action;
  };
  std::vector<Button> buttons_;
  const TypeInfo* isle_ = nullptr;
  const TypeInfo* fringe_ = nullptr;
  const TypeInfo* stalag_ = nullptr;
  const TypeInfo* cloud_ = nullptr;
  double time_ = 0;
  bool dragging_ = false, quit_ = false, showGrid_ = false;
  // Bridge building
  const TypeInfo* bridgeType_ = nullptr;
  BridgeQueue queue_{1234};
  int player_ = 1;
  int hoverX_ = -1, hoverY_ = -1;
  int pressX_ = 0, pressY_ = 0;
  bool leftDown_ = false, moved_ = false;
  uint32_t seed_ = 1;
  std::vector<std::string> missions_;
  int missionIdx_ = -1;
  std::string sceneName_ = "test layout";
  void frame(float minx, float miny, float maxx, float maxy);
  std::array<bool, 256> rimMask_{};  // palette indices used only by island rims
};

}  // namespace ns
