#pragma once
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string_view>

enum class Key : uint8_t {
  // 小文字アルファベット
  a,
  b,
  c,
  d,
  e,
  f,
  g,
  h,
  i,
  j,
  k,
  l,
  m,
  n,
  o,
  p,
  q,
  r,
  s,
  t,
  u,
  v,
  w,
  x,
  y,
  z,
  // 大文字アルファベット
  A,
  B,
  C,
  D,
  E,
  F,
  G,
  H,
  I,
  J,
  K,
  L,
  M,
  N,
  O,
  P,
  Q,
  R,
  S,
  T,
  U,
  V,
  W,
  X,
  Y,
  Z,
  // 数字
  Num0,
  Num1,
  Num2,
  Num3,
  Num4,
  Num5,
  Num6,
  Num7,
  Num8,
  Num9,
  // 安全な記号 (エスケープシーケンスと被らない1バイトのもの)
  Minus,
  Plus,
  Equal,
  Asterisk,
  Slash,
  Period,
  Comma,
  Question,
  Exclamation,
  // 特殊キー・矢印
  Up,
  Down,
  Left,
  Right,
  Space,
  Enter,
  Escape,
  Count, // キー総数
  Unknown
};

class InputDevice {
private:
  bool key_states[static_cast<size_t>(Key::Count)] = {false};
  bool prev_key_states[static_cast<size_t>(Key::Count)] = {false};
  float delta_time = 0.0f;

  // エスケープシーケンス解析用のステート
  enum class ParseState { Normal, ExpectBracket, ExpectSequence };
  ParseState parse_state = ParseState::Normal;

  static constexpr size_t BUFFER_SIZE = 8;
  static constexpr size_t BUFFER_MASK = BUFFER_SIZE - 1;

  // 各フレームごとのデータしか保証しない
  // また、Ascii8文字以上を入力はありえないはず
  static constexpr size_t MAX_INPUT_BYTES = 8;
  char input_buffer[MAX_INPUT_BYTES] = {0};
  uint8_t input_len = 0; // そのフレームで書き込まれた有効なバイト数

public:
  float getDeltaTime() const { return delta_time; }
  void setDeltaTime(float dt) { delta_time = dt; }

  bool getKey(Key k) const {
    if (k == Key::Unknown || k == Key::Count)
      return false;
    return key_states[static_cast<size_t>(k)];
  }

  bool getKeyDown(Key k) const {
    if (k == Key::Unknown || k == Key::Count)
      return false;
    return key_states[static_cast<size_t>(k)] &&
           !prev_key_states[static_cast<size_t>(k)];
  }

  [[nodiscard]] inline std::string_view getFrameInput() const noexcept {
    return std::string_view(input_buffer, input_len);
  }

  // server.cpp のセッションループ最末尾で呼び出す
  void nextFrame() {
    std::copy(std::begin(key_states), std::end(key_states),
              std::begin(prev_key_states));
    std::fill(std::begin(key_states), std::end(key_states), false);
    input_len = 0;
  }

  // クライアントから受信した生バイトを1バイトずつストリーム解析
  void pressKey(char ch) {
    uint8_t u_ch = static_cast<uint8_t>(ch);

    switch (parse_state) {
    case ParseState::Normal:
      if (u_ch == 0x1b) { // Escapeシーケンスの開始
        parse_state = ParseState::ExpectBracket;
      } else {
        Key k = mapCharToKey(ch);
        if (k != Key::Unknown)
          key_states[static_cast<size_t>(k)] = true;

        // ★ 英数記号限定でバッファに詰める (可視ASCII 0x20~0x7E, Enter,
        // Backspace)
        if ((u_ch >= 0x20 && u_ch <= 0x7E) || u_ch == '\n' || u_ch == '\r' ||
            u_ch == 0x08 || u_ch == 0x7F) {
          if (input_len < MAX_INPUT_BYTES) [[likely]] {
            input_buffer[input_len++] = ch;
          }
        }
      }
      break;

    case ParseState::ExpectBracket:
      if (ch == '[') {
        parse_state = ParseState::ExpectSequence;
      } else {
        // \x1b 単体、または後ろが [ 以外なら通常の Escape キー
        key_states[static_cast<size_t>(Key::Escape)] = true;
        parse_state = ParseState::Normal;

        // 届いた文字も通常文字として再処理
        Key k = mapCharToKey(ch);
        if (k != Key::Unknown)
          key_states[static_cast<size_t>(k)] = true;
      }
      break;

    case ParseState::ExpectSequence:
      switch (ch) {
      case 'A':
        key_states[static_cast<size_t>(Key::Up)] = true;
        break;
      case 'B':
        key_states[static_cast<size_t>(Key::Down)] = true;
        break;
      case 'C':
        key_states[static_cast<size_t>(Key::Right)] = true;
        break;
      case 'D':
        key_states[static_cast<size_t>(Key::Left)] = true;
        break;
      default:
        break; // 他のシーケンス（F1-F12やPageUp/Downなど複雑なものは一旦スルー）
      }
      parse_state = ParseState::Normal;
      break;
    }
  }

private:
  Key mapCharToKey(char ch) {
    // 小文字
    if (ch >= 'a' && ch <= 'z')
      return static_cast<Key>(static_cast<uint8_t>(Key::a) + (ch - 'a'));
    // 大文字
    if (ch >= 'A' && ch <= 'Z')
      return static_cast<Key>(static_cast<uint8_t>(Key::A) + (ch - 'A'));
    // 数字
    if (ch >= '0' && ch <= '9')
      return static_cast<Key>(static_cast<uint8_t>(Key::Num0) + (ch - '0'));

    // 安全な記号類のマッピング
    switch (ch) {
    case ' ':
      return Key::Space;
    case '\n':
    case '\r':
      return Key::Enter;
    case '-':
      return Key::Minus;
    case '+':
      return Key::Plus;
    case '=':
      return Key::Equal;
    case '*':
      return Key::Asterisk;
    case '/':
      return Key::Slash;
    case '.':
      return Key::Period;
    case ',':
      return Key::Comma;
    case '?':
      return Key::Question;
    case '!':
      return Key::Exclamation;
    default:
      return Key::Unknown;
    }
  }
};
