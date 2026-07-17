#include "TerminalController.hpp"
#include "NetworkStreamClient.hpp"

#include <iostream>
#include <string_view>
#include <thread>

// OS依存ヘッダー
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>

// 最後
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

struct TerminalController::Impl {
#if defined(_WIN32)
  DWORD orig_console_mode = 0;
  HANDLE hStdin = INVALID_HANDLE_VALUE;
  HANDLE hStdout = INVALID_HANDLE_VALUE;
  std::string unique_title;
  std::thread input_thread;
  bool thread_running = false;
#else
  struct termios orig_termios;
  bool raw_mode_enabled = false;
#endif
};

TerminalController::TerminalController() : pImpl(std::make_unique<Impl>()) {}

TerminalController::~TerminalController() {

  std::cout << "\033[2J\033[1;1H\033[?1049l\033[?25h" << std::flush;

  disable_raw_mode();
#if defined(_WIN32)
  if (pImpl->thread_running && pImpl->input_thread.joinable()) {
    pImpl->input_thread.detach();
  }
#endif
}

TerminalController::TerminalController(TerminalController &&) noexcept =
    default;
TerminalController &
TerminalController::operator=(TerminalController &&) noexcept = default;

void TerminalController::enable_raw_mode() {
#if defined(_WIN32)
  pImpl->hStdin = GetStdHandle(STD_INPUT_HANDLE);
  pImpl->hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

  GetConsoleMode(pImpl->hStdin, &pImpl->orig_console_mode);

  // カノニカルモード（行単位入力）と入力を画面に表示するエコーをオフにする
  DWORD raw_mode =
      pImpl->orig_console_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
  // 仮想ターミナル入力を有効化 (エスケープシーケンス等の解析に必要)
  raw_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
  SetConsoleMode(pImpl->hStdin, raw_mode);

  // 出力側もエスケープシーケンス（画面クリア等）を有効化
  DWORD out_mode;
  GetConsoleMode(pImpl->hStdout, &out_mode);
  SetConsoleMode(pImpl->hStdout, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
  tcgetattr(STDIN_FILENO, &pImpl->orig_termios);
  pImpl->raw_mode_enabled = true;

  struct termios raw = pImpl->orig_termios;
  raw.c_lflag &= ~(ECHO | ICANON);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
  std::cout << "\033[?1049h\033[?25l" << std::flush;
}

void TerminalController::disable_raw_mode() {
#if defined(_WIN32)
  if (pImpl->hStdin != INVALID_HANDLE_VALUE) {
    SetConsoleMode(pImpl->hStdin, pImpl->orig_console_mode);
  }
#else
  if (pImpl->raw_mode_enabled) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &pImpl->orig_termios);
    pImpl->raw_mode_enabled = false;
  }
#endif
}

void TerminalController::get_terminal_size(uint16_t &w, uint16_t &h) {
  w = 80;
  h = 24; // デフォルト値
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  }
#else
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
    w = ws.ws_col;
    h = ws.ws_row;
  }
#endif
}

void TerminalController::set_unique_title(const std::string &title) {
#if defined(_WIN32)
  pImpl->unique_title = title;
  SetConsoleTitleA(title.c_str());
#endif
}

void TerminalController::scan_and_send_keys(NetworkStreamClient &client) {
#if defined(_WIN32)
  HWND hFg = GetForegroundWindow();
  if (hFg == NULL)
    return;

  if (hFg != GetConsoleWindow()) {
    // Windows ターミナル環境のタブ判定
    char title[512] = {0};
    GetWindowTextA(hFg, title, sizeof(title));

    if (!std::string_view(title).contains(pImpl->unique_title)) {
      return;
    }
  }

  // 1. Shiftキーの押下状態を取得
  bool is_shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

  // 2. アルファベットキー（'A' ~ 'Z'）のチェック
  for (int vk = 'A'; vk <= 'Z'; vk++) {
    if (GetAsyncKeyState(vk) & 0x8000) {
      char ch;
      if (is_shift) {
        ch = (char)vk; // 大文字 'A' ~ 'Z'
      } else {
        ch = (char)(vk + ('a' - 'A')); // 小文字 'a' ~ 'z'
      }
      client.send_data(&ch, 1);
    }
  }

  // 3. 数字キー（'0' ~ '9'）のチェック
  for (int vk = '0'; vk <= '9'; vk++) {
    if (GetAsyncKeyState(vk) & 0x8000) {
      char ch = (char)vk;
      client.send_data(&ch, 1);
    }
  }

  // 4. 特殊キーのチェック
  if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
    char ch = ' ';
    client.send_data(&ch, 1);
  }
  if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
    char ch = '\n';
    client.send_data(&ch, 1);
  }
  if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
    char ch = 0x1b; // Escape
    client.send_data(&ch, 1);
  }

  // 5. 矢印キー
  if (GetAsyncKeyState(VK_UP) & 0x8000) {
    const char seq[3] = {0x1b, '[', 'A'};
    client.send_data(seq, 3);
  }
  if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
    const char seq[3] = {0x1b, '[', 'B'};
    client.send_data(seq, 3);
  }
  if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
    const char seq[3] = {0x1b, '[', 'C'};
    client.send_data(seq, 3);
  }
  if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
    const char seq[3] = {0x1b, '[', 'D'};
    client.send_data(seq, 3);
  }

  // 6. 安全な記号類
  struct KeyMap {
    int vk;
    char normal_ch;
    char shift_ch;
  };
  static const KeyMap gem_symbols[] = {
      {VK_OEM_MINUS, '-', '='},  {VK_OEM_PLUS, '+', ';'},
      {VK_OEM_PERIOD, '.', '>'}, {VK_OEM_COMMA, ',', '<'},
      {VK_OEM_2, '/', '?'},      {'1', '1', '!'},
      {'8', '8', '*'},
  };

  for (const auto &mapping : gem_symbols) {
    if (GetAsyncKeyState(mapping.vk) & 0x8000) {
      char ch = is_shift ? mapping.shift_ch : mapping.normal_ch;
      if (ch == '-' || ch == '+' || ch == '=' || ch == '*' || ch == '/' ||
          ch == '.' || ch == ',' || ch == '?' || ch == '!') {
        client.send_data(&ch, 1);
      }
    }
  }
#endif
}

int TerminalController::poll_events(NetworkStreamClient &client,
                                    int timeout_ms) {
  intptr_t socket_handle = client.get_socket_handle();

#if defined(_WIN32)
  WSAPOLLFD fds[1];
  fds[0].fd = static_cast<SOCKET>(socket_handle);
  fds[0].events = POLLIN;

  int actual_timeout = timeout_ms;
  if (actual_timeout == -1) {
    actual_timeout = 100;
  }
  int ret = WSAPoll(fds, 1, actual_timeout);
  if (ret < 0)
    return -1;
  if (ret == 0)
    return 0;
  if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))
    return -1;
  if (fds[0].revents & POLLIN)
    return 1;
  return 0;
#else
  struct pollfd fds[2];
  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;
  fds[1].fd = static_cast<int>(socket_handle);
  fds[1].events = POLLIN;

  int ret = poll(fds, 2, timeout_ms);
  if (ret < 0) {
    if (errno == EINTR)
      return 0;
    return -1;
  }
  if (ret == 0)
    return 0;

  if (fds[0].revents & POLLIN) {
    return 2; // 標準入力
  }
  if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
    return -1;
  }
  if (fds[1].revents & POLLIN) {
    return 1; // サーバーソケット
  }
  return 0;
#endif
}

int TerminalController::read_stdin(char &ch) {
#if defined(_WIN32)
  return 0;
#else
  return read(STDIN_FILENO, &ch, 1);
#endif
}

#if defined(_WIN32) || defined(_WIN64)
void TerminalController::start_input_thread(NetworkStreamClient &client) {
  pImpl->input_thread = std::thread([&client]() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    char buf[64];
    DWORD bytesRead;
    while (ReadFile(hIn, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
      client.send_data(buf, bytesRead);
    }
  });
  pImpl->thread_running = true;
  pImpl->input_thread.detach();
}
#endif
