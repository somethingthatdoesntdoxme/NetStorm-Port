#pragma once
// Relay protocol: lets players reach a host with no port forwarding. Everyone
// connects *out* to a relay server (netstorm-relay) that anyone can reach; the
// host opens a room and gets a code, players join with the code, and the relay
// passes frames between them. Game messages are carried unchanged.
//
// Frames use the normal length-prefixed framing; the first byte says what it is:
//   host  -> relay  RHost                                  open a room
//   relay -> host   RRoom   str code                       room opened
//   client-> relay  RJoin   str code                       join a room (first frame)
//   relay -> client RJoined | RNoRoom
//   relay -> host   ROpen   u32 id                         a player arrived
//   relay -> host   RData   u32 id, game frame             from that player
//   host  -> relay  RData   u32 id, game frame             to that player
//   relay -> host   RClose  u32 id                         that player left
//   host  -> relay  RKick   u32 id                         drop that player
// After RJoined, everything a client sends/receives is a plain game frame.
#include <cstdint>

namespace ns::relay {
constexpr uint16_t kPort = 27102;
enum : uint8_t { RHost = 0xA0, RRoom, RJoin, RJoined, RNoRoom, ROpen, RData, RClose, RKick };
}  // namespace ns::relay
