#pragma once
// Tiny dependency-free PNG writer (RGBA8, stored/uncompressed deflate).
// Only used by tools for dumping sprites -- size isn't a concern.
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace ns {

inline uint32_t crc32(const uint8_t* d, size_t n, uint32_t c = 0xffffffffu) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t v = i;
      for (int k = 0; k < 8; ++k) v = (v & 1) ? 0xedb88320u ^ (v >> 1) : v >> 1;
      table[i] = v;
    }
    init = true;
  }
  for (size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xff] ^ (c >> 8);
  return c;
}

inline bool writePng(const std::filesystem::path& path, int w, int h, const std::vector<uint8_t>& rgba) {
  std::vector<uint8_t> raw;
  raw.reserve(size_t(h) * (w * 4 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba.begin() + size_t(y) * w * 4, rgba.begin() + size_t(y + 1) * w * 4);
  }
  std::vector<uint8_t> z{0x78, 0x01};
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  size_t pos = 0;
  while (pos < raw.size() || raw.empty()) {
    size_t n = std::min<size_t>(65535, raw.size() - pos);
    bool last = pos + n >= raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back(n & 0xff);
    z.push_back(n >> 8);
    z.push_back(~n & 0xff);
    z.push_back((~n >> 8) & 0xff);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
    if (last) break;
  }
  uint32_t ad = (b << 16) | a;
  for (int s = 24; s >= 0; s -= 8) z.push_back((ad >> s) & 0xff);

  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  auto be32 = [&](uint32_t v) {
    uint8_t x[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    f.write(reinterpret_cast<char*>(x), 4);
  };
  auto chunk = [&](const char* type, const std::vector<uint8_t>& d) {
    be32(uint32_t(d.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), d.begin(), d.end());
    f.write(reinterpret_cast<const char*>(td.data()), std::streamsize(td.size()));
    be32(crc32(td.data(), td.size()) ^ 0xffffffffu);
  };
  const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  f.write(reinterpret_cast<const char*>(sig), 8);
  std::vector<uint8_t> ihdr = {uint8_t(w >> 24), uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w),
                               uint8_t(h >> 24), uint8_t(h >> 16), uint8_t(h >> 8), uint8_t(h),
                               8, 6, 0, 0, 0};
  chunk("IHDR", ihdr);
  chunk("IDAT", z);
  chunk("IEND", {});
  return bool(f);
}

}  // namespace ns
