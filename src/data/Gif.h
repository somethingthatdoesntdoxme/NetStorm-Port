#pragma once
// Minimal GIF decoder (first frame, global/local palette, interlace) for the
// original's menu/title/help art in d/*.gif.
#include "core/BinaryFile.h"

namespace ns {

struct RgbaImage {
  int w = 0, h = 0;
  std::vector<uint8_t> px;  // RGBA8
};

std::optional<RgbaImage> decodeGif(const Bytes& data);

}  // namespace ns
