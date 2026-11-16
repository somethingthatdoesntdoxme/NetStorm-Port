#pragma once
// Reader for ".fort" mission / fort files (decrypted from netstorm.tarc).
//
// Layout (see docs/FORMATS.md):
//   u8 'F' (version 70), u8 flags (254 = extended)
//   35+ sections, each: u16 length (including these 2 bytes), payload
//     0  name block        1  State (28 bytes)     4  Chaff: neutral world, 16x16 chunks
//     6  Territory (20 x 6-byte records)           7  TypeNames: u8 n, n x u32 name hash
//     10 Money (f32)       11 Deck                 15..34  Terr00..Terr19: player forts
//   A chunk list section: u8 version, then per chunk a marker byte; 99 ('c')
//   means u16 count + objects, anything else means an empty chunk.
//   Object: u8 pos (x<<4 | y within the chunk), u8 type, then optional fields
//   depending on the type's flags (frame, QA, QB, bridge piece, factory slot,
//   owner, contained objects). Types >= 246 are player land markers.
#include <cstdint>
#include <string>
#include <vector>

#include "data/TypeFile.h"

namespace ns {

struct FortObject {
  std::string type;     // type name ("geyser"), or "" for land markers
  bool landMarker = false;
  int player = 0;       // land marker's player (255 - type byte)
  int x = 0, y = 0;     // cell position within its section (chunk*16 + offset)
  int chunk = 0;        // chunk index within the section
  int frame = -1, qa = -1, qb = -1, bridgePiece = -1, factorySlot = -1;
  int owner = -1;       // player index for owned things
  int value = 0;        // land marker value byte
  std::vector<std::string> contents;  // units carried inside (vortex, factory...)
};

struct FortSection {
  int version = 0;
  int chunkCount = 0;
  std::vector<FortObject> objects;
};

struct TerritoryRecord {
  uint8_t a = 0, flags = 0, b = 0, c = 0;
  bool used() const { return flags == 9; }
};

class FortFile {
 public:
  // `types` are the parsed .type scripts (used for per-type field layout).
  bool parse(const std::vector<uint8_t>& data, const std::vector<TypeDef>& types, std::string* err);

  std::string name;
  float money = 0;
  FortSection chaff;                     // neutral world, 16x16 chunks
  std::vector<FortSection> territories;  // Terr00..Terr19
  std::vector<TerritoryRecord> territoryRecords;
  std::vector<std::string> typeNames;    // saved type id -> name ("" if unknown)
  int unknownTypes = 0;
};

uint32_t fortNameHash(const std::string& name);

}  // namespace ns
