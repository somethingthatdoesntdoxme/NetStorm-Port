#include "data/TypeFile.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace ns {

namespace {
std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}
std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}
std::string stripComments(const std::string& t) {
  std::string out;
  out.reserve(t.size());
  bool inStr = false;
  for (size_t i = 0; i < t.size(); ++i) {
    char c = t[i];
    if (c == '"') inStr = !inStr;
    if (!inStr && c == '/' && i + 1 < t.size() && t[i + 1] == '/') {
      while (i < t.size() && t[i] != '\n') ++i;
      out += '\n';
      continue;
    }
    if (!inStr && c == '/' && i + 1 < t.size() && t[i + 1] == '*') {
      i += 2;
      while (i + 1 < t.size() && !(t[i] == '*' && t[i + 1] == '/')) ++i;
      ++i;
      continue;
    }
    if (c == '\n') inStr = false;  // unterminated strings never span lines
    out += c;
  }
  return out;
}
std::vector<std::string> words(const std::string& s) {
  std::istringstream is(s);
  std::vector<std::string> w;
  std::string x;
  while (is >> x) w.push_back(x);
  return w;
}
bool isFrameId(const std::string& s, std::string& cluster, int& var) {
  size_t i = 0;
  while (i < s.size() && std::isupper(static_cast<unsigned char>(s[i]))) ++i;
  if (i == 0 || i + 2 != s.size()) return false;
  if (!std::isdigit(static_cast<unsigned char>(s[i])) || !std::isdigit(static_cast<unsigned char>(s[i + 1])))
    return false;
  cluster = s.substr(0, i);
  var = (s[i] - '0') * 10 + (s[i + 1] - '0');
  return true;
}
}  // namespace

bool TypeDef::hasFlag(const std::string& f) const {
  auto lf = lower(f);
  return std::any_of(flags.begin(), flags.end(), [&](const std::string& x) { return lower(x) == lf; });
}
std::string TypeDef::str(const std::string& k, const std::string& def) const {
  auto it = props.find(lower(k));
  return it == props.end() ? def : it->second;
}
double TypeDef::num(const std::string& k, double def) const {
  auto it = props.find(lower(k));
  if (it == props.end()) return def;
  char* end = nullptr;
  double v = std::strtod(it->second.c_str(), &end);
  return end == it->second.c_str() ? def : v;
}

TypeDef parseTypeFile(const std::string& raw, const std::string& fileName) {
  TypeDef t;
  t.file = fileName;
  std::string text = stripComments(raw);
  std::istringstream lines(text);
  std::string line;
  bool inBlock = false;
  std::string pending;  // statements can span lines until ';'
  while (std::getline(lines, line)) {
    std::string l = trim(line);
    if (l.empty()) continue;
    if (!inBlock && l.rfind("typename", 0) == 0) {
      auto w = words(l.substr(8));
      if (!w.empty()) {
        t.name = w[0];
        t.nameWords.assign(w.begin() + 1, w.end());
      }
      continue;
    }
    if (!inBlock && l.rfind("typeflags", 0) == 0) {
      std::string rest = l.substr(9);
      rest.erase(std::remove(rest.begin(), rest.end(), ';'), rest.end());
      for (auto& w : words(rest)) t.flags.push_back(w);
      continue;
    }
    if (l[0] == '{') {
      inBlock = true;
      l = trim(l.substr(1));
      if (l.empty()) continue;
    }
    if (inBlock) {
      bool close = false;
      if (auto c = l.find('}'); c != std::string::npos) {
        close = true;
        l = l.substr(0, c);
      }
      pending += l;
      size_t semi;
      while ((semi = pending.find(';')) != std::string::npos) {
        std::string stmt = pending.substr(0, semi);
        pending.erase(0, semi + 1);
        auto eq = stmt.find('=');
        if (eq == std::string::npos) continue;
        std::string k = lower(trim(stmt.substr(0, eq)));
        std::string v = trim(stmt.substr(eq + 1));
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        t.props[k] = v;
      }
      if (close) {
        inBlock = false;
        pending.clear();
      }
      continue;
    }
    // Frame line: ID : label : "x.gif" #n [: "y.gif" #m] ;
    auto colon = l.find(':');
    if (colon == std::string::npos) continue;
    FrameLine fl;
    if (!isFrameId(trim(l.substr(0, colon)), fl.cluster, fl.variation)) continue;
    std::string rest = l.substr(colon + 1);
    if (auto semi = rest.find(';'); semi != std::string::npos) rest = rest.substr(0, semi);
    auto c2 = rest.find(':');
    std::string labelPart = c2 == std::string::npos ? rest : rest.substr(0, c2);
    fl.label = trim(labelPart);
    if (fl.label.find('"') != std::string::npos) fl.label.clear();
    size_t p = 0;
    while ((p = rest.find('"', p)) != std::string::npos) {
      size_t q = rest.find('"', p + 1);
      if (q == std::string::npos) break;
      ImageRef r;
      r.gif = lower(rest.substr(p + 1, q - p - 1));
      size_t h = rest.find('#', q);
      if (h != std::string::npos) r.frame = std::atoi(rest.c_str() + h + 1);
      fl.images.push_back(r);
      p = q + 1;
    }
    t.frames.push_back(std::move(fl));
  }
  return t;
}

}  // namespace ns
