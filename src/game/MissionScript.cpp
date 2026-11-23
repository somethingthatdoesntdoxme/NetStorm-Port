#include "game/MissionScript.h"

#include <cstdlib>

namespace ns {

namespace {
int templeHp(const Sim& sim, int p) {
  int hp = 0;
  for (auto& e : sim.entities())
    if (e.alive && e.owner == p && e.role == Role::Temple) hp += e.hp;
  return hp;
}
const Entity* priestOf(const Sim& sim, int p) {
  for (auto& e : sim.entities())
    if (e.alive && e.owner == p && e.role == Role::Priest) return &e;
  return nullptr;
}
bool captured(const Entity* pr) { return pr && (pr->carriedBy >= 0 || pr->chainedAt >= 0); }
}  // namespace

void MissionScript::reset(const MissionInfo* info, const Sim& sim, int me) {
  info_ = info;
  me_ = me;
  fired_.clear();
  for (int p = 0; p <= 8; ++p) {
    templeHp0_[size_t(p)] = templeHp(sim, p);
    wasCaptured_[size_t(p)] = captured(priestOf(sim, p));
  }
}

bool MissionScript::triggered(const std::string& key, const Sim& sim, bool& isTrigger) {
  if (key.size() > 1 && key[0] == '@') {
    isTrigger = true;
    return sim.time() >= std::atof(key.c_str() + 1);
  }
  if (key.rfind("ai", 0) != 0) return false;
  size_t p = 2;
  while (p < key.size() && std::isdigit(static_cast<unsigned char>(key[p]))) ++p;
  int n = p > 2 ? std::atoi(key.substr(2, p - 2).c_str()) : 2;
  if (n < 1 || n > 8) return false;
  std::string what = key.substr(p);
  const Entity* pr = priestOf(sim, n);
  if (captured(pr)) wasCaptured_[size_t(n)] = true;
  if (what == "priestcaptured") return isTrigger = true, captured(pr);
  if (what == "priestdead") return isTrigger = true, !pr || sim.eliminated(n);
  if (what == "priestsaved") return isTrigger = true, wasCaptured_[size_t(n)] && pr && !captured(pr);
  if (what == "templedead") return isTrigger = true, templeHp0_[size_t(n)] > 0 && templeHp(sim, n) == 0;
  if (what == "templehalf")
    return isTrigger = true, templeHp0_[size_t(n)] > 0 && templeHp(sim, n) * 2 <= templeHp0_[size_t(n)];
  return false;
}

std::vector<ScriptEvent> MissionScript::step(const Sim& sim) {
  std::vector<ScriptEvent> out;
  if (!info_) return out;
  for (size_t i = 0; i < info_->script.size(); ++i) {
    if (fired_.count(i)) continue;
    auto& sec = info_->script[i];
    bool any = false, fire = false;
    int outcome = 0;
    for (auto& k : sec.keys) {
      if (k == "succeeded") outcome = 1;
      if (k == "failed") outcome = -1;
      bool isTrig = false;
      if (triggered(k, sim, isTrig)) fire = true;
      any |= isTrig;
    }
    if (!any || !fire) continue;
    fired_.insert(i);
    out.push_back({&sec, outcome});
  }
  return out;
}

}  // namespace ns
