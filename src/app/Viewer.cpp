#include "app/Viewer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ns {

Viewer::Viewer(Renderer& r, const GameData& gd) : r_(r), gd_(gd), cache_(r.sdl(), gd) { setSet(0); }

void Viewer::setSet(int s) {
  int n = gd_.shapes.setCount();
  set_ = ((s % n) + n) % n;
  layout();
  updateTitle();
}

// Lay out all frames of the set in rows, anchored so their bounding boxes
// don't overlap, then fit the camera to the result.
void Viewer::layout() {
  cells_.clear();
  int n = gd_.shapes.frameCount(set_);
  int sw, sh;
  r_.outputSize(sw, sh);
  float maxRow = std::max(400.0f, sw / 2.0f);
  float x = 0, y = 0, rowH = 0;
  contentW_ = 0;
  for (int f = 0; f < n; ++f) {
    auto fr = gd_.shapes.decode(set_, f);
    if (fr.width <= 0) continue;
    if (x > 0 && x + fr.width > maxRow) {
      x = 0;
      y += rowH + 6;
      rowH = 0;
    }
    cells_.push_back({f, x - fr.originX, y - fr.originY});
    x += fr.width + 6;
    rowH = std::max(rowH, float(fr.height));
    contentW_ = std::max(contentW_, x);
  }
  contentH_ = y + rowH;
  cam_.screenW = sw;
  cam_.screenH = sh;
  cam_.zoom = std::clamp(std::min(sw * 0.9f / std::max(1.0f, contentW_), sh * 0.9f / std::max(1.0f, contentH_)),
                         0.25f, 8.0f);
  cam_.x = contentW_ / 2;
  cam_.y = contentH_ / 2;
}

void Viewer::updateTitle() {
  char t[256];
  std::snprintf(t, sizeof t,
                "NetStorm Port - sprite set %d/%d %s (%d frames)  zoom %.2fx  %s  fx %d  [<-/-> set, wheel zoom, "
                "drag pan, F filter, T effect table, Space animate]",
                set_, gd_.shapes.setCount() - 1,
                gd_.registry.bySet(set_) ? gd_.registry.bySet(set_)->name.c_str() : "?", gd_.shapes.frameCount(set_),
                cam_.zoom,
                cache_.smooth() ? "smooth" : "sharp", remap_);
  SDL_SetWindowTitle(r_.window(), t);
}

void Viewer::handle(const SDL_Event& e) {
  switch (e.type) {
    case SDL_QUIT:
      quit_ = true;
      break;
    case SDL_KEYDOWN:
      switch (e.key.keysym.sym) {
        case SDLK_ESCAPE: quit_ = true; break;
        case SDLK_RIGHT: setSet(set_ + 1); break;
        case SDLK_LEFT: setSet(set_ - 1); break;
        case SDLK_PAGEDOWN: setSet(set_ + 10); break;
        case SDLK_PAGEUP: setSet(set_ - 10); break;
        case SDLK_f:
          cache_.setSmooth(!cache_.smooth());
          updateTitle();
          break;
        case SDLK_t:
          remap_ = (remap_ + 1) % (int(gd_.remaps.tables.size()) + 1);
          updateTitle();
          break;
        case SDLK_SPACE: animate_ = !animate_; break;
        case SDLK_F11: {
          bool fs = SDL_GetWindowFlags(r_.window()) & SDL_WINDOW_FULLSCREEN_DESKTOP;
          SDL_SetWindowFullscreen(r_.window(), fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
          break;
        }
        default: break;
      }
      break;
    case SDL_MOUSEWHEEL: {
      int mx, my;
      SDL_GetMouseState(&mx, &my);
      float wx, wy;
      cam_.screenToWorld(mx, my, wx, wy);
      cam_.zoom = std::clamp(cam_.zoom * (e.wheel.y > 0 ? 1.15f : 1 / 1.15f), 0.1f, 16.0f);
      float wx2, wy2;
      cam_.screenToWorld(mx, my, wx2, wy2);  // keep the point under the cursor fixed
      cam_.x += wx - wx2;
      cam_.y += wy - wy2;
      updateTitle();
      break;
    }
    case SDL_MOUSEBUTTONDOWN: dragging_ = true; break;
    case SDL_MOUSEBUTTONUP: dragging_ = false; break;
    case SDL_MOUSEMOTION:
      if (dragging_) {
        cam_.x -= e.motion.xrel / cam_.zoom;
        cam_.y -= e.motion.yrel / cam_.zoom;
      }
      break;
    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        r_.outputSize(cam_.screenW, cam_.screenH);
      }
      break;
    default: break;
  }
}

void Viewer::update(double dt) {
  if (animate_) animT_ += dt;
}

void Viewer::draw() {
  SDL_Renderer* ren = r_.sdl();
  SDL_SetRenderDrawColor(ren, 22, 26, 40, 255);
  SDL_RenderClear(ren);
  if (animate_ && !cells_.empty()) {
    // Play the set as an animation at the screen centre, 12 fps.
    size_t i = size_t(animT_ * 12) % cells_.size();
    const auto& st = cache_.get(set_, cells_[i].frame, remap_);
    if (st.tex) {
      float z = cam_.zoom * 2;
      SDL_FRect d{cam_.screenW * 0.5f + st.originX * z, cam_.screenH * 0.5f + st.originY * z, st.w * z, st.h * z};
      SDL_RenderCopyF(ren, st.tex, nullptr, &d);
    }
    return;
  }
  for (auto& c : cells_) {
    const auto& st = cache_.get(set_, c.frame, remap_);
    if (!st.tex) continue;
    SDL_FRect d = cam_.toScreen(c.x + st.originX, c.y + st.originY, float(st.w), float(st.h));
    if (d.x > cam_.screenW || d.y > cam_.screenH || d.x + d.w < 0 || d.y + d.h < 0) continue;
    SDL_SetRenderDrawColor(ren, 40, 46, 66, 255);
    SDL_RenderFillRectF(ren, &d);
    SDL_RenderCopyF(ren, st.tex, nullptr, &d);
  }
}

}  // namespace ns
