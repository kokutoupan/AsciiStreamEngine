#include <cstdint>
#include <format>
#include <iostream>
#include <fstream>
#include <print>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cctype>

#if defined(_WIN32)
#include <io.h>
#define ISATTY(fd) _isatty(fd)
#define STDIN_FILENO 0
#else
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <cerrno>
#define ISATTY(fd) isatty(fd)
#endif

#include "./NetworkStreamClient.hpp"
#include "./TerminalBuffer.hpp"
#include "./TerminalController.hpp"

// zlib は双方共通
#include <zconf.h>
#include <zlib.h>

constexpr std::string_view HOST_PORT = "12345";
constexpr std::string_view HOST_DOMAIN = "localhost";

// =============================================================================
// コマンド定義とパース用ヘルパー
// =============================================================================
struct Command {
  std::string type; // "wait", "key", "type", "raw"
  int wait_ms = 0;
  std::string text;
  std::vector<uint8_t> raw_bytes;
};

// \n \t \r \\ \xHH のデコード
std::string parse_type_string(std::string_view input) {
  std::string result;
  result.reserve(input.length());
  for (size_t i = 0; i < input.length(); ++i) {
    if (input[i] == '\\' && i + 1 < input.length()) {
      char next = input[i + 1];
      if (next == 'n') {
        result += '\n';
        i += 1;
      } else if (next == 't') {
        result += '\t';
        i += 1;
      } else if (next == 'r') {
        result += '\r';
        i += 1;
      } else if (next == '\\') {
        result += '\\';
        i += 1;
      } else if (next == 'x' && i + 3 < input.length()) {
        std::string hex_str{input.substr(i + 2, 2)};
        char val = static_cast<char>(std::strtol(hex_str.c_str(), nullptr, 16));
        result += val;
        i += 3;
      } else {
        result += '\\';
      }
    } else {
      result += input[i];
    }
  }
  return result;
}

std::vector<uint8_t> parse_hex(std::string_view hex) {
  std::vector<uint8_t> bytes;
  std::string clean_hex;
  for (char c : hex) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      clean_hex += c;
    }
  }
  bytes.reserve(clean_hex.length() / 2);
  for (size_t i = 0; i + 1 < clean_hex.length(); i += 2) {
    std::string byteString = clean_hex.substr(i, 2);
    uint8_t byte = static_cast<uint8_t>(std::strtol(byteString.c_str(), nullptr, 16));
    bytes.push_back(byte);
  }
  return bytes;
}

std::string get_key_sequence(std::string_view key_name) {
  if (key_name == "up") return "\x1b[A";
  if (key_name == "down") return "\x1b[B";
  if (key_name == "right") return "\x1b[C";
  if (key_name == "left") return "\x1b[D";
  if (key_name == "space") return " ";
  if (key_name == "enter") return "\n";
  if (key_name == "escape") return "\x1b";
  if (key_name.length() == 1) return std::string(key_name);
  return "";
}

std::vector<Command> parse_script(std::istream& in) {
  std::vector<Command> commands;
  std::string line;
  while (std::getline(in, line)) {
    // 先頭の余白を除去
    size_t start = line.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) continue; // 空行
    std::string_view sv = std::string_view(line).substr(start);
    if (sv.starts_with('#')) continue; // コメント

    // コマンド名と引数の分割
    size_t cmd_end = sv.find_first_of(" \t");
    std::string cmd_name = std::string(sv.substr(0, cmd_end));
    std::transform(cmd_name.begin(), cmd_name.end(), cmd_name.begin(), [](unsigned char c){ return std::tolower(c); });

    std::string_view args_sv = "";
    if (cmd_end != std::string::npos) {
      size_t args_start = sv.find_first_not_of(" \t", cmd_end);
      if (args_start != std::string::npos) {
        args_sv = sv.substr(args_start);
      }
    }

    // 末尾の改行・復帰を除去
    while (!args_sv.empty() && (args_sv.back() == '\r' || args_sv.back() == '\n')) {
      args_sv.remove_suffix(1);
    }

    Command cmd;
    if (cmd_name == "wait") {
      cmd.type = "wait";
      try {
        cmd.wait_ms = std::stoi(std::string(args_sv));
      } catch (...) {
        cmd.wait_ms = 0;
      }
      commands.push_back(cmd);
    } else if (cmd_name == "key") {
      cmd.type = "key";
      cmd.text = std::string(args_sv);
      commands.push_back(cmd);
    } else if (cmd_name == "type") {
      cmd.type = "type";
      cmd.text = parse_type_string(args_sv);
      commands.push_back(cmd);
    } else if (cmd_name == "raw") {
      cmd.type = "raw";
      cmd.raw_bytes = parse_hex(args_sv);
      commands.push_back(cmd);
    } else {
      std::println(std::cerr, "Warning: Unknown script command '{}'", cmd_name);
    }
  }
  return commands;
}

// =============================================================================
// ソケットポーリング用関数 (標準入力を含めないため独自定義)
// =============================================================================
int poll_socket(intptr_t socket_handle, int timeout_ms) {
#if defined(_WIN32)
  WSAPOLLFD fd;
  fd.fd = static_cast<SOCKET>(socket_handle);
  fd.events = POLLIN;
  int ret = WSAPoll(&fd, 1, timeout_ms);
  if (ret < 0) return -1;
  if (ret == 0) return 0;
  if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;
  if (fd.revents & POLLIN) return 1;
  return 0;
#else
  struct pollfd fd;
  fd.fd = static_cast<int>(socket_handle);
  fd.events = POLLIN;
  int ret = poll(&fd, 1, timeout_ms);
  if (ret < 0) {
    if (errno == EINTR) return 0;
    return -1;
  }
  if (ret == 0) return 0;
  if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;
  if (fd.revents & POLLIN) return 1;
  return 0;
#endif
}

// 画面イメージ書き出し用
void output_screen(const std::vector<uint8_t>& screen, uint8_t w, uint8_t h, std::ostream& out) {
  if (screen.size() != static_cast<size_t>(w * h)) {
    out << "[Error: screen size (" << screen.size() << ") does not match w * h (" << static_cast<int>(w) << " * " << static_cast<int>(h) << ")]\n";
    return;
  }
  for (uint8_t y = 0; y < h; ++y) {
    out.write(reinterpret_cast<const char*>(screen.data() + y * w), w);
    out << '\n';
  }
}

// =============================================================================
// メイン関数
// =============================================================================
int main(int argc, char *argv[]) {
  const char *port = HOST_PORT.data();
  const char *host = HOST_DOMAIN.data();
  std::string script_file = "";
  bool headless = false;
  bool print_screen = false;
  std::string write_screen_file = "";
  int timeout_seconds = 0;
  int cmd_width = 0;
  int cmd_height = 0;
  bool quit_on_finish = true;

  for (int i = 1; i < argc; i++) {
    std::string_view arg(argv[i]);
    if (arg == "-p" && i + 1 < argc) {
      port = argv[++i];
    } else if (arg == "-h" && i + 1 < argc) {
      host = argv[++i];
    } else if (arg == "-f" && i + 1 < argc) {
      script_file = argv[++i];
    } else if (arg == "--headless") {
      headless = true;
    } else if (arg == "--print-screen") {
      print_screen = true;
    } else if (arg == "--write-screen" && i + 1 < argc) {
      write_screen_file = argv[++i];
    } else if (arg == "--timeout" && i + 1 < argc) {
      timeout_seconds = std::stoi(argv[++i]);
    } else if (arg == "--width" && i + 1 < argc) {
      cmd_width = std::stoi(argv[++i]);
    } else if (arg == "--height" && i + 1 < argc) {
      cmd_height = std::stoi(argv[++i]);
    } else if (arg == "--no-quit") {
      quit_on_finish = false;
    } else {
      std::println(std::cerr, "Usage: {} [-h host] [-p port] [-f script_file] [--headless] [--print-screen] [--write-screen file] [--timeout sec] [--width w] [--height h] [--no-quit]", argv[0]);
      return 1;
    }
  }

  // スクリプト入力元の確定
  bool script_in_stdin = false;
  if (script_file.empty()) {
    if (!ISATTY(STDIN_FILENO)) {
      script_in_stdin = true;
    } else {
      std::println(std::cerr, "Error: No script file specified via -f, and stdin is a TTY.");
      std::println(std::cerr, "Usage: {} [-h host] [-p port] [-f script_file] [--headless] [--print-screen] [--write-screen file] [--timeout sec] [--width w] [--height h] [--no-quit]", argv[0]);
      return 1;
    }
  } else if (script_file == "-") {
    script_in_stdin = true;
  }

  // スクリプトの読み込みと解析 (起動前に完了させる)
  std::vector<Command> commands;
  if (script_in_stdin) {
    commands = parse_script(std::cin);
  } else {
    std::ifstream file(script_file);
    if (!file.is_open()) {
      std::println(std::cerr, "Error: Failed to open script file: {}", script_file);
      return 1;
    }
    commands = parse_script(file);
  }

  NetworkStreamClient client;
  if (!client.connect(host, port)) {
    return -1;
  }

  TerminalController term;

  // 画面サイズの決定
  uint16_t w = 80;
  uint16_t h = 24;
  if (!headless) {
    uint16_t term_w = 0, term_h = 0;
    term.get_terminal_size(term_w, term_h);
    if (term_w > 0 && term_h > 0) {
      w = term_w;
      h = term_h;
    }
  }
  if (cmd_width > 0) w = cmd_width;
  if (cmd_height > 0) h = cmd_height;

  if (0 >= w || 0 >= h) {
    std::println(std::cerr, "Error: Invalid window size {}x{}", w, h);
    return -1;
  }
  if (w > 256) w = 256;
  if (h > 256) h = 256;

  if (!client.send_window_size(w, h)) {
    std::println(std::cerr, "Error: Failed to send window size");
    return -1;
  }

  if (!headless) {
    term.enable_raw_mode();
  }

  unsigned char buffer[65536];
  unsigned char out_buf[65536];
  TerminalBuffer terminal_buf;

  // 最新描画結果の保存用
  std::vector<uint8_t> last_screen;
  uint8_t last_width = 0;
  uint8_t last_height = 0;
  std::mutex screen_mutex;
  std::mutex send_mutex;

  std::atomic<bool> script_finished{false};
  std::atomic<bool> should_exit{false};

  // スクリプト実行用スレッドの起動
  std::thread script_thread([&]() {
    for (const auto& cmd : commands) {
      if (should_exit) break;

      if (cmd.type == "wait") {
        std::this_thread::sleep_for(std::chrono::milliseconds(cmd.wait_ms));
      } else if (cmd.type == "key") {
        std::string seq = get_key_sequence(cmd.text);
        if (!seq.empty()) {
          std::lock_guard<std::mutex> lock(send_mutex);
          client.send_data(seq.data(), seq.size());
        }
      } else if (cmd.type == "type") {
        // キーボード入力オーバーフローを防ぐため、1文字ずつ30ms間隔で送信
        for (char ch : cmd.text) {
          if (should_exit) break;
          {
            std::lock_guard<std::mutex> lock(send_mutex);
            client.send_data(&ch, 1);
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
      } else if (cmd.type == "raw") {
        if (!cmd.raw_bytes.empty()) {
          std::lock_guard<std::mutex> lock(send_mutex);
          client.send_data(cmd.raw_bytes.data(), cmd.raw_bytes.size());
        }
      }
    }
    script_finished = true;
  });

  auto start_time = std::chrono::steady_clock::now();
  intptr_t socket_handle = client.get_socket_handle();

  while (!should_exit) {
    // 50msごとにポーリング
    int event_type = poll_socket(socket_handle, 50);
    if (event_type < 0) {
      break; // ソケットエラーまたは切断
    }

    if (event_type == 1) {
      uint32_t len_oder;
      uint8_t packet_w, packet_h;

      if (client.recv_packet_header(len_oder, packet_w, packet_h) <= 0) {
        break; // サーバー切断
      }

      if (len_oder <= 0 || len_oder >= 256 * 256) {
        std::println(std::cerr, "Error: Receive buffer too big ({})", len_oder);
        break;
      }

      int len = client.recv_exact(buffer, len_oder);
      if (len <= 0) {
        break;
      }

      uLongf out_len = sizeof(out_buf);
      int res = uncompress(out_buf, &out_len, buffer, len);

      if (res == Z_OK) {
        {
          std::lock_guard<std::mutex> lock(screen_mutex);
          last_width = packet_w;
          last_height = packet_h;
          last_screen.assign(out_buf, out_buf + out_len);
        }

        if (!headless) {
          terminal_buf.ensure_size(packet_w, packet_h);
          terminal_buf.draw(std::span<const unsigned char>(out_buf, out_len));
        }
      }
    }

    // スクリプト終了時の処理
    if (script_finished && quit_on_finish) {
      // サーバーに最終フレームの処理時間・送受信猶予を与えるため少し待機
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      // 最後の残データがあれば受信する
      while (poll_socket(socket_handle, 0) == 1) {
        uint32_t len_oder;
        uint8_t packet_w, packet_h;
        if (client.recv_packet_header(len_oder, packet_w, packet_h) <= 0) break;
        if (len_oder <= 0 || len_oder >= 256 * 256) break;
        int len = client.recv_exact(buffer, len_oder);
        if (len <= 0) break;
        uLongf out_len = sizeof(out_buf);
        int res = uncompress(out_buf, &out_len, buffer, len);
        if (res == Z_OK) {
          std::lock_guard<std::mutex> lock(screen_mutex);
          last_width = packet_w;
          last_height = packet_h;
          last_screen.assign(out_buf, out_buf + out_len);
        }
      }
      break;
    }

    // グローバルのタイムアウト監視
    if (timeout_seconds > 0) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
      if (elapsed >= timeout_seconds) {
        std::println(std::cerr, "Error: Timeout of {} seconds reached.", timeout_seconds);
        break;
      }
    }
  }

  // スクリプトスレッドがまだ動いていれば終了を促してjoin
  should_exit = true;
  if (script_thread.joinable()) {
    script_thread.join();
  }

  if (!headless) {
    term.disable_raw_mode();
  }

  client.close();

  // 最終画面状態を出力・ファイルに保存
  if (!write_screen_file.empty()) {
    std::ofstream out_file(write_screen_file);
    if (out_file.is_open()) {
      std::lock_guard<std::mutex> lock(screen_mutex);
      output_screen(last_screen, last_width, last_height, out_file);
    } else {
      std::println(std::cerr, "Error: Failed to open output screen file {}", write_screen_file);
    }
  }

  if (print_screen) {
    std::lock_guard<std::mutex> lock(screen_mutex);
    output_screen(last_screen, last_width, last_height, std::cout);
  }

  return 0;
}
