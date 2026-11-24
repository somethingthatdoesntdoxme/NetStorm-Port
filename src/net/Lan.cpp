#include "net/Lan.h"

#include "net/Relay.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <sstream>

namespace ns {

namespace {
enum Msg : uint8_t { Hello = 1, Welcome, Lobby, Start, Cmds, Turn, Hash, Desync, Reject, Chat };

int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct W {
  std::vector<uint8_t> b;
  explicit W(Msg m) { b.push_back(m); }
  W& u8(uint8_t v) { return b.push_back(v), *this; }
  W& u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(uint8_t(v >> (8 * i)));
    return *this;
  }
  W& u64(uint64_t v) { return u32(uint32_t(v)).u32(uint32_t(v >> 32)); }
  W& str(const std::string& s) {
    u32(uint32_t(s.size()));
    b.insert(b.end(), s.begin(), s.end());
    return *this;
  }
  W& cmds(const std::vector<Command>& cs) {
    u32(uint32_t(cs.size()));
    for (auto& c : cs) c.encode(b);
    return *this;
  }
};

struct R {
  const std::vector<uint8_t>& b;
  size_t p = 1;
  bool ok = true;
  explicit R(const std::vector<uint8_t>& v) : b(v) {}
  uint8_t u8() {
    if (p + 1 > b.size()) return ok = false, 0;
    return b[p++];
  }
  uint32_t u32() {
    if (p + 4 > b.size()) return ok = false, 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= uint32_t(b[p + size_t(i)]) << (8 * i);
    p += 4;
    return v;
  }
  uint64_t u64() {
    uint64_t lo = u32();
    return lo | uint64_t(u32()) << 32;
  }
  std::string str() {
    uint32_t n = u32();
    if (!ok || n > 4096 || p + n > b.size()) return ok = false, "";
    std::string s(b.begin() + long(p), b.begin() + long(p + n));
    p += n;
    return s;
  }
  std::vector<Command> cmds() {
    uint32_t n = u32();
    std::vector<Command> out;
    if (!ok || n > 4096) return ok = false, out;
    for (uint32_t i = 0; i < n && ok; ++i) {
      Command c;
      if (!Command::decode(b, p, c)) ok = false;
      else out.push_back(c);
    }
    return out;
  }
};
}  // namespace

// ---- Peers (direct or relayed) ----------------------------------------------------------

void LanSession::Peer::send(const std::vector<uint8_t>& m) {
  if (relayId < 0) return tcp.send(m);
  if (closed || !relay) return;
  std::vector<uint8_t> f{relay::RData};
  for (int i = 0; i < 4; ++i) f.push_back(uint8_t(uint32_t(relayId) >> (8 * i)));
  f.insert(f.end(), m.begin(), m.end());
  relay->send(f);
}

bool LanSession::Peer::receive(std::vector<uint8_t>& m) {
  if (relayId < 0) return tcp.receive(m);
  if (inbox.empty()) return false;
  m = std::move(inbox.front());
  inbox.pop_front();
  return true;
}

void LanSession::Peer::close() {
  if (relayId < 0) return tcp.close();
  if (!closed && relay) {
    std::vector<uint8_t> f{relay::RKick};
    for (int i = 0; i < 4; ++i) f.push_back(uint8_t(uint32_t(relayId) >> (8 * i)));
    relay->send(f);
  }
  closed = true;
}

// ---- Setup ----------------------------------------------------------------------

bool LanSession::hostViaRelay(const std::string& relayAddr, const std::string& name, const SkirmishConfig& cfg,
                              uint16_t relayPort, std::string* err) {
  leave();
  if (!relay_.connect(relayAddr, relayPort, err)) return false;
  relay_.send({relay::RHost});
  role_ = Role::Host;
  state_ = State::Lobby;
  local_ = 1;
  myName_ = name;
  cfg_ = cfg;
  cfg_.slots[0].kind = SkirmishSlot::Human;
  names = {};
  names[0] = name;
  status = "opening a room on " + relayAddr + "...";
  return true;
}

bool LanSession::joinRoom(const std::string& relayAddr, const std::string& code, const std::string& name,
                          uint16_t relayPort, std::string* err) {
  leave();
  if (!server_.connect(relayAddr, relayPort, err)) return false;
  role_ = Role::Client;
  state_ = State::Connecting;
  myName_ = name;
  addr_ = relayAddr;
  port_ = relayPort;
  roomCode_ = code;
  W j{static_cast<Msg>(relay::RJoin)};
  j.str(code);
  server_.send(j.b);
  sendHello();  // the relay passes it on once we're in the room
  status = "joining room " + code;
  return true;
}

void LanSession::pollRelay() {
  relay_.pump();
  std::vector<uint8_t> m;
  while (relay_.receive(m)) {
    if (m.empty()) continue;
    R r(m);
    if (m[0] == relay::RRoom) {
      roomCode_ = r.str();
      status = "room code " + roomCode_ + " - tell your friends";
    } else if (m[0] == relay::ROpen) {
      auto p = std::make_unique<Peer>();
      p->relayId = int(r.u32());
      p->relay = &relay_;
      peers_.push_back(std::move(p));
    } else if (m[0] == relay::RData || m[0] == relay::RClose) {
      int id = int(r.u32());
      for (auto& p : peers_)
        if (p->relayId == id) {
          if (m[0] == relay::RClose) p->closed = true;
          else p->inbox.emplace_back(m.begin() + 5, m.end());
        }
    }
  }
  if (relay_.failed() && !roomCode_.empty()) {
    status = "lost the connection to the relay";
    for (auto& p : peers_)
      if (p->relayId >= 0) p->closed = true;
    roomCode_.clear();
  }
}

bool LanSession::host(const std::string& name, const SkirmishConfig& cfg, uint16_t port, std::string* err) {
  leave();
  if (!listen_.listen(port, err)) return false;
  disc_.bind(kDiscoveryPort);  // best effort: discovery is a convenience
  role_ = Role::Host;
  state_ = State::Lobby;
  local_ = 1;
  myName_ = name;
  cfg_ = cfg;
  cfg_.slots[0].kind = SkirmishSlot::Human;
  names = {};
  names[0] = name;
  status = "hosting on port " + std::to_string(port);
  return true;
}

bool LanSession::join(const std::string& address, const std::string& name, uint16_t port, std::string* err) {
  leave();
  if (!server_.connect(address, port, err)) return false;
  role_ = Role::Client;
  state_ = State::Connecting;
  myName_ = name;
  addr_ = address;
  port_ = port;
  sendHello();
  status = "connecting to " + address;
  return true;
}

void LanSession::sendHello() {
  // session/player are 0 for a new player; set when coming back after a drop.
  server_.send(W(Hello).u32(kProtocol).str(myName_).u32(session_).u8(uint8_t(session_ ? local_ : 0)).b);
}

void LanSession::say(const std::string& text) {
  if (text.empty() || role_ == Role::None) return;
  std::string t = text.substr(0, 120);
  if (role_ == Role::Host) {
    chat.push_back({local_, myName_, t});
    sendAll(W(Chat).u8(uint8_t(local_)).str(myName_).str(t).b);
  } else {
    server_.send(W(Chat).u8(0).str("").str(t).b);
  }
}

void LanSession::leave() {
  relay_.close();
  roomCode_.clear();
  session_ = 0;
  history_.clear();
  away_ = {};
  awayUntil_ = {};
  reconnecting_ = false;
  chat.clear();
  peers_.clear();
  listen_.close();
  server_.close();
  disc_.close();
  role_ = Role::None;
  state_ = State::Idle;
  local_ = 0;
  started_ = false;
  pending_.clear();
  batches_.clear();
  turns_.clear();
  myHashes_.clear();
  theirHashes_.clear();
  sealed_ = -1;
  nextFinal_ = kDelayTurns;
  required_ = {};
  desync = false;
}

int LanSession::humansWaiting() const {
  int n = 0;
  for (int i = 0; i < 8; ++i) n += cfg_.slots[size_t(i)].kind == SkirmishSlot::Human && names[size_t(i)].empty();
  return n;
}

std::vector<uint8_t> LanSession::lobbyMsg() const {
  W w(Lobby);
  w.str(cfg_.encode());
  for (auto& n : names) w.str(n);
  return w.b;
}

void LanSession::setConfig(const SkirmishConfig& c) {
  if (role_ != Role::Host || state_ != State::Lobby) return;
  SkirmishConfig n = c;
  n.slots[0].kind = SkirmishSlot::Human;  // the host is always player 1
  // Anyone sitting in a slot that's no longer a Human slot gets bumped.
  for (auto& p : peers_)
    if (p->player && n.slots[size_t(p->player - 1)].kind != SkirmishSlot::Human) {
      names[size_t(p->player - 1)].clear();
      p->send(W(Reject).str("the host closed your slot").b);
      p->player = 0;
    }
  cfg_ = n;
  for (int i = 0; i < 8; ++i)
    if (cfg_.slots[size_t(i)].kind != SkirmishSlot::Human) names[size_t(i)].clear();
  sendAll(lobbyMsg());
}

bool LanSession::start() {
  if (role_ != Role::Host || state_ != State::Lobby || humansWaiting() > 0) return false;
  cfg_.seed = cfg_.seed ? cfg_.seed : 1;
  for (int i = 0; i < 8; ++i) cfg_.slots[size_t(i)].name = names[size_t(i)];
  required_ = {};
  required_[1] = true;
  for (auto& p : peers_)
    if (p->player) required_[size_t(p->player)] = true;
  session_ = uint32_t(std::random_device{}()) | 1u;
  sendAll(W(Start).str(cfg_.encode()).u32(session_).b);
  state_ = State::Playing;
  started_ = true;
  status = "";
  return true;
}

bool LanSession::justStarted() {
  bool s = started_;
  started_ = false;
  return s;
}

void LanSession::sendAll(const std::vector<uint8_t>& msg) {
  for (auto& p : peers_)
    if (p->hello) p->send(msg);
}

void LanSession::discover() {
  found.clear();
  if (!disc_.bind(0)) return;
  disc_.sendBroadcast(kDiscoveryPort, "NSDISCOVER 1");
  disc_.sendTo("127.0.0.1", kDiscoveryPort, "NSDISCOVER 1");  // same machine (testing)
}

void LanSession::answerDiscovery() {
  std::string msg, from;
  uint16_t port;
  while (disc_.receive(msg, from, port)) {
    if (role_ == Role::Host && msg.rfind("NSDISCOVER", 0) == 0 && state_ == State::Lobby) {
      std::ostringstream o;
      o << "NSGAME " << kPort << ' ' << humansWaiting() << ' ' << myName_;
      disc_.sendTo(from, port, o.str());
    } else if (role_ != Role::Host && msg.rfind("NSGAME ", 0) == 0) {
      std::istringstream in(msg.substr(7));
      Found f;
      f.addr = from;
      in >> f.port >> f.open;
      std::getline(in >> std::ws, f.name);
      bool dup = false;
      for (auto& g : found) dup |= g.addr == f.addr && g.port == f.port;
      if (!dup) found.push_back(f);
    }
  }
}

// ---- Network pump ------------------------------------------------------------------

void LanSession::poll() {
  answerDiscovery();
  if (role_ == Role::Host) {
    if (relay_.open() || relay_.failed()) pollRelay();
    net::Tcp c;
    while (listen_.accept(c)) {
      auto p = std::make_unique<Peer>();
      p->tcp = std::move(c);
      peers_.push_back(std::move(p));
    }
    for (size_t i = 0; i < peers_.size(); ++i) {
      Peer& p = *peers_[i];
      p.pump();
      std::vector<uint8_t> m;
      while (p.receive(m)) handle(&p, m);
      if (p.failed()) dropPeer(p, "lost connection");
    }
    peers_.erase(std::remove_if(peers_.begin(), peers_.end(), [](auto& p) { return !p->open(); }), peers_.end());
    for (int pl = 2; pl <= 8 && state_ == State::Playing; ++pl)
      if (away_[size_t(pl)] && nowMs() > awayUntil_[size_t(pl)]) {
        away_[size_t(pl)] = false;
        Command c;
        c.type = Command::Surrender;
        c.player = uint8_t(pl);
        pending_.push_back(c);  // rides in our (the host's) next batch
        status = "player " + std::to_string(pl) + " didn't come back and has surrendered";
      }
    tryFinalize();
    for (auto& p : peers_) p->pump();
    relay_.pump();
  } else if (role_ == Role::Client) {
    server_.pump();
    std::vector<uint8_t> m;
    while (server_.receive(m)) handle(nullptr, m);
    if (server_.failed() && state_ != State::Ended) {
      if (state_ == State::Playing && session_ && !reconnecting_) {
        // Keep the seat: try to get back in, replaying the battle when we do.
        reconnecting_ = true;
        giveUpAt_ = nowMs() + kRejoinSeconds * 1000;
        nextRetry_ = 0;
        status = "connection lost - reconnecting...";
      } else if (!reconnecting_) {
        status = state_ == State::Connecting ? "couldn't reach the host" : "lost connection to the host";
        state_ = State::Ended;
      }
    }
    if (reconnecting_) tryReconnect();
    server_.pump();
  }
}

void LanSession::tryReconnect() {
  int64_t t = nowMs();
  if (t > giveUpAt_) {
    reconnecting_ = false;
    state_ = State::Ended;
    status = "couldn't reconnect to the host";
    return;
  }
  if (server_.open() && !server_.failed()) return;  // attempt in progress
  if (t < nextRetry_) return;
  nextRetry_ = t + 3000;
  std::string err;
  if (server_.connect(addr_, port_, &err)) {
    if (!roomCode_.empty()) {  // through a relay: back into the room first
      W j{static_cast<Msg>(relay::RJoin)};
      j.str(roomCode_);
      server_.send(j.b);
    }
    sendHello();
  }
}

void LanSession::dropPeer(Peer& p, const std::string& why) {
  int pl = p.player;
  p.close();
  if (!pl) return;
  names[size_t(pl - 1)].clear();
  if (state_ == State::Lobby) {
    sendAll(lobbyMsg());
  } else if (state_ == State::Playing && (required_[size_t(pl)] || away_[size_t(pl)])) {
    // Their army stands idle and their seat is kept for a while; the battle goes
    // on for everyone else. If they don't come back in time, they surrender.
    required_[size_t(pl)] = false;
    away_[size_t(pl)] = true;
    awayUntil_[size_t(pl)] = nowMs() + kRejoinSeconds * 1000;
    status = "player " + std::to_string(pl) + " " + why + " - seat kept for " + std::to_string(kRejoinSeconds / 60) +
             " minutes";
  }
}

void LanSession::handle(Peer* from, const std::vector<uint8_t>& m) {
  if (m.empty()) return;
  if (!from && m[0] == relay::RJoined) return;
  if (!from && m[0] == relay::RNoRoom) {
    status = "no room with that code";
    state_ = State::Ended;
    reconnecting_ = false;
    return;
  }
  R r(m);
  switch (Msg(m[0])) {
    case Hello: {  // host <- client
      if (!from || from->hello) return;
      uint32_t proto = r.u32();
      std::string name = r.str();
      uint32_t session = r.u32();
      int seat = r.u8();
      if (!r.ok || proto != kProtocol) {
        from->send(W(Reject).str("different game version").b);
        from->close();
        return;
      }
      if (state_ == State::Playing && session == session_ && seat >= 2 && seat <= 8 && away_[size_t(seat)]) {
        // Welcome back: send the setup and every turn so far; they replay to catch up.
        from->hello = true;
        from->player = seat;
        from->name = name;
        names[size_t(seat - 1)] = name;
        away_[size_t(seat)] = false;
        from->send(W(Welcome).u8(uint8_t(seat)).b);
        from->send(W(Start).str(cfg_.encode()).u32(session_).b);
        for (auto& [turn, cs] : history_) from->send(W(Turn).u32(turn).cmds(cs).b);
        status = name + " is back - catching up";
        return;
      }
      if (state_ != State::Lobby) {
        from->send(W(Reject).str("the battle has already started").b);
        return;
      }
      int slot = -1;
      for (int i = 1; i < 8 && slot < 0; ++i)
        if (cfg_.slots[size_t(i)].kind == SkirmishSlot::Human && names[size_t(i)].empty()) slot = i;
      if (slot < 0) {
        from->send(W(Reject).str("no free player slot").b);
        return;
      }
      from->hello = true;
      from->player = slot + 1;
      from->name = name.empty() ? "Player " + std::to_string(slot + 1) : name.substr(0, 24);
      names[size_t(slot)] = from->name;
      from->send(W(Welcome).u8(uint8_t(from->player)).b);
      sendAll(lobbyMsg());
      break;
    }
    case Welcome:  // client <- host
      local_ = r.u8();
      if (r.ok) state_ = State::Lobby, status = "";
      break;
    case Lobby: {
      SkirmishConfig c;
      std::string text = r.str();
      std::array<std::string, 8> n;
      for (auto& s : n) s = r.str();
      if (r.ok && SkirmishConfig::decode(text, c)) cfg_ = c, names = n;
      break;
    }
    case Start: {
      SkirmishConfig c;
      std::string text = r.str();
      uint32_t session = r.u32();
      if (r.ok && SkirmishConfig::decode(text, c)) {
        for (int i = 0; i < 8; ++i) c.slots[size_t(i)].name = names[size_t(i)];
        cfg_ = c;
        session_ = session;
        state_ = State::Playing;
        started_ = true;
        // Fresh lockstep state: the battle (re)starts from turn 0 and replays.
        turns_.clear();
        pending_.clear();
        sealed_ = -1;
        if (reconnecting_) status = "reconnected - catching up...";
        reconnecting_ = false;
      }
      break;
    }
    case Reject:
      status = r.str();
      if (role_ == Role::Client) state_ = State::Ended;
      break;
    case Cmds: {  // host <- client: their batch for a turn
      if (!from || !from->player) return;
      uint32_t turn = r.u32();
      auto cs = r.cmds();
      if (!r.ok) return;
      for (auto& c : cs) c.player = uint8_t(from->player);  // nobody gives orders for someone else
      if (!required_[size_t(from->player)] && state_ == State::Playing) {
        if (turn < nextFinal_) return;  // still replaying old turns
        required_[size_t(from->player)] = true;  // caught up: wait for them again
        status = from->name + " has caught up";
      }
      addBatch(turn, from->player, cs);
      break;
    }
    case Turn: {  // client <- host: a complete turn
      uint32_t turn = r.u32();
      auto cs = r.cmds();
      if (r.ok) turns_[turn] = cs;
      break;
    }
    case Hash: {  // host <- client
      if (!from || !from->player) return;
      uint32_t turn = r.u32();
      uint64_t h = r.u64();
      if (!r.ok) return;
      theirHashes_[turn][from->player] = h;
      if (myHashes_.count(turn) && myHashes_[turn] != h) {
        desync = true;
        status = "DESYNC with player " + std::to_string(from->player) + " at turn " + std::to_string(turn);
        sendAll(W(Desync).u32(turn).b);
      }
      break;
    }
    case Chat: {
      uint8_t pl = r.u8();
      std::string name = r.str(), text = r.str().substr(0, 120);
      if (!r.ok) return;
      if (role_ == Role::Host) {  // relay with the sender's real seat and name
        if (!from || !from->player) return;
        chat.push_back({from->player, from->name, text});
        sendAll(W(Chat).u8(uint8_t(from->player)).str(from->name).str(text).b);
      } else {
        chat.push_back({pl, name, text});
      }
      break;
    }
    case Desync:
      desync = true;
      status = "DESYNC at turn " + std::to_string(r.u32());
      break;
  }
}

// ---- Lockstep -----------------------------------------------------------------------

void LanSession::issue(const Command& c) {
  if (state_ == State::Playing) pending_.push_back(c);
}

void LanSession::addBatch(uint32_t turn, int player, const std::vector<Command>& cmds) {
  if (turn < nextFinal_) return;  // too late (shouldn't happen: peers can't run ahead)
  auto& v = batches_[turn][player];
  v.insert(v.end(), cmds.begin(), cmds.end());
  batches_[turn][player];  // mark the batch as received even when it's empty
}

void LanSession::tryFinalize() {
  for (;;) {
    auto it = batches_.find(nextFinal_);
    bool complete = it != batches_.end();
    if (complete)
      for (int p = 1; p <= 8; ++p)
        if (required_[size_t(p)] && !it->second.count(p)) complete = false;
    if (!complete) return;
    std::vector<Command> merged;  // fixed order: by player, then as issued
    for (auto& [pl, cs] : it->second) merged.insert(merged.end(), cs.begin(), cs.end());
    turns_[nextFinal_] = merged;
    history_[nextFinal_] = merged;
    sendAll(W(Turn).u32(nextFinal_).cmds(merged).b);
    batches_.erase(it);
    ++nextFinal_;
  }
}

bool LanSession::tickCommands(uint32_t tick, const std::function<uint64_t()>& hashFn, std::vector<Command>& out) {
  out.clear();
  if (state_ != State::Playing) return false;
  if (tick % kTicksPerTurn) return true;
  uint32_t turn = tick / kTicksPerTurn;
  // Passing a turn boundary: our orders since the last one go out for turn + delay.
  if (int64_t(turn) > sealed_) {
    sealed_ = turn;
    uint32_t target = turn + kDelayTurns;
    if (role_ == Role::Host) {
      addBatch(target, local_, pending_);
      tryFinalize();
    } else {
      server_.send(W(Cmds).u32(target).cmds(pending_).b);
      server_.pump();
    }
    pending_.clear();
    if (turn % kHashEvery == 0) {
      uint64_t stateHash = hashFn();
      if (role_ == Role::Host) {
        myHashes_[turn] = stateHash;
        for (auto& [pl, h] : theirHashes_[turn])
          if (h != stateHash) {
            desync = true;
            status = "DESYNC with player " + std::to_string(pl) + " at turn " + std::to_string(turn);
            sendAll(W(Desync).u32(turn).b);
          }
      } else {
        server_.send(W(Hash).u32(turn).u64(stateHash).b);
      }
    }
  }
  if (turn < kDelayTurns) return true;  // the first turns are empty by definition
  auto it = turns_.find(turn);
  if (it == turns_.end()) {
    status = "waiting for other players...";
    return false;
  }
  out = it->second;
  turns_.erase(it);
  if (status == "waiting for other players...") status.clear();
  return true;
}

}  // namespace ns
