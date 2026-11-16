#include "app/WorldView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ns {

WorldView::WorldView(Renderer& r, const GameData& gd) : r_(r), gd_(gd), cache_(r.sdl(), gd) {
  isle_ = gd.registry.byName("isle");
  fringe_ = gd.registry.byName("fringe");
  stalag_ = gd.registry.byName("islandStalag");
  cloud_ = gd.registry.byName("mcloud");
  r_.outputSize(cam_.screenW, cam_.screenH);
  buildTestScene(seed_);
}

void WorldView::buildTestScene(uint32_t seed) {
  islands_.clear();
  objects_.clear();
  const Element els[4] = {Element::Wind, Element::Sun, Element::Rain, Element::Thunder};
  const int pos[4][2] = {{0, 0}, {30, -4}, {4, 26}, {34, 24}};
  for (int i = 0; i < 4; ++i) {
    Island isl = makeBlobIsland(22 + int(seed % 3) * 2, 18, els[i], seed * 31 + i * 7);
    isl.worldX = pos[i][0] * kCellW;
    isl.worldY = pos[i][1] * kCellH;
    islands_.push_back(std::move(isl));
  }
  // Frame the whole layout.
  float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
  for (auto& isl : islands_) {
    minx = std::min(minx, float(isl.worldX));
    miny = std::min(miny, float(isl.worldY));
    maxx = std::max(maxx, float(isl.worldX + isl.width() * kCellW));
    maxy = std::max(maxy, float(isl.worldY + isl.height() * kCellH + 40));
  }
  cam_.x = (minx + maxx) / 2;
  cam_.y = (miny + maxy) / 2;
  cam_.zoom = std::min(cam_.screenW * 0.92f / (maxx - minx), cam_.screenH * 0.92f / (maxy - miny));
  auto add = [&](const char* type, int island, int cx, int cy, const char* cluster = "", int team = 0) {
    const TypeInfo* t = gd_.registry.byName(type);
    if (!t || !t->def) return;
    PlacedObject o;
    o.type = t;
    o.island = island;
    o.cellX = cx;
    o.cellY = cy;
    o.cluster = cluster;
    o.team = team;
    o.animOffset = float((cx * 7 + cy * 13) % 10) * 0.1f;
    objects_.push_back(o);
  };
  // A sample of the game's buildings and units on each island.
  add("windFactory", 0, 6, 4);
  add("windArcher", 0, 13, 9);
  add("windArcher", 0, 4, 11);
  add("geyser", 0, 9, 11);
  add("priest", 0, 15, 5);
  add("sunFactory", 1, 7, 5);
  add("sunArcher", 1, 13, 10);
  add("sunCannon", 1, 4, 10);
  add("rainFactory", 2, 6, 5);
  add("rainBlocker", 2, 13, 10);
  add("rainCannon", 2, 5, 11);
  add("thunderFactory", 3, 6, 5);
  add("thunderArcher", 3, 13, 10);
  add("thunderCannon", 3, 5, 11);
  add("altar", 3, 15, 4);
}

void WorldView::handle(const SDL_Event& e) {
  switch (e.type) {
    case SDL_QUIT: quit_ = true; break;
    case SDL_KEYDOWN:
      switch (e.key.keysym.sym) {
        case SDLK_ESCAPE: quit_ = true; break;
        case SDLK_g: showGrid_ = !showGrid_; break;
        case SDLK_n: buildTestScene(++seed_); break;
        case SDLK_f: cache_.setSmooth(!cache_.smooth()); break;
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
      float wx, wy, wx2, wy2;
      cam_.screenToWorld(mx, my, wx, wy);
      cam_.zoom = std::clamp(cam_.zoom * (e.wheel.y > 0 ? 1.15f : 1 / 1.15f), 0.5f, 12.0f);
      cam_.screenToWorld(mx, my, wx2, wy2);
      cam_.x += wx - wx2;
      cam_.y += wy - wy2;
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
      if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) r_.outputSize(cam_.screenW, cam_.screenH);
      break;
    default: break;
  }
}

void WorldView::update(double dt) {
  time_ += dt;
  // Keyboard / Steam Deck d-pad panning.
  const Uint8* k = SDL_GetKeyboardState(nullptr);
  float sp = float(400 * dt / cam_.zoom);
  if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) cam_.x -= sp;
  if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) cam_.x += sp;
  if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) cam_.y -= sp;
  if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) cam_.y += sp;
  for (size_t i = 0; i < islands_.size(); ++i)
    islands_[i].bob = float(std::sin(time_ * 0.6 + i * 1.7) * 1.5);
}

void WorldView::blit(const SpriteTex& st, float wx, float wy) {
  if (!st.tex) return;
  SDL_FRect d = cam_.toScreen(wx + st.originX, wy + st.originY, float(st.w), float(st.h));
  if (d.x > cam_.screenW || d.y > cam_.screenH || d.x + d.w < 0 || d.y + d.h < 0) return;
  SDL_RenderCopyF(r_.sdl(), st.tex, nullptr, &d);
}

void WorldView::drawSky() {
  SDL_Renderer* ren = r_.sdl();
  // Vertical gradient: deep storm blue to warm haze.
  for (int y = 0; y < cam_.screenH; y += 4) {
    float t = float(y) / cam_.screenH;
    SDL_SetRenderDrawColor(ren, Uint8(40 + 70 * t), Uint8(52 + 60 * t), Uint8(96 + 40 * t), 255);
    SDL_FRect band{0, float(y), float(cam_.screenW), 4};
    SDL_RenderFillRectF(ren, &band);
  }
  if (!cloud_ || cloud_->set < 0) return;
  // The "mcloud" frames are 128x128 tileable sky textures. Tile them with
  // slow parallax and drift behind the islands.
  const float par = 0.35f;
  const float z = cam_.zoom;
  const float tile = 128.0f;
  float drift = float(time_ * 4.0);
  float left = cam_.x * par - cam_.screenW * 0.5f / z + drift;
  float top = cam_.y * par - cam_.screenH * 0.5f / z;
  int tx0 = int(std::floor(left / tile)), ty0 = int(std::floor(top / tile));
  int nx = int(cam_.screenW / (tile * z)) + 2, ny = int(cam_.screenH / (tile * z)) + 2;
  int nf = gd_.shapes.frameCount(cloud_->set);
  for (int ty = ty0; ty < ty0 + ny; ++ty)
    for (int tx = tx0; tx < tx0 + nx; ++tx) {
      int f = int(uint32_t(tx * 7 + ty * 13) % uint32_t(nf));
      const SpriteTex& st = cache_.get(cloud_->set, f);
      if (!st.tex) continue;
      SDL_FRect d{(tx * tile - left) * z, (ty * tile - top) * z, tile * z + 1, tile * z + 1};
      SDL_SetTextureColorMod(st.tex, 175, 180, 205);  // push the sky back a bit
      SDL_RenderCopyF(ren, st.tex, nullptr, &d);
      SDL_SetTextureColorMod(st.tex, 255, 255, 255);
    }
}

void WorldView::drawIsland(const Island& isl) {
  if (!isle_ || isle_->set < 0) return;
  const float ox = float(isl.worldX), oy = float(isl.worldY) + isl.bob;
  // Underside first: stalactites and fringe rock below bottom-facing tiles.
  for (int y = 0; y < isl.height(); ++y)
    for (int x = 0; x < isl.width(); ++x) {
      if (!isl.filled(x, y)) continue;
      auto tc = isl.tileFor(x, y, seed_);
      if (!tc.fringe) continue;
      float ax = ox + (x + 1) * kCellW - 1, ay = oy + (y + 1) * kCellH - 1;
      if (stalag_ && stalag_->set >= 0 && (x * 5 + y * 3) % 4 == 0) {
        int f = (x * 7 + y) % gd_.shapes.frameCount(stalag_->set);
        blit(cache_.get(stalag_->set, f), ax + 14, ay - 4);
      }
      if (fringe_ && fringe_->set >= 0) {
        int f = (x * 3 + y * 5) % 9;  // EA01..EA09
        blit(cache_.get(fringe_->set, f), ax, ay + 40);
      }
    }
  // Surface tiles.
  for (int y = 0; y < isl.height(); ++y)
    for (int x = 0; x < isl.width(); ++x) {
      if (!isl.filled(x, y)) continue;
      auto tc = isl.tileFor(x, y, seed_);
      int line = isle_->findLine(tc.cluster, tc.variation);
      if (line < 0) line = isle_->findLine(tc.cluster, 1);
      if (line < 0) continue;
      float ax = ox + (x + 1) * kCellW - 1, ay = oy + (y + 1) * kCellH - 1;
      blit(cache_.get(isle_->set, isle_->frame(line)), ax, ay);
      if (showGrid_) {
        SDL_FRect d = cam_.toScreen(ax - kCellW + 1, ay - kCellH + 1, kCellW, kCellH);
        SDL_SetRenderDrawColor(r_.sdl(), 255, 255, 255, 40);
        SDL_RenderDrawRectF(r_.sdl(), &d);
      }
    }
}

// Buildings anchor at the bottom-right pixel of their footprint, like tiles.
// Mobile units (priest etc.) anchor at their feet, centre of the cell.
void WorldView::anchorOf(const PlacedObject& o, float& wx, float& wy) const {
  const Island& isl = islands_[o.island];
  int fx = std::max(1, int(o.type->def->num("foot_x", 1)));
  int fy = std::max(1, int(o.type->def->num("foot_y", 1)));
  bool mobile = o.type->def->hasFlag("walker") || o.type->name == "priest";
  if (mobile) {
    wx = isl.worldX + (o.cellX + 0.5f) * kCellW;
    wy = isl.worldY + isl.bob + (o.cellY + 0.5f) * kCellH;
  } else {
    wx = isl.worldX + float(o.cellX + fx) * kCellW - 1;
    wy = isl.worldY + isl.bob + float(o.cellY + fy) * kCellH - 1;
  }
}

void WorldView::drawObjects() {
  struct Item {
    float y;
    const PlacedObject* o;
  };
  std::vector<Item> order;
  for (auto& o : objects_) {
    float wx, wy;
    anchorOf(o, wx, wy);
    order.push_back({wy, &o});
  }
  std::sort(order.begin(), order.end(), [](const Item& a, const Item& b) { return a.y < b.y; });

  auto baseFor = [&](const PlacedObject& o) {
    const auto& frames = o.type->def->frames;
    for (size_t i = 0; i < frames.size() && i < size_t(o.type->imageFrames); ++i)
      if (frames[i].label.find("baseframe") != std::string::npos) return int(i);
    return -1;
  };
  auto frameFor = [&](const PlacedObject& o) {
    const auto& frames = o.type->def->frames;
    // Animate through consecutive frame lines of the first cluster. For types
    // with a "baseframe", these are overlays drawn on top of the base.
    std::string cl = o.cluster.empty() ? frames.front().cluster : o.cluster;
    std::vector<int> lines;
    for (size_t i = 0; i < frames.size() && i < size_t(o.type->imageFrames); ++i)
      if (frames[i].cluster == cl) lines.push_back(int(i));
    if (lines.empty()) return 0;
    double fps = 8.0;
    return lines[size_t((time_ + o.animOffset) * fps) % lines.size()];
  };

  // Shadows under everything, then the objects back to front.
  for (auto& it : order) {
    const PlacedObject& o = *it.o;
    int base = baseFor(o);
    int line = base >= 0 ? base : frameFor(o);
    int sf = o.type->shadowFrame(line);
    if (sf < 0 || sf >= gd_.shapes.frameCount(o.type->set)) continue;
    float wx, wy;
    anchorOf(o, wx, wy);
    blit(cache_.shadow(o.type->set, sf), wx, wy);
  }
  for (auto& it : order) {
    const PlacedObject& o = *it.o;
    float wx, wy;
    anchorOf(o, wx, wy);
    int base = baseFor(o);
    int anim = frameFor(o);
    if (base >= 0) blit(cache_.get(o.type->set, o.type->frame(base), o.team), wx, wy);
    if (anim != base) blit(cache_.get(o.type->set, o.type->frame(anim), o.team), wx, wy);
  }
}

void WorldView::draw() {
  drawSky();
  for (auto& isl : islands_) drawIsland(isl);
  drawObjects();
  char t[160];
  std::snprintf(t, sizeof t,
                "NetStorm Port - world test  zoom %.2fx  [drag/WASD pan, wheel zoom, N new layout, G grid, F filter, "
                "Tab sprite viewer]",
                cam_.zoom);
  SDL_SetWindowTitle(r_.window(), t);
}

}  // namespace ns
