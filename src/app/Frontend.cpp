#include "app/Frontend.h"

#include <algorithm>
#include <fstream>

#include "data/Gif.h"
#include "render/TinyFont.h"

namespace ns {

namespace {
const SDL_Color kGold{255, 220, 110, 255}, kWhite{235, 235, 235, 255}, kGrey{150, 150, 160, 255},
    kGreen{120, 230, 120, 255};

std::string upper(std::string s) {
  for (auto& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

Frontend::Frontend(Renderer& r, const GameData& gd) : r_(r), gd_(gd) {
  // Title art from the user's install (1080p in the community build).
  for (const char* name : {"titleMenu25.gif", "titleMenu.gif", "TITLE00.GIF"}) {
    auto dir = findFileCI(gd.root, "d");
    if (!dir) break;
    auto f = findFileCI(*dir, name);
    if (!f) continue;
    auto bytes = readFile(*f);
    if (!bytes) continue;
    auto img = decodeGif(*bytes);
    if (!img) continue;
    bg_ = SDL_CreateTexture(r_.sdl(), SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, img->w, img->h);
    SDL_UpdateTexture(bg_, nullptr, img->px.data(), img->w * 4);
    SDL_SetTextureScaleMode(bg_, SDL_ScaleModeLinear);
    bgW_ = img->w;
    bgH_ = img->h;
    break;
  }
  // All missions that have a playable map (Easy/Hard share the Normal map).
  for (auto& [name, bytes] : gd.archive.entries()) {
    if (name.size() < 9 || name.compare(name.size() - 8, 8, ".english") != 0) continue;
    std::string id = name.substr(name.rfind('/') + 1);
    id = id.substr(0, id.size() - 8);
    MissionInfo m = MissionInfo::parse(std::string(bytes.begin(), bytes.end()), id);
    if (!gd.archive.find("d/" + m.fort + ".fort")) continue;
    missions_.push_back(std::move(m));
  }
  chapters_ = {{"Struggle For Freedom", "thewarbegins"},
               {"A Nation Rises", "thenoose"},
               {"Complete Victory", "breakingthrough"},
               {"Priest Training", "bc2bridging76"}};
  loadProgress();
  navigate("title");
}

Frontend::~Frontend() {
  if (bg_) SDL_DestroyTexture(bg_);
}

const MissionInfo* Frontend::mission(const std::string& id) const {
  for (auto& m : missions_)
    if (m.id == id) return &m;
  return nullptr;
}

void Frontend::loadProgress() {
  char* p = SDL_GetPrefPath("NetStormPort", "NetStorm");
  if (!p) return;
  std::ifstream f(std::string(p) + "progress.txt");
  SDL_free(p);
  std::string line;
  while (std::getline(f, line))
    if (!line.empty()) done_.insert(line);
}

void Frontend::saveProgress() {
  char* p = SDL_GetPrefPath("NetStormPort", "NetStorm");
  if (!p) return;
  std::ofstream f(std::string(p) + "progress.txt");
  SDL_free(p);
  for (auto& d : done_) f << d << "\n";
}

void Frontend::markDone(const std::string& id) {
  done_.insert(id);
  saveProgress();
}

void Frontend::showResult(const std::string& id, bool won, const std::string& text) {
  current_ = id;
  resultText_ = text;
  lastWon_ = won;
  if (won) markDone(id);
  navigate("result");
}

// Builds a centred column of buttons.
void Frontend::layoutButtons(std::vector<std::pair<std::string, std::string>> items, float top) {
  buttons_.clear();
  int sw, sh;
  r_.outputSize(sw, sh);
  float w = 520, h = 44;
  for (size_t i = 0; i < items.size(); ++i) {
    Button b;
    b.r = {(sw - w) / 2, top + float(i) * (h + 10), w, h};
    b.label = items[i].first;
    b.go = items[i].second;
    buttons_.push_back(b);
  }
}

void Frontend::navigate(const std::string& go) {
  int sw, sh;
  r_.outputSize(sw, sh);
  hover_ = -1;
  if (go == "title") {
    screen_ = Screen::Title;
    layoutButtons({{"CAMPAIGN", "chapters"}, {"SKIRMISH (TEST MAP)", "skirmish"}, {"QUIT", "quit"}}, sh * 0.55f);
  } else if (go == "chapters") {
    screen_ = Screen::Chapters;
    std::vector<std::pair<std::string, std::string>> items;
    for (auto& c : chapters_) items.push_back({upper(c.name), "missions:" + c.start});
    items.push_back({"BACK", "title"});
    layoutButtons(items, sh * 0.35f);
  } else if (go.rfind("missions:", 0) == 0 || go.rfind("diff:", 0) == 0) {
    if (go.rfind("missions:", 0) == 0) chapter_ = go.substr(9);
    else difficulty_ = go.substr(5) == "normal" ? "" : go.substr(5);
    screen_ = Screen::Missions;
    // Follow the "next mission" chain from the chapter start.
    std::vector<std::pair<std::string, std::string>> items;
    bool training = chapter_.rfind("bc", 0) == 0;
    if (training) {
      for (auto& m : missions_)
        if (m.id.rfind("bc", 0) == 0 && m.id != "bc1menu76")
          items.push_back({(done_.count(m.id) ? "* " : "  ") + upper(m.title), "brief:" + m.id});
      std::sort(items.begin(), items.end(), [](auto& a, auto& b) { return a.second < b.second; });
    } else {
      std::string id = chapter_;
      std::set<std::string> seen;
      while (!id.empty() && !seen.count(id) && items.size() < 20) {
        bool otherChapter = false;
        for (auto& c : chapters_) otherChapter |= (c.start == id && id != chapter_);
        if (otherChapter) break;
        seen.insert(id);
        std::string withDiff = id + difficulty_;
        const MissionInfo* m = mission(withDiff);
        if (!m) m = mission(id);
        if (!m) break;
        items.push_back({(done_.count(m->id) ? "* " : "  ") + upper(m->title), "brief:" + m->id});
        const MissionInfo* base = mission(id);
        id = base ? base->next : "";
      }
    }
    layoutButtons(items, 150);
    for (auto& b : buttons_) b.r.h = 34, b.r.y = 150 + float(&b - &buttons_[0]) * 38;
    if (!training) {
      const char* diffs[3] = {"easy", "normal", "hard"};
      for (int i = 0; i < 3; ++i) {
        Button b;
        b.r = {float(sw) / 2 - 300 + i * 205.0f, 90, 190, 40};
        std::string cur = difficulty_.empty() ? "normal" : difficulty_;
        b.label = (cur == diffs[i] ? "> " : "") + upper(diffs[i]);
        b.go = std::string("diff:") + diffs[i];
        buttons_.push_back(b);
      }
    }
    Button back;
    back.r = {30, float(sh) - 70, 200, 44};
    back.label = "BACK";
    back.go = "chapters";
    buttons_.push_back(back);
  } else if (go.rfind("brief:", 0) == 0) {
    current_ = go.substr(6);
    screen_ = Screen::Briefing;
    buttons_.clear();
    Button play, back;
    play.r = {float(sw) - 330, float(sh) - 80, 300, 50};
    play.label = "PLAY MISSION";
    play.action = {FrontendAction::Play, current_};
    back.r = {30, float(sh) - 80, 200, 50};
    back.label = "BACK";
    back.go = "missions:" + chapter_;
    buttons_ = {play, back};
  } else if (go == "result") {
    screen_ = Screen::Result;
    buttons_.clear();
    const MissionInfo* m = mission(current_);
    Button a, b;
    a.r = {float(sw) - 330, float(sh) - 80, 300, 50};
    b.r = {30, float(sh) - 80, 260, 50};
    b.label = "MISSION LIST";
    b.go = "missions:" + chapter_;
    if (lastWon_ && m && !m->next.empty() && mission(m->next)) {
      a.label = "NEXT MISSION";
      a.go = "brief:" + m->next;
    } else {
      a.label = lastWon_ ? "PLAY AGAIN" : "TRY AGAIN";
      a.action = {FrontendAction::Play, current_};
    }
    buttons_ = {a, b};
  } else if (go == "pause") {
    screen_ = Screen::Pause;
    layoutButtons({{"RESUME", "resume"}, {"RESTART MISSION", "restart"}, {"QUIT TO MENU", "menu"}}, sh * 0.4f);
  }
}

FrontendAction Frontend::handle(const SDL_Event& e) {
  if (e.type == SDL_MOUSEMOTION) {
    hover_ = -1;
    for (size_t i = 0; i < buttons_.size(); ++i) {
      auto& b = buttons_[i].r;
      if (e.motion.x >= b.x && e.motion.x <= b.x + b.w && e.motion.y >= b.y && e.motion.y <= b.y + b.h)
        hover_ = int(i);
    }
  }
  if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
    if (screen_ == Screen::Pause) {
      navigate("title");
      return {FrontendAction::Resume, ""};
    }
    if (screen_ == Screen::Title) return {FrontendAction::Quit, ""};
    navigate(screen_ == Screen::Briefing ? "missions:" + chapter_ : "title");
  }
  if (e.type == SDL_QUIT) return {FrontendAction::Quit, ""};
  if (e.type != SDL_MOUSEBUTTONUP || e.button.button != SDL_BUTTON_LEFT) return {};
  for (auto& b : buttons_) {
    if (e.button.x < b.r.x || e.button.x > b.r.x + b.r.w || e.button.y < b.r.y || e.button.y > b.r.y + b.r.h)
      continue;
    if (b.action.kind != FrontendAction::None) return b.action;
    if (b.go == "quit") return {FrontendAction::Quit, ""};
    if (b.go == "skirmish") return {FrontendAction::Skirmish, ""};
    if (b.go == "resume") {
      navigate("title");
      return {FrontendAction::Resume, ""};
    }
    if (b.go == "restart") {
      navigate("title");
      return {FrontendAction::Restart, ""};
    }
    if (b.go == "menu") {
      navigate("title");
      return {FrontendAction::QuitToMenu, ""};
    }
    navigate(b.go);
    return {};
  }
  return {};
}

void Frontend::drawBackground(bool dim) {
  SDL_Renderer* ren = r_.sdl();
  int sw, sh;
  r_.outputSize(sw, sh);
  SDL_SetRenderDrawColor(ren, 10, 12, 24, 255);
  if (screen_ != Screen::Pause) SDL_RenderClear(ren);
  if (bg_ && screen_ != Screen::Pause) {
    // Cover the window, keeping aspect.
    float s = std::max(float(sw) / bgW_, float(sh) / bgH_);
    SDL_FRect d{(sw - bgW_ * s) / 2, (sh - bgH_ * s) / 2, bgW_ * s, bgH_ * s};
    SDL_RenderCopyF(ren, bg_, nullptr, &d);
  }
  if (dim) {
    SDL_SetRenderDrawColor(ren, 8, 8, 18, screen_ == Screen::Pause ? 150 : 185);
    SDL_FRect all{0, 0, float(sw), float(sh)};
    SDL_RenderFillRectF(ren, &all);
  }
}

void Frontend::drawPanelText(float x, float y, float w, float scale, const std::string& text, SDL_Color c, float maxY) {
  drawWrapped(r_.sdl(), x, y, w, scale, text, c, maxY);
}

void Frontend::draw() {
  SDL_Renderer* ren = r_.sdl();
  int sw, sh;
  r_.outputSize(sw, sh);
  bool dim = screen_ != Screen::Title;
  drawBackground(dim);
  if (screen_ == Screen::Chapters) {
    drawText(ren, sw / 2.0f - 7 * 18, sh * 0.2f, 6, "CAMPAIGN", kGold);
  } else if (screen_ == Screen::Missions) {
    for (auto& c : chapters_)
      if (c.start == chapter_) drawText(ren, 40, 40, 4, upper(c.name), kGold);
    drawText(ren, float(sw) - 380, 50, 2, "* = COMPLETED", kGrey);
  } else if (screen_ == Screen::Briefing || screen_ == Screen::Result) {
    const MissionInfo* m = mission(current_);
    if (m) {
      float x = std::max(40.0f, sw / 2.0f - 560), w = std::min(1120.0f, sw - 80.0f);
      std::string head = screen_ == Screen::Result ? (lastWon_ ? "SUCCESS!" : "FAILURE!") : upper(m->title);
      drawText(ren, x, 50, 5, head, screen_ == Screen::Result ? (lastWon_ ? kGreen : SDL_Color{255, 90, 80, 255}) : kGold);
      std::string body = screen_ == Screen::Briefing ? m->briefing : (lastWon_ ? m->success : m->failure);
      if (screen_ == Screen::Result && !resultText_.empty()) body = resultText_;
      // Drop a repeated title line at the top of the text.
      auto nl = body.find('\n');
      if (nl != std::string::npos && upper(body.substr(0, nl)) == upper(m->title)) body = body.substr(nl);
      if (body.rfind("Success!", 0) == 0 || body.rfind("Failure!", 0) == 0) body = body.substr(8);
      drawPanelText(x, 120, w, 2.5f, body, kWhite, float(sh) - 110);
      if (screen_ == Screen::Briefing) {
        std::string who;
        for (int p = 2; p <= 8; ++p)
          if (m->players[size_t(p)].present && !m->players[size_t(p)].name.empty())
            who += (who.empty() ? "" : "   ") + upper(m->players[size_t(p)].name);
        if (!who.empty()) drawText(ren, x, float(sh) - 130, 2, "OPPONENTS/ALLIES: " + who, kGrey);
      }
    }
  } else if (screen_ == Screen::Pause) {
    drawText(ren, sw / 2.0f - 6 * 18, sh * 0.25f, 6, "PAUSED", kGold);
  }
  for (size_t i = 0; i < buttons_.size(); ++i) {
    auto& b = buttons_[i];
    bool hot = int(i) == hover_;
    SDL_SetRenderDrawColor(ren, hot ? 60 : 22, hot ? 50 : 22, hot ? 30 : 34, 215);
    SDL_RenderFillRectF(ren, &b.r);
    SDL_SetRenderDrawColor(ren, 190, 160, 100, 255);
    SDL_RenderDrawRectF(ren, &b.r);
    float scale = b.r.h >= 44 ? 3 : 2;
    float tw = float(b.label.size()) * 6 * scale;
    drawText(ren, b.r.x + (b.r.w - tw) / 2, b.r.y + (b.r.h - 7 * scale) / 2, scale, b.label, hot ? kGold : kWhite);
  }
}

}  // namespace ns
