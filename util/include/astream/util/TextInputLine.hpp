#pragma once

#include <astream/InputDevice.hpp>
#include <string>

namespace astream::util {

class TextInputLine {
private:
  std::string m_text; // 利用者が永続して保持したい文字列（チャットの中身など）
  size_t m_max_length;

public:
  explicit TextInputLine(size_t maxLength = 64) : m_max_length(maxLength) {}

  bool update(const InputDevice &input) {
    // そのフレームに届いた文字（最大8バイト）を取得
    std::string_view frame_chars = input.getFrameInput();
    if (frame_chars.empty()) {
      return false;
    }

    bool changed = false;
    for (char ch : frame_chars) {
      if (ch == 0x08 || ch == 0x7F) { // Backspace
        if (!m_text.empty()) {
          m_text.pop_back();
          changed = true;
        }
      } else {
        if (m_text.length() < m_max_length) {
          m_text.push_back(ch);
          changed = true;
        }
      }
    }
    return changed;
  }

  [[nodiscard]] const std::string &str() const noexcept { return m_text; }
  void clear() noexcept { m_text.clear(); }
};

} // namespace astream::util
