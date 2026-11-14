#include "data/Palette.h"

namespace ns {

std::optional<Palette> Palette::fromCol(const Bytes& b) {
  if (b.size() < 8 + 768) return std::nullopt;
  Palette p;
  for (int i = 0; i < 256; ++i) p.c[i] = {b[8 + i * 3], b[8 + i * 3 + 1], b[8 + i * 3 + 2]};
  return p;
}

std::optional<Palette> Palette::fromDib(const Bytes& b) {
  try {
    uint32_t hs = rd<uint32_t>(b, 0);
    uint16_t bpp = rd<uint16_t>(b, 14);
    if (bpp != 8) return std::nullopt;
    uint32_t used = rd<uint32_t>(b, 32);
    if (used == 0 || used > 256) used = 256;
    Palette p;
    for (uint32_t i = 0; i < used; ++i) {
      size_t o = hs + i * 4;
      p.c[i] = {rd<uint8_t>(b, o + 2), rd<uint8_t>(b, o + 1), rd<uint8_t>(b, o)};
    }
    return p;
  } catch (const DataError&) {
    return std::nullopt;
  }
}

RemapTables RemapTables::fromColorDat(const Bytes& b) {
  RemapTables r;
  for (size_t off = 0; off + 256 <= b.size(); off += 256) {
    std::array<uint8_t, 256> t;
    std::memcpy(t.data(), b.data() + off, 256);
    r.tables.push_back(t);
  }
  return r;
}

}  // namespace ns
