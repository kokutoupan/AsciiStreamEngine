#pragma once
#include <array>
#include <cstring>
#include <vector>
#include <zlib.h>

#include <astream/Config.hpp>
#include <astream/net_internal/EncryptedStream.hpp> // OS固有のネットワーク・システムコールヘッダーの切り替え
#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h の max/min マクロが std::max と衝突するのを防ぐ
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace astream::detail::net {

// エラーが「データがまだ届いてないだけ（正常）」かどうかを判定するヘルパー
inline bool IsWouldBlock() {
#if defined(_WIN32)
  return WSAGetLastError() == WSAEWOULDBLOCK;
#else
  return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

// zlibによる圧縮送信ヘルパー (引数の型キャストをOSに合わせて調整)
inline int send_engine_frame_compressed(int sock, const char *raw_data,
                                        size_t raw_len, uint8_t width,
                                        uint8_t height,
                                        std::vector<uint8_t> &comp_buf,
                                        EncryptedStream &stream) {
  // 1. 必要十分な圧縮バッファを確保（初回やサイズ変更時のみ resize が走る）
  uLongf max_comp_len = compressBound(raw_len);
  if (comp_buf.size() != max_comp_len) {
    comp_buf.resize(max_comp_len);
  }

  // 2. raw_data から直接圧縮
  uLongf comp_len = max_comp_len;
  int res =
      compress(comp_buf.data(), &comp_len, (const Bytef *)raw_data, raw_len);
  if (res != Z_OK)
    return -1;

  if (stream.get_mode() == EncryptedStream::Mode::Plaintext) {
    // 3. サイズ（4バイト）を送信
    uint32_t net_len = htonl((uint32_t)comp_len);
    if (send(sock, (const char *)&net_len, sizeof(net_len), 0) <= 0)
      return -1;

    // 4. width と height（計2バイト）を直接送信
    uint8_t meta[2] = {width, height};
    if (send(sock, (const char *)meta, sizeof(meta), 0) <= 0)
      return -1;

    // 5. 圧縮データを送信
    return send(sock, (const char *)comp_buf.data(), (int)comp_len, 0);
  } else {
    // Encrypted mode: construct payload [4 bytes net_len] [1 byte width] [1
    // byte height] [comp_len bytes compressed data]
    std::vector<uint8_t> payload(6 + comp_len);
    uint32_t net_len = htonl((uint32_t)comp_len);
    std::memcpy(payload.data(), &net_len, 4);
    payload[4] = width;
    payload[5] = height;
    std::memcpy(payload.data() + 6, comp_buf.data(), comp_len);

    std::vector<uint8_t> wrapped =
        stream.wrap_outgoing(payload.data(), payload.size());
    return send(sock, (const char *)wrapped.data(), (int)wrapped.size(), 0);
  }
}

// 0: スキップ, 0 < データ量, 0 > エラー
template <astream::EngineConfig Config>
inline int recv_encrypted_frame(int fd, EncryptedStream &stream,
                                std::vector<uint8_t> &out_plain) {
  std::array<uint8_t, Config.max_receive_frame_size> peek_buf;
#if defined(_WIN32)
  int n = recv(fd, (char *)peek_buf.data(), peek_buf.size(), MSG_PEEK);
#else
  int n = recv(fd, (char *)peek_buf.data(), peek_buf.size(),
               MSG_PEEK | MSG_DONTWAIT);
#endif

  if (n == 0) {
    return -1;
  }
  if (n < 2) {
    if (IsWouldBlock()) {
      return 0; // ★重要: 単に「今データがないだけ」なら 0 を返す
    }
    return -1; // 本物のエラーは切断扱いにする
  }

  uint16_t size = (peek_buf[0] << 8) | peek_buf[1];
  size_t frame_size = 2 + size + 16;

  if (frame_size > Config.max_receive_frame_size) {
    return -1; // 即座に切断
  }

  if (n < (int)frame_size) {
    return 0; // まだ足りないので、1バイトも消費せず次のポーリングに完全丸投げ
  }

  std::vector<uint8_t> frame(frame_size);
  int read_bytes = recv(fd, (char *)frame.data(), frame_size, 0);
  if (read_bytes != (int)frame_size) {
    return -1; // 基本的には通らないはずだが、万が一のソケットエラー用
  }

  if (!stream.unwrap_incoming(frame, out_plain)) {
    return -1; // 改ざん、またはカウンターズレ（リプレイ攻撃）を検知して切断
  }

  return (int)out_plain.size();
}

} // namespace astream::detail::net
