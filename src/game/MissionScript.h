#pragma once
// Runs a mission's scripted events: timed notes ([@120]) and triggers on an
// AI's priest/temple ([Ai2PriestCaptured], [AiTempleHalf], [AiPriestDead],
// [AiTempleDead], [Ai3PriestSaved]). A section that also carries [Succeeded]
// or [Failed] ends the mission with its own text.
#include <set>
#include <vector>

#include "data/MissionInfo.h"
#include "game/Sim.h"

namespace ns {

struct ScriptEvent {
  const ScriptSection* section = nullptr;
  int outcome = 0;  // +1 forces a win, -1 a loss, 0 = just a note
};

class MissionScript {
 public:
  void reset(const MissionInfo* info, const Sim& sim, int me);
  std::vector<ScriptEvent> step(const Sim& sim);

 private:
  bool triggered(const std::string& key, const Sim& sim, bool& isTrigger);
  const MissionInfo* info_ = nullptr;
  int me_ = 1;
  std::set<size_t> fired_;
  std::array<int, 9> templeHp0_{};
  std::array<bool, 9> wasCaptured_{};
};

}  // namespace ns
