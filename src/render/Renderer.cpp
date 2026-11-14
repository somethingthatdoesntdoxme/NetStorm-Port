#include "render/Renderer.h"

#include <vector>

namespace ns {

bool Renderer::init(const WindowConfig& cfg, std::string* err) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
    if (err) *err = SDL_GetError();
    return false;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // per-texture scale mode is set explicitly
  Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  if (cfg.fullscreenDesktop) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  if (cfg.hidden) flags |= SDL_WINDOW_HIDDEN;
  win_ = SDL_CreateWindow("NetStorm", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cfg.width, cfg.height,
                          flags);
  if (!win_) {
    if (err) *err = SDL_GetError();
    return false;
  }
  Uint32 rflags = SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE;
  if (cfg.vsync) rflags |= SDL_RENDERER_PRESENTVSYNC;
  ren_ = SDL_CreateRenderer(win_, -1, rflags);
  if (!ren_) ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_SOFTWARE);
  if (!ren_) {
    if (err) *err = SDL_GetError();
    return false;
  }
  SDL_SetRenderDrawBlendMode(ren_, SDL_BLENDMODE_BLEND);
  return true;
}

void Renderer::shutdown() {
  if (ren_) SDL_DestroyRenderer(ren_);
  if (win_) SDL_DestroyWindow(win_);
  ren_ = nullptr;
  win_ = nullptr;
}

void Renderer::outputSize(int& w, int& h) const { SDL_GetRendererOutputSize(ren_, &w, &h); }

bool Renderer::screenshot(const std::string& path) {
  int w, h;
  outputSize(w, h);
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return false;
  bool ok = SDL_RenderReadPixels(ren_, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0 &&
            SDL_SaveBMP(s, path.c_str()) == 0;
  SDL_FreeSurface(s);
  return ok;
}

}  // namespace ns
