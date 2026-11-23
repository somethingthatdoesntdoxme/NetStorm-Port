#pragma once
// Mission settings + texts from "<mission>.english" (decrypted from netstorm.tarc).
//
//   [Header]   missionType, myStartMoney, myTech="a;b;c", title,
//              aiName/aiNName, aiNTech, aiNStartMoney, aiNCollectors,
//              aiNTimeBetweenMoves, aiNAllyList="1;3", aiNColor, myAllyList
//   [A.]       briefing (HTML-ish), $Button=Label,Action,Arg lines
//   [Succeeded][BadTeamDead]  success text, $Button=Next Mission,MissionBegin,<Next>
//   [Failed]   failure text
// Player 1 is you; "ai" and "ai2" are player 2, "aiN" is player N.
#include <array>
#include <map>
#include <string>
#include <vector>

namespace ns {

struct MissionPlayer {
  bool present = false;
  std::string name;
  std::vector<std::string> tech;  // empty = everything
  float startMoney = 3000;
  int collectors = 2;
  float timeBetweenMoves = 3;
  std::vector<int> allies;
  std::string colour;  // "purple", "orange", ...
};

struct MissionInfo {
  std::string id;        // file base name, e.g. "rainvsraineasy"
  std::string fort;      // fort file base, e.g. "rainvsrain"
  std::string title;
  std::string type;
  std::string briefing, success, failure;  // plain text, paragraphs separated by blank lines
  std::string next;      // next mission id (lower-case) or ""
  std::array<MissionPlayer, 9> players;    // [1] = you

  // Parses the .english text. `id` is the file base name.
  static MissionInfo parse(const std::string& text, const std::string& id);
};

// Plain-text conversion of the original's HTML-ish markup.
std::string missionTextToPlain(const std::string& html);

}  // namespace ns
