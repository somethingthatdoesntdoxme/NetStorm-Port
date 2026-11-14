#pragma once
// Milestone-0 asset viewer: browses every sprite set straight out of the
// user's _shapes.shp, rendered through the real engine path (camera, zoom,
// texture cache, team-colour remaps).
#include <memory>

#include "render/Renderer.h"
#include "render/SpriteCache.h"

namespace ns {

class Viewer {
 public:
  Viewer(Renderer& r, const GameData& gd);
  void handle(const SDL_Event& e);
  void update(double dt);
  void draw();
  void setSet(int s);
  bool wantsQuit() const { return quit_; }

 private:
  void layout();
  void updateTitle();

  Renderer& r_;
  const GameData& gd_;
  SpriteCache cache_;
  Camera cam_;
  int set_ = 0;
  int remap_ = 0;
  bool animate_ = false;
  double animT_ = 0;
  bool dragging_ = false;
  bool quit_ = false;
  struct Cell {
    int frame;
    float x, y;  // anchor position in world space
  };
  std::vector<Cell> cells_;
  float contentW_ = 0, contentH_ = 0;
};

}  // namespace ns
