#include "render/SpriteCache.h"

namespace ns {

const SpriteTex& SpriteCache::get(int set, int frame, int remap) {
  uint64_t key = (uint64_t(uint32_t(set)) << 40) | (uint64_t(uint32_t(frame)) << 8) | uint8_t(remap);
  auto it = map_.find(key);
  if (it != map_.end()) return it->second;

  SpriteTex st;
  ShapeFrame f = gd_.shapes.decode(set, frame);
  if (f.width > 0) {
    const std::array<uint8_t, 256>* table = nullptr;
    if (remap > 0 && size_t(remap) <= gd_.remaps.tables.size()) table = &gd_.remaps.tables[remap - 1];
    std::vector<uint32_t> px(size_t(f.width) * f.height);
    for (size_t i = 0; i < px.size(); ++i) {
      uint8_t idx = table ? (*table)[f.index[i]] : f.index[i];
      Rgb c = gd_.gamePalette.c[idx];
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
  map_.clear();
}

}  // namespace ns
