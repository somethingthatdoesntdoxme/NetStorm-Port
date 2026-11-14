#include "data/PeResources.h"

namespace ns {

namespace {
struct Section {
  uint32_t va, vsize, raw, rawSize;
};

std::optional<size_t> vaToOff(const std::vector<Section>& secs, uint32_t rva) {
  for (auto& s : secs)
    if (rva >= s.va && rva < s.va + std::max(s.vsize, s.rawSize)) return size_t(s.raw) + (rva - s.va);
  return std::nullopt;
}
}  // namespace

std::optional<Bytes> peResource(const Bytes& b, uint32_t type, uint32_t id) {
  try {
    if (b.size() < 0x40 || b[0] != 'M' || b[1] != 'Z') return std::nullopt;
    uint32_t pe = rd<uint32_t>(b, 0x3c);
    if (rd<uint32_t>(b, pe) != 0x00004550) return std::nullopt;
    uint16_t nsec = rd<uint16_t>(b, pe + 6);
    uint16_t optSize = rd<uint16_t>(b, pe + 20);
    size_t opt = pe + 24;
    bool pe32plus = rd<uint16_t>(b, opt) == 0x20b;
    size_t dirs = opt + (pe32plus ? 112 : 96);
    uint32_t resRva = rd<uint32_t>(b, dirs + 2 * 8);
    std::vector<Section> secs;
    size_t sh = opt + optSize;
    for (int i = 0; i < nsec; ++i, sh += 40)
      secs.push_back({rd<uint32_t>(b, sh + 12), rd<uint32_t>(b, sh + 8), rd<uint32_t>(b, sh + 20),
                      rd<uint32_t>(b, sh + 16)});
    auto root = vaToOff(secs, resRva);
    if (!root) return std::nullopt;

    // Find an entry with a given numeric id in a directory; returns its offset field.
    auto findId = [&](size_t dir, uint32_t want, bool any) -> std::optional<uint32_t> {
      uint16_t named = rd<uint16_t>(b, dir + 12), ids = rd<uint16_t>(b, dir + 14);
      size_t e = dir + 16;
      for (int i = 0; i < named + ids; ++i, e += 8) {
        uint32_t name = rd<uint32_t>(b, e), off = rd<uint32_t>(b, e + 4);
        if (any || (!(name & 0x80000000) && name == want)) return off;
      }
      return std::nullopt;
    };
    auto t = findId(*root, type, false);
    if (!t || !(*t & 0x80000000)) return std::nullopt;
    auto n = findId(*root + (*t & 0x7fffffff), id, false);
    if (!n || !(*n & 0x80000000)) return std::nullopt;
    auto l = findId(*root + (*n & 0x7fffffff), 0, true);
    if (!l || (*l & 0x80000000)) return std::nullopt;
    size_t de = *root + *l;
    uint32_t dataRva = rd<uint32_t>(b, de), size = rd<uint32_t>(b, de + 4);
    auto off = vaToOff(secs, dataRva);
    if (!off || *off + size > b.size()) return std::nullopt;
    return Bytes(b.begin() + *off, b.begin() + *off + size);
  } catch (const DataError&) {
    return std::nullopt;
  }
}

}  // namespace ns
