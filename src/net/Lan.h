#pragma once
// LAN / internet battles: one host, up to 7 clients, deterministic lockstep.
//
// Every peer runs the same simulation. Orders (Commands) a player gives during
// turn T are scheduled for turn T + kDelayTurns; the host collects every
// player's batch for a turn, merges them in a fixed order and sends the turn to
// everyone. Nobody simulates a turn until it has arrived, so all peers apply the
// same orders at the same tick. Every few turns each peer reports a state hash;
// a mismatch is reported as a desync instead of silently drifting apart.
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/Command.h"
#include "game/Skirmish.h"
#include "net/Socket.h"

namespace ns {

class LanSession {
 public:
  static constexpr uint16_t kPort = 27100;          // TCP game port (forward this for internet play)
  static constexpr uint16_t kDiscoveryPort = 27101;  // UDP, LAN game discovery
  static constexpr uint32_t kTicksPerTurn = 4;       // 200 ms turns at 20 ticks/s
  static constexpr uint32_t kDelayTurns = 2;         // orders take effect 2 turns later
  static constexpr uint32_t kHashEvery = 10;         // turns between desync checks
  static constexpr uint32_t kProtocol = 2;
  static constexpr int kRejoinSeconds = 180;  // a dropped player's seat is kept this long

  enum class Role { None, Host, Client };
  enum class State { Idle, Connecting, Lobby, Playing, Ended };

  // Hosting: we are player 1; the config's other Human slots are filled by joiners.
  bool host(const std::string& name, const SkirmishConfig& cfg, uint16_t port = kPort, std::string* err = nullptr);
  bool join(const std::string& address, const std::string& name, uint16_t port = kPort, std::string* err = nullptr);
  // Internet play without port forwarding, through a netstorm-relay server.
  bool hostViaRelay(const std::string& relayAddr, const std::string& name, const SkirmishConfig& cfg,
                    uint16_t relayPort = 27102, std::string* err = nullptr);
  bool joinRoom(const std::string& relayAddr, const std::string& code, const std::string& name,
                uint16_t relayPort = 27102, std::string* err = nullptr);
  const std::string& roomCode() const { return roomCode_; }  // host via relay: give this to your friends
  void leave();

  void poll();  // every frame: network IO, lobby, turn assembly

  // Lobby
  Role role() const { return role_; }
  State state() const { return state_; }
  int localPlayer() const { return local_; }
  const SkirmishConfig& config() const { return cfg_; }
  void setConfig(const SkirmishConfig& c);  // host only; re-sent to everyone
  std::array<std::string, 8> names;         // who's in each slot
  int humansWaiting() const;                // open Human slots nobody has joined yet
  bool start();                             // host: everyone's in -> START
  bool justStarted();                       // true once, when the battle should begin

  // Lockstep hooks (see WorldView::netIssue / netTick)
  void issue(const Command& c);
  bool tickCommands(uint32_t tick, const std::function<uint64_t()>& stateHash, std::vector<Command>& out);

  // Chat (lobby and battle). Never touches the simulation.
  struct ChatLine {
    int player;
    std::string name, text;
  };
  std::vector<ChatLine> chat;
  void say(const std::string& text);
  // Complete turns waiting to be simulated: a rejoining player fast-forwards through these.
  size_t backlog() const { return turns_.size(); }
  bool reconnecting() const { return reconnecting_; }
  void debugDropConnection() { server_.abort(); }  // tests: simulate a network failure

  std::string status;  // human-readable: "waiting for Alice", "connection lost"...
  bool desync = false;

  // LAN discovery (client side)
  struct Found {
    std::string addr, name;
    uint16_t port;
    int open;
  };
  void discover();  // broadcast a query; replies arrive via poll()
  std::vector<Found> found;

 private:
  // A player connected to us (the host): directly over TCP, or through a relay
  // (then `relayId` names them on our one relay connection).
  struct Peer {
    net::Tcp tcp;
    int relayId = -1;
    net::Tcp* relay = nullptr;
    std::deque<std::vector<uint8_t>> inbox;  // relayed frames waiting to be handled
    bool closed = false;
    int player = 0;
    std::string name;
    bool hello = false;
    void send(const std::vector<uint8_t>& m);
    bool receive(std::vector<uint8_t>& m);
    void pump() {
      if (relayId < 0) tcp.pump();
    }
    bool failed() const { return relayId >= 0 ? closed : tcp.failed(); }
    bool open() const { return relayId >= 0 ? !closed : tcp.open(); }
    void close();
  };
  void tryReconnect();
  void sendHello();
  void pollRelay();
  net::Tcp relay_;            // host: our connection to the relay
  std::string roomCode_;      // host: our room; client: the room we joined (for reconnecting)
  uint32_t session_ = 0;  // identifies this battle (for rejoining)
  std::map<uint32_t, std::vector<Command>> history_;  // host: every finished turn, for rejoiners
  std::array<bool, 9> away_{};
  std::array<int64_t, 9> awayUntil_{};
  bool reconnecting_ = false;
  int64_t nextRetry_ = 0, giveUpAt_ = 0;
  std::string addr_;
  uint16_t port_ = kPort;
  void sendAll(const std::vector<uint8_t>& msg);
  void handle(Peer* from, const std::vector<uint8_t>& msg);
  void addBatch(uint32_t turn, int player, const std::vector<Command>& cmds);
  void tryFinalize();
  void dropPeer(Peer& p, const std::string& why);
  std::vector<uint8_t> lobbyMsg() const;
  void answerDiscovery();

  Role role_ = Role::None;
  State state_ = State::Idle;
  int local_ = 0;
  std::string myName_;
  SkirmishConfig cfg_;
  net::Tcp listen_, server_;
  net::Udp disc_;
  std::vector<std::unique_ptr<Peer>> peers_;
  bool started_ = false;

  std::vector<Command> pending_;  // our orders since the last turn boundary
  int64_t sealed_ = -1;           // last turn whose boundary we passed (our batch went out)
  std::map<uint32_t, std::map<int, std::vector<Command>>> batches_;  // host: turn -> player -> orders
  std::map<uint32_t, std::vector<Command>> turns_;                    // complete turns, ready to run
  uint32_t nextFinal_ = kDelayTurns;                                  // host: next turn to complete
  std::array<bool, 9> required_{};                                    // players whose batches we wait for
  std::map<uint32_t, uint64_t> myHashes_;
  std::map<uint32_t, std::map<int, uint64_t>> theirHashes_;
};

}  // namespace ns
