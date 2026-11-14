#pragma once
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ns {

using Bytes = std::vector<uint8_t>;

// Reads a whole file. Returns nullopt if it can't be opened.
std::optional<Bytes> readFile(const std::filesystem::path& p);

// Case-insensitive lookup of a file inside a directory (the original data is
// Windows-only, so names like "_SHAPES.SHP" vs "_shapes.shp" vary).
std::optional<std::filesystem::path> findFileCI(const std::filesystem::path& dir,
                                                const std::string& name);

struct DataError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Little-endian readers with bounds checks.
template <class T>
inline T rd(const Bytes& b, size_t off) {
  if (off + sizeof(T) > b.size()) throw DataError("read past end of buffer");
  T v;
  std::memcpy(&v, b.data() + off, sizeof(T));
  return v;
}

}  // namespace ns
