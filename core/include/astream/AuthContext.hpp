#pragma once
#include "astream/util/StringUtil.hpp"
#include <astream/GraphicsDevice.hpp>
#include <astream/InputDevice.hpp>
#include <astream/UserStore.hpp>
#include <astream/util/TextInputLine.hpp>
#include <string>
#include <future>

struct AuthResult {
  bool success = false;
  std::string username = "";
};

class AuthContext {
public:
  bool m_isDirty = true;
  enum class State { SelectMode, InputUsername, InputPassword, Verifying };

  AuthContext(UserStore *_userStore) : userStore(_userStore) {}

  std::string getMenuText() const {
    if (userStore && userStore->getRegisterPolicy() == RegisterPolicy::AllowAll) {
      return "=================================\n"
             "    AsciiStreamEngine Login\n"
             "=================================\n"
             "1. Login\n"
             "2. Register\n\n"
             "Select mode (1-2): ";
    } else {
      return "=================================\n"
             "    AsciiStreamEngine Login\n"
             "=================================\n"
             "1. Login\n"
             "             \n\n"
             "Select mode (1):   ";
    }
  }

  AuthResult update(int fd, const InputDevice &input) {
    AuthResult result;

    // 現在の状態を記憶（状態が変わったか検知するため）
    State previousState = m_state;

    switch (m_state) {
    case State::SelectMode: {
      // 入力によってテキストラインが更新されたら Dirty
      if (m_inputLine.update(input)) {
        m_isDirty = true;
      }

      if (input.getKeyDown(Key::Enter)) {
        std::string currentInput = m_inputLine.str();
        std::erase_if(currentInput,
                      [](unsigned char c) { return std::isspace(c); });

        if (currentInput == "1") {
          m_isRegisterMode = false;
          m_state = State::InputUsername;
          m_inputLine.clear();
        } else if (currentInput == "2" && userStore && userStore->getRegisterPolicy() == RegisterPolicy::AllowAll) {
          m_isRegisterMode = true;
          m_state = State::InputUsername;
          m_inputLine.clear();
        } else {
          m_inputLine.clear();
        }
      }
      break;
    }

    case State::InputUsername: {
      if (m_inputLine.update(input)) {
        m_isDirty = true;
      }

      if (input.getKeyDown(Key::Enter)) {
        std::string currentInput = m_inputLine.str();
        astream::util::trim_inplace(currentInput);
        if (!currentInput.empty()) {
          m_username = currentInput;
          m_state = State::InputPassword;
          m_inputLine.clear();
        }
      }
      break;
    }

    case State::InputPassword: {
      if (m_inputLine.update(input)) {
        m_isDirty = true;
      }

      if (input.getKeyDown(Key::Enter)) {
        std::string currentInput = m_inputLine.str();
        astream::util::trim_inplace(currentInput);
        m_password = currentInput;
        m_inputLine.clear();

        m_state = State::Verifying;
        m_isDirty = true;

        m_authFuture = std::async(std::launch::async, [this]() {
          if (m_isRegisterMode) {
            return userStore->registerUser(m_username, m_password);
          } else {
            return userStore->authenticate(m_username, m_password);
          }
        });
      }
      break;
    }

    case State::Verifying: {
      if (m_authFuture.valid()) {
        auto status = m_authFuture.wait_for(std::chrono::seconds(0));
        if (status == std::future_status::ready) {
          bool success = m_authFuture.get();
          if (success) {
            result.success = true;
            result.username = m_username;
          } else {
            m_state = State::SelectMode;
          }
          m_isDirty = true;
        }
      }
      break;
    }
    }

    // 途中で m_state 自体が変わっていたら強制的に Dirty にする
    if (m_state != previousState) {
      m_isDirty = true;
    }

    return result;
  }

  bool render(TextureView<char> colorBuffer) {
    // 変更がないなら、クリアも描画も一切スキップ
    if (!m_isDirty) {
      return false;
    }

    // 1. 画面のクリア
    colorBuffer.clear(' ');

    // 2. 現在の状態の見た目をバッファに描き込む
    switch (m_state) {
    case State::SelectMode: {
      drawText(colorBuffer, getMenuText(), 2, 2);
      drawText(colorBuffer, m_inputLine.str(), 21, 8);
      break;
    }

    case State::InputUsername: {
      std::string title =
          m_isRegisterMode ? "[User Registration]" : "[User Login]";
      drawText(colorBuffer, title, 2, 2);
      drawText(colorBuffer, "Username: ", 2, 4);
      drawText(colorBuffer, m_inputLine.str(), 12, 4);
      break;
    }

    case State::InputPassword: {
      std::string title =
          m_isRegisterMode ? "[User Registration]" : "[User Login]";
      drawText(colorBuffer, title, 2, 2);
      drawText(colorBuffer, "Username: " + m_username, 2, 4);
      drawText(colorBuffer, "Password: ", 2, 5);

      std::string masked(m_inputLine.str().length(), '*');
      drawText(colorBuffer, masked, 12, 5);
      break;
    }

    case State::Verifying: {
      std::string title =
          m_isRegisterMode ? "[User Registration]" : "[User Login]";
      drawText(colorBuffer, title, 2, 2);
      drawText(colorBuffer, "Username: " + m_username, 2, 4);
      drawText(colorBuffer, "Verifying... Please wait.", 2, 5);
      break;
    }
    }

    m_isDirty = false; // 描画完了したのでフラグを落とす
    return true;       // 「画面が書き換わった」ことを通知
  }

private:
  UserStore *userStore;
  State m_state = State::SelectMode;
  bool m_isRegisterMode = false;
  std::string m_username = "";
  std::string m_password = "";
  std::future<bool> m_authFuture;

  astream::util::TextInputLine
      m_inputLine; // チャットでも使っている一行入力バッファ

  // 簡易テキスト描画ヘルパー
  void drawText(TextureView<char> &buffer, const std::string &text, int startX,
                int startY) {
    int x = startX;
    int y = startY;
    for (char c : text) {
      if (c == '\n') {
        x = startX;
        y++;
        continue;
      }
      if (x < buffer.width() && y < buffer.height()) {
        buffer.at(x, y) = c;
      }
      x++;
    }
  }
};
