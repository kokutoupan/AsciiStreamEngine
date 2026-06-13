#include <algorithm>
#include <arpa/inet.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <math.h>
#include <memory>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <zconf.h>
#include <zlib.h>

#include "ConnectionContext.hpp"
#include "CubeApp.hpp"
#include "Texture2D.hpp"

constexpr int PORT = 12345;

// --- ネットワーク送信用の圧縮ヘルパー ---
int send_frame_compressed(int sock, const char *raw_data, size_t raw_len,
                          int is_raw_send) {
  if (is_raw_send) {
    return send(sock, raw_data, raw_len, 0);
  }
  uLongf comp_len = compressBound(raw_len);
  std::vector<Bytef> comp_buf(comp_len);

  int res =
      compress(comp_buf.data(), &comp_len, (const Bytef *)raw_data, raw_len);
  if (res != Z_OK) {
    return -1;
  }

  uint32_t net_len = htonl((uint32_t)comp_len);
  res = send(sock, &net_len, sizeof(net_len), 0);
  if (res <= 0) {
    return res;
  }
  return send(sock, comp_buf.data(), comp_len, 0);
}

// アプリケーション生成ファクトリ
std::unique_ptr<ConnectionContext> create_user_app() {
  return std::make_unique<CubeApp>();
}

// --- 構造体：各クライアントの情報を保持 ---
struct ClientSession {
  int fd;
  int width = 80;
  int height = 24;
  bool size_initialized = false;
  InputDevice input;
  std::unique_ptr<ConnectionContext> app;
  std::unique_ptr<Texture2D<char>> color_buffer;
  std::vector<char> output_buffer;
};

// --- メイン関数：I/O多重化によるサーバーエンジン ---
int main() {
  signal(SIGPIPE, SIG_IGN);
  int server_sock;
  struct sockaddr_in server_addr;

  if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    return -1;
  }

  int opt = 1;
  setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);

  if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind failed");
    return -1;
  }

  if (listen(server_sock, 5) < 0) {
    perror("listen failed");
    return -1;
  }

  std::vector<struct pollfd> poll_fds;
  poll_fds.push_back({server_sock, POLLIN, 0}); // リスニングソケット登録

  std::unordered_map<int, ClientSession> sessions;
  printf("AsciiStreamEngine Server listening on port %d...\n", PORT);

  while (true) {
    // 30FPS~60FPSのタイマー制御を兼ねて16msでpollブロック
    int ret = poll(poll_fds.data(), poll_fds.size(), 16);
    if (ret < 0) {
      perror("poll failed");
      break;
    }

    // 1. 新規接続チェック
    if (poll_fds[0].revents & POLLIN) {
      struct sockaddr_in client_addr;
      socklen_t addr_len = sizeof(client_addr);
      int client_sock =
          accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
      if (client_sock >= 0) {
        printf("New connection accepted in event loop: fd %d\n", client_sock);
        poll_fds.push_back({client_sock, POLLIN, 0});

        ClientSession session;
        session.fd = client_sock;
        session.app = create_user_app();
        sessions[client_sock] = std::move(session);
      }
    }

    // 2. クライアントデータ（サイズパケットなど）受信チェック
    for (size_t i = 1; i < poll_fds.size(); ++i) {
      int fd = poll_fds[i].fd;
      if (poll_fds[i].revents & POLLIN) {
        auto &session = sessions[fd];

        if (!session.size_initialized) {
          uint16_t size_packet[2] = {0, 0};
          int len = recv(fd, size_packet, sizeof(size_packet), 0);
          if (len <= 0) {
            printf("Client closed during handshake: fd %d\n", fd);
            close(fd);
            sessions.erase(fd);
            poll_fds.erase(poll_fds.begin() + i);
            --i;
            continue;
          }
          session.width = ntohs(size_packet[0]);
          session.height = ntohs(size_packet[1]);
          if (session.width < 1)
            session.width = 80;
          if (session.height < 1)
            session.height = 24;

          printf("Client fd %d initialized size: %d x %d\n", fd, session.width,
                 session.height);
          session.app->init(session.width, session.height);
          session.size_initialized = true;

          session.color_buffer = std::make_unique<Texture2D<char>>(
              session.width, session.height, ' ');
          // 送信ストリームバッファの初期化設定 (\x1b[2J + 画面バッファサイズ)
          session.output_buffer.resize(
              4 + (session.width + 1) * session.height + 1, '\0');
          std::copy("\x1b[2J", "\x1b[2J" + 4, session.output_buffer.begin());
          for (int y = 0; y < session.height; ++y) {
            session.output_buffer[4 + (session.width + 1) * y + session.width] =
                '\n';
          }
          session.output_buffer[session.output_buffer.size() - 1] = '\0';
        } else {
          // 通常の入力パケット：クライアントから届いたキーデータをノンブロッキングで回収
          char recv_buf[32];
          int n = recv(fd, recv_buf, sizeof(recv_buf), MSG_DONTWAIT);
          if (n > 0) {
            for (int j = 0; j < n; ++j) {
              session.input.pressKey(recv_buf[j]);
            }
          } else if (n == 0) {
            printf("Client disconnected: fd %d\n", fd);
            close(fd);
            sessions.erase(fd);
            poll_fds.erase(poll_fds.begin() + i);
            --i;
            continue;
          }
        }
      }
    }

    // 3. ゲームループの進行 ＆ 描画 ＆ 送信（一括実行）
    for (auto &[fd, session] : sessions) {
      if (!session.size_initialized)
        continue;

      session.app->update(session.input);         // ロジック更新
      session.app->render(*session.color_buffer); // レンダリング
      //
      size_t streamIdx = 4;   // \x1b[2J の後ろから
      int w = session.width;  //
      int h = session.height; //

      for (int y = 0; y < h; ++y) {
        // Texture2D の1行分のポインタを取得
        const char *src_row = session.color_buffer->getData() + (y * w);
        char *dst_row = session.output_buffer.data() + streamIdx;

        std::memcpy(dst_row, src_row, w);

        streamIdx += (w + 1); // 幅 + '\n' 分をスキップ
      }

      // 送信
      if (send_frame_compressed(fd, session.output_buffer.data(),
                                session.output_buffer.size(), 0) <= 0) {
        // 送信失敗
      }
      session.input.nextFrame();
    }

    // 約30FPS維持のためのウェイト
    usleep(33000);
  }

  close(server_sock);
  return 0;
}
