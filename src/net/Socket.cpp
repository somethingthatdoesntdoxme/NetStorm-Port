#include "net/Socket.h"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socklen_t = int;
static int lastErr() { return WSAGetLastError(); }
static bool wouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY; }
static void closeFd(intptr_t fd) { closesocket(SOCKET(fd)); }
static void setNonBlocking(intptr_t fd) {
  u_long on = 1;
  ioctlsocket(SOCKET(fd), FIONBIO, &on);
}
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
static int lastErr() { return errno; }
static bool wouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS; }
static void closeFd(intptr_t fd) { ::close(int(fd)); }
static void setNonBlocking(intptr_t fd) { fcntl(int(fd), F_SETFL, fcntl(int(fd), F_GETFL, 0) | O_NONBLOCK); }
#endif

namespace ns::net {

bool init() {
#ifdef _WIN32
  static bool done = false;
  if (!done) {
    WSADATA d;
    done = WSAStartup(MAKEWORD(2, 2), &d) == 0;
  }
  return done;
#else
  return true;
#endif
}

Tcp& Tcp::operator=(Tcp&& o) noexcept {
  if (this != &o) {
    close();
    fd_ = o.fd_, connecting_ = o.connecting_, failed_ = o.failed_;
    in_ = std::move(o.in_), out_ = std::move(o.out_);
    o.fd_ = -1;
  }
  return *this;
}

void Tcp::close() {
  if (fd_ >= 0) closeFd(fd_);
  fd_ = -1;
  connecting_ = false;
  in_.clear(), out_.clear();
}

static void noDelay(intptr_t fd) {
  int one = 1;
  setsockopt(decltype(socket(0, 0, 0))(fd), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
}

bool Tcp::listen(uint16_t port, std::string* err) {
  init();
  close();
  auto s = socket(AF_INET, SOCK_STREAM, 0);
  if (intptr_t(s) < 0) return err && (*err = "socket() failed", false);
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(s, 8) != 0) {
    closeFd(intptr_t(s));
    if (err) *err = "port " + std::to_string(port) + " is in use";
    return false;
  }
  fd_ = intptr_t(s);
  setNonBlocking(fd_);
  failed_ = false;
  return true;
}

bool Tcp::accept(Tcp& out) {
  if (fd_ < 0) return false;
  auto c = ::accept(decltype(socket(0, 0, 0))(fd_), nullptr, nullptr);
  if (intptr_t(c) < 0) return false;
  out.close();
  out.fd_ = intptr_t(c);
  out.failed_ = false;
  setNonBlocking(out.fd_);
  noDelay(out.fd_);
  return true;
}

bool Tcp::connect(const std::string& host, uint16_t port, std::string* err) {
  init();
  close();
  addrinfo hints{}, *res = nullptr;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
    if (err) *err = "can't find " + host;
    return false;
  }
  auto s = socket(AF_INET, SOCK_STREAM, 0);
  setNonBlocking(intptr_t(s));
  noDelay(intptr_t(s));
  int r = ::connect(s, res->ai_addr, socklen_t(res->ai_addrlen));
  freeaddrinfo(res);
  if (r != 0 && !wouldBlock(lastErr())) {
    closeFd(intptr_t(s));
    if (err) *err = "can't connect to " + host;
    return false;
  }
  fd_ = intptr_t(s);
  connecting_ = r != 0;
  failed_ = false;
  return true;
}

void Tcp::send(const std::vector<uint8_t>& payload) {
  uint32_t n = uint32_t(payload.size());
  for (int i = 0; i < 4; ++i) out_.push_back(uint8_t(n >> (8 * i)));
  out_.insert(out_.end(), payload.begin(), payload.end());
}

bool Tcp::receive(std::vector<uint8_t>& payload) {
  if (in_.size() < 4) return false;
  uint32_t n = uint32_t(in_[0]) | uint32_t(in_[1]) << 8 | uint32_t(in_[2]) << 16 | uint32_t(in_[3]) << 24;
  if (n > (8u << 20)) {  // nonsense length: drop the connection
    failed_ = true;
    return false;
  }
  if (in_.size() < 4 + size_t(n)) return false;
  payload.assign(in_.begin() + 4, in_.begin() + 4 + long(n));
  in_.erase(in_.begin(), in_.begin() + 4 + long(n));
  return true;
}

void Tcp::pump() {
  if (fd_ < 0 || failed_) return;
  auto s = decltype(socket(0, 0, 0))(fd_);
  if (connecting_) {
    fd_set w;
    FD_ZERO(&w);
    FD_SET(s, &w);
    timeval tv{0, 0};
    if (select(int(s) + 1, nullptr, &w, nullptr, &tv) <= 0) return;
    int e = 0;
    socklen_t len = sizeof e;
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&e), &len);
    if (e != 0) {
      failed_ = true;
      return;
    }
    connecting_ = false;
  }
  while (!out_.empty()) {
    auto n = ::send(s, reinterpret_cast<const char*>(out_.data()), int(out_.size()), 0);
    if (n > 0) {
      out_.erase(out_.begin(), out_.begin() + n);
      continue;
    }
    if (n < 0 && wouldBlock(lastErr())) break;
    failed_ = true;
    return;
  }
  char buf[16384];
  for (;;) {
    auto n = ::recv(s, buf, sizeof buf, 0);
    if (n > 0) {
      in_.insert(in_.end(), buf, buf + n);
      continue;
    }
    if (n < 0 && wouldBlock(lastErr())) break;
    failed_ = true;  // 0 = closed by the other side
    break;
  }
}

std::string Tcp::peer() const {
  sockaddr_in a{};
  socklen_t len = sizeof a;
  if (fd_ < 0 || getpeername(decltype(socket(0, 0, 0))(fd_), reinterpret_cast<sockaddr*>(&a), &len) != 0) return "?";
  char b[64];
  inet_ntop(AF_INET, &a.sin_addr, b, sizeof b);
  return b;
}

bool Udp::bind(uint16_t port) {
  init();
  close();
  auto s = socket(AF_INET, SOCK_DGRAM, 0);
  if (intptr_t(s) < 0) return false;
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof one);
  setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
    closeFd(intptr_t(s));
    return false;
  }
  fd_ = intptr_t(s);
  setNonBlocking(fd_);
  return true;
}

bool Udp::sendTo(const std::string& addr, uint16_t port, const std::string& msg) {
  if (fd_ < 0) return false;
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  if (inet_pton(AF_INET, addr.c_str(), &a.sin_addr) != 1) return false;
  return ::sendto(decltype(socket(0, 0, 0))(fd_), msg.data(), int(msg.size()), 0, reinterpret_cast<sockaddr*>(&a),
                  sizeof a) == int(msg.size());
}

bool Udp::sendBroadcast(uint16_t port, const std::string& msg) { return sendTo("255.255.255.255", port, msg); }

bool Udp::receive(std::string& msg, std::string& from, uint16_t& fromPort) {
  if (fd_ < 0) return false;
  char buf[1500];
  sockaddr_in a{};
  socklen_t len = sizeof a;
  auto n = ::recvfrom(decltype(socket(0, 0, 0))(fd_), buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&a), &len);
  if (n <= 0) return false;
  msg.assign(buf, size_t(n));
  char b[64];
  inet_ntop(AF_INET, &a.sin_addr, b, sizeof b);
  from = b;
  fromPort = ntohs(a.sin_port);
  return true;
}

void Udp::close() {
  if (fd_ >= 0) closeFd(fd_);
  fd_ = -1;
}

}  // namespace ns::net
