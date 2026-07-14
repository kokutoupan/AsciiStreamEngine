#include "NetworkStreamClient.hpp"

#include <cstring>
#include <iostream>
#include <print>
#include <vector>
#include <random>
#include <algorithm>


#include <monocypher.h>

#include <astream/EncryptedStream.hpp>

// OS依存ヘッダー
#if defined(_WIN32) || defined(_WIN64)
#define NOMINMAX
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

  EncryptedStream stream;
  std::vector<uint8_t> decrypted_recv_buffer;
  size_t decrypted_recv_offset = 0;

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

// Forward declarations for raw helpers
static int send_data_raw(intptr_t fd, const void *buf, size_t len);
static int recv_data_raw(intptr_t fd, void *buf, size_t len);
static int recv_exact_raw(intptr_t fd, void *buf, size_t len);

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

  // Handshake to receive server encryption choice
  uint8_t mode_byte = 0;
  if (recv_exact_raw(pImpl->fd, &mode_byte, 1) <= 0) {
    std::println(std::cerr,
                 "failed to receive encryption mode byte from server");
    pImpl->cleanup();
    return false;
  }

  if (mode_byte == 1) {
    uint8_t server_public_key[32];
    if (recv_exact_raw(pImpl->fd, server_public_key, 32) <= 0) {
      std::println(std::cerr, "failed to receive server public key");
      pImpl->cleanup();
      return false;
    }

    // Generate ephemeral client keys
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 255);
    uint8_t client_secret_key[32];
    for (auto &b : client_secret_key) {
      b = static_cast<uint8_t>(dist(rd));
    }

    uint8_t client_public_key[32];
    crypto_x25519_public_key(client_public_key, client_secret_key);

    // Send client public key to server
    if (send_data_raw(pImpl->fd, client_public_key, 32) != 32) {
      std::println(std::cerr, "failed to send client public key to server");
      pImpl->cleanup();
      return false;
    }

    // Compute raw shared secret
    uint8_t raw_shared_secret[32];
    crypto_x25519(raw_shared_secret, client_secret_key, server_public_key);

    // Derive session key and nonces
    uint8_t shared_key[32];
    uint8_t tx_base_nonce[24];
    uint8_t rx_base_nonce[24];

    crypto_blake2b_keyed(shared_key, 32, raw_shared_secret, 32,
                         reinterpret_cast<const uint8_t *>("key"), 3);

    uint8_t tx_nonce_buf[32];
    crypto_blake2b_keyed(tx_nonce_buf, 32, raw_shared_secret, 32,
                         reinterpret_cast<const uint8_t *>("client_to_server"), 16);
    std::memcpy(tx_base_nonce, tx_nonce_buf, 24);

    uint8_t rx_nonce_buf[32];
    crypto_blake2b_keyed(rx_nonce_buf, 32, raw_shared_secret, 32,
                         reinterpret_cast<const uint8_t *>("server_to_client"), 16);
    std::memcpy(rx_base_nonce, rx_nonce_buf, 24);

    pImpl->stream.initialize_encryption(EncryptedStream::Mode::Encrypted,
                                        shared_key, tx_base_nonce,
                                        rx_base_nonce);
  } else {
    uint8_t dummy[32] = {0};
    pImpl->stream.initialize_encryption(EncryptedStream::Mode::Plaintext, dummy,
                                        dummy, dummy);
  }

  return true;
}

void NetworkStreamClient::close() { pImpl->cleanup(); }

// Raw socket send helper
static int send_data_raw(intptr_t fd, const void *buf, size_t len) {
#if defined(_WIN32)
  if (fd == INVALID_SOCKET)
    return -1;
  return send(static_cast<SOCKET>(fd), static_cast<const char *>(buf),
              static_cast<int>(len), 0);
#else
  if (fd < 0)
    return -1;
  return send(static_cast<int>(fd), static_cast<const char *>(buf), len, 0);
#endif
}

// Raw socket recv helper
static int recv_data_raw(intptr_t fd, void *buf, size_t len) {
#if defined(_WIN32)
  if (fd == INVALID_SOCKET)
    return -1;
  return recv(static_cast<SOCKET>(fd), static_cast<char *>(buf),
              static_cast<int>(len), 0);
#else
  if (fd < 0)
    return -1;
  return recv(static_cast<int>(fd), static_cast<char *>(buf), len, 0);
#endif
}

// Raw socket recv exact helper
static int recv_exact_raw(intptr_t fd, void *buf, size_t len) {
  size_t recvd = 0;
  char *p = (char *)buf;
  while (recvd < len) {
    int n = recv_data_raw(fd, p + recvd, len - recvd);
    if (n == 0)
      return 0; // closed
    if (n < 0)
      return -1; // error
    recvd += n;
  }
  return (int)recvd;
}

int NetworkStreamClient::send_data(const void *buf, size_t len) {
  if (pImpl->stream.get_mode() == EncryptedStream::Mode::Plaintext) {
    return send_data_raw(pImpl->fd, buf, len);
  } else {
    std::vector<uint8_t> wrapped =
        pImpl->stream.wrap_outgoing(static_cast<const uint8_t *>(buf), len);
    int n = send_data_raw(pImpl->fd, wrapped.data(), wrapped.size());
    if (n <= 0)
      return n;
    return (int)len;
  }
}

int NetworkStreamClient::recv_data(void *buf, size_t len) {
  if (pImpl->stream.get_mode() == EncryptedStream::Mode::Plaintext) {
    return recv_data_raw(pImpl->fd, buf, len);
  } else {
    if (pImpl->decrypted_recv_buffer.empty()) {
      return -1;
    }
    size_t available =
        pImpl->decrypted_recv_buffer.size() - pImpl->decrypted_recv_offset;
    size_t to_read = std::min(len, available);
    std::memcpy(
        buf, pImpl->decrypted_recv_buffer.data() + pImpl->decrypted_recv_offset,
        to_read);
    pImpl->decrypted_recv_offset += to_read;
    if (pImpl->decrypted_recv_offset == pImpl->decrypted_recv_buffer.size()) {
      pImpl->decrypted_recv_buffer.clear();
      pImpl->decrypted_recv_offset = 0;
    }
    return (int)to_read;
  }
}

int NetworkStreamClient::recv_exact(void *buf, size_t len) {
  if (pImpl->stream.get_mode() == EncryptedStream::Mode::Plaintext) {
    size_t recvd = 0;
    char *p = (char *)buf;
    while (recvd < len) {
      int n = recv_data_raw(pImpl->fd, p + recvd, len - recvd);
      if (n == 0)
        return 0; // closed
      if (n < 0)
        return -1; // error
      recvd += n;
    }
    return (int)recvd;
  } else {
    if (pImpl->decrypted_recv_offset + len >
        pImpl->decrypted_recv_buffer.size()) {
      std::println(
          std::cerr, "Buffer underflow: requested {}, available {}", len,
          pImpl->decrypted_recv_buffer.size() - pImpl->decrypted_recv_offset);
      return -1;
    }
    std::memcpy(
        buf, pImpl->decrypted_recv_buffer.data() + pImpl->decrypted_recv_offset,
        len);
    pImpl->decrypted_recv_offset += len;
    if (pImpl->decrypted_recv_offset == pImpl->decrypted_recv_buffer.size()) {
      pImpl->decrypted_recv_buffer.clear();
      pImpl->decrypted_recv_offset = 0;
    }
    return (int)len;
  }
}

bool NetworkStreamClient::send_window_size(uint16_t w, uint16_t h) {
  uint16_t size_packet[2] = {htons(w), htons(h)};
  return send_data(size_packet, sizeof(size_packet)) == sizeof(size_packet);
}

int NetworkStreamClient::recv_packet_header(uint32_t &out_len, uint8_t &out_w,
                                            uint8_t &out_h) {
  if (pImpl->stream.get_mode() == EncryptedStream::Mode::Plaintext) {
    uint32_t net_len;
    int n = recv_exact_raw(pImpl->fd, &net_len, 4);
    if (n <= 0)
      return n;

    out_len = ntohl(net_len);

    uint8_t meta[2];
    n = recv_exact_raw(pImpl->fd, meta, sizeof(meta));
    if (n <= 0)
      return n;

    out_w = meta[0];
    out_h = meta[1];

    return 1;
  } else {
    uint8_t frame_header[2];
    if (recv_exact_raw(pImpl->fd, frame_header, 2) <= 0) {
      return -1;
    }
    uint16_t frame_payload_size = (frame_header[0] << 8) | frame_header[1];
    std::vector<uint8_t> frame(2 + frame_payload_size + 16);
    frame[0] = frame_header[0];
    frame[1] = frame_header[1];
    if (recv_exact_raw(pImpl->fd, frame.data() + 2, frame_payload_size + 16) <=
        0) {
      return -1;
    }
    std::vector<uint8_t> decrypted;
    if (!pImpl->stream.unwrap_incoming(frame, decrypted)) {
      std::println(std::cerr, "Decryption error on client!");
      return -1;
    }
    if (decrypted.size() < 6) {
      return -1;
    }
    uint32_t net_len;
    std::memcpy(&net_len, decrypted.data(), 4);
    out_len = ntohl(net_len);
    out_w = decrypted[4];
    out_h = decrypted[5];
    pImpl->decrypted_recv_buffer.assign(decrypted.begin() + 6, decrypted.end());
    pImpl->decrypted_recv_offset = 0;
    return 1;
  }
}

intptr_t NetworkStreamClient::get_socket_handle() const {
  return static_cast<intptr_t>(pImpl->fd);
}
