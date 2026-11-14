#pragma once
// Sprite database "d/_shapes.shp".
//
// The original memory-maps this file and uses it in place. Layout:
//   A run of "sets" at the start of the file, back to back:
//     char  magic[4] = "1.10"
//     u32   frameCount
//     struct { u32 offset; u32 zero; } frames[frameCount]   // offset relative to set start
//   Each frame offset points at a frame body; 36 bytes BEFORE it is a header:
//     f32   unknown[6]        (iso footprint / extents, used by placement code)
//     i16   width, height     (logical size)
//     i16   hotX, hotY        (anchor)
//     i16   unknown[2]
//   Frame body:
//     u8    unknown[8]
//     i32   x0, y0, x1, y1    bounding box of drawn pixels, relative to anchor
//     RLE rows (y0..y1), per row a stream of codes ending in 0:
//       0          end of row
//       1, n       skip n transparent pixels
//       even 2n, c run of n pixels of colour c
//       odd 2n+1   n literal pixels follow
//   Pixels are 8-bit palette indices; team colours are applied through a
//   256-entry remap table before the palette lookup.
#include <span>

#include "core/BinaryFile.h"

namespace ns {

struct ShapeFrame {
  int width = 0, height = 0;   // decoded bitmap size (bbox)
  int originX = 0, originY = 0;  // bitmap top-left relative to the anchor point
  std::vector<uint8_t> index;  // palette indices, width*height
  std::vector<uint8_t> mask;   // 255 = opaque, 0 = transparent
};

struct FrameHeader {
  float f[6];
  int16_t width, height, hotX, hotY, u0, u1;
};

class ShapeDb {
 public:
  bool open(const std::filesystem::path& path, std::string* err = nullptr);

  int setCount() const { return int(sets_.size()); }
  int frameCount(int set) const { return int(sets_.at(set).size()); }
  int totalFrames() const { return total_; }

  FrameHeader header(int set, int frame) const;
  ShapeFrame decode(int set, int frame) const;

 private:
  Bytes data_;
  std::vector<std::vector<uint32_t>> sets_;  // absolute file offsets of frame bodies
  int total_ = 0;
};

}  // namespace ns
