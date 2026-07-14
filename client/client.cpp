#include <cstdint>
#include <format>
#include <iostream>
#include <print>
#include <random>
#include <span>
#include <string>
#include <string_view>

#include <astream/graphics/Texture2D.hpp>

#include "./NetworkStreamClient.hpp"
#include "./TerminalBuffer.hpp"
#include "./TerminalController.hpp"

// zlib は双方共通
#include <zconf.h>
#include <zlib.h>

constexpr std::string_view HOST_PORT = "12345";
constexpr std::string_view HOST_DOMAIN = "localhost";

#ifndef USE_ASYNC_KEY_STATE
#define USE_ASYNC_KEY_STATE 0
#endif

// =============================================================================
// メイン関数
// =============================================================================
int main(int argc, char *argv[]) {
  std::string unique_title;
#if defined(_WIN32)
  // 1. ハードウェア由来のシードを使ってメルセンヌ・ツイスタ乱数生成器を初期化
  std::random_device rd;
  std::mt19937 gen(rd());
  // 2. 32ビット整数の範囲でランダムな値を生成
  std::uniform_int_distribution<uint32_t> dis;

  unique_title = std::format("AsciiStreamEngine_Client_{:08X}", dis(gen));
#endif

  const char *port = HOST_PORT.data();
  const char *host = HOST_DOMAIN.data();

  // Windows環境対応の簡易引数解析
  for (int i = 1; i < argc; i++) {
    std::string_view arg(argv[i]);
    if (arg == "-p" && i + 1 < argc) {
      port = argv[++i];
    } else if (arg == "-h" && i + 1 < argc) {
      host = argv[++i];
    } else {
      std::println(std::cerr, "usage: {} [-p port] [-h domain]", argv[0]);
      return 1;
    }
  }

  NetworkStreamClient client;
  if (!client.connect(host, port)) {
    return -1;
  }

  TerminalController term;
  if (!unique_title.empty()) {
    term.set_unique_title(unique_title);
  }

  // 画面サイズの取得
  uint16_t w, h;
  term.get_terminal_size(w, h);

  // 最大256x256
  if (0 >= w || 0 >= h) {
    std::cerr << "windows size error";
    return -1;
  }
  if (w > 256) {
    w = 256;
  }
  if (h > 256) {
    h = 256;
  }

  if (!client.send_window_size(w, h)) {
    std::cerr << "failed to send window size";
    return -1;
  }

  // 接続が確立したらターミナルをRawモード化
  term.enable_raw_mode();

  unsigned char buffer[65536];
  unsigned char out_buf[65536];
  TerminalBuffer terminal_buf;

#if defined(_WIN32) && !USE_ASYNC_KEY_STATE
  term.start_input_thread(client);
#endif

  while (1) {
    int timeout = -1;
#if defined(_WIN32) && USE_ASYNC_KEY_STATE
    term.scan_and_send_keys(client);
    timeout = 5;
#endif

    int event_type = term.poll_events(client, timeout);
    if (event_type < 0) {
      break;
    }

    if (event_type == 2) {
      char ch;
      if (term.read_stdin(ch) > 0) {
        client.send_data(&ch, 1);
      }
    } else if (event_type == 1) {
      uint32_t len_oder;
      uint8_t packet_w, packet_h;

      if (client.recv_packet_header(len_oder, packet_w, packet_h) <= 0) {
        break; // サーバー切断
      }

      if (len_oder <= 0 || len_oder >= 256 * 256) {
        std::cerr << "receive buffer too big\n";
        return -1;
      }

      int len = client.recv_exact(buffer, len_oder);
      if (len <= 0)
        break;

      uLongf out_len = sizeof(out_buf);
      int res = uncompress(out_buf, &out_len, buffer, len);

      if (res == Z_OK) {
        terminal_buf.ensure_size(packet_w, packet_h);
        // 描画を実行
        terminal_buf.draw(std::span<const unsigned char>(out_buf, out_len));
      }
    }
  }

  client.close();

  return 0;
}
