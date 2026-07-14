#pragma once

#include <algorithm>
#include <chrono>
#include <execution>
#include <iostream>
#include <memory>
#include <print>
#include <random>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>
#include <zlib.h>

#include <astream/AuthContext.hpp>
#include <astream/UserStore.hpp>
#include <astream/net/EncryptedStream.hpp>
#include <astream/net/NetworkUtil.hpp>

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

// socketの後
#include <windows.h>

// 最後にインクルード
#include <timeapi.h>

#define close closesocket

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
  switch (ctrlType) {
  case CTRL_C_EVENT:
  case CTRL_BREAK_EVENT:
  case CTRL_CLOSE_EVENT:
    // 終了を検知したらタイマーを元に戻す
    timeEndPeriod(1);
    return FALSE;
  default:
    return FALSE;
  }
}

#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <astream/Debug.hpp>
#include <astream/InputDevice.hpp>
#include <astream/Texture2D.hpp>

template <typename T>
concept IsGameWorld =
    requires(T world, int clientId, const InputDevice &input, float dt) {
      { world.processPlayerInput(clientId, input) } -> std::same_as<void>;
      { world.globalUpdate(dt) } -> std::same_as<void>;
    };

template <typename T>
concept HasKickFds = requires(T &t) {
  { t.getKickFds() } -> std::same_as<std::vector<int>>;
};

template <typename T>
concept HasPreUpdate = requires(T &t) { t.preUpdate(); };

template <typename T>
concept HasPostUpdate = requires(T &t) { t.postUpdate(); };

template <typename Session, typename World>
concept IsConnectionSession =
    requires(Session s, int clientId, int w, int h,
             const std::string &user_name, World &world,
             const World &const_world, const InputDevice &input,
             TextureView<char> buf) {
      { s.init(clientId, w, h, user_name, world) } -> std::same_as<void>;
      { s.onDisconnect(world) } -> std::same_as<void>;
      { s.update(clientId, input, world) } -> std::same_as<void>;
      { s.render(buf, const_world) } -> std::same_as<bool>;
    };

template <typename T, typename World>
concept HasSessionPostUpdate =
    requires(T &t, int id, World &w) { t.postUpdate(id, w); };

template <typename T, typename World>
concept HasSessionEndFrame =
    requires(T &t, int id, World &w) { t.endFrame(id, w); };

// =============================================================================
// エンジンコアクラス
// =============================================================================
template <typename WorldType, typename SessionType>
  requires IsGameWorld<WorldType> && IsConnectionSession<SessionType, WorldType>
class Engine {
private:
  void banSession(int fd) {
    m_handshakes.erase(fd);
    auto activeSession_it = m_sessions.find(fd);
    if (activeSession_it != m_sessions.end()) {
      if (m_enableAuth) {
        m_userStore.banUser(activeSession_it->second.core.user_name);
      }
      activeSession_it->second.context->onDisconnect(m_world);
      m_sessions.erase(activeSession_it);
    } else {
      m_authenticatingSessions.erase(fd);
    }

    close(fd);

    auto it = std::find_if(m_pollFds.begin(), m_pollFds.end(),
                           [fd](const auto &pfd) { return (int)pfd.fd == fd; });
    if (it != m_pollFds.end()) {
      *it = m_pollFds.back();
      m_pollFds.pop_back();
    }
  }

  bool m_enableAuth;
  UserStore m_userStore;
  int m_port;
  int m_serverSock = -1;
  bool m_enableEncryption = false;
  std::unordered_map<int, astream::net::EncryptedStream> m_pendingStreams;

  struct HandshakeSession {
    int fd;
    uint8_t server_secret_key[32];
    uint8_t client_public_key[32];
    size_t bytes_received = 0;
  };
  std::unordered_map<int, HandshakeSession> m_handshakes;

  // 多重化バッファの型をOSネイティブなもので素直に書き分ける
#if defined(_WIN32)
  std::vector<WSAPOLLFD> m_pollFds;
#else
  std::vector<struct pollfd> m_pollFds;
#endif

  WorldType m_world; // サーバー上に1つだけ実体化する世界の真実

  struct SessionCore {
    int fd;
    int width = 80;
    int height = 24;
    InputDevice input;
    std::string user_name;
    std::unique_ptr<Texture2D<char>> colorBuffer;
    std::vector<uint8_t> compressedBuffer;
    astream::net::EncryptedStream stream;
  };

  // 認証中のセッション
  struct AuthenticatingSession {
    SessionCore core; // 共通要素
    std::unique_ptr<AuthContext> authContext;
  };

  // 認証後のアクティブなセッション
  struct ActiveSession {
    SessionCore core; // 共通要素
    std::unique_ptr<SessionType> context;
  };

  std::unordered_map<int, AuthenticatingSession> m_authenticatingSessions;
  std::unordered_map<int, ActiveSession> m_sessions;

  float m_targetFps = 30.0f;

public:
  Engine(int port = 12345, float targetFps = 30.0f, bool enableAuth = false,
         RegisterPolicy policy = RegisterPolicy::AdminOnly,
         bool enableEncryption = false)
      : m_enableAuth(enableAuth), m_port(port),
        m_targetFps(targetFps > 0.0f ? targetFps : 30.0f), m_userStore(policy),
        m_enableEncryption(enableEncryption) {}
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

    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    // 高精度なタイマー
    timeBeginPeriod(1);
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
      int ret = WSAPoll(m_pollFds.data(), (ULONG)m_pollFds.size(), 0);
#else
      int ret = poll(m_pollFds.data(), m_pollFds.size(), 2);
#endif
      if (ret < 0) {
        std::println(std::cerr, "poll failed: {}",
                     std::generic_category().message(errno));
        break;
      }
      Debug::LoopProfiler main_loop("Server Full Loop");
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
          if (m_enableEncryption) {
            static thread_local std::mt19937 generator(std::random_device{}());
            std::uniform_int_distribution<int> distribution(0, 255);
            HandshakeSession handshake_session;
            handshake_session.fd = client_sock;
            for (auto &b : handshake_session.server_secret_key)
              b = static_cast<uint8_t>(distribution(generator));

            uint8_t server_public_key[32];
            crypto_x25519_public_key(server_public_key,
                                     handshake_session.server_secret_key);

            uint8_t handshake[33];
            handshake[0] = 1; // Encrypted mode
            std::memcpy(handshake + 1, server_public_key, 32);
            send(client_sock, (const char *)handshake, 33, 0);

            handshake_session.bytes_received = 0;
            m_handshakes[client_sock] = std::move(handshake_session);
          } else {
            uint8_t mode_byte = 0; // Plaintext
            send(client_sock, (const char *)&mode_byte, 1, 0);
            uint8_t dummy[32] = {0};
            astream::net::EncryptedStream stream;
            stream.initialize_encryption(
                astream::net::EncryptedStream::Mode::Plaintext, dummy, dummy,
                dummy);
            m_pendingStreams[client_sock] = std::move(stream);
          }

#if defined(_WIN32)
          // Windows固有：MSG_DONTWAITの代わりに対象ソケットを非ブロッキングモードに切り替える
          unsigned long mode = 1;
          ioctlsocket(client_sock, FIONBIO, &mode);
          m_pollFds.push_back({(SOCKET)client_sock, POLLIN, 0});
#else
          m_pollFds.push_back({client_sock, POLLIN, 0});
#endif
        }
      }

      // 2. 受信＆切断検知処理
      for (size_t i = 1; i < m_pollFds.size(); ++i) {
        int fd = (int)m_pollFds[i].fd;
        if (m_pollFds[i].revents & POLLIN) {

          auto handshake_it = m_handshakes.find(fd);
          if (handshake_it != m_handshakes.end()) {
            HandshakeSession &session = handshake_it->second;
            uint8_t *buf = session.client_public_key + session.bytes_received;
            size_t remaining = 32 - session.bytes_received;
#if defined(_WIN32)
            int n = recv(fd, (char *)buf, (int)remaining, 0);
#else
            int n = recv(fd, (char *)buf, remaining, MSG_DONTWAIT);
#endif
            if (n > 0) {
              session.bytes_received += n;
              if (session.bytes_received == 32) {
                uint8_t raw_shared_secret[32];
                crypto_x25519(raw_shared_secret, session.server_secret_key,
                              session.client_public_key);

                uint8_t shared_key[32];
                uint8_t tx_base_nonce[24];
                uint8_t rx_base_nonce[24];

                crypto_blake2b_keyed(shared_key, 32, raw_shared_secret, 32,
                                     reinterpret_cast<const uint8_t *>("key"),
                                     3);

                uint8_t tx_nonce_buf[32];
                crypto_blake2b_keyed(
                    tx_nonce_buf, 32, raw_shared_secret, 32,
                    reinterpret_cast<const uint8_t *>("server_to_client"), 16);
                std::memcpy(tx_base_nonce, tx_nonce_buf, 24);

                uint8_t rx_nonce_buf[32];
                crypto_blake2b_keyed(
                    rx_nonce_buf, 32, raw_shared_secret, 32,
                    reinterpret_cast<const uint8_t *>("client_to_server"), 16);
                std::memcpy(rx_base_nonce, rx_nonce_buf, 24);

                astream::net::EncryptedStream stream;
                stream.initialize_encryption(
                    astream::net::EncryptedStream::Mode::Encrypted, shared_key,
                    tx_base_nonce, rx_base_nonce);

                m_pendingStreams[fd] = std::move(stream);
                m_handshakes.erase(handshake_it);
              }
            } else if (n == 0 || (n < 0 && !astream::net::IsWouldBlock())) {
              close(fd);
              m_handshakes.erase(handshake_it);
              m_pollFds[i] = m_pollFds.back();
              m_pollFds.pop_back();
              --i;
            }
            continue;
          }

          auto activeSession_it = m_sessions.find(fd);

          auto authing_it = m_authenticatingSessions.find(fd);
          // if (activeSession_it == m_sessions.end()) {
          //   auto authing_it = m_authenticatingSessions.find(fd);
          // }

          // 初期コネクション前
          if (activeSession_it == m_sessions.end() &&
              authing_it == m_authenticatingSessions.end()) {

            SessionCore sessionCore;
            sessionCore.fd = fd;

            auto it_stream = m_pendingStreams.find(fd);
            if (it_stream == m_pendingStreams.end()) {
              close(fd);
              m_pollFds[i] = m_pollFds.back();
              m_pollFds.pop_back();
              --i;
              continue;
            }
            astream::net::EncryptedStream &stream = it_stream->second;

            int w = 0;
            int h = 0;

            if (stream.get_mode() ==
                astream::net::EncryptedStream::Mode::Plaintext) {
              uint16_t size_packet[2];
#if defined(_WIN32)
              int len =
                  recv(fd, (char *)size_packet, sizeof(size_packet), MSG_PEEK);
#else
              int len = recv(fd, (char *)size_packet, sizeof(size_packet),
                             MSG_PEEK | MSG_DONTWAIT);
#endif
              if (len < 0 && astream::net::IsWouldBlock()) {
                continue;
              }
              if (len < (int)sizeof(size_packet)) {
                if (len == 0 || (len < 0 && !astream::net::IsWouldBlock())) {
                  close(fd);
                  m_pendingStreams.erase(it_stream);
                  m_pollFds[i] = m_pollFds.back();
                  m_pollFds.pop_back();
                  --i;
                }
                continue;
              }
              recv(fd, (char *)size_packet, sizeof(size_packet), 0);
              w = ntohs(size_packet[0]);
              h = ntohs(size_packet[1]);
            } else {
              std::vector<uint8_t> out_plain;
              int len =
                  astream::net::recv_encrypted_frame(fd, stream, out_plain);
              if (len == 0) {
                continue;
              }
              if (len < 0 || out_plain.size() < 4) {
                close(fd);
                m_pendingStreams.erase(it_stream);
                m_pollFds[i] = m_pollFds.back();
                m_pollFds.pop_back();
                --i;
                continue;
              }
              uint16_t size_packet[2];
              std::memcpy(size_packet, out_plain.data(), 4);
              w = ntohs(size_packet[0]);
              h = ntohs(size_packet[1]);
            }

            sessionCore.stream = std::move(stream);
            m_pendingStreams.erase(it_stream);

            sessionCore.width = w;
            sessionCore.height = h;

            if (w < 1)
              sessionCore.width = 80;
            else if (w > 254)
              sessionCore.width = 254;
            if (h < 1)
              sessionCore.height = 24;
            else if (h > 254)
              sessionCore.height = 254;

            sessionCore.colorBuffer = std::make_unique<Texture2D<char>>(
                sessionCore.width, sessionCore.height, ' ');

            if (!m_enableAuth) {

              sessionCore.user_name = "Guest_" + std::to_string(fd);
              ActiveSession session;
              session.context = std::make_unique<SessionType>();
              session.core = std::move(sessionCore);

              m_sessions[fd] = std::move(session);
              m_sessions[fd].context->init(
                  fd, w, h, m_sessions[fd].core.user_name, m_world);
              continue;

            } else {
              AuthenticatingSession session;

              session.authContext = std::make_unique<AuthContext>(&m_userStore);
              session.core = std::move(sessionCore);

              m_authenticatingSessions[fd] = std::move(session);
              continue;
            }
          } else {
            astream::net::EncryptedStream &stream =
                (activeSession_it != m_sessions.end())
                    ? activeSession_it->second.core.stream
                    : authing_it->second.core.stream;

            bool disconnect = false;

            if (stream.get_mode() ==
                astream::net::EncryptedStream::Mode::Plaintext) {
              char recv_buf[64];
#if defined(_WIN32)
              int n = recv(fd, recv_buf, sizeof(recv_buf), 0);
#else
              int n = recv(fd, recv_buf, sizeof(recv_buf), MSG_DONTWAIT);
#endif
              if (n > 0) {
                for (int j = 0; j < n; ++j) {
                  if (activeSession_it != m_sessions.end()) {
                    activeSession_it->second.core.input.pressKey(recv_buf[j]);
                  } else {
                    authing_it->second.core.input.pressKey(recv_buf[j]);
                  }
                }
              } else if (n == 0 || (n < 0 && !astream::net::IsWouldBlock())) {
                disconnect = true;
              }
            } else {
              while (true) {
                std::vector<uint8_t> out_plain;
                int n =
                    astream::net::recv_encrypted_frame(fd, stream, out_plain);
                if (n > 0) {
                  for (size_t j = 0; j < out_plain.size(); ++j) {
                    if (activeSession_it != m_sessions.end()) {
                      activeSession_it->second.core.input.pressKey(
                          out_plain[j]);
                    } else {
                      authing_it->second.core.input.pressKey(out_plain[j]);
                    }
                  }
                } else if (n < 0) {
                  disconnect = true;
                  break;
                } else {
                  break;
                }
              }
            }

            if (disconnect) {
              if (activeSession_it != m_sessions.end()) {
                activeSession_it->second.context->onDisconnect(m_world);
                m_sessions.erase(activeSession_it);
              } else {
                m_authenticatingSessions.erase(authing_it);
              }

              close(fd);
              m_pollFds[i] = m_pollFds.back();
              m_pollFds.pop_back();
              --i;

              continue;
            }
          }
        }
      }

      // イテレータを使って全要素をループ
      auto authing_it = m_authenticatingSessions.begin();
      while (authing_it != m_authenticatingSessions.end()) {

        // 認証中のセッション参照を取得
        auto &authing = authing_it->second;

        int fd = authing.core.fd;

        // アップデート処理
        const AuthResult &result =
            authing.authContext->update(fd, authing.core.input);
        const bool isDirty =
            authing.authContext->render(authing.core.colorBuffer->view());

        if (isDirty)
          astream::net::send_engine_frame_compressed(
              authing.core.fd, authing.core.colorBuffer->data(),
              authing.core.colorBuffer->size(), authing.core.width,
              authing.core.height, authing.core.compressedBuffer,
              authing.core.stream);

        authing.core.input.nextFrame();

        if (result.success) {

          ActiveSession session;
          session.context = std::make_unique<SessionType>();

          // m_sessions 側に所有権をムーブ
          session.core = std::move(authing.core);
          session.core.user_name = result.username;

          m_sessions[fd] = std::move(session);
          m_sessions[fd].context->init(fd, m_sessions[fd].core.width,
                                       m_sessions[fd].core.height,
                                       m_sessions[fd].core.user_name, m_world);

          authing_it = m_authenticatingSessions.erase(authing_it);
        } else {
          // 削除しなかった場合のみ、自力でイテレータを次に進める
          ++authing_it;
        }
      }

      // 3. サーバーコアゲームループライン
      if constexpr (HasPreUpdate<WorldType>) {
        m_world.preUpdate();
      }

      // 3-A. 各プレイヤーの入力をワールド側に流し込んで集約
      for (auto &[fd, session] : m_sessions) {
        session.core.input.setDeltaTime(deltaTime);
        session.context->update(fd, session.core.input, m_world);
      }

      // 3-B. サーバー側のグローバル更新
      m_world.globalUpdate(deltaTime);

      if constexpr (HasKickFds<WorldType>) {
        auto kickFds = m_world.getKickFds();
        for (int fd : kickFds) {
          banSession(fd);
        }
      }

      if constexpr (HasSessionPostUpdate<SessionType, WorldType>) {
        for (auto &[fd, session] : m_sessions) {
          session.core.input.setDeltaTime(deltaTime);
          session.context->postUpdate(fd, m_world);
        }
      }

      // 3-C. 各プレイヤー視点での独立描画・コピー・最速送信

      std::vector<ActiveSession *> active_sessions;
      active_sessions.reserve(m_sessions.size());

      for (auto &[fd, session] : m_sessions) {
        active_sessions.push_back(&session);
      }
      // 2. 集めたセッションに対して、並列実行
      const auto &const_world = m_world;

      std::for_each(std::execution::par, active_sessions.begin(),
                    active_sessions.end(),
                    [&const_world](ActiveSession *session_ptr) {
                      auto &session = *session_ptr;

                      // [A] 各プレイヤー視点での描画（const world を渡す）
                      bool should_send = session.context->render(
                          session.core.colorBuffer->view(), const_world);

                      // [B]
                      // 圧縮と送信（スレッドごとに個別に実行されるため、重いcompressが並列化される）
                      if (should_send) {
                        astream::net::send_engine_frame_compressed(
                            session.core.fd, session.core.colorBuffer->data(),
                            session.core.colorBuffer->size(),
                            session.core.width, session.core.height,
                            session.core.compressedBuffer, session.core.stream);
                      }
                    });

      // 3.
      // 次フレームへの入力状態の遷移は、メインスレッドに戻って安全に一括処理
      for (auto *session_ptr : active_sessions) {
        session_ptr->core.input.nextFrame();
      }

      if constexpr (HasPostUpdate<WorldType>) {
        m_world.postUpdate();
      }

      if constexpr (HasSessionEndFrame<SessionType, WorldType>) {
        for (auto &[fd, session] : m_sessions) {
          session.core.input.setDeltaTime(deltaTime);
          session.context->endFrame(fd, m_world);
        }
      }

      main_loop.stop();

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
