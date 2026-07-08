#include "NetworkStreamClient.hpp"

#include <iostream>
#include <print>

// OS依存ヘッダー
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

struct NetworkStreamClient::Impl {
#if defined(_WIN32)
  SOCKET fd = INVALID_SOCKET;
#else
  int fd = -1;
#endif
  struct addrinfo *addr_res = nullptr;
  bool wsa_initialized = false;

  Impl() {
#if defined(_WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
      wsa_initialized = true;
    } else {
      std::println(std::cerr, "WSAStartup failed");
    }
#endif
  }

  ~Impl() { cleanup(); }

  void cleanup() {
#if defined(_WIN32)
    if (fd != INVALID_SOCKET) {
      closesocket(fd);
      fd = INVALID_SOCKET;
    }
    if (addr_res) {
      freeaddrinfo(addr_res);
      addr_res = nullptr;
    }
    if (wsa_initialized) {
      WSACleanup();
      wsa_initialized = false;
    }
#else
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
    if (addr_res) {
      freeaddrinfo(addr_res);
      addr_res = nullptr;
    }
#endif
  }
};

NetworkStreamClient::NetworkStreamClient() : pImpl(std::make_unique<Impl>()) {}
NetworkStreamClient::~NetworkStreamClient() = default;
NetworkStreamClient::NetworkStreamClient(NetworkStreamClient &&) noexcept =
    default;
NetworkStreamClient &
NetworkStreamClient::operator=(NetworkStreamClient &&) noexcept = default;

bool NetworkStreamClient::connect(const std::string &host,
                                  const std::string &port) {
  if (pImpl->fd !=
#if defined(_WIN32)
      INVALID_SOCKET
#else
      -1
#endif
  ) {
    return false; // すでに接続されている
  }

  struct addrinfo hints = {};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_INET;

  int result =
      getaddrinfo(host.c_str(), port.c_str(), &hints, &pImpl->addr_res);
  if (result) {
    std::println(std::cerr, "info error: {}", result);
    return false;
  }

  pImpl->fd = socket(pImpl->addr_res->ai_family, pImpl->addr_res->ai_socktype,
                     pImpl->addr_res->ai_protocol);
#if defined(_WIN32)
  if (pImpl->fd == INVALID_SOCKET) {
    std::println(std::cerr, "error socket:{}", WSAGetLastError());
    return false;
  }
#else
  if (pImpl->fd < 0) {
    std::println(std::cerr, "error socket:{}", errno);
    return false;
  }
#endif

  if (::connect(pImpl->fd, pImpl->addr_res->ai_addr,
                (int)pImpl->addr_res->ai_addrlen) < 0) {
#if defined(_WIN32)
    std::println(std::cerr, "failed connect: {};", WSAGetLastError());
#else
    std::println(std::cerr, "failed connect: {};", errno);
#endif
    return false;
  }

  return true;
}

void NetworkStreamClient::close() { pImpl->cleanup(); }

int NetworkStreamClient::send_data(const void *buf, size_t len) {
#if defined(_WIN32)
  if (pImpl->fd == INVALID_SOCKET)
    return -1;
  return send(pImpl->fd, static_cast<const char *>(buf), static_cast<int>(len),
              0);
#else
  if (pImpl->fd < 0)
    return -1;
  return send(pImpl->fd, static_cast<const char *>(buf), len, 0);
#endif
}

int NetworkStreamClient::recv_data(void *buf, size_t len) {
#if defined(_WIN32)
  if (pImpl->fd == INVALID_SOCKET)
    return -1;
  return recv(pImpl->fd, static_cast<char *>(buf), static_cast<int>(len), 0);
#else
  if (pImpl->fd < 0)
    return -1;
  return recv(pImpl->fd, static_cast<char *>(buf), len, 0);
#endif
}

int NetworkStreamClient::recv_exact(void *buf, size_t len) {
  size_t recvd = 0;
  char *p = (char *)buf;

  while (recvd < len) {
    int n = recv_data(p + recvd, len - recvd);
    if (n == 0)
      return 0; // closed
    if (n < 0)
      return -1; // error
    recvd += n;
  }
  return (int)recvd;
}

bool NetworkStreamClient::send_window_size(uint16_t w, uint16_t h) {
  uint16_t size_packet[2] = {htons(w), htons(h)};
  return send_data(size_packet, sizeof(size_packet)) == sizeof(size_packet);
}

int NetworkStreamClient::recv_packet_header(uint32_t &out_len, uint8_t &out_w,
                                            uint8_t &out_h) {
  uint32_t net_len;
  int n = recv_data(&net_len, 4);
  if (n <= 0)
    return n;

  out_len = ntohl(net_len);

  uint8_t meta[2];
  n = recv_data(meta, sizeof(meta));
  if (n <= 0)
    return n;

  out_w = meta[0];
  out_h = meta[1];

  return 1;
}

intptr_t NetworkStreamClient::get_socket_handle() const {
  return static_cast<intptr_t>(pImpl->fd);
}
