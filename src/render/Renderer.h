#pragma once
#include <SDL.h>

#include <string>

namespace ns {

// Owns the window + SDL renderer. All drawing happens in "world" space and is
// mapped to the screen by the Camera, so the game scales to any resolution
// instead of drawing 1997-sized sprites in a corner.
struct Camera {
  float x = 0, y = 0;  // world point at the centre of the screen
  float zoom = 2.0f;   // screen pixels per world pixel
  int screenW = 1280, screenH = 720;

  SDL_FRect toScreen(float wx, float wy, float ww, float wh) const {
    return {(wx - x) * zoom + screenW * 0.5f, (wy - y) * zoom + screenH * 0.5f, ww * zoom, wh * zoom};
  }
  void screenToWorld(int sx, int sy, float& wx, float& wy) const {
    wx = (sx - screenW * 0.5f) / zoom + x;
    wy = (sy - screenH * 0.5f) / zoom + y;
  }
};

struct WindowConfig {
  int width = 1600, height = 900;
  bool fullscreenDesktop = false;  // borderless fullscreen at desktop res
  bool vsync = true;
  bool hidden = false;  // for headless screenshots / tests
};

class Renderer {
 public:
  bool init(const WindowConfig& cfg, std::string* err);
  void shutdown();
  ~Renderer() { shutdown(); }

  SDL_Renderer* sdl() const { return ren_; }
  SDL_Window* window() const { return win_; }
  void outputSize(int& w, int& h) const;

  // Saves the current back buffer as a BMP (call before present()).
  bool screenshot(const std::string& path);

 private:
  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
};

}  // namespace ns
