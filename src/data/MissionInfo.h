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

struct ScriptButton {
  std::string label, action, arg;  // action/arg lower-case, e.g. "tell","surrender"
};
// One [Key][Key2]... block of the script. Keys are lower-case without brackets:
// "@120" (seconds in), "ai2priestcaptured", "aitempledead", "failed", "surrender"...
struct ScriptSection {
  std::vector<std::string> keys;
  std::string text;  // plain text
  std::vector<ScriptButton> buttons;
  bool shared = false;  // came from d/tell.english, not the mission's own script
};

struct MissionInfo {
  std::string id;        // file base name, e.g. "rainvsraineasy"
  std::string fort;      // fort file base, e.g. "rainvsrain"
  std::string title;
  std::string type;
  std::string briefing, success, failure;  // plain text, paragraphs separated by blank lines
  std::string next;      // next mission id (lower-case) or ""
  std::array<MissionPlayer, 9> players;    // [1] = you
  std::vector<ScriptSection> script;
  const ScriptSection* section(const std::string& key) const;  // by any of its keys

  // Parses the .english text. `id` is the file base name.
  static MissionInfo parse(const std::string& text, const std::string& id);
};

// Plain-text conversion of the original's HTML-ish markup.
struct GameData;  // must match the definition: MSVC mangles class and struct differently
// Parses d/<id>.english and adds the shared sections from d/tell.english
// (e.g. [TryAgain], used by every mission's failure screen) that the
// mission doesn't define itself.
MissionInfo loadMission(const GameData& gd, const std::string& id);

std::string missionTextToPlain(const std::string& html);

}  // namespace ns
