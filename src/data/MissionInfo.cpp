#include "data/MissionInfo.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>

namespace ns {

namespace {
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}
std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n\"");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n\";");
  return s.substr(a, b - a + 1);
}
std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) {
      if (!trim(cur).empty()) out.push_back(trim(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!trim(cur).empty()) out.push_back(trim(cur));
  return out;
}
}  // namespace

std::string missionTextToPlain(const std::string& html) {
  std::string out;
  for (size_t i = 0; i < html.size(); ++i) {
    char c = html[i];
    if (c == '<') {
      size_t e = html.find('>', i);
      if (e == std::string::npos) break;
      std::string tag = lower(html.substr(i + 1, e - i - 1));
      if (tag == "p" || tag.rfind("h2", 0) == 0 || tag == "/h2") out += "\n\n";
      else if (tag == "br") out += "\n";
      i = e;
      continue;
    }
    if (c == '\r') continue;
    if (c == '\n' || c == '\t') c = ' ';
    out += c;
  }
  // Collapse spaces, keep paragraph breaks.
  std::string clean;
  int nl = 0;
  bool space = false;
  for (char c : out) {
    if (c == '\n') {
      ++nl;
      space = false;
      continue;
    }
    if (c == ' ') {
      space = true;
      continue;
    }
    if (nl) clean += nl >= 2 ? "\n\n" : "\n";
    else if (space && !clean.empty()) clean += ' ';
    nl = 0;
    space = false;
    clean += c;
  }
  size_t a = clean.find_first_not_of("\n ");
  return a == std::string::npos ? "" : clean.substr(a);
}

MissionInfo MissionInfo::parse(const std::string& text, const std::string& id) {
  MissionInfo m;
  m.id = lower(id);
  m.fort = m.id;
  for (const char* suffix : {"easy", "hard"}) {
    std::string sfx = suffix;
    if (m.fort.size() > sfx.size() && m.fort.compare(m.fort.size() - sfx.size(), sfx.size(), sfx) == 0)
      m.fort = m.fort.substr(0, m.fort.size() - sfx.size());
  }
  m.players[1].present = true;
  m.players[1].name = "You";

  // Split into [Section] blocks.
  std::map<std::string, std::string> sections;
  std::string cur = "header";
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    std::string t = trim(line);
    if (!t.empty() && t[0] == '[' && t.find(']') != std::string::npos) {
      cur = lower(t.substr(1, t.find(']') - 1));
      continue;
    }
    sections[cur] += line + "\n";
  }

  // Header key = value lines.
  std::istringstream hs(sections["header"]);
  while (std::getline(hs, line)) {
    auto cpos = line.find("//");
    if (cpos != std::string::npos) line = line.substr(0, cpos);
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = lower(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
    if (k == "title") m.title = v;
    else if (k == "missiontype") m.type = v;
    else if (k == "mystartmoney") m.players[1].startMoney = float(std::atof(v.c_str()));
    else if (k == "mytech") m.players[1].tech = split(v, ';');
    else if (k == "myallylist") {
      for (auto& a : split(v, ';')) m.players[1].allies.push_back(std::atoi(a.c_str()));
    } else if (k.rfind("ai", 0) == 0) {
      // aiName / ai2Name ... : player index from the digits after "ai".
      size_t p = 2;
      while (p < k.size() && std::isdigit(static_cast<unsigned char>(k[p]))) ++p;
      int idx = p > 2 ? std::atoi(k.substr(2, p - 2).c_str()) : 2;
      if (idx < 1 || idx > 8) continue;
      std::string field = k.substr(p);
      MissionPlayer& pl = m.players[size_t(idx)];
      if (idx != 1) pl.present = true;
      if (field == "name" && idx != 1) pl.name = v;
      else if (field == "tech") pl.tech = split(v, ';');
      else if (field == "startmoney") pl.startMoney = float(std::atof(v.c_str()));
      else if (field == "collectors") pl.collectors = std::atoi(v.c_str());
      else if (field == "timebetweenmoves") pl.timeBetweenMoves = float(std::atof(v.c_str()));
      else if (field == "allylist")
        for (auto& a : split(v, ';')) pl.allies.push_back(std::atoi(a.c_str()));
      else if (field == "color") pl.colour = lower(v);
    }
  }
  if (m.title.empty()) m.title = id;

  auto findSection = [&](const char* prefix) -> std::string {
    for (auto& [k, v] : sections)
      if (k.rfind(prefix, 0) == 0) return v;
    return "";
  };
  auto body = [&](const std::string& s) {
    std::string kept;
    std::istringstream ss(s);
    while (std::getline(ss, line)) {
      std::string t = trim(line);
      if (t.rfind("$Button=", 0) == 0) {
        auto parts = split(t.substr(8), ',');
        if (parts.size() >= 3 && lower(parts[1]) == "missionbegin") m.next = lower(parts[2]);
        continue;
      }
      if (!t.empty() && (t[0] == '$' || t.rfind("<$", 0) == 0)) continue;
      kept += line + "\n";
    }
    return missionTextToPlain(kept);
  };
  m.briefing = body(findSection("a."));
  // Success text, following "Tell" hops (e.g. Succeeded -> VictoryNote),
  // which is also where some missions keep their Next Mission button.
  {
    std::string sec = findSection("succeeded"), all;
    std::set<std::string> seen;
    for (int hop = 0; hop < 4 && !sec.empty(); ++hop) {
      all += sec + "\n<p>\n";
      std::string tell;
      std::istringstream ss(sec);
      while (std::getline(ss, line)) {
        std::string t = trim(line);
        if (t.rfind("$Button=", 0) != 0) continue;
        auto parts = split(t.substr(8), ',');
        if (parts.size() >= 3 && lower(parts[1]) == "tell" && tell.empty()) tell = lower(trim(parts[2]));
      }
      if (tell.empty() || seen.count(tell)) break;
      seen.insert(tell);
      sec.clear();
      for (auto& [k, v] : sections)
        if (k == tell || k.rfind(tell + "]", 0) == 0) sec = v;
    }
    m.success = body(all);
  }
  m.failure = body(findSection("failed"));
  return m;
}

}  // namespace ns
