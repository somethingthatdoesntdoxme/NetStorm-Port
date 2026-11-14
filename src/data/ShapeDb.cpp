#include "data/ShapeDb.h"

namespace ns {

bool ShapeDb::open(const std::filesystem::path& path, std::string* err) {
  auto fail = [&](const std::string& m) {
    if (err) *err = m;
    return false;
  };
  auto file = readFile(path);
  if (!file) return fail("cannot open " + path.string());
  data_ = std::move(*file);
  sets_.clear();
  total_ = 0;
  try {
    size_t p = 0;
    while (p + 8 <= data_.size() && std::memcmp(data_.data() + p, "1.10", 4) == 0) {
      uint32_t n = rd<uint32_t>(data_, p + 4);
      std::vector<uint32_t> frames;
      frames.reserve(n);
      for (uint32_t i = 0; i < n; ++i) {
        uint32_t off = rd<uint32_t>(data_, p + 8 + i * 8);
        size_t abs = p + off;
        if (abs < 36 || abs + 24 > data_.size()) return fail("frame offset out of range");
        frames.push_back(uint32_t(abs));
      }
      total_ += int(n);
      sets_.push_back(std::move(frames));
      p += 8 + size_t(n) * 8;
    }
  } catch (const DataError& e) {
    return fail(e.what());
  }
  if (sets_.empty()) return fail("no shape sets found (wrong file?)");
  return true;
}

FrameHeader ShapeDb::header(int set, int frame) const {
  FrameHeader h{};
  size_t f = sets_.at(set).at(frame);
  std::memcpy(&h, data_.data() + f - 36, sizeof(h));
  return h;
}

ShapeFrame ShapeDb::decode(int set, int frame) const {
  size_t f = sets_.at(set).at(frame);
  ShapeFrame out;
  int x0 = rd<int32_t>(data_, f + 8), y0 = rd<int32_t>(data_, f + 12);
  int x1 = rd<int32_t>(data_, f + 16), y1 = rd<int32_t>(data_, f + 20);
  out.width = x1 - x0 + 1;
  out.height = y1 - y0 + 1;
  out.originX = x0;
  out.originY = y0;
  if (out.width <= 0 || out.height <= 0 || out.width > 4096 || out.height > 4096) {
    out.width = out.height = 0;
    return out;
  }
  out.index.assign(size_t(out.width) * out.height, 0);
  out.mask.assign(size_t(out.width) * out.height, 0);
  const uint8_t* b = data_.data();
  const size_t end = data_.size();
  size_t p = f + 24;
  auto put = [&](int x, int y, uint8_t c) {
    if (x >= 0 && x < out.width) {
      size_t i = size_t(y) * out.width + x;
      out.index[i] = c;
      out.mask[i] = 255;
    }
  };
  for (int y = 0; y < out.height; ++y) {
    int x = 0;
    while (p < end) {
      uint8_t c = b[p++];
      if (c == 0) break;
      if (c == 1) {
        if (p >= end) break;
        x += b[p++];
      } else if ((c & 1) == 0) {
        if (p >= end) break;
        uint8_t v = b[p++];
        for (int i = 0; i < (c >> 1); ++i) put(x++, y, v);
      } else {
        int n = c >> 1;
        for (int i = 0; i < n && p < end; ++i) put(x++, y, b[p++]);
      }
    }
  }
  return out;
}

}  // namespace ns
