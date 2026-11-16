#include "data/FortFile.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

#include "data/TypeRegistry.h"

namespace ns {

uint32_t fortNameHash(const std::string& name) {
  // Matches the engine (template.cpp): each byte added at shift (i&3)*8.
  uint32_t h = 0;
  for (size_t i = 0; i < name.size(); ++i) h += uint32_t(uint8_t(name[i])) << ((i & 3) * 8);
  return h;
}

namespace {

// Type flag bits as the engine stores them (RiftType.cpp). f1 = +0xe8, f2 = +0xec.
struct Bits {
  uint32_t f1 = 0, f2 = 0;
};

Bits flagBits(const TypeDef& t) {
  static const std::unordered_map<std::string, uint32_t> F1 = {
      {"carribleinvehicle", 0x10000}, {"container", 0x20000}, {"defaulthotspot", 1}, {"shadow", 0x40000},
      {"notselectable", 0x8000000},   {"createsisland", 0x400}, {"maydroponrim", 4},  {"maydroponisle", 2},
      {"surface", 0x800},             {"randframe", 0x100000},  {"matchframe", 0x200000},
      {"opaquecollide", 0x800000},    {"dontsave", 0x20000000}, {"saveframe", 0x20},  {"saveqa", 0x1000},
      {"saveqb", 0x2000}};
  static const std::unordered_map<std::string, uint32_t> F2 = {
      {"notreal", 0x2000000},  {"dropblocking", 0x10},  {"blocking", 0x18},       {"shotblocking", 0x20},
      {"fencepost", 1},        {"fence", 0x20000000},   {"buried", 0x2000},       {"nugget", 0x8000},
      {"bomb", 0x100},         {"emplacement", 0x40000}, {"bridge", 4},           {"walker", 0x10000},
      {"balloon", 0x20000},    {"island", 2},           {"islandthreebythree", 0x1000000},
      {"factory", 0x4000},     {"vortex", 0x200},       {"edgefarm", 0x80000},    {"guy", 0x400},
      {"geyser", 0x10000000},  {"flyer", 0x100000},     {"residence", 0x40000000}, {"fringe", 0x1000},
      {"priest", 0x200000},    {"dais", 0x400000},      {"altar", 0x80000000u}};
  static const std::unordered_map<std::string, int> Groups = {
      {"archer", 0}, {"cannon", 1}, {"blocker", 2}, {"aviary", 3},   {"flyer", 4},      {"battery", 5},
      {"fence", 6},  {"walker", 7}, {"balloon", 8}, {"misc", 9},     {"anti-air", 10}};
  Bits b;
  for (auto f : t.flags) {
    std::string k;
    for (char c : f)
      if (c != '_') k += char(std::tolower(static_cast<unsigned char>(c)));
    if (auto it = F1.find(k); it != F1.end()) b.f1 |= it->second;
    if (auto it = F2.find(k); it != F2.end()) b.f2 |= it->second;
  }
  std::string g = t.str("group");
  std::transform(g.begin(), g.end(), g.begin(), [](unsigned char c) { return std::tolower(c); });
  int grp = Groups.count(g) ? Groups.at(g) : 11;
  // Implicit flags (RiftType.cpp 0x4c6347).
  if (grp == 5) b.f2 |= 0x800;
  if (grp == 0 || grp == 1) b.f2 |= 0x8000000;
  if (grp == 2) b.f2 |= 0x4000000;
  if (b.f2 & 0x34200) b.f1 |= 0x20000;  // vortex/factory/walker/balloon carry units
  if (b.f2 & 0x2000) b.f1 |= 0x1000;    // buried things save QA
  return b;
}

constexpr uint32_t kOwnerMask = 0x5d77cf00;  // f2 bits that save an owner byte

struct Reader {
  const uint8_t* d;
  size_t n, p = 0;
  bool ok = true;
  int u8() {
    if (p >= n) {
      ok = false;
      return 0;
    }
    return d[p++];
  }
  int u16() {
    int a = u8(), b = u8();
    return a | (b << 8);
  }
};

}  // namespace

bool FortFile::parse(const std::vector<uint8_t>& b, const std::vector<TypeDef>& types, std::string* err) {
  auto fail = [&](const std::string& m) {
    if (err) *err = m;
    return false;
  };
  if (b.size() < 4 || b[0] != 'F') return fail("not a fort file");

  // Sections.
  std::vector<std::pair<size_t, size_t>> sec;  // payload offset, length
  for (size_t p = 2; p + 2 <= b.size();) {
    size_t len = b[p] | (b[p + 1] << 8);
    if (len < 2 || p + len > b.size()) break;
    sec.push_back({p + 2, len - 2});
    p += len;
  }
  if (sec.size() < 35) return fail("too few sections (" + std::to_string(sec.size()) + ")");
  auto S = [&](int i) { return Reader{b.data() + sec[i].first, sec[i].second}; };

  // Name: section 0 = 4 bytes, u16 length, chars.
  {
    auto [off, len] = sec[0];
    if (len > 6) name.assign(reinterpret_cast<const char*>(b.data() + off + 6), len - 6);
    name.erase(std::find(name.begin(), name.end(), '\0'), name.end());
  }
  if (sec[10].second >= 4) std::memcpy(&money, b.data() + sec[10].first, 4);

  // Type name hashes -> names.
  std::unordered_map<uint32_t, const TypeDef*> byHash;
  std::unordered_map<uint32_t, std::string> nameByHash;
  // Saved hashes use the engine's own spelling of each name ("residence"),
  // which can differ in case from the script's typename ("Residence").
  auto lower = [](std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
  };
  std::unordered_map<std::string, const TypeDef*> defByLower;
  for (auto& t : types) defByLower[lower(t.name)] = &t;
  auto add = [&](const std::string& n) {
    uint32_t h = fortNameHash(n);
    auto it = defByLower.find(lower(n));
    nameByHash.emplace(h, it != defByLower.end() ? it->second->name : n);
    if (it != defByLower.end()) byHash.emplace(h, it->second);
  };
  for (auto& n : engineTypeNames()) add(n);
  for (auto& t : types) add(t.name);
  std::vector<const TypeDef*> idDef;
  std::vector<Bits> idBits;
  {
    Reader r = S(7);
    int n = r.u8();
    for (int i = 0; i < n; ++i) {
      uint32_t h = uint32_t(r.u16()) | (uint32_t(r.u16()) << 16);
      auto it = nameByHash.find(h);
      typeNames.push_back(it == nameByHash.end() ? std::string() : it->second);
      auto dt = byHash.find(h);
      idDef.push_back(dt == byHash.end() ? nullptr : dt->second);
      idBits.push_back(dt == byHash.end() ? Bits{} : flagBits(*dt->second));
      if (h && it == nameByHash.end()) ++unknownTypes;
    }
  }
  auto nameOf = [&](int id) { return id < int(typeNames.size()) ? typeNames[id] : std::string(); };
  auto bitsOf = [&](int id) { return id < int(idBits.size()) ? idBits[id] : Bits{}; };

  auto readSection = [&](int index, FortSection& out) -> bool {
    Reader r = S(index);
    if (r.n == 0) return true;
    out.version = r.u8();
    int chunk = 0;
    while (r.p < r.n && r.ok) {
      int marker = r.u8();
      if (marker == 99) {
        int count = r.u16();
        for (int k = 0; k < count && r.ok; ++k) {
          FortObject o;
          int pos = r.u8(), t = r.u8();
          o.chunk = chunk;
          o.x = (pos >> 4);
          o.y = (pos & 15);
          if (t >= 246) {
            o.landMarker = true;
            o.player = 255 - t;
            o.value = r.u8();
            out.objects.push_back(o);
            continue;
          }
          if (t == 0) {
            r.u8();
            continue;
          }
          o.type = nameOf(t);
          Bits fb = bitsOf(t);
          if (fb.f1 & 0x20) o.frame = r.u8();
          if (fb.f1 & 0x1000) o.qa = r.u8();
          if (fb.f1 & 0x2000) o.qb = r.u16();
          if (fb.f2 & 4) o.bridgePiece = r.u8();
          if (out.version >= 2 && (fb.f2 & 0x4000)) o.factorySlot = r.u8();
          if (out.version > 0 && (fb.f2 & kOwnerMask)) o.owner = r.u8();
          if (fb.f1 & 0x20000) {
            int nc = r.u8();
            for (int c = 0; c < nc && r.ok; ++c) {
              int t2 = r.u8();
              Bits cb = bitsOf(t2);
              if (cb.f1 & 0x1000) r.u8();
              if (cb.f1 & 0x2000) r.u16();
              if (!(cb.f2 & 0x100)) r.u8();
              o.contents.push_back(nameOf(t2));
            }
          }
          out.objects.push_back(std::move(o));
        }
      }
      ++chunk;
    }
    out.chunkCount = chunk;
    return r.ok && r.p == r.n;
  };

  if (!readSection(4, chaff)) return fail("could not decode the world section");
  for (int i = 0; i < 20; ++i) {
    FortSection s;
    if (!readSection(15 + i, s)) return fail("could not decode territory " + std::to_string(i));
    territories.push_back(std::move(s));
  }
  {
    Reader r = S(6);
    for (int i = 0; i < 20 && r.p + 6 <= r.n; ++i) {
      TerritoryRecord t;
      t.a = uint8_t(r.u8());
      t.flags = uint8_t(r.u8());
      t.b = uint8_t(r.u8());
      t.c = uint8_t(r.u8());
      r.u16();
      territoryRecords.push_back(t);
    }
  }
  return true;
}

}  // namespace ns
