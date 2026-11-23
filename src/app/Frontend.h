#pragma once
// Menus around the game: title, campaign/difficulty select, mission list,
// briefing, results, in-game pause menu. Uses the original's own title art
// (d/titleMenu25.gif) and mission texts.
#include <SDL.h>

#include <set>
#include <string>
#include <vector>

#include "data/GameData.h"
#include "data/MissionInfo.h"
#include "render/Renderer.h"

namespace ns {

struct FrontendAction {
  enum Kind { None, Play, Skirmish, Resume, Restart, QuitToMenu, Quit } kind = None;
  std::string mission;  // for Play
};

class Frontend {
 public:
  enum class Screen { Title, Chapters, Missions, Briefing, Result, Pause };

  Frontend(Renderer& r, const GameData& gd);
  ~Frontend();

  FrontendAction handle(const SDL_Event& e);
  void draw();
  Screen screen() const { return screen_; }

  void showTitle() { navigate("title"); }
  void showPause() { navigate("pause"); }
  void showResult(const std::string& missionId, bool won);
  const MissionInfo* mission(const std::string& id) const;
  void markDone(const std::string& id);

 private:
  struct Button {
    SDL_FRect r;
    std::string label;
    FrontendAction action;
    std::string go;  // navigation target ("chapters", "missions:<chapter>", "brief:<id>", "back")
  };
  void layoutButtons(std::vector<std::pair<std::string, std::string>> items, float top);
  void drawBackground(bool dim);
  void drawPanelText(float x, float y, float w, float scale, const std::string& text, SDL_Color c, float maxY);
  void navigate(const std::string& go);
  void loadProgress();
  void saveProgress();

  Renderer& r_;
  const GameData& gd_;
  SDL_Texture* bg_ = nullptr;
  int bgW_ = 0, bgH_ = 0;
  Screen screen_ = Screen::Title;
  std::vector<MissionInfo> missions_;
  struct Chapter {
    std::string name, start;
  };
  std::vector<Chapter> chapters_;
  std::string chapter_, difficulty_ = "", current_;
  bool lastWon_ = false;
  std::set<std::string> done_;
  std::vector<Button> buttons_;
  int hover_ = -1;
  int scroll_ = 0;
};

}  // namespace ns
