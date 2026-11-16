#pragma once
#include <array>

#include "core/BinaryFile.h"

namespace ns {

struct Rgb {
  uint8_t r, g, b;
};

struct Palette {
  std::array<Rgb, 256> c{};

  // ".COL" file: u32 size(0x308), u32 magic(0xB123), 256 x RGB.
  static std::optional<Palette> fromCol(const Bytes& b);
  // 8-bit Windows DIB (resource without BITMAPFILEHEADER).
  static std::optional<Palette> fromDib(const Bytes& b);
};

// Lighting/effect remap tables from "!color.dat" (9 x 256 bytes): brighten,
// darken, black silhouette, red damage flash, identity (table 4), ...
// Team colours are NOT done this way; ownership is shown as rim glow lines.
struct RemapTables {
  std::vector<std::array<uint8_t, 256>> tables;
  static RemapTables fromColorDat(const Bytes& b);
};

}  // namespace ns
