#pragma once
#include <cstdint>
#include <memory>
#include <string>

class NetworkStreamClient;

class TerminalController {
public:
  TerminalController();
  ~TerminalController();

  // コピー禁止、ムーブ許可
  TerminalController(const TerminalController &) = delete;
  TerminalController &operator=(const TerminalController &) = delete;
  TerminalController(TerminalController &&) noexcept;
  TerminalController &operator=(TerminalController &&) noexcept;

  void enable_raw_mode();
  void disable_raw_mode();
  void get_terminal_size(uint16_t &w, uint16_t &h);

  void set_unique_title(const std::string &title);
  void scan_and_send_keys(NetworkStreamClient &client);

  // timeout_ms: -1 はブロッキング、0以上はタイムアウト時間(ミリ秒)
  // 戻り値:
  //   0: タイムアウト
  //  -1: エラーまたは切断
  //   1: サーバーからデータ受信可能
  //   2: 標準入力からデータ受信可能 (Linuxのみ)
  int poll_events(NetworkStreamClient &client, int timeout_ms);

  int read_stdin(char &ch);

#if defined(_WIN32) || defined(_WIN64)
  void start_input_thread(NetworkStreamClient &client);
#endif

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};
