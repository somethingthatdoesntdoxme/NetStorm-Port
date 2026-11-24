#pragma once
// A player's order. In lockstep play every peer applies the same commands at
// the same tick, so this (plus the AIs, which run on every peer) is the only
// way game state changes after the start.
#include <cstdint>
#include <string>
#include <vector>

namespace ns {

struct Command {
  enum Type : uint8_t {
    None,
    Bridge,     // a,b = cell: place the player's current bridge piece
    Rotate,     // rotate the player's current bridge piece
    Construct,  // a = priest, s = type, b,c = anchor
    Capture,    // a = golem, b = enemy priest
    Drop,       // a = index in the ready list, b,c = anchor
    Move,       // a = unit, b,c = cell
    Build,      // a = workshop, s = unit type
    Golem,      // make a golem at the temple
    Upgrade,    // a = workshop
    Gift,       // s = unit type or "upgrade"
    Resume,     // a = priest, b = half-built building
    Surrender,  // the player leaves the battle
  };
  Type type = None;
  uint8_t player = 0;
  int32_t a = 0, b = 0, c = 0;
  std::string s;

  // Compact binary form for the network: type, player, 3 x int32 (LE), u8 length + bytes.
  void encode(std::vector<uint8_t>& out) const;
  // Reads one command at `pos`; returns false on malformed input (pos unchanged).
  static bool decode(const std::vector<uint8_t>& in, size_t& pos, Command& out);
  bool operator==(const Command& o) const {
    return type == o.type && player == o.player && a == o.a && b == o.b && c == o.c && s == o.s;
  }
};

}  // namespace ns
