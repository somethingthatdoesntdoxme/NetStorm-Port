#include "app/WorldView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "render/TinyFont.h"

namespace ns {

WorldView::WorldView(Renderer& r, const GameData& gd) : r_(r), gd_(gd), cache_(r.sdl(), gd) {
  isle_ = gd.registry.byName("isle");
  fringe_ = gd.registry.byName("fringe");
  stalag_ = gd.registry.byName("islandStalag");
  cloud_ = gd.registry.byName("mcloud");
  bridgeType_ = gd.registry.byName("bridge");
  r_.outputSize(cam_.screenW, cam_.screenH);
  // Rim colours = indices that appear in edge/corner tiles but never in the
  // filled "AA" tiles. Owned islands get these recoloured to the player colour.
  if (isle_ && isle_->def) {
    std::array<int, 256> inEdge{}, inFill{};
    for (size_t i = 0; i < isle_->def->frames.size() && int(i) < isle_->imageFrames; ++i) {
      auto f = gd_.shapes.decode(isle_->set, int(i));
      auto& cnt = isle_->def->frames[i].cluster == "AA" ? inFill : inEdge;
      for (size_t p = 0; p < f.index.size(); ++p)
        if (f.mask[p]) cnt[f.index[p]]++;
    }
    for (int i = 0; i < 256; ++i) {
      const Rgb& c = gd_.gamePalette.c[i];
      bool warm = c.r > c.b + 60 && c.r >= c.g;  // the rim highlight is orange
      rimMask_[i] = inEdge[i] > 0 && inFill[i] == 0 && warm;
    }
  }
  for (auto& [name, bytes] : gd_.archive.entries())
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".fort") == 0) missions_.push_back(name);
  buildTestScene(seed_);
}

void WorldView::frame(float minx, float miny, float maxx, float maxy) {
  cam_.x = (minx + maxx) / 2;
  cam_.y = (miny + maxy) / 2;
  cam_.zoom = std::max(0.25f, std::min(cam_.screenW * 0.92f / std::max(1.0f, maxx - minx),
                                       cam_.screenH * 0.92f / std::max(1.0f, maxy - miny)));
}

bool WorldView::loadMissionByName(const std::string& part) {
  for (size_t i = 0; i < missions_.size(); ++i)
    if (missions_[i].find(part) != std::string::npos) return loadMission(int(i));
  return false;
}

bool WorldView::loadMission(int index) {
  if (missions_.empty()) return false;
  index = ((index % int(missions_.size())) + int(missions_.size())) % int(missions_.size());
  const Bytes* data = gd_.archive.find(missions_[index]);
  if (!data) return false;
  FortFile fort;
  std::string err;
  if (!fort.parse(*data, gd_.types, &err)) {
    std::fprintf(stderr, "%s: %s\n", missions_[index].c_str(), err.c_str());
    return false;
  }
  MissionWorld mw;
  buildMission(gd_, fort, mw, seed_);
  missionIdx_ = index;
  sceneName_ = missions_[index];
  islands_.clear();
  islands_.push_back(std::move(mw.land));
  player_ = 1;
  for (auto& mo : mw.objects)
    if (mo.type->name == "priest" && mo.owner > 0) {
      player_ = mo.owner;
      break;
    }
  sim_ = std::make_unique<Sim>(gd_, islands_[0]);
  for (auto& mo : mw.objects) {
    bool mobile = mo.type->def->hasFlag("walker") || mo.type->name == "priest";
    sim_->spawn(mo.type, mo.owner, mobile ? mo.cellX + 0.5f : float(mo.cellX), mobile ? mo.cellY + 0.5f : float(mo.cellY));
  }
  startSim(3000);
  frame(float(mw.minX * kCellW), float(mw.minY * kCellH), float((mw.maxX + 1) * kCellW),
        float((mw.maxY + 1) * kCellH + 40));
  return true;
}

// Common setup once a world + entities exist: starting Storm Power and a pair
// of golems at every temple.
void WorldView::startSim(float startPower) {
  selectedWorkshop_ = dropIndex_ = -1;
  for (int p = 0; p <= 8; ++p) sim_->player(p).power = startPower;
  const TypeInfo* golem = gd_.registry.byName("sunWalker");
  if (!golem) return;
  std::vector<Entity> temples;
  for (auto& e : sim_->entities())
    if (e.role == Role::Temple && e.owner > 0) temples.push_back(e);
  for (auto& t : temples) {
    int fx = std::max(1, int(t.type->def->num("foot_x", 1)));
    for (int k = 0; k < 2; ++k) sim_->spawn(golem, t.owner, t.x - fx / 2 + k + 0.5f, t.y + 1.5f);
  }
}
void WorldView::buildTestScene(uint32_t seed) {
  // One world grid with four blob islands stamped in (same layout as before).
  islands_.clear();
  Island world(80, 60);
  const Element els[4] = {Element::Sun, Element::Wind, Element::Rain, Element::Thunder};
  const int pos[4][2] = {{4, 6}, {34, 2}, {8, 32}, {38, 30}};
  const int owners[4] = {1, 0, 0, 2};
  for (int i = 0; i < 4; ++i) {
    Island blob = makeBlobIsland(22 + int(seed % 3) * 2, 18, els[i], seed * 31 + i * 7);
    for (int y = 0; y < blob.height(); ++y)
      for (int x = 0; x < blob.width(); ++x)
        if (blob.filled(x, y)) {
          world.set(pos[i][0] + x, pos[i][1] + y, blob.at(x, y));
          world.setOwner(pos[i][0] + x, pos[i][1] + y, owners[i]);
        }
  }
  islands_.push_back(std::move(world));
  sim_ = std::make_unique<Sim>(gd_, islands_[0]);
  player_ = 1;
  // add(type, island, footprint top-left cell)
  auto add = [&](const char* type, int island, int cx, int cy) {
    const TypeInfo* t = gd_.registry.byName(type);
    if (!t || !t->def) return;
    int fx = std::max(1, int(t->def->num("foot_x", 1))), fy = std::max(1, int(t->def->num("foot_y", 1)));
    int owner = owners[island];
    bool mobile = t->def->hasFlag("walker");
    float ax = mobile ? pos[island][0] + cx + 0.5f : float(pos[island][0] + cx + fx - 1);
    float ay = mobile ? pos[island][1] + cy + 0.5f : float(pos[island][1] + cy + fy - 1);
    if (t->def->hasFlag("geyser")) owner = 0;
    sim_->spawn(t, owner, ax, ay);
  };
  // Red player (1): thunder base with temple, workshop, generator, geyser.
  add("thunderFactory", 0, 3, 3);
  add("rainVortex", 0, 12, 2);
  add("thunderBattery", 0, 4, 10);
  add("thunderArcher", 0, 15, 10);
  add("geyser", 0, 9, 12);
  add("priest", 0, 11, 9);
  // Neutral islands with geysers to claim by bridge.
  add("geyser", 1, 8, 6);
  add("geyser", 1, 14, 9);
  add("geyser", 2, 9, 7);
  // Blue player (2).
  add("sunFactory", 3, 4, 3);
  add("sunArcher", 3, 13, 10);
  add("sunCannon", 3, 5, 11);
  add("windBattery", 3, 12, 4);
  startSim(3000);
  frame(0, 0, float(world.width() * kCellW), float(world.height() * kCellH + 40));
}
void WorldView::handle(const SDL_Event& e) {
  switch (e.type) {
    case SDL_QUIT: quit_ = true; break;
    case SDL_KEYDOWN:
      switch (e.key.keysym.sym) {
        case SDLK_ESCAPE:
          if (selectedWorkshop_ >= 0 || dropIndex_ >= 0) selectedWorkshop_ = dropIndex_ = -1;
          else quit_ = true;
          break;
        case SDLK_g: showGrid_ = !showGrid_; break;
        case SDLK_r: queue_.rotate(); break;
        case SDLK_b: autoBuild(1); break;  // let the AI place one piece
        case SDLK_n:
          sceneName_ = "test layout";
          missionIdx_ = -1;
          buildTestScene(++seed_);
          break;
        case SDLK_m:
        case SDLK_RIGHTBRACKET: loadMission(missionIdx_ + 1); break;
        case SDLK_LEFTBRACKET: loadMission(missionIdx_ - 1); break;
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
    // Left click drops the current bridge piece (left-drag pans), right
    // click / R rotates it, middle-drag pans.
    case SDL_MOUSEBUTTONDOWN:
      if (e.button.button == SDL_BUTTON_RIGHT) {
        queue_.rotate();
      } else {
        dragging_ = true;
        leftDown_ = e.button.button == SDL_BUTTON_LEFT;
        moved_ = false;
        pressX_ = e.button.x;
        pressY_ = e.button.y;
      }
      break;
    case SDL_MOUSEBUTTONUP:
      if (leftDown_ && !moved_ && !islands_.empty()) {
        int sx = e.button.x, sy = e.button.y;
        if (handleUiClick(sx, sy)) {
          // HUD button
        } else if (dropIndex_ >= 0 && sim_) {
          const auto& ready = sim_->player(player_).ready;
          if (dropIndex_ < int(ready.size())) {
            const TypeInfo* t = gd_.registry.byName(ready[size_t(dropIndex_)]);
            int fx = std::max(1, int(t->def->num("foot_x", 1))), fy = std::max(1, int(t->def->num("foot_y", 1)));
            float wx, wy;
            cam_.screenToWorld(sx, sy, wx, wy);
            int ax = int(std::floor(wx / kCellW)) + fx / 2, ay = int(std::floor(wy / kCellH)) + fy / 2;
            if (sim_->dropUnit(player_, dropIndex_, ax, ay)) dropIndex_ = -1;
            else message_ = "CANT DROP THERE - NEEDS OPEN SKY NEXT TO YOUR LAND", messageTime_ = time_;
          } else {
            dropIndex_ = -1;
          }
        } else if (const Entity* ent = entityAtScreen(sx, sy);
                   ent && ent->role == Role::Workshop && ent->owner == player_) {
          selectedWorkshop_ = ent->id;
        } else if (hoverX_ >= 0 && canPlaceBridge(islands_[0], queue_.current(), hoverX_, hoverY_, player_)) {
          placeBridge(islands_[0], queue_.current(), hoverX_, hoverY_, player_);
          queue_.next();
        }
      }
      dragging_ = leftDown_ = false;
      break;
    case SDL_MOUSEMOTION:
      if (dragging_ && (std::abs(e.motion.x - pressX_) + std::abs(e.motion.y - pressY_) > 5)) moved_ = true;
      if (dragging_ && moved_) {
        cam_.x -= e.motion.xrel / cam_.zoom;
        cam_.y -= e.motion.yrel / cam_.zoom;
      }
      hoverCell(e.motion.x, e.motion.y);
      break;
    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) r_.outputSize(cam_.screenW, cam_.screenH);
      break;
    default: break;
  }
}

void WorldView::update(double dt) {
  time_ += dt;
  if (sim_) {
    simAcc_ += std::min(dt, 0.25);
    while (simAcc_ >= Sim::kDt) {
      sim_->step();
      simAcc_ -= Sim::kDt;
    }
  }
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
      SDL_SetTextureColorMod(st.tex, 112, 104, 128);  // dusky purple-grey, as in the original
      SDL_RenderCopyF(ren, st.tex, nullptr, &d);
      SDL_SetTextureColorMod(st.tex, 255, 255, 255);
    }
}

// Player colours for owned-island rims.
static SDL_Color playerColour(int owner) {
  static const SDL_Color kPlayer[9] = {{0, 0, 0, 0},       {235, 45, 35, 255}, {45, 120, 255, 255},
                                       {250, 215, 45, 255}, {45, 205, 75, 255}, {205, 65, 225, 255},
                                       {45, 225, 225, 255}, {250, 145, 35, 255}, {240, 240, 240, 255}};
  return kPlayer[(owner >= 0 && owner <= 8) ? owner : 0];
}

void WorldView::drawIsland(const Island& isl) {
  if (!isle_ || isle_->set < 0) return;
  const float ox = float(isl.worldX), oy = float(isl.worldY) + isl.bob;
  // Only visit cells near the screen (mission worlds are 512x512 cells).
  float wl, wt, wr, wb;
  cam_.screenToWorld(0, 0, wl, wt);
  cam_.screenToWorld(cam_.screenW, cam_.screenH, wr, wb);
  const int cx0 = std::max(0, int((wl - ox) / kCellW) - 4), cx1 = std::min(isl.width() - 1, int((wr - ox) / kCellW) + 4);
  const int cy0 = std::max(0, int((wt - oy) / kCellH) - 8), cy1 = std::min(isl.height() - 1, int((wb - oy) / kCellH) + 2);
  // Underside first: stalactites and fringe rock below bottom-facing tiles.
  for (int y = cy0; y <= cy1; ++y)
    for (int x = cx0; x <= cx1; ++x) {
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
  drawBridges(isl, cx0, cy0, cx1, cy1);
  // Surface tiles.
  for (int y = cy0; y <= cy1; ++y)
    for (int x = cx0; x <= cx1; ++x) {
      if (!isl.filled(x, y)) continue;
      auto tc = isl.tileFor(x, y, seed_);
      int line = isle_->findLine(tc.cluster, tc.variation);
      if (line < 0) line = isle_->findLine(tc.cluster, 1);
      if (line < 0) continue;
      float ax = ox + (x + 1) * kCellW - 1, ay = oy + (y + 1) * kCellH - 1;
      int own = isl.ownerAt(x, y) ? isl.ownerAt(x, y) : isl.owner;
      if (own > 0 && tc.cluster != "AA")
        blit(cache_.tinted(isle_->set, isle_->frame(line), playerColour(own), rimMask_), ax, ay);
      else
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
void WorldView::anchorOf(const Entity& e, float& wx, float& wy) const {
  const Island& isl = islands_[0];
  bool mobile = e.type->def->hasFlag("walker") || e.role == Role::Golem || e.role == Role::Priest;
  if (mobile) {
    wx = isl.worldX + e.x * kCellW;
    wy = isl.worldY + isl.bob + e.y * kCellH;
  } else {
    wx = isl.worldX + (e.x + 1) * kCellW - 1;
    wy = isl.worldY + isl.bob + (e.y + 1) * kCellH - 1;
  }
}
int WorldView::autoBuild(int pieces) {
  if (islands_.empty()) return 0;
  int placed = 0;
  for (int i = 0; i < pieces; ++i) {
    int x, y;
    if (!bestBridgeMove(islands_[0], queue_, player_, x, y)) {
      queue_.next();  // can't use this piece: discard it like the original
      continue;
    }
    placeBridge(islands_[0], queue_.current(), x, y, player_);
    queue_.next();
    ++placed;
  }
  return placed;
}

void WorldView::hoverCell(int sx, int sy) {
  if (islands_.empty()) return;
  const Island& isl = islands_[0];
  float wx, wy;
  cam_.screenToWorld(sx, sy, wx, wy);
  int cx = int(std::floor((wx - isl.worldX) / kCellW)), cy = int(std::floor((wy - isl.worldY - isl.bob) / kCellH));
  // Centre the piece on the cursor.
  int mw = 0, mh = 0;
  for (auto [x, y] : queue_.current().cells) mw = std::max(mw, x), mh = std::max(mh, y);
  hoverX_ = cx - mw / 2;
  hoverY_ = cy - mh / 2;
}

void WorldView::drawBridges(const Island& isl, int cx0, int cy0, int cx1, int cy1) {
  if (!bridgeType_ || bridgeType_->set < 0) return;
  const float ox = float(isl.worldX), oy = float(isl.worldY) + isl.bob;
  for (int y = cy0; y <= cy1; ++y)
    for (int x = cx0; x <= cx1; ++x) {
      if (!isl.bridgeAt(x, y)) continue;
      int line = bridgeType_->findLine(isl.bridgeCluster(x, y), 1);
      if (line < 0) continue;
      blit(cache_.get(bridgeType_->set, bridgeType_->frame(line)), ox + (x + 1) * kCellW - 1,
           oy + (y + 1) * kCellH - 1);
    }
}

void WorldView::drawBuildUi() {
  if (!bridgeType_ || bridgeType_->set < 0 || islands_.empty()) return;
  SDL_Renderer* ren = r_.sdl();
  const Island& isl = islands_[0];
  // Ghost of the current piece under the cursor.
  if (hoverX_ >= 0) {
    bool ok = canPlaceBridge(isl, queue_.current(), hoverX_, hoverY_, player_);
    int line = bridgeType_->findLine("A", 1);
    const SpriteTex& st = cache_.get(bridgeType_->set, bridgeType_->frame(line));
    if (st.tex) {
      SDL_SetTextureAlphaMod(st.tex, 170);
      SDL_SetTextureColorMod(st.tex, ok ? 150 : 255, ok ? 255 : 90, ok ? 150 : 90);
      for (auto [dx, dy] : queue_.current().cells)
        blit(st, float(isl.worldX + (hoverX_ + dx + 1) * kCellW - 1),
             float(isl.worldY + isl.bob + (hoverY_ + dy + 1) * kCellH - 1));
      SDL_SetTextureAlphaMod(st.tex, 255);
      SDL_SetTextureColorMod(st.tex, 255, 255, 255);
    }
  }
  // Piece queue panel (top-left), drawn at a fixed screen scale.
  const float sc = 3.0f;
  SDL_FRect panel{12, 12, 130, 360};
  SDL_SetRenderDrawColor(ren, 20, 20, 30, 190);
  SDL_RenderFillRectF(ren, &panel);
  SDL_SetRenderDrawColor(ren, 160, 140, 100, 255);
  SDL_RenderDrawRectF(ren, &panel);
  int line = bridgeType_->findLine("A", 1);
  const SpriteTex& st = cache_.get(bridgeType_->set, bridgeType_->frame(line));
  for (int i = 0; i < BridgeQueue::kVisible; ++i) {
    const BridgeShape& s = queue_.upcoming(i);
    float px = 30, py = 30 + i * 112.0f;
    float k = i == 0 ? sc : sc * 0.75f;
    for (auto [dx, dy] : s.cells) {
      SDL_FRect d{px + dx * 16 * k / 1.6f, py + dy * 11 * k / 1.6f, 16 * k / 1.6f, (st.h) * k / 1.6f};
      if (st.tex) SDL_RenderCopyF(ren, st.tex, nullptr, &d);
    }
  }
}

std::string WorldView::demoProduction() {
  if (!sim_) return "";
  for (auto& e : sim_->entities()) {
    if (e.role != Role::Workshop || e.owner != player_) continue;
    selectedWorkshop_ = e.id;
    for (auto* t : sim_->buildable(e.id))
      if (sim_->startBuild(e.id, t->name)) return t->name;
  }
  return "";
}

const Entity* WorldView::entityAtScreen(int sx, int sy) const {
  if (!sim_) return nullptr;
  float wx, wy;
  cam_.screenToWorld(sx, sy, wx, wy);
  int cx = int(std::floor(wx / kCellW)), cy = int(std::floor(wy / kCellH));
  for (auto& e : sim_->entities()) {
    if (!e.alive || !e.type || !e.type->def) continue;
    int fx = std::max(1, int(e.type->def->num("foot_x", 1))), fy = std::max(1, int(e.type->def->num("foot_y", 1)));
    if (cx > int(e.x) - fx && cx <= int(e.x) && cy > int(e.y) - fy && cy <= int(e.y)) return &e;
  }
  return nullptr;
}

bool WorldView::handleUiClick(int sx, int sy) {
  for (auto& b : buttons_) {
    if (sx < b.r.x || sy < b.r.y || sx > b.r.x + b.r.w || sy > b.r.y + b.r.h) continue;
    if (b.action.rfind("build:", 0) == 0 && sim_) {
      std::string why;
      if (!sim_->startBuild(selectedWorkshop_, b.action.substr(6), &why)) {
        for (auto& c : why) c = char(std::toupper(static_cast<unsigned char>(c)));
        message_ = why;
        messageTime_ = time_;
      }
    } else if (b.action.rfind("drop:", 0) == 0) {
      dropIndex_ = std::atoi(b.action.c_str() + 5);
      selectedWorkshop_ = -1;
    } else if (b.action == "close") {
      selectedWorkshop_ = -1;
    }
    return true;
  }
  return false;
}

void WorldView::drawHud() {
  if (!sim_) return;
  SDL_Renderer* ren = r_.sdl();
  buttons_.clear();
  auto panel = [&](SDL_FRect r) {
    SDL_SetRenderDrawColor(ren, 18, 18, 28, 210);
    SDL_RenderFillRectF(ren, &r);
    SDL_SetRenderDrawColor(ren, 160, 140, 100, 255);
    SDL_RenderDrawRectF(ren, &r);
  };
  const SDL_Color gold{255, 220, 110, 255}, white{235, 235, 235, 255}, grey{150, 150, 150, 255},
      red{255, 90, 80, 255};
  PlayerState& me = sim_->player(player_);
  // Storm Power (yellow under 2000, red under 1000, like the original).
  panel({150, 12, 330, 34});
  SDL_Color pc = me.power < 1000 ? red : me.power < 2000 ? SDL_Color{255, 230, 60, 255} : white;
  float x = drawText(ren, 160, 21, 2, "STORM POWER ", gold);
  drawText(ren, 160 + x, 21, 2, std::to_string(int(me.power)), pc);

  // Ready units waiting to be dropped.
  float ry = 56;
  for (size_t i = 0; i < me.ready.size() && i < 6; ++i) {
    SDL_FRect r{150, ry, 330, 26};
    panel(r);
    std::string label = "PLACE " + me.ready[i];
    drawText(ren, 158, ry + 6, 2, label, dropIndex_ == int(i) ? gold : white);
    buttons_.push_back({r, "drop:" + std::to_string(i)});
    ry += 30;
  }

  // Production menu for the selected workshop.
  if (Entity* w = sim_->find(selectedWorkshop_)) {
    auto list = sim_->buildable(w->id);
    float pw = 380, px = float(cam_.screenW) - pw - 12, py = 12;
    float ph = 60 + 30.0f * float(list.size()) + 30;
    panel({px, py, pw, ph});
    drawText(ren, px + 10, py + 10, 2, w->type->name, gold);
    SDL_FRect close{px + pw - 30, py + 6, 22, 22};
    drawText(ren, close.x + 5, close.y + 4, 2, "X", red);
    buttons_.push_back({close, "close"});
    if (!w->building.empty()) {
      const TypeInfo* t = gd_.registry.byName(w->building);
      float total = t ? std::max(4.0f, float(t->def->num("cost", 400)) / 50.0f) : 8.0f;
      drawText(ren, px + 10, py + 34, 2, "BUILDING " + w->building, white);
      SDL_FRect bar{px + 10, py + 52, (pw - 20) * std::min(1.0f, w->progress / total), 4};
      SDL_SetRenderDrawColor(ren, 255, 220, 110, 255);
      SDL_RenderFillRectF(ren, &bar);
    } else {
      drawText(ren, px + 10, py + 34, 2, "CHOOSE A UNIT", grey);
    }
    float y = py + 64;
    for (auto* t : list) {
      SDL_FRect r{px + 8, y, pw - 16, 26};
      int cost = int(t->def->num("cost", 0));
      int need = int(t->def->num("minUsage", 0));
      int have = sim_->energyAt(player_, t->def->str("theme"), int(w->x), int(w->y));
      bool affordable = me.power >= cost && (need <= 0 || have >= need) && w->building.empty();
      SDL_SetRenderDrawColor(ren, 40, 40, 55, 255);
      SDL_RenderFillRectF(ren, &r);
      drawText(ren, r.x + 6, y + 6, 2, t->name, affordable ? white : grey);
      std::string c = std::to_string(cost);
      drawText(ren, r.x + r.w - 6 - float(c.size()) * 12, y + 6, 2, c, me.power >= cost ? gold : red);
      buttons_.push_back({r, "build:" + t->name});
      y += 30;
    }
  }

  // Drop ghost for the unit being placed.
  if (dropIndex_ >= 0 && dropIndex_ < int(me.ready.size())) {
    const TypeInfo* t = gd_.registry.byName(me.ready[size_t(dropIndex_)]);
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    float wx, wy;
    cam_.screenToWorld(mx, my, wx, wy);
    int fx = std::max(1, int(t->def->num("foot_x", 1))), fy = std::max(1, int(t->def->num("foot_y", 1)));
    int ax = int(std::floor(wx / kCellW)) + fx / 2, ay = int(std::floor(wy / kCellH)) + fy / 2;
    bool ok = sim_->canDropUnit(t, ax, ay, player_);
    SDL_FRect fp = cam_.toScreen(float((ax - fx + 1) * kCellW), float((ay - fy + 1) * kCellH), float(fx * kCellW),
                                 float(fy * kCellH));
    SDL_SetRenderDrawColor(ren, ok ? 80 : 255, ok ? 255 : 70, 80, 90);
    SDL_RenderFillRectF(ren, &fp);
    const SpriteTex& st = cache_.get(t->set, 0);
    if (st.tex) {
      SDL_SetTextureAlphaMod(st.tex, 180);
      blit(st, float((ax + 1) * kCellW - 1), float((ay + 1) * kCellH - 1));
      SDL_SetTextureAlphaMod(st.tex, 255);
    }
  }

  if (!message_.empty() && time_ - messageTime_ < 3.0) {
    float w = float(message_.size()) * 12 + 20;
    panel({(cam_.screenW - w) / 2, float(cam_.screenH) - 50, w, 30});
    drawText(ren, (cam_.screenW - w) / 2 + 10, float(cam_.screenH) - 42, 2, message_, red);
  }
}

void WorldView::drawObjects() {
  struct Item {
    float y;
    const Entity* o;
  };
  std::vector<Item> order;
  if (!sim_) return;
  for (auto& o : sim_->entities()) {
    if (!o.alive || !o.type || !o.type->def || o.type->set < 0) continue;
    float wx, wy;
    anchorOf(o, wx, wy);
    order.push_back({wy, &o});
  }
  std::sort(order.begin(), order.end(), [](const Item& a, const Item& b) { return a.y < b.y; });

  auto baseFor = [&](const Entity& o) {
    const auto& frames = o.type->def->frames;
    for (size_t i = 0; i < frames.size() && i < size_t(o.type->imageFrames); ++i)
      if (frames[i].label.find("baseframe") != std::string::npos) return int(i);
    return -1;
  };
  auto frameFor = [&](const Entity& o) {
    const auto& frames = o.type->def->frames;
    // Animate through consecutive frame lines of the first cluster. For types
    // with a "baseframe", these are overlays drawn on top of the base.
    // Default cluster: the line labelled "default", else the first line that
    // isn't a help/info picture.
    std::string cl;
    if (cl.empty()) {
      for (size_t i = 0; i < frames.size() && i < size_t(o.type->imageFrames); ++i)
        if (frames[i].label.find("default") != std::string::npos) {
          cl = frames[i].cluster;
          break;
        }
    }
    if (cl.empty()) {
      for (size_t i = 0; i < frames.size() && i < size_t(o.type->imageFrames); ++i)
        if (frames[i].label.find("help") == std::string::npos && frames[i].label.find("info") == std::string::npos) {
          cl = frames[i].cluster;
          break;
        }
    }
    if (cl.empty()) cl = frames.front().cluster;
    std::vector<int> lines;
    for (size_t i = 0; i < frames.size() && i < size_t(o.type->imageFrames); ++i)
      if (frames[i].cluster == cl && frames[i].label.find("help") == std::string::npos &&
          frames[i].label.find("info") == std::string::npos)
        lines.push_back(int(i));
    if (lines.empty()) return 0;
    double fps = 8.0;
    return lines[size_t((time_ + o.animOffset) * fps) % lines.size()];
  };

  // Shadows under everything, then the objects back to front.
  for (auto& it : order) {
    const Entity& o = *it.o;
    int base = baseFor(o);
    int line = base >= 0 ? base : frameFor(o);
    int sf = o.type->shadowFrame(line);
    if (sf < 0 || sf >= gd_.shapes.frameCount(o.type->set)) continue;
    float wx, wy;
    anchorOf(o, wx, wy);
    blit(cache_.shadow(o.type->set, sf), wx, wy);
  }
  for (auto& it : order) {
    const Entity& o = *it.o;
    float wx, wy;
    anchorOf(o, wx, wy);
    int base = baseFor(o);
    int anim = frameFor(o);
    if (base >= 0) blit(cache_.get(o.type->set, o.type->frame(base)), wx, wy);
    if (anim != base) blit(cache_.get(o.type->set, o.type->frame(anim)), wx, wy);
    // A golem carrying a crystal shows a little gold marker.
    if (o.role == Role::Golem && o.carrying) {
      SDL_FRect d = cam_.toScreen(wx - 2, wy - 16, 4, 4);
      SDL_SetRenderDrawColor(r_.sdl(), 255, 220, 80, 255);
      SDL_RenderFillRectF(r_.sdl(), &d);
    }
  }
}

void WorldView::draw() {
  drawSky();
  for (auto& isl : islands_) drawIsland(isl);
  drawObjects();
  if (dropIndex_ < 0) drawBuildUi();
  drawHud();
  char t[256];
  std::snprintf(t, sizeof t,
                "NetStorm Port - %s  zoom %.2fx  [M/[ ] missions, N test layout, wheel zoom, drag pan, G grid, "
                "Tab sprites]",
                sceneName_.c_str(), cam_.zoom);
  SDL_SetWindowTitle(r_.window(), t);
}

}  // namespace ns
