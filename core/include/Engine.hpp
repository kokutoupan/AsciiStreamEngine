#pragma once
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <zlib.h>

#include "InputDevice.hpp"
#include "Texture2D.hpp"

// zlibによる圧縮送信ヘルパー
inline int send_engine_frame_compressed(int sock, const char *raw_data,
                                        size_t raw_len) {
  uLongf comp_len = compressBound(raw_len);
  std::vector<Bytef> comp_buf(comp_len);

  int res =
      compress(comp_buf.data(), &comp_len, (const Bytef *)raw_data, raw_len);
  if (res != Z_OK)
    return -1;

  uint32_t net_len = htonl((uint32_t)comp_len);
  if (send(sock, &net_len, sizeof(net_len), 0) <= 0)
    return -1;
  return send(sock, comp_buf.data(), comp_len, 0);
}

template <typename WorldType, typename SessionType> class Engine {
private:
  int m_port;
  int m_serverSock = -1;
  std::vector<struct pollfd> m_pollFds;

  WorldType m_world; // サーバー上に1つだけ実体化する世界の真実

  struct ClientSession {
    int fd;
    int width = 80;
    int height = 24;
    bool size_initialized = false;
    InputDevice input;
    std::unique_ptr<SessionType> context;
    std::unique_ptr<Texture2D<char>> colorBuffer;
    std::vector<char> outputBuffer;
  };

  std::unordered_map<int, ClientSession> m_sessions;
  float m_targetFps = 30.0f;

public:
  Engine(int port = 12345, float targetFps = 30.0f)
      : m_port(port), m_targetFps(targetFps > 0.0f ? targetFps : 30.0f) {}
  ~Engine() {
    if (m_serverSock >= 0)
      close(m_serverSock);
  }

  bool run() {
    signal(SIGPIPE, SIG_IGN);

    m_serverSock = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSock < 0) {
      perror("socket failed");
      return false;
    }

    int opt = 1;
    setsockopt(m_serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(m_port);

    if (bind(m_serverSock, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
      perror("bind failed");
      return false;
    }
    if (listen(m_serverSock, 5) < 0) {
      perror("listen failed");
      return false;
    }

    m_pollFds.push_back({m_serverSock, POLLIN, 0});
    printf("AsciiStreamEngine core running on port %d...\n", m_port);

    auto lastFrameStart = std::chrono::steady_clock::now();
    float deltaTime = 1.0f / m_targetFps;

    while (true) {
      auto frameStart = std::chrono::steady_clock::now();
      double elapsedMs =
          std::chrono::duration<double, std::milli>(frameStart - lastFrameStart)
              .count();
      if (elapsedMs > 0.0) {
        deltaTime = static_cast<float>(elapsedMs / 1000.0);
      }
      lastFrameStart = frameStart;

      int ret = poll(m_pollFds.data(), m_pollFds.size(),
                     2); // 2ms timeout for responsiveness
      if (ret < 0) {
        perror("poll failed");
        break;
      }

      // 1. 新規接続処理
      if (m_pollFds[0].revents & POLLIN) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock =
            accept(m_serverSock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock >= 0) {
          m_pollFds.push_back({client_sock, POLLIN, 0});

          ClientSession session;
          session.fd = client_sock;
          session.context = std::make_unique<SessionType>();
          m_sessions[client_sock] = std::move(session);
        }
      }

      // 2. 受信＆切断検知処理
      for (size_t i = 1; i < m_pollFds.size(); ++i) {
        int fd = m_pollFds[i].fd;
        if (m_pollFds[i].revents & POLLIN) {
          auto &session = m_sessions[fd];

          if (!session.size_initialized) {
            uint16_t size_packet[2] = {0, 0};
            int len = recv(fd, size_packet, sizeof(size_packet), 0);
            if (len <= 0) {
              close(fd);
              m_sessions.erase(fd);
              m_pollFds.erase(m_pollFds.begin() + i);
              --i;
              continue;
            }
            session.width = ntohs(size_packet[0]);
            session.height = ntohs(size_packet[1]);
            if (session.width < 1)
              session.width = 80;
            if (session.height < 1)
              session.height = 24;

            session.context->init(fd, session.width, session.height, m_world);
            session.colorBuffer = std::make_unique<Texture2D<char>>(
                session.width, session.height, ' ');

            const char *CLEAR_SCREEN_SEQ = "\x1b[2J\x1b[H";
            size_t seq_len = 7;

            // 送信用平坦化バッファの初期化 (シーケンス分 + 各行の文字と改行)
            session.outputBuffer.resize(
                seq_len + (session.width + 1) * session.height, '\0');
            std::copy(CLEAR_SCREEN_SEQ, CLEAR_SCREEN_SEQ + seq_len,
                      session.outputBuffer.begin());

            for (int y = 0; y < session.height - 1; ++y) {
              session.outputBuffer[seq_len + (session.width + 1) * y +
                                   session.width] = '\n';
            }
            session.outputBuffer[session.outputBuffer.size() - 1] = '\0';
            session.size_initialized = true;
          } else {
            char recv_buf[64];
            int n = recv(fd, recv_buf, sizeof(recv_buf), MSG_DONTWAIT);
            if (n > 0) {
              for (int j = 0; j < n; ++j)
                session.input.pressKey(recv_buf[j]);
            } else if (n == 0) {
              session.context->onDisconnect(m_world);
              close(fd);
              m_sessions.erase(fd);
              m_pollFds.erase(m_pollFds.begin() + i);
              --i;
              continue;
            }
          }
        }
      }

      // 3. サーバーコアゲームループライン

      // 3-A. 各プレイヤーの入力をワールド側に流し込んで集約
      for (auto &[fd, session] : m_sessions) {
        if (!session.size_initialized)
          continue;
        session.input.setDeltaTime(deltaTime);
        session.context->update(fd, session.input, m_world);
      }

      // 3-B. サーバー側のグローバル更新
      m_world.globalUpdate();

      // 3-C. 各プレイヤー視点での独立描画・コピー・最速送信
      for (auto &[fd, session] : m_sessions) {
        if (!session.size_initialized)
          continue;

        // 利用者の描画関数を実行（StateからViewを再構成）
        session.context->render(*session.colorBuffer, m_world);

        size_t streamIdx = 7;
        int w = session.width;
        int h = session.height;
        for (int y = 0; y < h; ++y) {
          const char *src_row = session.colorBuffer->getData() + (y * w);
          char *dst_row = session.outputBuffer.data() + streamIdx;
          std::memcpy(dst_row, src_row, w);
          streamIdx += (w + 1);
        }

        // zlibによる高速圧縮送信
        send_engine_frame_compressed(fd, session.outputBuffer.data(),
                                     session.outputBuffer.size());

        // 入力フレームのスライド・クリア処理
        session.input.nextFrame();
      }

      auto frameEnd = std::chrono::steady_clock::now();
      double activeTimeMs =
          std::chrono::duration<double, std::milli>(frameEnd - frameStart)
              .count();
      double targetFrameTimeMs = 1000.0 / m_targetFps;
      double sleepTimeMs = targetFrameTimeMs - activeTimeMs;
      if (sleepTimeMs > 0.0) {
        usleep(static_cast<useconds_t>(sleepTimeMs * 1000.0));
      }
    }

    return false;
  }
};
