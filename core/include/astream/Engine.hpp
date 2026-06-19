#pragma once
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <print>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>
#include <zlib.h>

// =============================================================================
// OS固有のネットワーク・システムコールヘッダーの切り替え
// =============================================================================
#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // windows.h が古い winsock.h を巻き込むのを防ぐ
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h の max/min マクロが std::max と衝突するのを防ぐ
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

// 最後
#include <windows.h>
#define close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <astream/InputDevice.hpp>
#include <astream/Texture2D.hpp>

// =============================================================================
// zlibによる圧縮送信ヘルパー (引数の型キャストをOSに合わせて調整)
// =============================================================================
inline int send_engine_frame_compressed(int sock, const char *raw_data,
                                        size_t raw_len) {
  uLongf comp_len = compressBound(raw_len);
  std::vector<Bytef> comp_buf(comp_len);

  int res =
      compress(comp_buf.data(), &comp_len, (const Bytef *)raw_data, raw_len);
  if (res != Z_OK)
    return -1;

  uint32_t net_len = htonl((uint32_t)comp_len);

  // Windowsのsendは第2引数が const char* になるためキャストを明示
  if (send(sock, (const char *)&net_len, sizeof(net_len), 0) <= 0)
    return -1;
  return send(sock, (const char *)comp_buf.data(), (int)comp_len, 0);
}

// =============================================================================
// エンジンコアクラス
// =============================================================================
template <typename WorldType, typename SessionType> class Engine {
private:
  int m_port;
  int m_serverSock = -1;

  // 多重化バッファの型をOSネイティブなもので素直に書き分ける
#if defined(_WIN32)
  std::vector<WSAPOLLFD> m_pollFds;
#else
  std::vector<struct pollfd> m_pollFds;
#endif

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
    if (m_serverSock >= 0) {
      close(m_serverSock);
    }
#if defined(_WIN32)
    WSACleanup();
#endif
  }

  bool run() {
#if defined(_WIN32)
    // Windows固有：ネットワークサブシステムの初期化
    WSADATA wsaData;
    if (int err = WSAStartup(MAKEWORD(2, 2), &wsaData); err != 0) {
      std::println(std::cerr, "WSAStartup failed: {}", err);
      return false;
    }
#else
    // Linux固有：パイプ破断シグナルの無視
    signal(SIGPIPE, SIG_IGN);
#endif

    m_serverSock = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverSock < 0) {
      std::println(std::cerr, "socket failed: {}",
                   std::generic_category().message(errno));
      return false;
    }

    // WindowsとLinuxでsetsockoptの第4引数の型が微妙に異なる対策
#if defined(_WIN32)
    char opt = 1;
#else
    int opt = 1;
#endif
    setsockopt(m_serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(m_port);

    // bind / listen の第2引数のキャスト
    if (bind(m_serverSock, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
      std::println(std::cerr, "bind failed: {}",
                   std::generic_category().message(errno));
      return false;
    }
    if (listen(m_serverSock, 5) < 0) {
      std::println(std::cerr, "listen failed: {}",
                   std::generic_category().message(errno));
      return false;
    }

    // 待ち受けソケットを多重化配列の先頭に登録
#if defined(_WIN32)
    m_pollFds.push_back({(SOCKET)m_serverSock, POLLIN, 0});
#else
    m_pollFds.push_back({m_serverSock, POLLIN, 0});
#endif
    std::println("AsciiStreamEngine core running on port {}...", m_port);

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

      // 多重化システムコールの呼び出しをOSごと綺麗に書き分ける
#if defined(_WIN32)
      int ret = WSAPoll(m_pollFds.data(), (ULONG)m_pollFds.size(), 2);
#else
      int ret = poll(m_pollFds.data(), m_pollFds.size(), 2);
#endif
      if (ret < 0) {
        std::println(std::cerr, "poll failed: {}",
                     std::generic_category().message(errno));
        break;
      }

      // 1. 新規接続処理
      if (m_pollFds[0].revents & POLLIN) {
        struct sockaddr_in client_addr;
#if defined(_WIN32)
        int addr_len = sizeof(client_addr);
#else
        socklen_t addr_len = sizeof(client_addr);
#endif
        int client_sock = (int)accept(
            m_serverSock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock >= 0) {
#if defined(_WIN32)
          // Windows固有：MSG_DONTWAITの代わりに対象ソケットを非ブロッキングモードに切り替える
          unsigned long mode = 1;
          ioctlsocket(client_sock, FIONBIO, &mode);
          m_pollFds.push_back({(SOCKET)client_sock, POLLIN, 0});
#else
          m_pollFds.push_back({client_sock, POLLIN, 0});
#endif

          ClientSession session;
          session.fd = client_sock;
          session.context = std::make_unique<SessionType>();
          m_sessions[client_sock] = std::move(session);
        }
      }

      // 2. 受信＆切断検知処理
      for (size_t i = 1; i < m_pollFds.size(); ++i) {
        int fd = (int)m_pollFds[i].fd;
        if (m_pollFds[i].revents & POLLIN) {
          auto &session = m_sessions[fd];

          if (!session.size_initialized) {
            uint16_t size_packet[2] = {0, 0};
            int len = recv(fd, (char *)size_packet, sizeof(size_packet), 0);
            if (len <= 0) {
              close(fd);
              m_pollFds[i] = m_pollFds.back();
              m_pollFds.pop_back();
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

            const char *CLEAR_SCREEN_SEQ = "\x1b[H";
            size_t seq_len = 3;

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
#if defined(_WIN32)
            // あらかじめ非ブロッキング化しているため、MSG_DONTWAIT不要で0指定
            int n = recv(fd, recv_buf, sizeof(recv_buf), 0);
#else
            int n = recv(fd, recv_buf, sizeof(recv_buf), MSG_DONTWAIT);
#endif
            if (n > 0) {
              for (int j = 0; j < n; ++j)
                session.input.pressKey(recv_buf[j]);
            } else if (n == 0 || (n < 0 &&
#if defined(_WIN32)
                                  WSAGetLastError() != WSAEWOULDBLOCK
#else
                                  errno != EAGAIN && errno != EWOULDBLOCK
#endif
                                  )) {
              // エラーまたは正常切断時のクリーンアップ処理
              session.context->onDisconnect(m_world);
              close(fd);
              m_pollFds[i] = m_pollFds.back();
              m_pollFds.pop_back();
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

        session.context->render(*session.colorBuffer, m_world);

        size_t streamIdx = 3;
        int w = session.width;
        int h = session.height;
        for (int y = 0; y < h; ++y) {
          const char *src_row = session.colorBuffer->getData() + (y * w);
          char *dst_row = session.outputBuffer.data() + streamIdx;
          std::copy_n(src_row, w, dst_row);
          streamIdx += (w + 1);
        }

        send_engine_frame_compressed(fd, session.outputBuffer.data(),
                                     session.outputBuffer.size());

        session.input.nextFrame();
      }

      auto frameEnd = std::chrono::steady_clock::now();
      double activeTimeMs =
          std::chrono::duration<double, std::milli>(frameEnd - frameStart)
              .count();
      double targetFrameTimeMs = 1000.0 / m_targetFps;
      double sleepTimeMs = targetFrameTimeMs - activeTimeMs;
      if (sleepTimeMs > 0.0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(sleepTimeMs)));
      }
    }

    return false;
  }
};
