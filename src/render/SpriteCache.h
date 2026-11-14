#pragma once
// Turns indexed shape frames into GPU textures on demand.
//
// Key = (set, frame, remap). Remap 0 means "no team colour". The cache is the
// single place upscaled art will plug in later: if an HD replacement exists
// for a frame, it's loaded here instead and drawn at the same world size.
#include <SDL.h>

#include <unordered_map>

#include "data/GameData.h"

namespace ns {

struct SpriteTex {
  SDL_Texture* tex = nullptr;
  int w = 0, h = 0;          // world-space size (original pixels)
  int originX = 0, originY = 0;  // top-left relative to anchor
};

class SpriteCache {
 public:
  SpriteCache(SDL_Renderer* r, const GameData& gd) : ren_(r), gd_(gd) {}
  ~SpriteCache() { clear(); }

  const SpriteTex& get(int set, int frame, int remap = 0);
  void clear();
  void setSmooth(bool s);  // linear vs nearest filtering when zoomed
  bool smooth() const { return smooth_; }

 private:
  SDL_Renderer* ren_;
  const GameData& gd_;
  bool smooth_ = false;
  std::unordered_map<uint64_t, SpriteTex> map_;
};

}  // namespace ns
