#include "app/WorldView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <cstdio>
#include <cstdlib>

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
  hasMission_ = false;
  forcedOutcome_ = 0;
  resultText_.clear();
  dialogs_.clear();
  abortRequested = restartRequested = false;
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
  ais_.clear();
  std::array<bool, 9> present{};
  for (auto& e : sim_->entities())
    if (e.owner > 0 && e.owner <= 8) present[size_t(e.owner)] = true;
  for (int p = 1; p <= 8; ++p)
    if (present[size_t(p)]) ais_.emplace_back(p, uint32_t(1000 + p * 77 + seed_));
  for (int p = 0; p <= 8; ++p) sim_->player(p).power = startPower;
  // Skirmish rules: start with the common Knowledge, earn the rest by sacrifice.
  sim_->knowledgeGifts = true;
  for (int p = 1; p <= 8; ++p) sim_->player(p).tech = sim_->starterKnowledge();
  const TypeInfo* golem = gd_.registry.byName("sunWalker");
  if (!golem) return;
  std::vector<std::pair<int, int>> temples;  // id, owner
  for (auto& e : sim_->entities())
    if (e.role == Role::Temple && e.owner > 0) temples.push_back({e.id, e.owner});
  for (auto [id, owner] : temples)
    for (int k = 0; k < 2; ++k) sim_->spawnWalkerNear(golem, owner, id);
}
void WorldView::buildTestScene(uint32_t seed) {
  hasMission_ = false;
  forcedOutcome_ = 0;
  resultText_.clear();
  dialogs_.clear();
  // One world grid with four blob islands stamped in (same layout as before).
  islands_.clear();
  Island world(100, 76);
  const Element els[4] = {Element::Sun, Element::Wind, Element::Rain, Element::Thunder};
  const int pos[4][2] = {{4, 6}, {44, 2}, {8, 40}, {48, 38}};
  const int owners[4] = {1, 0, 0, 2};
  for (int i = 0; i < 4; ++i) {
    Island blob = makeBlobIsland(30 + int(seed % 3) * 2, 24, els[i], seed * 31 + i * 7);
    for (int y = 0; y < blob.height(); ++y)
      for (int x = 0; x < blob.width(); ++x)
        if (blob.filled(x, y)) {
          world.set(pos[i][0] + x, pos[i][1] + y, blob.at(x, y));
          world.setOwner(pos[i][0] + x, pos[i][1] + y, owners[i]);
        }
  }
  world.markAllPermanent();
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
  add("sunBattery", 3, 12, 4);
  add("rainVortex", 3, 14, 9);
  add("priest", 3, 10, 14);
  startSim(3000);
  frame(0, 0, float(world.width() * kCellW), float(world.height() * kCellH + 40));
}
void WorldView::handle(const SDL_Event& e) {
  // Chat (LAN battles): Enter opens the line, Enter sends, Esc cancels.
  if (chatting_) {
    if (e.type == SDL_TEXTINPUT) {
      for (const char* c = e.text.text; *c; ++c)
        if (chatInput_.size() < 100 && *c >= 32 && *c < 127) chatInput_ += *c;
      return;
    }
    if (e.type == SDL_KEYDOWN) {
      if (e.key.keysym.sym == SDLK_BACKSPACE && !chatInput_.empty()) chatInput_.pop_back();
      if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER) {
        if (netSay && !chatInput_.empty()) netSay(chatInput_);
        chatInput_.clear();
        chatting_ = false;
        SDL_StopTextInput();
      }
      if (e.key.keysym.sym == SDLK_ESCAPE) {
        chatInput_.clear();
        chatting_ = false;
        SDL_StopTextInput();
      }
      return;
    }
  } else if (netSay && e.type == SDL_KEYDOWN &&
             (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_KP_ENTER)) {
    chatting_ = true;
    chatInput_.clear();
    SDL_StartTextInput();
    return;
  }
  if (handleDialog(e)) return;
  if (handleGift(e)) return;
  switch (e.type) {
    case SDL_QUIT: quit_ = true; break;
    case SDL_KEYDOWN:
      switch (e.key.keysym.sym) {
        case SDLK_ESCAPE:
          if (selectedWorkshop_ >= 0 || dropIndex_ >= 0 || selected_ >= 0 || !constructType_.empty()) {
            selectedWorkshop_ = dropIndex_ = selected_ = -1;
            constructType_.clear();
          } else {
            menuRequested = true;
          }
          break;
        case SDLK_g: showGrid_ = !showGrid_; break;
        case SDLK_r: issue({Command::Rotate}); break;
        case SDLK_p:
          if (!lockstep()) paused = !paused;
          break;
        case SDLK_EQUALS:
        case SDLK_KP_PLUS: speed = std::min(4.0f, speed * 2); break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: speed = std::max(0.25f, speed / 2); break;
        case SDLK_b: autoBuild(1); break;  // let the AI place one piece
        case SDLK_i:
          aiForMe_ = !aiForMe_;
          message_ = aiForMe_ ? "AI IS PLAYING FOR YOU" : "YOU HAVE CONTROL";
          messageTime_ = time_;
          break;
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
        issue({Command::Rotate});
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
        const Entity* clicked = entityAtScreen(sx, sy);
        if (handleUiClick(sx, sy)) {
          // HUD button
        } else if (!constructType_.empty() && sim_) {
          int ax, ay;
          anchorForCursor(gd_.registry.byName(constructType_), sx, sy, ax, ay);
          std::string why;
          if (issue({Command::Construct, 0, selected_, ax, ay, constructType_}, &why)) constructType_.clear();
          else {
            for (auto& c : why) c = char(std::toupper(static_cast<unsigned char>(c)));
            message_ = why, messageTime_ = time_;
          }
        } else if (clicked && clicked->owner == player_ &&
                   (clicked->role == Role::Priest || clicked->role == Role::Golem)) {
          selected_ = clicked->id;
          selectedWorkshop_ = -1;
        } else if (clicked && clicked->role == Role::Priest && clicked->owner != player_ && sim_ &&
                   sim_->find(selected_) && sim_->find(selected_)->role == Role::Golem) {
          std::string why;
          if (!issue({Command::Capture, 0, selected_, clicked->id}, &why)) {
            for (auto& c : why) c = char(std::toupper(static_cast<unsigned char>(c)));
            message_ = why, messageTime_ = time_;
          } else {
            message_ = "GOLEM SENT TO CAPTURE THE PRIEST", messageTime_ = time_;
          }
        } else if (dropIndex_ >= 0 && sim_) {
          const auto& ready = sim_->player(player_).ready;
          if (dropIndex_ < int(ready.size())) {
            const TypeInfo* t = gd_.registry.byName(ready[size_t(dropIndex_)]);
            int fx = std::max(1, int(t->def->num("foot_x", 1))), fy = std::max(1, int(t->def->num("foot_y", 1)));
            float wx, wy;
            cam_.screenToWorld(sx, sy, wx, wy);
            int ax = int(std::floor(wx / kCellW)) + fx / 2, ay = int(std::floor(wy / kCellH)) + fy / 2;
            if (issue({Command::Drop, 0, dropIndex_, ax, ay})) dropIndex_ = -1;
            else message_ = "CANT DROP THERE - NEEDS OPEN SKY NEXT TO YOUR LAND", messageTime_ = time_;
          } else {
            dropIndex_ = -1;
          }
        } else if (clicked && clicked->role == Role::Workshop && clicked->owner == player_) {
          selectedWorkshop_ = clicked->id;
          selected_ = -1;
        } else if (sim_ && sim_->find(selected_) && !clicked) {
          float wx, wy;
          cam_.screenToWorld(sx, sy, wx, wy);
          int cx = int(std::floor(wx / kCellW)), cy = int(std::floor(wy / kCellH));
          if (sim_->walkableAt(cx, cy)) {
            if (!issue({Command::Move, 0, selected_, cx, cy})) message_ = "CANT GET THERE", messageTime_ = time_;
          } else {
            selected_ = -1;  // clicked sky: deselect
          }
        } else if (hoverX_ >= 0 && canPlaceBridge(islands_[0], myQueue().current(), hoverX_, hoverY_, player_)) {
          issue({Command::Bridge, 0, hoverX_, hoverY_});
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
  if (sim_ && !paused && dialogs_.empty() && forcedOutcome_ == 0) {
    simAcc_ += std::min(dt, 0.25) * speed;
    if (lockstep()) speed = 1;  // everyone runs at the same pace
    // Rejoining after a drop: race through the backlog of turns (up to 30 s of game a frame).
    if (netBacklog && netBacklog() > 2)
      simAcc_ = std::max(simAcc_, double(std::min<size_t>(netBacklog() * 4, 600)) * Sim::kDt);
    while (simAcc_ >= Sim::kDt) {
      if (stopAtTick && sim_->tick() >= stopAtTick) {
        simAcc_ = 0;
        break;
      }
      if (netTick) {
        std::vector<Command> cmds;
        if (!netTick(sim_->tick(), cmds)) {  // waiting for other players' orders
          simAcc_ = std::min(simAcc_, double(Sim::kDt));
          break;
        }
        for (auto& c : cmds) sim_->apply(c);
      }
      if (autopilot && float(sim_->time()) >= nextAutopilot_) {
        nextAutopilot_ = float(sim_->time()) + 2.0f;
        autopilotStep();
      }
      sim_->step();
      for (auto& ai : ais_)
        // The local "AI plays for me" acts directly on the sim, so it's off in
        // lockstep games (it would desync); the autopilot issues commands instead.
        if (ai.player() != player_ || (aiForMe_ && !lockstep())) ai.step(*sim_, islands_[0]);
      if (afterTick) afterTick(sim_->tick());
      simAcc_ -= Sim::kDt;
      if (hasMission_) {
        for (auto& ev : script_.step(*sim_)) {
          if (ev.outcome != 0) {
            forcedOutcome_ = ev.outcome;
            resultText_ = ev.section->text;
          } else {
            openSection(ev.section);
          }
        }
        if (forcedOutcome_ || !dialogs_.empty()) {
          simAcc_ = 0;
          break;
        }
      }
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
      SDL_SetTextureColorMod(st.tex, 96, 102, 118);  // blue-grey, measured off original footage
      SDL_RenderCopyF(ren, st.tex, nullptr, &d);
      SDL_SetTextureColorMod(st.tex, 255, 255, 255);
    }
}

// Player colours for owned-island rims. Missions can name colours per player.
SDL_Color WorldView::colourOf(int owner) const {
  static const SDL_Color kDefault[9] = {{0, 0, 0, 0},       {235, 45, 35, 255}, {45, 120, 255, 255},
                                        {250, 215, 45, 255}, {45, 205, 75, 255}, {205, 65, 225, 255},
                                        {45, 225, 225, 255}, {250, 145, 35, 255}, {240, 240, 240, 255}};
  owner = (owner >= 0 && owner <= 8) ? owner : 0;
  return colours_[size_t(owner)].a ? colours_[size_t(owner)] : kDefault[owner];
}

static SDL_Color namedColour(const std::string& n) {
  if (n == "red") return {235, 45, 35, 255};
  if (n == "blue") return {45, 120, 255, 255};
  if (n == "yellow") return {250, 215, 45, 255};
  if (n == "green") return {45, 205, 75, 255};
  if (n == "purple") return {170, 70, 230, 255};
  if (n == "orange") return {250, 145, 35, 255};
  if (n == "white") return {240, 240, 240, 255};
  if (n == "cyan" || n == "teal") return {45, 225, 225, 255};
  return {0, 0, 0, 0};
}

bool WorldView::startMission(const MissionInfo& info) {
  if (!loadMissionByName(info.fort + ".fort")) return false;
  sceneName_ = info.title;
  missionTitle_ = info.title;
  sim_->knowledgeGifts = false;  // campaign: each mission sets its own Knowledge
  player_ = 1;
  colours_ = {};
  colours_[1] = {45, 120, 255, 255};  // you are blue
  for (int p = 1; p <= 8; ++p) {
    const MissionPlayer& mp = info.players[size_t(p)];
    PlayerState& ps = sim_->player(p);
    names_[size_t(p)] = mp.present ? mp.name : "";
    if (!mp.present) continue;
    ps.power = mp.startMoney;
    ps.golemTarget = std::clamp(mp.collectors, 2, 6);
    ps.tech.clear();
    for (auto t : mp.tech) {
      std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
      ps.tech.push_back(t);
    }
    ps.allies = 0;
    for (int a : mp.allies)
      if (a >= 1 && a <= 8) ps.allies |= uint16_t(1 << a);
    if (p != 1 && !mp.colour.empty()) colours_[size_t(p)] = namedColour(mp.colour);
    if (p != 1 && colours_[size_t(p)].a == 0) colours_[size_t(p)] = {235, 45, 35, 255};
  }
  // Alliances are mutual.
  for (int a = 1; a <= 8; ++a)
    for (int b = 1; b <= 8; ++b)
      if ((sim_->player(a).allies >> b) & 1) sim_->player(b).allies |= uint16_t(1 << a);
  // AIs for every present opponent/ally; extra golems per "collectors".
  ais_.clear();
  ais_.emplace_back(player_, uint32_t(4242));  // only runs when "AI plays for me" is on
  const TypeInfo* golem = gd_.registry.byName("sunWalker");
  for (int p = 2; p <= 8; ++p) {
    const MissionPlayer& mp = info.players[size_t(p)];
    bool hasStuff = false;
    for (auto& e : sim_->entities())
      if (e.owner == p) hasStuff = true;
    if (!mp.present || !hasStuff) continue;
    SkirmishAi ai(p, uint32_t(1000 + p * 77));
    ai.setMoveInterval(mp.timeBetweenMoves);
    ais_.push_back(ai);
    int have = 0, temple = -1;
    for (auto& e : sim_->entities()) {
      if (e.owner == p && e.role == Role::Golem) ++have;
      if (e.owner == p && e.role == Role::Temple) temple = e.id;
    }
    for (int k = have; k < std::min(mp.collectors, 6) && temple >= 0 && golem; ++k)
      sim_->spawnWalkerNear(golem, p, temple);
  }
  mission_ = info;
  hasMission_ = true;
  script_.reset(&mission_, *sim_, player_);
  return true;
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
      if (own > 0 && own != player_ && tc.cluster != "AA") {
        // As in the original: your land keeps the plain rim, everyone else's
        // gets a dark shade of their colour.
        SDL_Color c = colourOf(own);
        c = {Uint8(c.r * 0.6f), Uint8(c.g * 0.6f), Uint8(c.b * 0.6f), 255};
        blit(cache_.tinted(isle_->set, isle_->frame(line), c, rimMask_), ax, ay);
      }
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
    if (!sim_) break;
    BridgeQueue probe = sim_->bridgeQueue(player_);  // a copy: the search may rotate it
    if (!bestBridgeMove(islands_[0], probe, player_, x, y)) break;
    // Match the rotation the search settled on, then place.
    BridgeShape want = probe.current();
    for (int r = 0; r < 4 && !(myQueue().current().cells == want.cells); ++r) issue({Command::Rotate});
    if (!issue({Command::Bridge, 0, x, y})) break;
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
  for (auto [x, y] : myQueue().current().cells) mw = std::max(mw, x), mh = std::max(mh, y);
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
    bool ok = canPlaceBridge(isl, myQueue().current(), hoverX_, hoverY_, player_);
    int line = bridgeType_->findLine("A", 1);
    const SpriteTex& st = cache_.get(bridgeType_->set, bridgeType_->frame(line));
    if (st.tex) {
      SDL_SetTextureAlphaMod(st.tex, 170);
      SDL_SetTextureColorMod(st.tex, ok ? 150 : 255, ok ? 255 : 90, ok ? 150 : 90);
      for (auto [dx, dy] : myQueue().current().cells)
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
    const BridgeShape& s = myQueue().upcoming(i);
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
      if (!issue({Command::Build, 0, selectedWorkshop_, 0, 0, b.action.substr(6)}, &why)) {
        for (auto& c : why) c = char(std::toupper(static_cast<unsigned char>(c)));
        message_ = why;
        messageTime_ = time_;
      }
    } else if (b.action == "golem" && sim_) {
      std::string why;
      if (!issue({Command::Golem}, &why)) {
        for (auto& c : why) c = char(std::toupper(static_cast<unsigned char>(c)));
        message_ = why;
      } else {
        message_ = "A GOLEM RISES FROM THE TEMPLE";
      }
      messageTime_ = time_;
    } else if (b.action == "upgrade" && sim_) {
      std::string why;
      if (!issue({Command::Upgrade, 0, selectedWorkshop_}, &why)) {
        for (auto& c : why) c = char(std::toupper(static_cast<unsigned char>(c)));
        message_ = why;
      } else {
        message_ = "WORKSHOP UPGRADED";
      }
      messageTime_ = time_;
    } else if (b.action.rfind("drop:", 0) == 0) {
      dropIndex_ = std::atoi(b.action.c_str() + 5);
      selectedWorkshop_ = -1;
    } else if (b.action.rfind("construct:", 0) == 0) {
      constructType_ = b.action.substr(10);
      dropIndex_ = -1;
    } else if (b.action == "close") {
      selectedWorkshop_ = -1;
      selected_ = -1;
      constructType_.clear();
    }
    return true;
  }
  return false;
}

void WorldView::anchorForCursor(const TypeInfo* t, int sx, int sy, int& ax, int& ay) const {
  float wx, wy;
  cam_.screenToWorld(sx, sy, wx, wy);
  int fx = t && t->def ? std::max(1, int(t->def->num("foot_x", 1))) : 1;
  int fy = t && t->def ? std::max(1, int(t->def->num("foot_y", 1))) : 1;
  ax = int(std::floor(wx / kCellW)) + fx / 2;
  ay = int(std::floor(wy / kCellH)) + fy / 2;
}

void WorldView::drawHud() {
  if (!sim_) return;
  SDL_Renderer* ren = r_.sdl();
  buttons_.clear();
  auto panel = [&](SDL_FRect r) {
    SDL_SetRenderDrawColor(ren, 18, 18, 28, 255);
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
  {  // Make a golem at the temple
    bool temple = false;
    for (auto& e : sim_->entities()) temple |= e.alive && e.owner == player_ && e.role == Role::Temple;
    if (temple) {
      SDL_FRect gb{490, 12, 170, 32};
      bool can = me.power >= Sim::kGolemCost;
      panel(gb);
      drawText(ren, gb.x + 10, 21, 2, "+ GOLEM " + std::to_string(Sim::kGolemCost), can ? gold : grey);
      buttons_.push_back({gb, "golem"});
    }
  }

  // Mission title and the other players, in their colours (top right).
  if (!missionTitle_.empty()) {
    int sw, sh;
    r_.outputSize(sw, sh);
    std::string t = missionTitle_;
    for (auto& c : t) c = char(std::toupper(static_cast<unsigned char>(c)));
    int rows = 1;
    for (int p = 1; p <= 8; ++p) rows += !names_[size_t(p)].empty();
    float w = 360;
    panel({float(sw) - w - 12, 12, w, 14.0f + rows * 18.0f});
    drawText(ren, float(sw) - w, 20, 2, t, gold);
    float yy = 40;
    for (int p = 1; p <= 8; ++p) {
      if (names_[size_t(p)].empty()) continue;
      std::string n = names_[size_t(p)];
      for (auto& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
      bool out = sim_->outcome(p) < 0;
      std::string tag = p != player_ && sim_->allied(player_, p) ? " (ALLY)" : "";
      drawText(ren, float(sw) - w, yy, 2, n + tag + (out ? " - OUT" : ""), out ? grey : colourOf(p));
      yy += 18;
    }
  }

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
    bool slotsOn = sim_->knowledgeGifts;
    float ph = 60 + 30.0f * float(list.size()) + 30 + (slotsOn ? 60 : 0);
    panel({px, py, pw, ph});
    drawText(ren, px + 10, py + 10, 2, w->type->name + (slotsOn ? " LV " + std::to_string(w->workshopLevel) : ""), gold);
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
      bool committed = std::find(w->slots.begin(), w->slots.end(), t->name) != w->slots.end();
      drawText(ren, r.x + 6, y + 6, 2, (slotsOn ? (committed ? "* " : "+ ") : "") + t->name, affordable ? white : grey);
      std::string c = std::to_string(cost);
      drawText(ren, r.x + r.w - 6 - float(c.size()) * 12, y + 6, 2, c, me.power >= cost ? gold : red);
      buttons_.push_back({r, "build:" + t->name});
      y += 30;
    }
    if (slotsOn) {  // production slots + upgrade
      std::string slotsTxt = "SLOTS " + std::to_string(w->slots.size()) + "/" + std::to_string(sim_->slotCapacity(*w)) +
                             "   (* COMMITTED, + USES A SLOT)";
      drawText(ren, px + 10, y + 4, 2, slotsTxt, grey);
      y += 24;
      SDL_FRect r{px + 8, y, pw - 16, 26};
      SDL_SetRenderDrawColor(ren, 55, 45, 30, 255);
      SDL_RenderFillRectF(ren, &r);
      if (w->workshopLevel < 4) {
        int cost = sim_->upgradeCost(*w);
        drawText(ren, r.x + 6, y + 6, 2, "UPGRADE (+1 SLOT)", me.power >= cost ? gold : grey);
        std::string c = std::to_string(cost);
        drawText(ren, r.x + r.w - 6 - float(c.size()) * 12, y + 6, 2, c, me.power >= cost ? gold : red);
        buttons_.push_back({r, "upgrade"});
      } else {
        drawText(ren, r.x + 6, y + 6, 2, "FULLY UPGRADED", grey);
      }
    }
  }

  // Priest panel: construction options.
  if (Entity* pr = sim_->find(selected_); pr && pr->role == Role::Priest && selectedWorkshop_ < 0) {
    auto list = sim_->constructable();
    float pw = 380, px = float(cam_.screenW) - pw - 12, py = 12;
    panel({px, py, pw, 64 + 30.0f * float(list.size())});
    drawText(ren, px + 10, py + 10, 2, pr->immobile ? "HIGH PRIEST - IMMOBILISED" : "HIGH PRIEST - BUILD", gold);
    SDL_FRect close{px + pw - 30, py + 6, 22, 22};
    drawText(ren, close.x + 5, close.y + 4, 2, "X", red);
    buttons_.push_back({close, "close"});
    drawText(ren, px + 10, py + 34, 2, "CLICK LAND TO WALK", grey);
    float y = py + 60;
    for (auto* t : list) {
      SDL_FRect r{px + 8, y, pw - 16, 26};
      int cost = int(t->def->num("cost", 0));
      SDL_SetRenderDrawColor(ren, constructType_ == t->name ? 70 : 40, 40, 55, 255);
      SDL_RenderFillRectF(ren, &r);
      drawText(ren, r.x + 6, y + 6, 2, t->name, me.power >= cost ? white : grey);
      std::string c = std::to_string(cost);
      drawText(ren, r.x + r.w - 6 - float(c.size()) * 12, y + 6, 2, c, me.power >= cost ? gold : red);
      buttons_.push_back({r, "construct:" + t->name});
      y += 30;
    }
  } else if (Entity* g = sim_->find(selected_); g && g->role == Role::Golem) {
    float pw = 380, px = float(cam_.screenW) - pw - 12;
    panel({px, 12, pw, 60});
    drawText(ren, px + 10, 22, 2, "GOLEM", gold);
    drawText(ren, px + 10, 44, 2, "CLICK A DOWNED ENEMY PRIEST", grey);
  }
  // Construction ghost.
  if (!constructType_.empty()) {
    const TypeInfo* t = gd_.registry.byName(constructType_);
    int mx, my, ax, ay;
    SDL_GetMouseState(&mx, &my);
    anchorForCursor(t, mx, my, ax, ay);
    int fx = std::max(1, int(t->def->num("foot_x", 1))), fy = std::max(1, int(t->def->num("foot_y", 1)));
    bool ok = sim_->canConstruct(selected_, t, ax, ay);
    SDL_FRect fp = cam_.toScreen(float((ax - fx + 1) * kCellW), float((ay - fy + 1) * kCellH), float(fx * kCellW),
                                 float(fy * kCellH));
    SDL_SetRenderDrawColor(ren, ok ? 80 : 255, ok ? 255 : 70, 80, 90);
    SDL_RenderFillRectF(ren, &fp);
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

void WorldView::drawCombat() {
  if (!sim_) return;
  SDL_Renderer* ren = r_.sdl();
  for (auto& p : sim_->projectiles()) {
    float t = std::min(1.0f, p.t / p.dur);
    float cx = p.x0 + (p.x1 - p.x0) * t, cy = p.y0 + (p.y1 - p.y0) * t;
    float lift = p.arc ? std::sin(t * 3.14159f) * 4.0f : 0.6f;  // cells
    float wx = cx * kCellW, wy = (cy - lift) * kCellH;
    bool drawn = false;
    if (p.missile && p.missile->set >= 0 && p.missile->imageFrames > 0) {
      // Orientation frames: pick by flight angle.
      int n = std::max(1, p.missile->imageFrames);
      float ang = std::atan2(p.y1 - p.y0, p.x1 - p.x0);
      int f = int(std::lround((ang + 3.14159f) / (2 * 3.14159f) * n)) % n;
      const SpriteTex& st = cache_.get(p.missile->set, f);
      if (st.tex && st.w < 60) {
        blit(st, wx, wy);
        drawn = true;
      }
    }
    if (!drawn) {
      SDL_Color c = p.theme == "sun" ? SDL_Color{255, 230, 120, 255}
                    : p.theme == "rain" ? SDL_Color{140, 200, 255, 255}
                    : p.theme == "wind" ? SDL_Color{220, 255, 200, 255}
                                        : SDL_Color{255, 140, 255, 255};
      SDL_FRect d = cam_.toScreen(wx - 2, wy - 2, 4, 4);
      SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
      SDL_RenderFillRectF(ren, &d);
    }
  }
  for (auto& fx : sim_->effects()) {
    if (!fx.type || fx.type->set < 0) continue;
    // Animation lines, skipping help/info pictures.
    std::vector<int> lines;
    if (fx.type->def)
      for (size_t i = 0; i < fx.type->def->frames.size() && int(i) < fx.type->imageFrames; ++i) {
        const auto& l = fx.type->def->frames[i].label;
        if (l.find("help") == std::string::npos && l.find("info") == std::string::npos) lines.push_back(int(i));
      }
    if (lines.empty()) continue;
    int f = lines[size_t(std::min(int(lines.size()) - 1, int(fx.t / fx.life * float(lines.size()))))];
    blit(cache_.get(fx.type->set, f), fx.x * kCellW, fx.y * kCellH);
  }
  if (sim_->winner()) {
    std::string msg = sim_->winner() == player_ ? "VICTORY" : "PLAYER " + std::to_string(sim_->winner()) + " WINS";
    if (sim_->eliminated(player_)) msg = "YOUR PRIEST WAS SACRIFICED";
    float w = float(msg.size()) * 30;
    drawText(ren, (cam_.screenW - w) / 2, cam_.screenH * 0.4f, 5, msg, SDL_Color{255, 220, 110, 255});
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
    if (!o.alive || !o.type || !o.type->def || o.type->set < 0 || o.role == Role::Flyer) continue;
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
    if (o.air) wy -= 24 + float(std::sin(time_ * 2 + o.id)) * 2;  // balloons float above their shadow
    int base = baseFor(o);
    int anim = frameFor(o);
    auto drawFrame = [&](int line) {
      const SpriteTex& st = cache_.get(o.type->set, o.type->frame(line));
      if (o.flash > 0 && st.tex) SDL_SetTextureColorMod(st.tex, 255, 120, 110);
      if (o.built < 1 && st.tex) SDL_SetTextureAlphaMod(st.tex, Uint8(60 + 150 * o.built));
      blit(st, wx, wy);
      if (st.tex) SDL_SetTextureColorMod(st.tex, 255, 255, 255), SDL_SetTextureAlphaMod(st.tex, 255);
    };
    if (base >= 0) drawFrame(base);
    if (anim != base) drawFrame(anim);
    SDL_Renderer* rr = r_.sdl();
    if (o.id == selected_) {  // selection box
      SDL_FRect d = cam_.toScreen(wx - 9, wy - 26, 18, 28);
      SDL_SetRenderDrawColor(rr, 255, 230, 120, 255);
      SDL_RenderDrawRectF(rr, &d);
    }
    if (o.role == Role::Priest && o.immobile) {  // force field
      float pulse = 0.5f + 0.5f * float(std::sin(time_ * 6));
      SDL_FRect d = cam_.toScreen(wx - 10, wy - 24, 20, 26);
      SDL_SetRenderDrawColor(rr, 120, 200, 255, Uint8(90 + 120 * pulse));
      SDL_RenderDrawRectF(rr, &d);
    }
    if (o.built < 1) {  // under construction
      SDL_FRect bg = cam_.toScreen(wx - 40, wy + 4, 40, 3), fg = cam_.toScreen(wx - 40, wy + 4, 40 * o.built, 3);
      SDL_SetRenderDrawColor(rr, 20, 20, 30, 220);
      SDL_RenderFillRectF(rr, &bg);
      SDL_SetRenderDrawColor(rr, 120, 200, 255, 255);
      SDL_RenderFillRectF(rr, &fg);
    }
    if (o.role == Role::Altar && o.captive >= 0) {  // rune progress
      for (int k = 0; k < 5; ++k) {
        SDL_FRect d = cam_.toScreen(wx - 70 + k * 10.0f, wy + 8, 8, 4);
        bool lit = o.runes >= k + 1;
        SDL_SetRenderDrawColor(rr, lit ? 255 : 60, lit ? 200 : 60, lit ? 80 : 70, 255);
        SDL_RenderFillRectF(rr, &d);
      }
    }
    // Health bar when damaged.
    if (o.hp < o.maxHp && o.maxHp > 0 && o.built >= 1) {
      float w = 20, f = std::max(0.0f, float(o.hp) / float(o.maxHp));
      SDL_FRect bg = cam_.toScreen(wx - 16, wy + 2, w, 2), fg = cam_.toScreen(wx - 16, wy + 2, w * f, 2);
      SDL_SetRenderDrawColor(r_.sdl(), 30, 0, 0, 220);
      SDL_RenderFillRectF(r_.sdl(), &bg);
      SDL_SetRenderDrawColor(r_.sdl(), f > 0.5f ? 80 : 255, f > 0.5f ? 220 : 160, 60, 255);
      SDL_RenderFillRectF(r_.sdl(), &fg);
    }
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
  drawFlyers();
  drawCombat();
  if (dropIndex_ < 0) drawBuildUi();
  drawHud();
  if (!netStatus.empty()) {
    int sw, sh;
    r_.outputSize(sw, sh);
    std::string t = netStatus;
    for (auto& c : t) c = char(std::toupper(static_cast<unsigned char>(c)));
    float w = float(t.size()) * 12 + 30;
    SDL_FRect box{(sw - w) / 2, 56, w, 34};
    SDL_SetRenderDrawColor(r_.sdl(), 20, 18, 30, 220);
    SDL_RenderFillRectF(r_.sdl(), &box);
    drawText(r_.sdl(), box.x + 15, box.y + 10, 2, t, {255, 210, 110, 255});
  }
  drawGift();
  drawChat();
  drawDialog();
  char t[256];
  std::snprintf(t, sizeof t,
                "NetStorm Port - %s  zoom %.2fx  [M/[ ] missions, N test layout, wheel zoom, drag pan, G grid, "
                "Tab sprites]",
                sceneName_.c_str(), cam_.zoom);
  SDL_SetWindowTitle(r_.window(), t);
}


// ---- Scripted notes (the Marquis' taunts etc.) --------------------------

// The section's buttons, plus "Ignore" when none of them just closes the note
// (the original's note window had a close box).
static std::vector<ScriptButton> noteButtons(const ScriptSection& sec) {
  std::vector<ScriptButton> b = sec.buttons;
  bool harmless = false;
  for (auto& x : b) harmless |= x.action == "donothing" || x.action == "showtechnology";
  if (!harmless) b.push_back({"Ignore", "donothing", ""});
  return b;
}
static int safeChoice(const std::vector<ScriptButton>& b) {
  for (size_t i = 0; i < b.size(); ++i)
    if (b[i].action == "donothing") return int(i);
  return 0;
}

void WorldView::openSection(const ScriptSection* sec) {
  if (sec) dialogs_.push_back(sec);
}

bool WorldView::handleDialog(const SDL_Event& e) {
  if (dialogs_.empty()) return false;
  if (e.type == SDL_MOUSEMOTION) {
    dialogHover_ = -1;
    for (size_t i = 0; i < dialogButtons_.size(); ++i) {
      auto& r = dialogButtons_[i];
      if (e.motion.x >= r.x && e.motion.x <= r.x + r.w && e.motion.y >= r.y && e.motion.y <= r.y + r.h)
        dialogHover_ = int(i);
    }
    return false;  // still allow hover/camera updates
  }
  int pick = -1;
  if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
    for (size_t i = 0; i < dialogButtons_.size(); ++i) {
      auto& r = dialogButtons_[i];
      if (e.button.x >= r.x && e.button.x <= r.x + r.w && e.button.y >= r.y && e.button.y <= r.y + r.h)
        pick = int(i);
    }
  } else if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_SPACE ||
                                       e.key.keysym.sym == SDLK_ESCAPE)) {
    pick = safeChoice(noteButtons(*dialogs_.front()));
  } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_9) {
    pick = e.key.keysym.sym - SDLK_1;
  } else if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
    pick = dialogHover_ >= 0 ? dialogHover_ : safeChoice(noteButtons(*dialogs_.front()));
  }
  if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP || e.type == SDL_KEYDOWN) {
    if (pick < 0) return true;  // modal: swallow other clicks/keys
  } else if (pick < 0) {
    return false;
  }
  const ScriptSection* sec = dialogs_.front();
  auto btns = noteButtons(*sec);
  if (pick >= int(btns.size())) return true;
  dialogs_.erase(dialogs_.begin());
  dialogHover_ = -1;
  const ScriptButton b = btns[size_t(pick)];
  if (b.action == "tell") {
    if (const ScriptSection* t = mission_.section(b.arg)) dialogs_.insert(dialogs_.begin(), t);
  } else if (b.action == "leavebattle") {
    forcedOutcome_ = -1;  // e.g. "Depart in Disgrace" after surrendering
  } else if (b.action == "missionabort") {
    abortRequested = true;
  } else if (b.action == "missionrestart") {
    restartRequested = true;
  }
  // donothing, showtechnology, missionbegin (only on result screens): just close.
  return true;
}

void WorldView::drawDialog() {
  dialogButtons_.clear();
  if (dialogs_.empty()) return;
  SDL_Renderer* ren = r_.sdl();
  int sw, sh;
  r_.outputSize(sw, sh);
  const ScriptSection* sec = dialogs_.front();
  float w = std::min(900.0f, sw - 60.0f), scale = 2.5f;
  float textH = drawWrapped(ren, 0, 0, w - 60, scale, sec->text, {}, 1e9f, true);
  auto btns = noteButtons(*sec);
  size_t nb = btns.size();
  float bh = 44, h = textH + 60 + float(nb) * (bh + 8);
  float x = (sw - w) / 2, y = std::max(20.0f, (sh - h) / 2);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 110);
  SDL_FRect all{0, 0, float(sw), float(sh)};
  SDL_RenderFillRectF(ren, &all);
  SDL_FRect box{x, y, w, h};
  SDL_SetRenderDrawColor(ren, 24, 22, 36, 240);
  SDL_RenderFillRectF(ren, &box);
  SDL_SetRenderDrawColor(ren, 190, 160, 100, 255);
  SDL_RenderDrawRectF(ren, &box);
  drawWrapped(ren, x + 30, y + 26, w - 60, scale, sec->text, {235, 235, 235, 255}, float(sh));
  float by = y + textH + 40;
  for (size_t i = 0; i < nb; ++i) {
    std::string label = btns[i].label;
    for (auto& c : label) c = char(std::toupper(static_cast<unsigned char>(c)));
    SDL_FRect r{x + 30, by, w - 60, bh};
    bool hot = int(i) == dialogHover_;
    SDL_SetRenderDrawColor(ren, hot ? 60 : 34, hot ? 50 : 32, hot ? 30 : 48, 255);
    SDL_RenderFillRectF(ren, &r);
    SDL_SetRenderDrawColor(ren, 190, 160, 100, 255);
    SDL_RenderDrawRectF(ren, &r);
    std::string t = std::to_string(i + 1) + ". " + label;
    drawText(ren, r.x + 16, r.y + (bh - 21) / 2, 3, t, hot ? SDL_Color{255, 220, 110, 255} : SDL_Color{235, 235, 235, 255});
    dialogButtons_.push_back(r);
    by += bh + 8;
  }
}

std::string WorldView::debugAi() {
  std::string out;
  if (!sim_ || islands_.empty()) return out;
  for (int p = 1; p <= 8; ++p) {
    int n = 0, ws = 0, temples = 0;
    std::string build;
    for (auto& e : sim_->entities()) {
      if (!e.alive || e.owner != p) continue;
      ++n;
      if (e.role == Role::Temple) ++temples;
      if (e.role != Role::Workshop) continue;
      ++ws;
      build += " [" + e.type->name + ":";
      for (auto* t : sim_->buildable(e.id)) build += " " + t->name;
      build += "]";
    }
    if (!n) continue;
    BridgeQueue q(99);
    int bx, by, moves = 0;
    uint16_t en = 0;
    for (int o = 1; o <= 8; ++o)
      if (o != p && !sim_->allied(o, p) && !sim_->eliminated(o)) en |= uint16_t(1u << o);
    int attack = 0;
    for (int i = 0; i < 6; ++i, q.next()) moves += bestBridgeMove(islands_[0], q, p, bx, by);
    std::vector<std::pair<int, int>> goals;
    for (auto& e : sim_->entities())
      if (e.alive && e.owner > 0 && e.owner != p && !sim_->allied(e.owner, p) && e.role != Role::Golem &&
          e.role != Role::Geyser)
        goals.push_back({int(e.x), int(e.y)});
    for (int i = 0; i < 6; ++i, q.next()) attack += bestBridgeMove(islands_[0], q, p, bx, by, 0, &goals, 10);
    out += "  p" + std::to_string(p) + " attackMoves/6=" + std::to_string(attack) + " enemyMask=" + std::to_string(en) + "\n";
    std::string tech;
    for (auto& t : sim_->player(p).tech) tech += t + ",";
    std::map<std::string, int> kinds;
    int powered = 0, shootersN = 0;
    for (auto& e : sim_->entities()) {
      if (!e.alive || e.owner != p || !e.type) continue;
      kinds[e.type->name + "/" + std::to_string(int(e.role))]++;
      if (e.role == Role::Shooter) {
        ++shootersN;
        int need = int(e.type->def->num("minUsage", 0));
        if (need <= 0 || sim_->energyAt(p, e.type->def->str("theme"), int(e.x), int(e.y)) >= need) ++powered;
      }
    }
    float gap = 1e9f, rng = 0;
    for (auto& e : sim_->entities()) {
      if (!e.alive || e.owner != p || e.role != Role::Shooter) continue;
      for (auto& t : sim_->entities()) {
        if (!t.alive || t.owner == 0 || t.owner == p || sim_->allied(p, t.owner) || t.role == Role::Geyser) continue;
        float d = std::sqrt((t.x - e.x) * (t.x - e.x) + (t.y - e.y) * (t.y - e.y));
        if (d < gap) gap = d, rng = float(e.type->def->num("range", 0));
      }
    }
    if (std::getenv("NS_DEBUG_SHOOTERS"))
      for (auto& e : sim_->entities()) {
        if (!e.alive || e.owner != p || e.role != Role::Shooter) continue;
        float g = 1e9f;
        for (auto& t : sim_->entities())
          if (t.alive && t.owner > 0 && t.owner != p && !sim_->allied(p, t.owner) && t.role != Role::Geyser)
            g = std::min(g, std::sqrt((t.x - e.x) * (t.x - e.x) + (t.y - e.y) * (t.y - e.y)));
        out += "    " + e.type->name + " range " + std::to_string(int(e.type->def->num("range", 0))) + " gap " +
               std::to_string(int(g)) + " energy " +
               std::to_string(sim_->energyAt(p, e.type->def->str("theme"), int(e.x), int(e.y))) + "\n";
      }
    out += "  p" + std::to_string(p) + " nearest enemy " + std::to_string(int(gap)) + " cells, that shooter's range " +
           std::to_string(int(rng)) + "\n";
    std::string kl;
    for (auto& [k, v] : kinds) kl += " " + k + "x" + std::to_string(v);
    out += "  p" + std::to_string(p) + " kinds:" + kl + " shooters=" + std::to_string(shootersN) +
           " powered=" + std::to_string(powered) + "\n";
    out += "p" + std::to_string(p) + ": ents=" + std::to_string(n) + " temples=" + std::to_string(temples) +
           " workshops=" + std::to_string(ws) + " bridgeMoves/6=" + std::to_string(moves) + " tech={" + tech + "}" +
           build + "\n";
  }
  return out;
}

// Air units: shadow on the ground, craft ~28px up, facing one of 8 directions.
void WorldView::drawFlyers() {
  if (!sim_) return;
  SDL_Renderer* rr = r_.sdl();
  for (auto& o : sim_->entities()) {
    if (!o.alive || o.role != Role::Flyer || !o.type || !o.type->def || o.type->set < 0) continue;
    const auto& frames = o.type->def->frames;
    std::vector<int> dirs;  // the "default" cluster: A00..A07
    std::string cl;
    for (size_t i = 0; i < frames.size() && i < size_t(o.type->imageFrames); ++i)
      if (frames[i].label.find("default") != std::string::npos) cl = frames[i].cluster;
    for (size_t i = 0; i < frames.size() && i < size_t(o.type->imageFrames); ++i)
      if (frames[i].cluster == cl && frames[i].label.find("help") == std::string::npos) dirs.push_back(int(i));
    if (dirs.empty()) continue;
    // Frame 0 faces north (screen up), then clockwise.
    float a = o.heading + 1.5707963f;
    int n = int(dirs.size());
    int idx = int(std::lround(a / (6.2831853f / float(n)))) % n;
    if (idx < 0) idx += n;
    int line = dirs[size_t(idx)];
    float wx = islands_[0].worldX + o.x * kCellW, wy = islands_[0].worldY + islands_[0].bob + o.y * kCellH;
    float bob = float(std::sin(time_ * 5 + o.id)) * 1.5f;
    int sf = o.type->shadowFrame(line);
    if (sf >= 0 && sf < gd_.shapes.frameCount(o.type->set)) {
      blit(cache_.shadow(o.type->set, sf), wx, wy);
    } else {
      SDL_FRect d = cam_.toScreen(wx - 5, wy - 2, 10, 4);
      SDL_SetRenderDrawColor(rr, 0, 0, 0, 90);
      SDL_RenderFillRectF(rr, &d);
    }
    const SpriteTex& st = cache_.get(o.type->set, o.type->frame(line));
    if (o.flash > 0 && st.tex) SDL_SetTextureColorMod(st.tex, 255, 120, 110);
    blit(st, wx, wy - 28 + bob);
    if (st.tex) SDL_SetTextureColorMod(st.tex, 255, 255, 255);
  }
}

int WorldView::flyersAirborne() const {
  int n = 0;
  if (sim_)
    for (auto& e : sim_->entities()) n += e.alive && e.role == Role::Flyer;
  return n;
}

void WorldView::focusOnAir() {
  if (!sim_) return;
  for (auto& e : sim_->entities())
    if (e.alive && e.role == Role::Flyer) {
      cam_.x = islands_[0].worldX + e.x * kCellW;
      cam_.y = islands_[0].worldY + e.y * kCellH - 10;
      cam_.zoom = 3.0f;
      return;
    }
}

// ---- Gift of the Furies: pick new Knowledge or upgrade the altar ----------

static std::string upperStr(std::string s) {
  for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

void WorldView::drawGift() {
  giftRects_.clear();
  giftPicks_.clear();
  if (!sim_ || aiForMe_ || sim_->player(player_).giftAltar < 0) return;
  SDL_Renderer* ren = r_.sdl();
  int sw, sh;
  r_.outputSize(sw, sh);
  auto opts = sim_->giftOptions(player_);
  std::sort(opts.begin(), opts.end(), [](const TypeInfo* a, const TypeInfo* b) {
    int la = int(a->def->num("level", 1)), lb = int(b->def->num("level", 1));
    return la != lb ? la > lb : a->def->num("cost", 0) > b->def->num("cost", 0);
  });
  struct Row {
    std::string label, pick;
    SDL_Color c;
  };
  std::vector<Row> rows;
  if (sim_->canUpgradeAltar(player_)) {
    int lvl = 1;
    for (auto& e : sim_->entities())
      if (e.id == sim_->player(player_).giftAltar) lvl = e.altarLevel;
    rows.push_back({"UPGRADE ALTAR TO LEVEL " + std::to_string(lvl + 1), "upgrade", {255, 220, 110, 255}});
  }
  for (auto* t : opts) {
    std::string name = t->def->str("description", t->name);
    std::string th = upperStr(t->def->str("theme"));
    rows.push_back({upperStr(name) + "  (" + th + ", LV " + std::to_string(int(t->def->num("level", 1))) + ", " +
                        std::to_string(int(t->def->num("cost", 0))) + ")",
                    t->name, {235, 235, 235, 255}});
  }
  float rh = 30, y = 96;
  size_t fit = size_t(std::max(1.0f, (float(sh) - y - 90) / (rh + 4)));
  int cols = rows.size() > fit ? 2 : 1;
  size_t perCol = (rows.size() + cols - 1) / cols;
  float colW = 560, w = colW * cols, x = float(sw) - w - 12;
  float h = 70 + perCol * (rh + 4);
  SDL_FRect box{x, y, w, h};
  SDL_SetRenderDrawColor(ren, 24, 22, 36, 235);
  SDL_RenderFillRectF(ren, &box);
  SDL_SetRenderDrawColor(ren, 255, 210, 90, 255);
  SDL_RenderDrawRectF(ren, &box);
  drawText(ren, x + 16, y + 14, 3, "GIFT OF THE FURIES", {255, 220, 110, 255});
  drawText(ren, x + 16, y + 42, 2, "CHOOSE NEW KNOWLEDGE (USES THE ALTAR)", {150, 150, 160, 255});
  int mx, my;
  SDL_GetMouseState(&mx, &my);
  for (size_t i = 0; i < rows.size(); ++i) {
    auto& r = rows[i];
    float ry = y + 64 + float(i % perCol) * (rh + 4);
    SDL_FRect rr{x + 12 + float(i / perCol) * colW, ry, colW - 24, rh};
    bool hot = mx >= rr.x && mx <= rr.x + rr.w && my >= rr.y && my <= rr.y + rr.h;
    SDL_SetRenderDrawColor(ren, hot ? 60 : 34, hot ? 50 : 32, hot ? 30 : 48, 255);
    SDL_RenderFillRectF(ren, &rr);
    drawText(ren, rr.x + 10, rr.y + 8, 2, r.label, r.c);
    giftRects_.push_back(rr);
    giftPicks_.push_back(r.pick);
  }
}

bool WorldView::handleGift(const SDL_Event& e) {
  if (giftRects_.empty() || e.type != SDL_MOUSEBUTTONUP || e.button.button != SDL_BUTTON_LEFT) return false;
  for (size_t i = 0; i < giftRects_.size(); ++i) {
    auto& r = giftRects_[i];
    if (e.button.x < r.x || e.button.x > r.x + r.w || e.button.y < r.y || e.button.y > r.y + r.h) continue;
    if (issue({Command::Gift, 0, 0, 0, 0, giftPicks_[i]})) {
      message_ = giftPicks_[i] == "upgrade" ? "YOUR ALTAR GROWS STRONGER" : "NEW KNOWLEDGE: " + upperStr(giftPicks_[i]);
      messageTime_ = time_;
    }
    return true;
  }
  return false;
}

void WorldView::debugGrantGift() {
  if (!sim_) return;
  sim_->knowledgeGifts = true;
  const TypeInfo* altar = gd_.registry.byName("altar");
  int temple = -1;
  for (auto& e : sim_->entities())
    if (e.alive && e.owner == player_ && e.role == Role::Temple) temple = e.id;
  Entity* t = sim_->find(temple);
  if (!altar || !t) return;
  int id = sim_->spawn(altar, player_, t->x + 8, t->y);
  sim_->find(id)->altarLevel = 2;
  sim_->player(player_).giftAltar = id;
}

int WorldView::balloonsAirborne() const {
  int n = 0;
  if (sim_)
    for (auto& e : sim_->entities()) n += e.alive && e.air;
  return n;
}

void WorldView::focusOnBalloon() {
  if (!sim_) return;
  for (auto& e : sim_->entities())
    if (e.alive && e.air && e.job != Entity::Job::Idle) {
      cam_.x = islands_[0].worldX + e.x * kCellW;
      cam_.y = islands_[0].worldY + e.y * kCellH - 10;
      cam_.zoom = 3.0f;
      return;
    }
}

bool WorldView::issue(Command c, std::string* why) {
  if (!sim_) return false;
  c.player = uint8_t(player_);
  if (netIssue) {  // lockstep: applied on every peer a few ticks from now
    netIssue(c);
    return true;
  }
  return sim_->apply(c, why);
}

void WorldView::startSkirmish(const SkirmishConfig& cfg, int local) {
  hasMission_ = false;
  forcedOutcome_ = 0;
  resultText_.clear();
  dialogs_.clear();
  islands_.clear();
  Island world(1, 1);
  std::vector<Placement> spawns;
  generateSkirmish(gd_, cfg, world, spawns);
  islands_.push_back(std::move(world));
  sim_ = std::make_unique<Sim>(gd_, islands_[0], cfg.seed);
  player_ = local;
  for (auto& pl : spawns) sim_->spawn(gd_.registry.byName(pl.type), pl.owner, pl.x, pl.y);
  // Rules, Knowledge, Storm Power, golems.
  sim_->knowledgeGifts = true;
  sim_->safetyGolem = true;
  for (int p = 1; p <= 8; ++p) {
    auto& ps = sim_->player(p);
    ps.power = float(cfg.startPower);
    ps.tech = sim_->starterKnowledge();
    ps.allies = 0;
    names_[size_t(p)] = cfg.slots[size_t(p - 1)].kind == SkirmishSlot::Closed ? ""
                        : !cfg.slots[size_t(p - 1)].name.empty() ? cfg.slots[size_t(p - 1)].name
                        : p == local ? "You"
                        : cfg.slots[size_t(p - 1)].kind == SkirmishSlot::AI ? "AI " + std::to_string(p)
                                                                           : "Player " + std::to_string(p);
  }
  for (int a = 1; a <= 8; ++a)
    for (int b = 1; b <= 8; ++b)
      if (a != b && cfg.slots[size_t(a - 1)].team && cfg.slots[size_t(a - 1)].team == cfg.slots[size_t(b - 1)].team)
        sim_->player(a).allies |= uint16_t(1u << b);
  const TypeInfo* golem = gd_.registry.byName("sunWalker");
  for (auto& e : std::vector<Entity>(sim_->entities()))
    if (e.role == Role::Temple && e.owner > 0 && golem)
      for (int k = 0; k < 2; ++k) sim_->spawnWalkerNear(golem, e.owner, e.id);
  // AIs only for AI slots (they run identically on every peer). The local human
  // gets one too, but it only acts while "AI plays for me" is on.
  ais_.clear();
  for (int p = 1; p <= 8; ++p) {
    bool ai = cfg.slots[size_t(p - 1)].kind == SkirmishSlot::AI;
    if (ai || p == local) ais_.emplace_back(p, cfg.seed * 7919u + uint32_t(p) * 104729u);
  }
  missionTitle_ = "SKIRMISH";
  selectedWorkshop_ = dropIndex_ = selected_ = -1;
  frame(0, 0, float(islands_[0].width() * kCellW), float(islands_[0].height() * kCellH + 40));
}

// A simple bot that plays the local player purely through commands, so it's
// safe in lockstep games and exercises the network path in tests.
void WorldView::autopilotStep() {
  if (!sim_ || sim_->eliminated(player_)) return;
  Island& w = islands_[0];
  auto& me = sim_->player(player_);
  // 1. A bridge piece towards unclaimed land (or the enemy).
  {
    BridgeQueue probe = sim_->bridgeQueue(player_);
    int x, y;
    if (bestBridgeMove(w, probe, player_, x, y)) {
      for (int r = 0; r < 4 && !(myQueue().current().cells == probe.current().cells); ++r) issue({Command::Rotate});
      // In lockstep the rotations haven't happened yet: send the rotations, then the
      // bridge for the rotated piece (all applied in order on the same tick).
      issue({Command::Bridge, 0, x, y});
    } else {
      issue({Command::Rotate});
    }
  }
  // 2. Priest: a sun workshop if we have none.
  const Entity* priest = nullptr;
  int workshops = 0;
  for (auto& e : sim_->entities())
    if (e.alive && e.owner == player_) {
      if (e.role == Role::Priest) priest = &e;
      workshops += e.role == Role::Workshop;
    }
  if (priest && !priest->immobile && priest->job == Entity::Job::Idle && workshops == 0 && me.power >= 800) {
    const TypeInfo* f = gd_.registry.byName("sunFactory");
    for (int r = 2; r < 14 && f; ++r) {
      bool done = false;
      for (int dy = -r; dy <= r && !done; dy += 2)
        for (int dx = -r; dx <= r && !done; dx += 2) {
          int ax = int(priest->x) + dx, ay = int(priest->y) + dy;
          if (sim_->canConstruct(priest->id, f, ax, ay)) {
            issue({Command::Construct, 0, priest->id, ax, ay, "sunFactory"});
            done = true;
          }
        }
      if (done) break;
    }
  }
  // 3. Workshops: build the most expensive affordable shooter (or a generator).
  for (auto& e : sim_->entities()) {
    if (!e.alive || e.owner != player_ || e.role != Role::Workshop || !e.building.empty() || me.ready.size() >= 2)
      continue;
    const TypeInfo* best = nullptr;
    for (auto* t : sim_->buildable(e.id)) {
      double cost = t->def->num("cost", 0);
      bool useful = t->def->num("range", 0) > 0 || t->def->str("group") == "battery";
      if (useful && cost <= me.power && (!best || cost > best->def->num("cost", 0))) best = t;
    }
    if (best) issue({Command::Build, 0, e.id, 0, 0, best->name});
  }
  // 4. Drop the first ready unit on the legal spot nearest an enemy building.
  if (!me.ready.empty()) {
    const TypeInfo* t = gd_.registry.byName(me.ready[0]);
    float ex = w.width() / 2.0f, ey = w.height() / 2.0f, bestE = 1e18f;
    float rx = priest ? priest->x : ex, ry = priest ? priest->y : ey;  // measure from our priest if we have one
    for (auto& e : sim_->entities())
      if (e.alive && e.owner > 0 && e.owner != player_ && !sim_->allied(e.owner, player_) && e.role != Role::Golem &&
          e.role != Role::Flyer) {
        float d = (e.x - rx) * (e.x - rx) + (e.y - ry) * (e.y - ry);
        if (d < bestE) bestE = d, ex = e.x, ey = e.y;
      }
    int bx = -1, by = -1;
    float bd = 1e18f;
    for (int y = 2; y < w.height() - 2 && t; y += 2)
      for (int x = 2; x < w.width() - 2; x += 2) {
        float d = (x - ex) * (x - ex) + (y - ey) * (y - ey);
        if (d < bd && sim_->canDropUnit(t, x, y, player_)) bd = d, bx = x, by = y;
      }
    if (bx >= 0) issue({Command::Drop, 0, 0, bx, by});
  }
}

void WorldView::addChat(const std::string& who, const std::string& text, bool mine) {
  chat_.push_back({who + ": " + text, time_, mine});
  if (chat_.size() > 40) chat_.erase(chat_.begin());
}

void WorldView::drawChat() {
  if (!netSay && chat_.empty()) return;
  SDL_Renderer* ren = r_.sdl();
  int sw, sh;
  r_.outputSize(sw, sh);
  float y = float(sh) - 60;
  if (chatting_) {
    std::string line = "SAY: " + chatInput_ + "_";
    SDL_FRect box{12, y - 6, std::max(420.0f, float(line.size()) * 12 + 24), 30};
    SDL_SetRenderDrawColor(ren, 20, 18, 30, 230);
    SDL_RenderFillRectF(ren, &box);
    drawText(ren, 24, y, 2, line, {255, 220, 110, 255});
  }
  y -= 30;
  int shown = 0;
  for (size_t i = chat_.size(); i-- > 0 && shown < 6;) {
    if (!chatting_ && time_ - chat_[i].at > 12) break;  // fade out; all recent lines while typing
    std::string t = chat_[i].text;
    for (auto& c : t) c = char(std::toupper(static_cast<unsigned char>(c)));
    SDL_FRect box{12, y - 4, float(t.size()) * 12 + 20, 24};
    SDL_SetRenderDrawColor(ren, 10, 10, 20, 170);
    SDL_RenderFillRectF(ren, &box);
    drawText(ren, 22, y, 2, t, chat_[i].mine ? SDL_Color{150, 210, 255, 255} : SDL_Color{235, 235, 235, 255});
    y -= 28;
    ++shown;
  }
}

void WorldView::focusHome(float zoom) {
  if (!sim_ || islands_.empty()) return;
  for (auto& e : sim_->entities())
    if (e.alive && e.owner == player_ && (e.role == Role::Temple || e.role == Role::Priest)) {
      cam_.x = islands_[0].worldX + e.x * kCellW;
      cam_.y = islands_[0].worldY + e.y * kCellH;
      cam_.zoom = zoom;
      if (e.role == Role::Temple) return;
    }
}

}  // namespace ns
