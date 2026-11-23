#include "data/Gif.h"

namespace ns {

std::optional<RgbaImage> decodeGif(const Bytes& b) {
  if (b.size() < 13 || std::memcmp(b.data(), "GIF8", 4) != 0) return std::nullopt;
  size_t p = 6;
  auto u16 = [&](size_t o) { return int(b[o] | (b[o + 1] << 8)); };
  int sw = u16(p), sh = u16(p + 2);
  uint8_t flags = b[p + 4];
  p += 7;
  std::vector<uint8_t> gpal;
  if (flags & 0x80) {
    size_t n = size_t(3) << ((flags & 7) + 1);
    if (p + n > b.size()) return std::nullopt;
    gpal.assign(b.begin() + p, b.begin() + p + n);
    p += n;
  }
  int transparent = -1;
  while (p < b.size()) {
    uint8_t c = b[p++];
    if (c == 0x21) {  // extension
      uint8_t label = b[p++];
      if (label == 0xF9 && p + 5 < b.size() && (b[p + 1] & 1)) transparent = b[p + 4];
      while (p < b.size() && b[p]) p += b[p] + 1;
      ++p;
      continue;
    }
    if (c != 0x2C) break;  // image descriptor
    if (p + 9 > b.size()) return std::nullopt;
    int ix = u16(p), iy = u16(p + 2), iw = u16(p + 4), ih = u16(p + 6);
    uint8_t iflags = b[p + 8];
    p += 9;
    std::vector<uint8_t> pal = gpal;
    if (iflags & 0x80) {
      size_t n = size_t(3) << ((iflags & 7) + 1);
      pal.assign(b.begin() + p, b.begin() + p + n);
      p += n;
    }
    bool interlace = iflags & 0x40;
    int minCode = b[p++];
    std::vector<uint8_t> data;
    while (p < b.size() && b[p]) {
      data.insert(data.end(), b.begin() + p + 1, b.begin() + p + 1 + b[p]);
      p += b[p] + 1;
    }
    // LZW
    std::vector<uint8_t> idx;
    idx.reserve(size_t(iw) * ih);
    int clear = 1 << minCode, eoi = clear + 1, size = minCode + 1, next = eoi + 1;
    std::vector<int> prefix(4096), suffix(4096), len(4096);
    for (int i = 0; i < clear; ++i) prefix[i] = -1, suffix[i] = i, len[i] = 1;
    int prev = -1;
    size_t bitpos = 0;
    std::vector<uint8_t> stack;
    while (true) {
      if ((bitpos + size) > data.size() * 8) break;
      int code = 0;
      for (int k = 0; k < size; ++k, ++bitpos)
        if (data[bitpos >> 3] & (1 << (bitpos & 7))) code |= 1 << k;
      if (code == clear) {
        size = minCode + 1;
        next = eoi + 1;
        prev = -1;
        continue;
      }
      if (code == eoi) break;
      int cur = code;
      int first;
      stack.clear();
      if (code >= next) {  // KwKwK
        if (prev < 0) break;
        cur = prev;
      }
      while (cur >= 0) {
        stack.push_back(uint8_t(suffix[cur]));
        cur = prefix[cur];
      }
      first = stack.back();
      for (auto it = stack.rbegin(); it != stack.rend(); ++it) idx.push_back(*it);
      if (code >= next) idx.push_back(uint8_t(first));
      if (prev >= 0 && next < 4096) {
        prefix[next] = prev;
        suffix[next] = first;
        ++next;
        if (next == (1 << size) && size < 12) ++size;
      }
      prev = code;
    }
    RgbaImage img;
    img.w = sw;
    img.h = sh;
    img.px.assign(size_t(sw) * sh * 4, 0);
    std::vector<int> rows(ih);
    if (interlace) {
      int r = 0;
      for (int pass = 0; pass < 4; ++pass) {
        static const int start[4] = {0, 4, 2, 1}, step[4] = {8, 8, 4, 2};
        for (int y = start[pass]; y < ih; y += step[pass]) rows[r++] = y;
      }
    } else {
      for (int y = 0; y < ih; ++y) rows[y] = y;
    }
    for (int r = 0; r < ih; ++r)
      for (int x = 0; x < iw; ++x) {
        size_t i = size_t(r) * iw + x;
        if (i >= idx.size()) break;
        int ci = idx[i];
        int X = ix + x, Y = iy + rows[r];
        if (X >= sw || Y >= sh || ci * 3 + 2 >= int(pal.size())) continue;
        uint8_t* d = &img.px[(size_t(Y) * sw + X) * 4];
        d[0] = pal[ci * 3];
        d[1] = pal[ci * 3 + 1];
        d[2] = pal[ci * 3 + 2];
        d[3] = ci == transparent ? 0 : 255;
      }
    return img;
  }
  return std::nullopt;
}

}  // namespace ns
