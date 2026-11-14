#pragma once
// TAFF archive ("netstorm.tarc"): holds the game's script data -- unit/building
// definitions (*.type), missions/forts (*.fort) and text (*.english).
// Entries are XOR-encrypted with a repeating 13-byte key, position counted
// from the start of each entry (BaseFile.cpp in the original).
#include <map>
#include <string>

#include "core/BinaryFile.h"

namespace ns {

// Decrypt/encrypt in place (XOR is symmetric). `pos` is the position of
// buf[0] relative to the start of the encrypted stream.
void taffCrypt(uint8_t* buf, size_t n, size_t pos = 0);

class TaffArchive {
 public:
  bool open(const std::filesystem::path& path, std::string* err = nullptr);

  // Names are stored lower-cased with forward slashes, e.g. "d/bridge.type".
  const std::map<std::string, Bytes>& entries() const { return entries_; }
  const Bytes* find(const std::string& name) const;
  std::string text(const std::string& name) const;  // empty if missing

 private:
  std::map<std::string, Bytes> entries_;
};

}  // namespace ns
