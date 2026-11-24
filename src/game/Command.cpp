#include "game/Command.h"

namespace ns {

namespace {
void put32(std::vector<uint8_t>& o, int32_t v) {
  uint32_t u = uint32_t(v);
  for (int i = 0; i < 4; ++i) o.push_back(uint8_t(u >> (8 * i)));
}
int32_t get32(const std::vector<uint8_t>& in, size_t p) {
  uint32_t u = 0;
  for (int i = 0; i < 4; ++i) u |= uint32_t(in[p + size_t(i)]) << (8 * i);
  return int32_t(u);
}
}  // namespace

void Command::encode(std::vector<uint8_t>& out) const {
  out.push_back(type);
  out.push_back(player);
  put32(out, a), put32(out, b), put32(out, c);
  size_t n = s.size() > 255 ? 255 : s.size();
  out.push_back(uint8_t(n));
  out.insert(out.end(), s.begin(), s.begin() + long(n));
}

bool Command::decode(const std::vector<uint8_t>& in, size_t& pos, Command& out) {
  if (pos + 15 > in.size()) return false;
  if (in[pos] > Surrender || in[pos + 1] > 8) return false;
  size_t n = in[pos + 14];
  if (pos + 15 + n > in.size()) return false;
  out.type = Type(in[pos]);
  out.player = in[pos + 1];
  out.a = get32(in, pos + 2), out.b = get32(in, pos + 6), out.c = get32(in, pos + 10);
  out.s.assign(reinterpret_cast<const char*>(in.data() + pos + 15), n);
  pos += 15 + n;
  return true;
}

}  // namespace ns
