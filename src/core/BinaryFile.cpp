#include "core/BinaryFile.h"

#include <algorithm>
#include <cctype>
#include <fstream>

namespace ns {

std::optional<Bytes> readFile(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return std::nullopt;
  f.seekg(0, std::ios::end);
  auto n = static_cast<size_t>(f.tellg());
  f.seekg(0);
  Bytes b(n);
  if (n) f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(n));
  return b;
}

static std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

std::optional<std::filesystem::path> findFileCI(const std::filesystem::path& dir,
                                                const std::string& name) {
  std::error_code ec;
  auto direct = dir / name;
  if (std::filesystem::exists(direct, ec)) return direct;
  auto want = lower(name);
  for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
    if (lower(e.path().filename().string()) == want) return e.path();
  }
  return std::nullopt;
}

}  // namespace ns
