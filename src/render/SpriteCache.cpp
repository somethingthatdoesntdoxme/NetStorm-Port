#include <algorithm>
#include "render/SpriteCache.h"

namespace ns {

static uint64_t makeKey(int set, int frame, int remap, bool shadow) {
  return (uint64_t(uint32_t(set)) << 40) | (uint64_t(uint32_t(frame)) << 9) | (uint64_t(shadow) << 8) |
         uint8_t(remap);
}

const SpriteTex& SpriteCache::get(int set, int frame, int remap) {
  uint64_t key = makeKey(set, frame, remap, false);
  auto it = map_.find(key);
  return it != map_.end() ? it->second : build(key, set, frame, remap, false);
}

const SpriteTex& SpriteCache::shadow(int set, int frame) {
  uint64_t key = makeKey(set, frame, 0, true);
  auto it = map_.find(key);
  return it != map_.end() ? it->second : build(key, set, frame, 0, true);
}

const SpriteTex& SpriteCache::tinted(int set, int frame, SDL_Color rgb, const std::array<bool, 256>& tintMask) {
  uint64_t key = (uint64_t(uint32_t(set)) << 44) | (uint64_t(uint32_t(frame)) << 24) | (uint64_t(rgb.r) << 16) |
                 (uint64_t(rgb.g) << 8) | rgb.b;
  auto it = tintMap_.find(key);
  if (it != tintMap_.end()) return it->second;
  // Build into the main map under a temporary key, then move it over.
  const SpriteTex& st = build(~key, set, frame, 0, false, &rgb, &tintMask);
  SpriteTex copy = st;
  map_.erase(~key);
  return tintMap_.emplace(key, copy).first->second;
}

const SpriteTex& SpriteCache::build(uint64_t key, int set, int frame, int remap, bool shadow, const SDL_Color* tint,
                                     const std::array<bool, 256>* tintMask) {
  SpriteTex st;
  ShapeFrame f = gd_.shapes.decode(set, frame);
  if (f.width > 0) {
    const std::array<uint8_t, 256>* table = nullptr;
    if (remap > 0 && size_t(remap) <= gd_.remaps.tables.size()) table = &gd_.remaps.tables[remap - 1];
    std::vector<uint32_t> px(size_t(f.width) * f.height);
    for (size_t i = 0; i < px.size(); ++i) {
      uint8_t idx = table ? (*table)[f.index[i]] : f.index[i];
      if (shadow) {
        px[i] = f.mask[i] ? 0x70000000u : 0u;  // ~44% black
        continue;
      }
      Rgb c = gd_.gamePalette.c[idx];
      if (tint && (*tintMask)[idx]) {
        int lum = (c.r * 3 + c.g * 6 + c.b) / 10;  // keep brightness, swap hue
        auto mix = [&](int ch) { return uint8_t(std::min(255, ch * (lum + 60) / 200)); };
        c = {mix(tint->r), mix(tint->g), mix(tint->b)};
      }
      px[i] = (uint32_t(f.mask[i]) << 24) | (uint32_t(c.r) << 16) | (uint32_t(c.g) << 8) | c.b;
    }
    st.tex = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, f.width, f.height);
    if (st.tex) {
      SDL_UpdateTexture(st.tex, nullptr, px.data(), f.width * 4);
      SDL_SetTextureBlendMode(st.tex, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(st.tex, smooth_ ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    }
    st.w = f.width;
    st.h = f.height;
    st.originX = f.originX;
    st.originY = f.originY;
  }
  return map_.emplace(key, st).first->second;
}

void SpriteCache::setSmooth(bool s) {
  smooth_ = s;
  for (auto& [k, v] : map_)
    if (v.tex) SDL_SetTextureScaleMode(v.tex, s ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
}

void SpriteCache::clear() {
  for (auto& [k, v] : map_)
    if (v.tex) SDL_DestroyTexture(v.tex);
  for (auto& [k, v] : tintMap_)
    if (v.tex) SDL_DestroyTexture(v.tex);
  map_.clear();
  tintMap_.clear();
}

}  // namespace ns
