// The in-game left sidebar: Storm Power, bridge pieces, ready units, actions
// for whatever is selected, a minimap and hover tooltips.
//
// All panel art here is drawn procedurally (no original interface art); unit
// thumbnails come from the player's own install via the sprite cache.
#include <algorithm>
#include <cmath>

#include "app/WorldView.h"
#include "render/TinyFont.h"

namespace ns {

namespace {
const SDL_Color kGold{255, 220, 110, 255}, kWhite{235, 235, 235, 255}, kGrey{140, 140, 150, 255},
    kRed{255, 90, 80, 255}, kGreen{120, 230, 120, 255};

uint32_t mix(uint32_t a) {
  a ^= a >> 16;
  a *= 0x7feb352du;
  a ^= a >> 15;
  a *= 0x846ca68bu;
  return a ^ (a >> 16);
}

std::string upper(std::string s) {
  for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}

// "sunArcher" -> "SUN ARCHER"
std::string pretty(const std::string& n) {
  std::string o;
  for (size_t i = 0; i < n.size(); ++i) {
    if (i && std::isupper(static_cast<unsigned char>(n[i])) && !std::isupper(static_cast<unsigned char>(n[i - 1])))
      o += ' ';
    o += char(std::toupper(static_cast<unsigned char>(n[i])));
  }
  return o;
}

float textW(const std::string& s, float scale) { return float(s.size()) * 6 * scale; }
}  // namespace

// Dark slate with flecks and faint cracks; tiled behind the sidebar.
static SDL_Texture* makeStone(SDL_Renderer* ren) {
  const int N = 128;
  std::vector<uint32_t> px(size_t(N * N));
  for (int y = 0; y < N; ++y)
    for (int x = 0; x < N; ++x) {
      // Two octaves of wrapped value noise.
      auto v = [&](int cell) {
        int gx = x / cell, gy = y / cell, n = N / cell;
        float fx = float(x % cell) / cell, fy = float(y % cell) / cell;
        auto h = [&](int a, int b) { return float(mix(uint32_t(((a + n) % n) * 92821 + ((b + n) % n) * 68917 + cell)) & 255) / 255.f; };
        float a = h(gx, gy) + (h(gx + 1, gy) - h(gx, gy)) * fx;
        float b = h(gx, gy + 1) + (h(gx + 1, gy + 1) - h(gx, gy + 1)) * fx;
        return a + (b - a) * fy;
      };
      float t = v(32) * 0.6f + v(8) * 0.3f + float(mix(uint32_t(x * 131 + y * 7)) & 15) / 15.f * 0.1f;
      int base = int(34 + t * 26);
      uint32_t r = uint32_t(base + 4), g = uint32_t(base + 2), b = uint32_t(base + 8);
      px[size_t(y * N + x)] = 0xff000000u | (r << 16) | (g << 8) | b;
    }
  SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, N, N);
  if (t) SDL_UpdateTexture(t, nullptr, px.data(), N * 4);
  return t;
}

// Bevelled inset box: dark well with a light lower-right and dark upper-left edge.
static void well(SDL_Renderer* ren, SDL_FRect r, bool hot = false, bool selected = false) {
  SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(ren, selected ? 70 : hot ? 46 : 16, selected ? 58 : hot ? 42 : 16, selected ? 30 : hot ? 56 : 24,
                         selected || hot ? 235 : 200);
  SDL_RenderFillRectF(ren, &r);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 200);
  SDL_RenderDrawLineF(ren, r.x, r.y, r.x + r.w - 1, r.y);
  SDL_RenderDrawLineF(ren, r.x, r.y, r.x, r.y + r.h - 1);
  SDL_SetRenderDrawColor(ren, 110, 104, 120, 200);
  SDL_RenderDrawLineF(ren, r.x + 1, r.y + r.h - 1, r.x + r.w - 1, r.y + r.h - 1);
  SDL_RenderDrawLineF(ren, r.x + r.w - 1, r.y + 1, r.x + r.w - 1, r.y + r.h - 1);
  if (selected) {
    SDL_SetRenderDrawColor(ren, kGold.r, kGold.g, kGold.b, 255);
    SDL_RenderDrawRectF(ren, &r);
  }
}

static void heading(SDL_Renderer* ren, float x, float y, float w, const std::string& t) {
  drawText(ren, x, y, 2, t, SDL_Color{190, 170, 120, 255});
  float tw = textW(t, 2) + 8;
  SDL_SetRenderDrawColor(ren, 120, 100, 70, 160);
  SDL_RenderDrawLineF(ren, x + tw, y + 7, x + w, y + 7);
}

void WorldView::thumb(const TypeInfo* t, SDL_FRect box) {
  if (!t || t->set < 0) return;
  // Prefer the type's "info"/"help" picture when it has one; else frame 0.
  int f = 0;
  if (t->def)
    for (size_t i = 0; i < t->def->frames.size(); ++i) {
      const auto& l = t->def->frames[i].label;
      if (l.find("info") != std::string::npos) { f = int(i); break; }
    }
  const SpriteTex& st = cache_.get(t->set, f);
  if (!st.tex || st.w <= 0 || st.h <= 0) return;
  float k = std::min((box.w - 6) / st.w, (box.h - 6) / st.h);
  k = std::min(k, 2.0f);
  SDL_FRect d{box.x + (box.w - st.w * k) / 2, box.y + (box.h - st.h * k) / 2, st.w * k, st.h * k};
  SDL_RenderCopyF(r_.sdl(), st.tex, nullptr, &d);
}

void WorldView::drawSidebar() {
  SDL_Renderer* ren = r_.sdl();
  const float W = kSidebarW, H = float(cam_.screenH);
  int mx, my;
  SDL_GetMouseState(&mx, &my);
  auto hot = [&](SDL_FRect r) { return mx >= r.x && my >= r.y && mx < r.x + r.w && my < r.y + r.h; };

  // Background: tiled stone with a carved right edge.
  if (!stoneTex_) stoneTex_ = makeStone(ren);
  for (float y = 0; y < H; y += 128)
    for (float x = 0; x < W; x += 128) {
      SDL_FRect d{x, y, std::min(128.f, W - x), std::min(128.f, H - y)};
      SDL_Rect s{0, 0, int(d.w), int(d.h)};
      if (stoneTex_) SDL_RenderCopyF(ren, stoneTex_, &s, &d);
    }
  SDL_SetRenderDrawColor(ren, 12, 10, 16, 255);
  SDL_FRect edge{W - 4, 0, 4, H};
  SDL_RenderFillRectF(ren, &edge);
  SDL_SetRenderDrawColor(ren, 130, 110, 80, 255);
  SDL_RenderDrawLineF(ren, W - 5, 0, W - 5, H);

  PlayerState& me = sim_->player(player_);
  const float pad = 10, iw = W - 2 * pad - 4;
  float y = 10;

  // --- Storm Power: number, gauge, income per minute ---
  const double simT = sim_->tick() * 0.05;  // sim clock: pauses with the game, works headless
  if (!powerHist_.empty() && powerHist_.back().first > simT) powerHist_.clear();  // new game
  if (powerHist_.empty() || simT - powerHist_.back().first >= 1.0) {
    powerHist_.push_back({simT, me.power});
    while (powerHist_.size() > 31) powerHist_.erase(powerHist_.begin());
  }
  heading(ren, pad, y, iw, "STORM POWER");
  y += 20;
  SDL_Color pc = me.power < 1000 ? kRed : me.power < 2000 ? SDL_Color{255, 230, 60, 255} : kWhite;
  drawText(ren, pad + 2, y, 4, std::to_string(int(me.power)), pc);
  if (powerHist_.size() > 5) {
    const auto& a = powerHist_.front();
    double span = simT - a.first;
    int rate = span > 0 ? int(std::lround((me.power - a.second) / span * 60.0)) : 0;
    std::string r = (rate >= 0 ? "+" : "") + std::to_string(rate) + "/MIN";
    drawText(ren, pad + iw - textW(r, 2), y + 8, 2, r, rate >= 0 ? kGreen : kRed);
  }
  y += 34;
  {
    SDL_FRect g{pad, y, iw, 8};
    well(ren, g);
    float f = std::clamp(me.power / 6000.f, 0.f, 1.f);
    SDL_FRect fill{g.x + 1, g.y + 1, (g.w - 2) * f, g.h - 2};
    SDL_SetRenderDrawColor(ren, pc.r, pc.g, pc.b, 255);
    SDL_RenderFillRectF(ren, &fill);
    buttons_.push_back({g, "", "STORM POWER PAYS FOR EVERYTHING. GOLEMS MINE IT FROM GEYSERS."});
  }
  y += 16;

  // Golem button.
  bool temple = false;
  for (auto& e : sim_->entities()) temple |= e.alive && e.owner == player_ && e.role == Role::Temple;
  if (temple) {
    SDL_FRect b{pad, y, iw, 28};
    bool can = me.power >= Sim::kGolemCost;
    well(ren, b, hot(b));
    drawText(ren, b.x + 8, b.y + 7, 2, "+ GOLEM", can ? kGold : kGrey);
    std::string c = std::to_string(Sim::kGolemCost);
    drawText(ren, b.x + b.w - 8 - textW(c, 2), b.y + 7, 2, c, can ? kGold : kRed);
    buttons_.push_back({b, "golem", "MAKE A GOLEM AT YOUR TEMPLE. GOLEMS MINE STORM POWER AND CAPTURE PRIESTS."});
    y += 36;
  }

  // --- Bridge pieces: current big, next two small ---
  heading(ren, pad, y, iw, "BRIDGE");
  y += 18;
  if (bridgeType_ && bridgeType_->set >= 0) {
    int line = bridgeType_->findLine("A", 1);
    const SpriteTex& st = cache_.get(bridgeType_->set, bridgeType_->frame(line));
    auto drawPiece = [&](const BridgeShape& s, SDL_FRect box, float cell) {
      int mw = 0, mh = 0;
      for (auto [dx, dy] : s.cells) mw = std::max(mw, dx), mh = std::max(mh, dy);
      float cw = cell, ch = cell * 11 / 16;
      float ox = box.x + (box.w - (mw + 1) * cw) / 2, oy = box.y + (box.h - (mh + 1) * ch) / 2 - ch * 0.3f;
      for (auto [dx, dy] : s.cells) {
        SDL_FRect d{ox + dx * cw, oy + dy * ch, cw, st.h * cw / 16};
        if (st.tex) SDL_RenderCopyF(ren, st.tex, nullptr, &d);
      }
    };
    SDL_FRect big{pad, y, iw * 0.58f, 96};
    well(ren, big, hot(big));
    drawPiece(myQueue().upcoming(0), big, 22);
    buttons_.push_back({big, "rotate", "NEXT BRIDGE PIECE. CLICK THE SKY NEXT TO YOUR LAND TO PLACE IT. RIGHT-CLICK OR R ROTATES."});
    float sx = pad + iw * 0.58f + 6, sw = iw - iw * 0.58f - 6;
    for (int i = 1; i < 3 && i < BridgeQueue::kVisible; ++i) {
      SDL_FRect sm{sx, y + (i - 1) * 49.f, sw, 47};
      well(ren, sm);
      drawPiece(myQueue().upcoming(i), sm, 11);
    }
    y += 104;
  }

  // --- Ready units ---
  if (!me.ready.empty()) {
    heading(ren, pad, y, iw, "READY TO PLACE");
    y += 18;
    const float tile = (iw - 2 * 6) / 3;
    for (size_t i = 0; i < me.ready.size() && i < 6; ++i) {
      SDL_FRect r{pad + float(i % 3) * (tile + 6), y + float(i / 3) * (tile + 6), tile, tile};
      well(ren, r, hot(r), dropIndex_ == int(i));
      thumb(gd_.registry.byName(me.ready[i]), r);
      buttons_.push_back({r, "drop:" + std::to_string(i),
                          pretty(me.ready[i]) + ". CLICK, THEN CLICK OPEN SKY NEXT TO YOUR LAND TO DROP IT."});
    }
    y += float((std::min<size_t>(me.ready.size(), 6) + 2) / 3) * (tile + 6) + 4;
  }

  // --- Context: what's selected ---
  const float mmH = iw;  // square minimap at the bottom
  const float ctxBottom = H - mmH - pad - 26;
  auto grid = [&](const std::vector<const TypeInfo*>& list, const std::string& verb, float top,
                  const std::function<bool(const TypeInfo*)>& usable, const std::string& selName) {
    const float tile = (iw - 2 * 6) / 3;
    float yy = top;
    for (size_t i = 0; i < list.size(); ++i) {
      const TypeInfo* t = list[i];
      SDL_FRect r{pad + float(i % 3) * (tile + 6), top + float(i / 3) * (tile + 18), tile, tile};
      if (r.y + r.h + 14 > ctxBottom) break;
      well(ren, r, hot(r), selName == t->name);
      thumb(t, r);
      int cost = int(t->def->num("cost", 0));
      bool ok = usable(t);
      if (!ok) {  // dim what you can't use right now
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 120);
        SDL_RenderFillRectF(ren, &r);
      }
      std::string c = std::to_string(cost);
      drawText(ren, r.x + (r.w - textW(c, 2)) / 2, r.y + r.h + 2, 2, c, me.power >= cost ? kGold : kRed);
      std::string tip = pretty(t->name) + " - " + c + " STORM POWER";
      int need = int(t->def->num("minUsage", 0));
      if (need > 0) tip += ". NEEDS " + std::to_string(need) + " " + upper(t->def->str("theme")) + " ENERGY NEARBY";
      buttons_.push_back({r, verb + t->name, tip});
      yy = r.y + r.h + 18;
    }
    return yy;
  };

  if (Entity* w = sim_->find(selectedWorkshop_)) {
    bool slotsOn = sim_->knowledgeGifts;
    heading(ren, pad, y, iw, pretty(w->type->name) + (slotsOn ? " LV" + std::to_string(w->workshopLevel) : ""));
    SDL_FRect close{pad + iw - 18, y - 2, 18, 18};
    drawText(ren, close.x + 4, close.y + 2, 2, "X", hot(close) ? kWhite : kRed);
    buttons_.push_back({close, "close", "CLOSE"});
    y += 20;
    if (!w->building.empty()) {
      const TypeInfo* t = gd_.registry.byName(w->building);
      float total = t ? std::max(4.0f, float(t->def->num("cost", 400)) / 50.0f) : 8.0f;
      SDL_FRect bar{pad, y, iw, 14};
      well(ren, bar);
      SDL_FRect fill{bar.x + 1, bar.y + 1, (bar.w - 2) * std::min(1.0f, w->progress / total), bar.h - 2};
      SDL_SetRenderDrawColor(ren, kGold.r, kGold.g, kGold.b, 255);
      SDL_RenderFillRectF(ren, &fill);
      drawText(ren, bar.x + 4, bar.y + 1, 1.5f, pretty(w->building), SDL_Color{30, 20, 10, 255});
    } else {
      drawText(ren, pad, y, 2, "PICK A UNIT TO MAKE", kGrey);
    }
    y += 22;
    auto list = sim_->buildable(w->id);
    y = grid(list, "build:", y,
             [&](const TypeInfo* t) {
               int cost = int(t->def->num("cost", 0)), need = int(t->def->num("minUsage", 0));
               int have = sim_->energyAt(player_, t->def->str("theme"), int(w->x), int(w->y));
               return me.power >= cost && (need <= 0 || have >= need) && w->building.empty();
             },
             w->building);
    if (slotsOn && y + 30 < ctxBottom) {
      SDL_FRect r{pad, y, iw, 26};
      well(ren, r, hot(r));
      if (w->workshopLevel < 4) {
        int cost = sim_->upgradeCost(*w);
        drawText(ren, r.x + 8, r.y + 6, 2, "UPGRADE", me.power >= cost ? kGold : kGrey);
        std::string c = std::to_string(cost);
        drawText(ren, r.x + r.w - 8 - textW(c, 2), r.y + 6, 2, c, me.power >= cost ? kGold : kRed);
        buttons_.push_back({r, "upgrade",
                            "ADD A PRODUCTION SLOT (" + std::to_string(w->slots.size()) + "/" +
                                std::to_string(sim_->slotCapacity(*w)) + " USED)"});
      } else {
        drawText(ren, r.x + 8, r.y + 6, 2, "FULLY UPGRADED", kGrey);
      }
    }
  } else if (Entity* pr = sim_->find(selected_); pr && pr->role == Role::Priest) {
    heading(ren, pad, y, iw, "HIGH PRIEST");
    SDL_FRect close{pad + iw - 18, y - 2, 18, 18};
    drawText(ren, close.x + 4, close.y + 2, 2, "X", hot(close) ? kWhite : kRed);
    buttons_.push_back({close, "close", "CLOSE"});
    y += 20;
    SDL_FRect hp{pad, y, iw, 8};
    well(ren, hp);
    SDL_FRect fill{hp.x + 1, hp.y + 1, (hp.w - 2) * std::clamp(float(pr->hp) / std::max(1, pr->maxHp), 0.f, 1.f), 6};
    SDL_SetRenderDrawColor(ren, pr->immobile ? 255 : 120, pr->immobile ? 90 : 220, 90, 255);
    SDL_RenderFillRectF(ren, &fill);
    y += 14;
    drawText(ren, pad, y, 2, pr->immobile ? "IMMOBILISED!" : "CLICK LAND TO WALK", pr->immobile ? kRed : kGrey);
    y += 22;
    grid(sim_->constructable(), "construct:", y, [&](const TypeInfo* t) { return me.power >= t->def->num("cost", 0); },
         constructType_);
  } else if (Entity* g = sim_->find(selected_); g && g->role == Role::Golem) {
    heading(ren, pad, y, iw, "GOLEM");
    y += 22;
    drawWrapped(ren, pad, y, iw, 2, "CLICK A DOWNED ENEMY PRIEST TO CAPTURE IT, OR LAND TO WALK.", kGrey, ctxBottom);
  } else {
    heading(ren, pad, y, iw, "ORDERS");
    y += 22;
    drawWrapped(ren, pad, y, iw, 2,
                "CLICK YOUR PRIEST TO BUILD.\n\nCLICK A WORKSHOP TO MAKE UNITS.\n\nCLICK THE SKY TO LAY BRIDGES.",
                kGrey, ctxBottom);
  }

  // --- Minimap ---
  heading(ren, pad, H - mmH - pad - 20, iw, "MAP");
  drawMinimap({pad, H - mmH - pad, iw, mmH});
}

void WorldView::drawMinimap(SDL_FRect area) {
  SDL_Renderer* ren = r_.sdl();
  well(ren, area);
  minimapRect_ = area;
  if (islands_.empty()) return;
  const Island& isl = islands_[0];
  // Rebuild the land picture twice a second.
  if (!minimapTex_ || time_ - minimapAt_ > 0.5 || minimapAt_ > time_) {
    minimapAt_ = time_;
    int x0 = isl.width(), y0 = isl.height(), x1 = 0, y1 = 0;
    for (int y = 0; y < isl.height(); ++y)
      for (int x = 0; x < isl.width(); ++x)
        if (isl.filled(x, y) || isl.bridgeAt(x, y))
          x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    if (x1 < x0) return;
    x0 -= 6, y0 -= 6, x1 += 6, y1 += 6;
    // Square in screen terms: cells are 16x11, so pad the short side.
    int w = x1 - x0 + 1, h = y1 - y0 + 1;
    float aspect = float(w * kCellW) / float(h * kCellH);
    if (aspect > 1) {
      int nh = int(std::ceil(w * kCellW / float(kCellH)));
      y0 -= (nh - h) / 2, h = nh;
    } else {
      int nw = int(std::ceil(h * kCellH / float(kCellW)));
      x0 -= (nw - w) / 2, w = nw;
    }
    if (minimapTex_ && (w != minimapW_ || h != minimapH_)) SDL_DestroyTexture(minimapTex_), minimapTex_ = nullptr;
    minimapX0_ = x0, minimapY0_ = y0, minimapW_ = w, minimapH_ = h;
    if (!minimapTex_) {
      minimapTex_ = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
      SDL_SetTextureBlendMode(minimapTex_, SDL_BLENDMODE_BLEND);
    }
    std::vector<uint32_t> px(size_t(w * h), 0);
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        int cx = x0 + x, cy = y0 + y;
        if (cx < 0 || cy < 0 || cx >= isl.width() || cy >= isl.height()) continue;
        uint32_t c = 0;
        if (isl.filled(cx, cy)) {
          switch (isl.at(cx, cy)) {
            case Element::Sun: c = 0x5a8a3a; break;
            case Element::Rain: c = 0xc8ccd8; break;
            case Element::Wind: c = 0xb09a5a; break;
            case Element::Thunder: c = 0x6a5a7a; break;
            default: c = 0x707070; break;
          }
          int own = isl.ownerAt(cx, cy);
          bool border = own > 0 && (isl.ownerAt(cx + 1, cy) != own || isl.ownerAt(cx - 1, cy) != own ||
                                    isl.ownerAt(cx, cy + 1) != own || isl.ownerAt(cx, cy - 1) != own);
          if (border) {
            SDL_Color oc = colourOf(own);
            c = (uint32_t(oc.r) << 16) | (uint32_t(oc.g) << 8) | oc.b;
          }
        } else if (isl.bridgeAt(cx, cy)) {
          c = 0xc07a30;
        } else {
          continue;
        }
        px[size_t(y * w + x)] = 0xff000000u | c;
      }
    SDL_UpdateTexture(minimapTex_, nullptr, px.data(), w * 4);
  }
  if (!minimapTex_) return;
  SDL_FRect in{area.x + 2, area.y + 2, area.w - 4, area.h - 4};
  SDL_RenderCopyF(ren, minimapTex_, nullptr, &in);
  float sx = in.w / minimapW_, sy = in.h / minimapH_;
  // Units and buildings as dots in their owner's colour.
  for (auto& e : sim_->entities()) {
    if (!e.alive || e.owner <= 0 || e.role == Role::Static) continue;
    float ex = (e.x - minimapX0_) * sx + in.x, ey = (e.y - minimapY0_) * sy + in.y;
    if (ex < in.x || ey < in.y || ex >= in.x + in.w || ey >= in.y + in.h) continue;
    SDL_Color c = colourOf(e.owner);
    float s = e.role == Role::Temple || e.role == Role::Priest ? 4 : 2;
    SDL_FRect d{ex - s / 2, ey - s / 2, s, s};
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, 255);
    SDL_RenderFillRectF(ren, &d);
  }
  // The camera's view.
  float wx0, wy0, wx1, wy1;
  cam_.screenToWorld(int(kSidebarW), 0, wx0, wy0);
  cam_.screenToWorld(cam_.screenW, cam_.screenH, wx1, wy1);
  float ox = float(isl.worldX), oy = float(isl.worldY);
  SDL_FRect v{(wx0 - ox) / kCellW * 1 - minimapX0_, (wy0 - oy) / kCellH - minimapY0_, (wx1 - wx0) / kCellW,
              (wy1 - wy0) / kCellH};
  v = {in.x + v.x * sx, in.y + v.y * sy, v.w * sx, v.h * sy};
  SDL_RenderSetClipRect(ren, nullptr);
  SDL_Rect clip{int(in.x), int(in.y), int(in.w), int(in.h)};
  SDL_RenderSetClipRect(ren, &clip);
  SDL_SetRenderDrawColor(ren, 255, 255, 255, 220);
  SDL_RenderDrawRectF(ren, &v);
  SDL_RenderSetClipRect(ren, nullptr);
}

bool WorldView::minimapToCamera(int sx, int sy) {
  const SDL_FRect& a = minimapRect_;
  if (a.w <= 0 || sx < a.x || sy < a.y || sx >= a.x + a.w || sy >= a.y + a.h || minimapW_ <= 0 || islands_.empty())
    return false;
  float fx = (sx - a.x - 2) / (a.w - 4), fy = (sy - a.y - 2) / (a.h - 4);
  float cx = minimapX0_ + fx * minimapW_, cy = minimapY0_ + fy * minimapH_;
  // Centre the clicked point in the part of the screen the sidebar leaves free.
  cam_.x = islands_[0].worldX + cx * kCellW - kSidebarW * 0.5f / cam_.zoom;
  cam_.y = islands_[0].worldY + cy * kCellH;
  return true;
}

void WorldView::drawTooltip() {
  int mx, my;
  SDL_GetMouseState(&mx, &my);
  for (auto& b : buttons_) {
    if (b.tip.empty() || mx < b.r.x || my < b.r.y || mx >= b.r.x + b.r.w || my >= b.r.y + b.r.h) continue;
    SDL_Renderer* ren = r_.sdl();
    float w = std::min(360.f, textW(b.tip, 2) + 16);
    float h = drawWrapped(ren, 0, 0, w - 16, 2, b.tip, kWhite, 9999, true) + 12;
    float x = float(mx) + 18, y = float(my) + 4;
    if (x + w > cam_.screenW) x = float(mx) - w - 8;
    if (y + h > cam_.screenH) y = float(cam_.screenH) - h - 4;
    SDL_FRect r{x, y, w, h};
    SDL_SetRenderDrawColor(ren, 14, 12, 20, 240);
    SDL_RenderFillRectF(ren, &r);
    SDL_SetRenderDrawColor(ren, 180, 150, 90, 255);
    SDL_RenderDrawRectF(ren, &r);
    drawWrapped(ren, x + 8, y + 6, w - 16, 2, b.tip, kWhite, y + h);
    return;
  }
}

}  // namespace ns
