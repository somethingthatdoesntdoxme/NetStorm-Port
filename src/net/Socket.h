#pragma once
// Minimal non-blocking sockets (POSIX + Winsock): TCP for the game, UDP
// broadcast to find games on the local network.
#include <cstdint>
#include <string>
#include <vector>

namespace ns::net {

bool init();  // WSAStartup on Windows; no-op elsewhere

class Tcp {
 public:
  Tcp() = default;
  ~Tcp() { close(); }
  Tcp(const Tcp&) = delete;
  Tcp& operator=(const Tcp&) = delete;
  Tcp(Tcp&& o) noexcept : fd_(o.fd_), in_(std::move(o.in_)), out_(std::move(o.out_)) { o.fd_ = -1; }
  Tcp& operator=(Tcp&& o) noexcept;

  bool listen(uint16_t port, std::string* err = nullptr);
  bool accept(Tcp& out);  // non-blocking: false if nobody's waiting
  // Starts a connection; poll connected() until true (or failed()).
  bool connect(const std::string& host, uint16_t port, std::string* err = nullptr);
  bool connecting() const { return fd_ >= 0 && connecting_; }
  bool open() const { return fd_ >= 0 && !failed_; }
  bool failed() const { return failed_; }
  void close();
  void abort() {  // drop the connection as if the network failed (tests)
    close();
    failed_ = true;
  }

  // Framed messages: u32 length (LE) + payload. pump() moves bytes both ways.
  void send(const std::vector<uint8_t>& payload);
  bool receive(std::vector<uint8_t>& payload);  // one complete frame, if any
  void pump();
  std::string peer() const;

 private:
  intptr_t fd_ = -1;
  bool connecting_ = false, failed_ = false;
  std::vector<uint8_t> in_, out_;
};

class Udp {
 public:
  ~Udp() { close(); }
  bool bind(uint16_t port);  // 0 = any port
  bool sendBroadcast(uint16_t port, const std::string& msg);
  bool sendTo(const std::string& addr, uint16_t port, const std::string& msg);
  bool receive(std::string& msg, std::string& fromAddr, uint16_t& fromPort);
  void close();

 private:
  intptr_t fd_ = -1;
};

}  // namespace ns::net
