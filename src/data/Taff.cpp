#include "data/Taff.h"

#include <algorithm>
#include <cctype>

namespace ns {

static constexpr char kKey[] = "mydoghasfleas";
static constexpr size_t kKeyLen = 13;

void taffCrypt(uint8_t* buf, size_t n, size_t pos) {
  for (size_t i = 0; i < n; ++i) buf[i] ^= static_cast<uint8_t>(kKey[(pos + i) % kKeyLen]);
}

// Header (44 bytes):
//   char  magic[16]   "TAFF v0.2\x1a" zero padded
//   u32   ?           (2)
//   u32   count
//   u32   ?           (2)
//   u32   offsetTableOff  (count x u32, offsets into the name block)
//   u32   namesOff
//   u32   namesSize   name block: { u32 dataOff, u32 size, char name[] \0 } ...
//   u32   dataOff     base for entry dataOff
bool TaffArchive::open(const std::filesystem::path& path, std::string* err) {
  auto fail = [&](const char* m) {
    if (err) *err = m;
    return false;
  };
  auto file = readFile(path);
  if (!file) return fail("cannot open archive");
  const Bytes& b = *file;
  if (b.size() < 44 || std::string(reinterpret_cast<const char*>(b.data()), 4) != "TAFF")
    return fail("not a TAFF archive");
  try {
    uint32_t count = rd<uint32_t>(b, 20);
    uint32_t namesOff = rd<uint32_t>(b, 32);
    uint32_t namesSize = rd<uint32_t>(b, 36);
    uint32_t dataBase = rd<uint32_t>(b, 40);
    size_t p = namesOff, end = size_t(namesOff) + namesSize;
    for (uint32_t i = 0; i < count && p < end; ++i) {
      uint32_t off = rd<uint32_t>(b, p);
      uint32_t size = rd<uint32_t>(b, p + 4);
      size_t s = p + 8;
      size_t e = s;
      while (e < b.size() && b[e]) ++e;
      std::string name(reinterpret_cast<const char*>(b.data() + s), e - s);
      p = e + 1;
      size_t start = size_t(dataBase) + off;
      if (start + size > b.size()) return fail("entry out of range");
      Bytes data(b.begin() + start, b.begin() + start + size);
      taffCrypt(data.data(), data.size(), 0);
      std::replace(name.begin(), name.end(), '\\', '/');
      if (!name.empty() && name[0] == '/') name.erase(0, 1);
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      entries_[name] = std::move(data);
    }
  } catch (const DataError& e) {
    return fail(e.what());
  }
  return true;
}

const Bytes* TaffArchive::find(const std::string& name) const {
  std::string k = name;
  std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return std::tolower(c); });
  auto it = entries_.find(k);
  return it == entries_.end() ? nullptr : &it->second;
}

std::string TaffArchive::text(const std::string& name) const {
  auto* b = find(name);
  return b ? std::string(b->begin(), b->end()) : std::string();
}

}  // namespace ns
