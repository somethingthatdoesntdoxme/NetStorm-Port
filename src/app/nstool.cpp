// nstool -- command line helper for working with the user's NetStorm data.
//
//   nstool <install-dir> info
//   nstool <install-dir> extract-scripts <out-dir>   decrypted .type/.fort/.english
//   nstool <install-dir> dump-sprites <out-dir>      every frame as PNG (+ shadows)
//   nstool <install-dir> types                       parsed unit/building summary
#include <cstdio>
#include <fstream>
#include <iostream>

#include "core/PngWriter.h"
#include "data/GameData.h"

using namespace ns;

static std::vector<uint8_t> toRgba(const ShapeFrame& f, const Palette& pal) {
  std::vector<uint8_t> out(size_t(f.width) * f.height * 4);
  for (size_t i = 0; i < f.index.size(); ++i) {
    auto c = pal.c[f.index[i]];
    out[i * 4 + 0] = c.r;
    out[i * 4 + 1] = c.g;
    out[i * 4 + 2] = c.b;
    out[i * 4 + 3] = f.mask[i];
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: nstool <netstorm-install-dir> <info|extract-scripts|dump-sprites|types> [out]\n");
    return 1;
  }
  GameData gd;
  std::string err;
  if (!gd.load(argv[1], &err)) {
    std::fprintf(stderr, "error: %s\n", err.c_str());
    return 1;
  }
  std::string cmd = argv[2];
  if (cmd == "info") {
    std::printf("script archive entries : %zu\n", gd.archive.entries().size());
    std::printf("type definitions       : %zu\n", gd.types.size());
    std::printf("sprite sets            : %d\n", gd.shapes.setCount());
    std::printf("sprite frames          : %d\n", gd.shapes.totalFrames());
    std::printf("team remap tables      : %zu\n", gd.remaps.tables.size());
    return 0;
  }
  if (cmd == "types") {
    for (auto& t : gd.types) {
      std::printf("%-28s %-22s hp=%-5g cost=%-5g foot=%gx%g frames=%zu flags=", t.file.c_str(),
                  t.str("description").c_str(), t.num("maxHitPoints"), t.num("cost"), t.num("foot_x"),
                  t.num("foot_y"), t.frames.size());
      for (auto& f : t.flags) std::printf("%s ", f.c_str());
      std::printf("\n");
    }
    return 0;
  }
  if (argc < 4) {
    std::fprintf(stderr, "missing output directory\n");
    return 1;
  }
  std::filesystem::path out = argv[3];
  std::filesystem::create_directories(out);
  if (cmd == "extract-scripts") {
    for (auto& [name, bytes] : gd.archive.entries()) {
      auto p = out / name;
      std::filesystem::create_directories(p.parent_path());
      std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                               std::streamsize(bytes.size()));
    }
    std::printf("wrote %zu files to %s\n", gd.archive.entries().size(), out.string().c_str());
    return 0;
  }
  if (cmd == "dump-sprites") {
    int n = 0;
    for (int s = 0; s < gd.shapes.setCount(); ++s) {
      char dir[32];
      std::snprintf(dir, sizeof dir, "set%03d", s);
      std::filesystem::create_directories(out / dir);
      for (int f = 0; f < gd.shapes.frameCount(s); ++f) {
        auto fr = gd.shapes.decode(s, f);
        if (fr.width <= 0) continue;
        char name[64];
        std::snprintf(name, sizeof name, "%03d_%04d.png", s, f);
        writePng(out / dir / name, fr.width, fr.height, toRgba(fr, gd.gamePalette));
        ++n;
      }
    }
    std::printf("wrote %d sprites to %s\n", n, out.string().c_str());
    return 0;
  }
  std::fprintf(stderr, "unknown command %s\n", cmd.c_str());
  return 1;
}
