// netstorm-relay: a tiny server so friends can play over the internet without
// anyone forwarding ports. Run it on any machine everyone can reach (a cheap VPS,
// a home server with TCP 27102 forwarded once), then in the game choose
// "Host via relay" / "Join room".
//
//   netstorm-relay [--port 27102]
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "net/Relay.h"
#include "net/Socket.h"

using namespace ns;

namespace {
struct Conn {
  net::Tcp tcp;
  enum { New, Host, Client } kind = New;
  std::string room;
  uint32_t id = 0;
  int64_t lastActive = 0;
};

int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::vector<uint8_t> frame(uint8_t type, uint32_t id, const std::vector<uint8_t>* payload = nullptr) {
  std::vector<uint8_t> f{type};
  for (int i = 0; i < 4; ++i) f.push_back(uint8_t(id >> (8 * i)));
  if (payload) f.insert(f.end(), payload->begin(), payload->end());
  return f;
}
std::vector<uint8_t> frameStr(uint8_t type, const std::string& s) {
  std::vector<uint8_t> f{type};
  uint32_t n = uint32_t(s.size());
  for (int i = 0; i < 4; ++i) f.push_back(uint8_t(n >> (8 * i)));
  f.insert(f.end(), s.begin(), s.end());
  return f;
}
std::string readStr(const std::vector<uint8_t>& m) {
  if (m.size() < 5) return "";
  uint32_t n = uint32_t(m[1]) | uint32_t(m[2]) << 8 | uint32_t(m[3]) << 16 | uint32_t(m[4]) << 24;
  if (n > 32 || 5 + n > m.size()) return "";
  return std::string(m.begin() + 5, m.begin() + 5 + n);
}
uint32_t readId(const std::vector<uint8_t>& m) {
  return m.size() < 5 ? 0 : uint32_t(m[1]) | uint32_t(m[2]) << 8 | uint32_t(m[3]) << 16 | uint32_t(m[4]) << 24;
}
}  // namespace

int main(int argc, char** argv) {
  uint16_t port = relay::kPort;
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--port" && i + 1 < argc) port = uint16_t(std::atoi(argv[++i]));
  net::Tcp listener;
  std::string err;
  if (!listener.listen(port, &err)) {
    std::fprintf(stderr, "netstorm-relay: %s\n", err.c_str());
    return 1;
  }
  std::printf("netstorm-relay listening on TCP %u\n", unsigned(port));
  std::fflush(stdout);
  std::mt19937 rng(std::random_device{}());
  std::vector<std::unique_ptr<Conn>> conns;
  std::map<std::string, Conn*> hosts;  // room code -> host
  uint32_t nextId = 1;

  for (;;) {
    bool busy = false;
    net::Tcp c;
    while (listener.accept(c)) {
      auto k = std::make_unique<Conn>();
      k->tcp = std::move(c);
      k->lastActive = nowMs();
      conns.push_back(std::move(k));
      busy = true;
    }
    for (size_t i = 0; i < conns.size(); ++i) {
      Conn& k = *conns[i];
      k.tcp.pump();
      std::vector<uint8_t> m;
      while (k.tcp.receive(m)) {
        busy = true;
        k.lastActive = nowMs();
        if (m.empty()) continue;
        if (k.kind == Conn::New && m[0] == relay::RHost) {
          std::string code;
          do {
            code.clear();
            for (int j = 0; j < 5; ++j) code += "ABCDEFGHJKLMNPQRSTUVWXYZ"[rng() % 24];  // no I/O: easy to read out
          } while (hosts.count(code));
          k.kind = Conn::Host;
          k.room = code;
          hosts[code] = &k;
          k.tcp.send(frameStr(relay::RRoom, code));
          std::printf("room %s opened (%s)\n", code.c_str(), k.tcp.peer().c_str());
        } else if (k.kind == Conn::New && m[0] == relay::RJoin) {
          std::string code = readStr(m);
          for (auto& ch : code) ch = char(std::toupper(static_cast<unsigned char>(ch)));
          auto it = hosts.find(code);
          if (it == hosts.end()) {
            k.tcp.send({relay::RNoRoom});
            continue;
          }
          k.kind = Conn::Client;
          k.room = code;
          k.id = nextId++;
          k.tcp.send({relay::RJoined});
          it->second->tcp.send(frame(relay::ROpen, k.id));
        } else if (k.kind == Conn::Client) {
          auto it = hosts.find(k.room);
          if (it != hosts.end()) it->second->tcp.send(frame(relay::RData, k.id, &m));
        } else if (k.kind == Conn::Host && (m[0] == relay::RData || m[0] == relay::RKick) && m.size() >= 5) {
          uint32_t id = readId(m);
          for (auto& o : conns)
            if (o->kind == Conn::Client && o->room == k.room && o->id == id) {
              if (m[0] == relay::RKick) o->tcp.abort();
              else o->tcp.send(std::vector<uint8_t>(m.begin() + 5, m.end()));
            }
        }
      }
    }
    // Clean up: a closed client is reported to its host; a closed host closes its room.
    for (auto& k : conns) {
      bool dead = k->tcp.failed() || !k->tcp.open() ||
                  (k->kind == Conn::New && nowMs() - k->lastActive > 30000);  // never said who it was
      if (!dead) continue;
      if (k->kind == Conn::Client) {
        auto it = hosts.find(k->room);
        if (it != hosts.end()) it->second->tcp.send(frame(relay::RClose, k->id));
      } else if (k->kind == Conn::Host) {
        hosts.erase(k->room);
        for (auto& o : conns)
          if (o->kind == Conn::Client && o->room == k->room) o->tcp.abort();
        std::printf("room %s closed\n", k->room.c_str());
      }
      k->tcp.close();
      k->kind = Conn::New;
      k->room.clear();
    }
    conns.erase(std::remove_if(conns.begin(), conns.end(), [](auto& k) { return !k->tcp.open(); }), conns.end());
    for (auto& k : conns) k->tcp.pump();
    std::fflush(stdout);
    if (!busy) std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
