// Skirmish setup, LAN browser and LAN lobby screens.
#include <algorithm>
#include <cstdlib>

#include "app/Frontend.h"
#include "render/TinyFont.h"

namespace ns {

namespace {
const SDL_Color kGold{255, 220, 110, 255}, kWhite{235, 235, 235, 255}, kGrey{150, 150, 160, 255},
    kGreen{120, 230, 120, 255}, kRed{255, 110, 100, 255};
const SDL_Color kFury[3] = {{120, 200, 255, 255}, {150, 230, 170, 255}, {255, 210, 90, 255}};  // rain wind thunder
const char* kFuries[3] = {"rain", "wind", "thunder"};
const int kPowers[5] = {1500, 3000, 5000, 10000, 25000};

std::string upper(std::string s) {
  for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
int furyIndex(const std::string& f) { return f == "wind" ? 1 : f == "thunder" ? 2 : 0; }
}  // namespace

void Frontend::addButton(SDL_FRect r, const std::string& label, const std::string& go) {
  Button b;
  b.r = r;
  b.label = label;
  b.go = go;
  buttons_.push_back(b);
}

// ---- Skirmish setup / LAN lobby -------------------------------------------------------

void Frontend::layoutSetup() {
  int sw, sh;
  r_.outputSize(sw, sh);
  bool client = lanMode_ && lan_ && lan_->role() == LanSession::Role::Client;
  bool host = lanMode_ && lan_ && lan_->role() == LanSession::Role::Host;
  if (lanMode_ && lan_) {
    setup_ = lan_->config();
    if (host) setup_.slots[0].kind = SkirmishSlot::Human;
  }
  buttons_.clear();
  float x = std::max(40.0f, sw / 2.0f - 470), y = 130, rowH = 40;
  for (int i = 0; i < 8; ++i) {
    const SkirmishSlot& s = setup_.slots[size_t(i)];
    std::string who;
    if (s.kind == SkirmishSlot::Closed) who = "CLOSED";
    else if (s.kind == SkirmishSlot::AI) who = "AI";
    else if (lanMode_ && lan_) {
      const std::string& n = lan_->names[size_t(i)];
      who = n.empty() ? "OPEN - WAITING..." : upper(n) + (i + 1 == lan_->localPlayer() ? " (YOU)" : "");
    } else {
      who = i == 0 ? "YOU" : "HUMAN";
    }
    std::string pfx = "P" + std::to_string(i + 1) + "  ";
    bool fixed = i == 0 || client;  // player 1 is the host/you; clients can't edit
    addButton({x, y, 420, rowH - 6}, pfx + who, fixed ? "" : "slot:" + std::to_string(i) + ":kind");
    if (s.kind != SkirmishSlot::Closed) {
      addButton({x + 430, y, 230, rowH - 6}, upper(s.fury), client ? "" : "slot:" + std::to_string(i) + ":fury");
      addButton({x + 670, y, 230, rowH - 6}, s.team ? "TEAM " + std::to_string(s.team) : "NO TEAM",
                client ? "" : "slot:" + std::to_string(i) + ":team");
    }
    y += rowH;
  }
  y += 14;
  static const char* sizes[3] = {"SMALL", "MEDIUM", "LARGE"};
  addButton({x, y, 290, 36}, std::string("MAP: ") + sizes[setup_.mapSize], client ? "" : "map");
  addButton({x + 305, y, 290, 36}, "POWER: " + std::to_string(setup_.startPower), client ? "" : "power");
  addButton({x + 610, y, 290, 36}, "ISLANDS: " + std::to_string(setup_.neutralIslands), client ? "" : "neutral");
  addButton({30, float(sh) - 70, 200, 44}, lanMode_ ? "LEAVE" : "BACK", "back");
  if (!client) {
    bool ready = !lanMode_ || (lan_ && lan_->humansWaiting() == 0);
    addButton({float(sw) - 330, float(sh) - 80, 300, 50}, ready ? "START BATTLE" : "WAITING FOR PLAYERS",
              ready ? "start" : "");
  }
}

void Frontend::drawSetup() {
  SDL_Renderer* ren = r_.sdl();
  int sw, sh;
  r_.outputSize(sw, sh);
  bool client = lanMode_ && lan_ && lan_->role() == LanSession::Role::Client;
  float x = std::max(40.0f, sw / 2.0f - 470);
  drawText(ren, x, 40, 5, lanMode_ ? (client ? "LAN BATTLE" : "HOSTING A LAN BATTLE") : "SKIRMISH", kGold);
  std::string sub = lanMode_ ? (client ? "THE HOST SETS UP THE BATTLE" : "CLICK A SLOT: CLOSED / AI / HUMAN (OPEN SEAT)")
                             : "CLICK TO CHANGE: OPPONENTS, FURIES, TEAMS";
  drawText(ren, x, 100, 2, sub, kGrey);
  if (lanMode_ && lan_ && !lan_->status.empty()) drawText(ren, x, float(sh) - 130, 2, upper(lan_->status), kWhite);
  if (lanMode_ && lan_ && lan_->role() == LanSession::Role::Host)
    drawText(ren, x, float(sh) - 110, 2,
             "OTHERS JOIN FROM LAN GAME ON THEIR MACHINE (INTERNET: FORWARD TCP PORT " +
                 std::to_string(LanSession::kPort) + ")",
             kGrey);
  // Lobby chat (right-hand side of the bottom area).
  if (lanMode_ && lan_) {
    float cx = x, cy = float(sh) - 250;
    drawText(ren, cx, cy, 2, editing_ == 3 ? "SAY: " + upper(chatInput_) + "_" : "PRESS ENTER TO CHAT", editing_ == 3 ? kGold : kGrey);
    int shown = 0;
    for (size_t i = lan_->chat.size(); i-- > 0 && shown < 4; ++shown) {
      auto& c = lan_->chat[i];
      drawText(ren, cx, cy - 24.0f * float(shown + 1), 2, upper(c.name + ": " + c.text),
               c.player == lan_->localPlayer() ? SDL_Color{150, 210, 255, 255} : kWhite);
    }
  }
  // Fury colour chips next to the fury buttons.
  for (auto& b : buttons_)
    if (b.go.find(":fury") != std::string::npos || (client && (b.label == "RAIN" || b.label == "WIND" ||
                                                               b.label == "THUNDER"))) {
      int f = b.label == "WIND" ? 1 : b.label == "THUNDER" ? 2 : 0;
      SDL_FRect chip{b.r.x + b.r.w - 22, b.r.y + 8, 14, b.r.h - 16};
      SDL_SetRenderDrawColor(ren, kFury[f].r, kFury[f].g, kFury[f].b, 255);
      SDL_RenderFillRectF(ren, &chip);
    }
}

FrontendAction Frontend::setupClick(const std::string& go) {
  bool host = lanMode_ && lan_ && lan_->role() == LanSession::Role::Host;
  bool client = lanMode_ && lan_ && lan_->role() == LanSession::Role::Client;
  if (go == "back") {
    if (lanMode_) {
      lanMode_ = false;
      if (lan_) lan_->leave();
      navigate("lan");
      return {FrontendAction::LeaveLan, ""};
    }
    navigate("title");
    return {};
  }
  if (client) return {};
  if (go == "start") {
    setup_.seed = setup_.seed ? setup_.seed : uint32_t(SDL_GetTicks64() | 1);
    if (host) {
      lan_->setConfig(setup_);
      lan_->start();  // main notices justStarted()
      return {};
    }
    return {FrontendAction::StartSkirmish, ""};
  }
  if (go.rfind("slot:", 0) == 0) {
    int i = go[5] - '0';
    SkirmishSlot& s = setup_.slots[size_t(i)];
    std::string what = go.substr(7);
    if (what == "kind") {
      if (lanMode_)  // Closed -> AI -> Human (an open seat for someone to join) -> Closed
        s.kind = s.kind == SkirmishSlot::Closed ? SkirmishSlot::AI
                 : s.kind == SkirmishSlot::AI   ? SkirmishSlot::Human
                                                : SkirmishSlot::Closed;
      else
        s.kind = s.kind == SkirmishSlot::Closed ? SkirmishSlot::AI : SkirmishSlot::Closed;
      // Keep at least one opponent.
      int n = 0;
      for (auto& o : setup_.slots) n += o.kind != SkirmishSlot::Closed;
      if (n < 2) s.kind = SkirmishSlot::AI;
    } else if (what == "fury") {
      s.fury = kFuries[(furyIndex(s.fury) + 1) % 3];
    } else if (what == "team") {
      s.team = uint8_t((s.team + 1) % 5);
    }
  } else if (go == "map") {
    setup_.mapSize = (setup_.mapSize + 1) % 3;
  } else if (go == "power") {
    int k = 0;
    while (k < 5 && kPowers[k] != setup_.startPower) ++k;
    setup_.startPower = kPowers[(k + 1) % 5];
  } else if (go == "neutral") {
    setup_.neutralIslands = (setup_.neutralIslands + 2) % 12;
  }
  if (host) lan_->setConfig(setup_);
  layoutSetup();
  return {};
}

// ---- LAN browser ----------------------------------------------------------------------

void Frontend::layoutLan() {
  int sw, sh;
  r_.outputSize(sw, sh);
  buttons_.clear();
  float x = std::max(40.0f, sw / 2.0f - 400), y = 130;
  addButton({x, y, 800, 44}, "NAME: " + upper(name_) + (editing_ == 1 ? "_" : ""), "edit:name");
  y += 70;
  addButton({x, y, 800, 50}, "HOST A BATTLE", "host");
  y += 80;
  if (lan_)
    for (size_t i = 0; i < lan_->found.size() && i < 6; ++i) {
      auto& f = lan_->found[i];
      addButton({x, y, 800, 40},
                "JOIN " + upper(f.name) + "  (" + f.addr + ", " + std::to_string(f.open) + " OPEN)",
                "join:" + f.addr);
      y += 46;
    }
  if (!lan_ || lan_->found.empty()) y += 46;
  y += 20;
  addButton({x, y, 580, 44}, "ADDRESS: " + addr_ + (editing_ == 2 ? "_" : ""), "edit:addr");
  addButton({x + 600, y, 200, 44}, "JOIN", "join:" + addr_);
  addButton({30, float(sh) - 70, 200, 44}, "BACK", "title");
}

void Frontend::drawLan() {
  SDL_Renderer* ren = r_.sdl();
  int sw, sh;
  r_.outputSize(sw, sh);
  float x = std::max(40.0f, sw / 2.0f - 400);
  drawText(ren, x, 40, 5, "LAN / INTERNET BATTLE", kGold);
  drawText(ren, x, 100, 2, "BATTLES ON YOUR NETWORK SHOW UP BELOW. OR TYPE THE HOST'S ADDRESS.", kGrey);
  if (lan_ && lan_->found.empty()) drawText(ren, x, 296, 2, "LOOKING FOR BATTLES ON YOUR NETWORK...", kGrey);
  if (lan_ && !lan_->status.empty() && lan_->state() == LanSession::State::Ended)
    drawText(ren, x, float(sh) - 130, 2, upper(lan_->status), kRed);
  // Re-ask the network every couple of seconds.
  if (lan_ && lan_->role() == LanSession::Role::None && SDL_GetTicks64() - lastDiscover_ > 2000) {
    lastDiscover_ = SDL_GetTicks64();
    lan_->discover();
  }
  layoutLan();
}

void Frontend::showSkirmishResult(bool won) {
  skirmishResult_ = true;
  lastWon_ = won;
  current_.clear();
  resultText_ = won ? "Your enemies' priests have fallen. The islands are yours."
                    : "Your priest has been sacrificed. The battle is lost.";
  screen_ = Screen::Result;
  buttons_.clear();
  int sw, sh;
  r_.outputSize(sw, sh);
  addButton({float(sw) - 330, float(sh) - 80, 300, 50}, "BACK TO MENU", "title");
}

}  // namespace ns
