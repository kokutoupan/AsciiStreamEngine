#pragma once
#include <cstdint>
#include <memory>
#include <string>

class NetworkStreamClient {
public:
  NetworkStreamClient();
  ~NetworkStreamClient();

  // コピー禁止、ムーブ許可
  NetworkStreamClient(const NetworkStreamClient &) = delete;
  NetworkStreamClient &operator=(const NetworkStreamClient &) = delete;
  NetworkStreamClient(NetworkStreamClient &&) noexcept;
  NetworkStreamClient &operator=(NetworkStreamClient &&) noexcept;

  bool connect(const std::string &host, const std::string &port);
  void close();

  int send_data(const void *buf, size_t len);
  int recv_data(void *buf, size_t len);
  int recv_exact(void *buf, size_t len);

  // プロトコルヘルパー（内部でバイトオーダー変換を処理）
  bool send_window_size(uint16_t w, uint16_t h);
  int recv_packet_header(uint32_t &out_len, uint8_t &out_w, uint8_t &out_h);

  // 内部のソケット記述子を取得（pollで使用するため）
  intptr_t get_socket_handle() const;

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};
