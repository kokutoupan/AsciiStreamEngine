#pragma once

#include <chrono>
#include <fstream>
#include <iostream>
#include <string_view>

namespace Debug {

class LoopProfiler {
#ifndef NDEBUG
private:
  std::string_view m_name;
  std::chrono::steady_clock::time_point m_start;
  bool m_stopped = false; // 計測終了フラグ

public:
  explicit LoopProfiler(std::string_view name)
      : m_name(name), m_start(std::chrono::steady_clock::now()) {}

  // 明示的に計測を終了して出力するメソッド
  void stop() {
    if (m_stopped)
      return;
    m_stopped = true;

    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms =
        std::chrono::duration<double, std::milli>(end - m_start).count();

    // 静的なofstream型で一度だけファイルを開き、破棄されるまでストリームを維持する
    static std::ofstream log_file("server_profile.log", std::ios::app);

    if (log_file.is_open()) {
      // CSV形式 [タスク名,処理時間(ms)] で追記
      log_file << m_name << "," << elapsed_ms << "\n";
      log_file.flush(); // 即座に書き込みを反映
    }
  }

  // stop() を呼ばずにスコープを抜けた場合でも自動で計測する（フォールバック）
  ~LoopProfiler() { stop(); }
#else
public:
  explicit LoopProfiler(std::string_view) {}
  ~LoopProfiler() = default;
  void stop() {} // リリース時は何もしない空関数（インライン最適化で消滅）
#endif
};

} // namespace Debug
