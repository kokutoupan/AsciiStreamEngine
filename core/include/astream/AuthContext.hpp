#pragma once
#include "astream/util/StringUtil.hpp"
#include <algorithm>
#include <astream/GraphicsDevice.hpp>
#include <astream/InputDevice.hpp>
#include <astream/UserStore.hpp>
#include <astream/util/TextInputLine.hpp>
#include <astream/util/TextureUtil.hpp>
#include <future>
#include <string>

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
    if (userStore &&
        userStore->getRegisterPolicy() == RegisterPolicy::AllowAll) {
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
        } else if (currentInput == "2" && userStore &&
                   userStore->getRegisterPolicy() == RegisterPolicy::AllowAll) {
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
        m_animFrame = 0;
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
      m_animFrame++;
      m_isDirty = true;
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

    int viewW = colorBuffer.width();
    int viewH = colorBuffer.height();
    int boxW = 44;
    int boxH = 11;
    int startX = std::max(0, (viewW - boxW) / 2);
    int startY = std::max(0, (viewH - boxH) / 2);

    // 2. 現在の状態の見た目をバッファに描き込む
    switch (m_state) {
    case State::SelectMode: {
      TextureUtil::drawBox(colorBuffer, startX, startY, boxW, boxH);

      std::string title = "AsciiStreamEngine Login";
      int titleX = startX + (boxW - (int)title.length()) / 2;
      TextureUtil::drawText(colorBuffer, titleX, startY + 1, title);
      TextureUtil::drawText(colorBuffer, startX, startY + 2,
                            "+" + std::string(boxW - 2, '-') + "+");

      TextureUtil::drawText(colorBuffer, startX + 4, startY + 4, "1. Login");
      if (userStore &&
          userStore->getRegisterPolicy() == RegisterPolicy::AllowAll) {
        TextureUtil::drawText(colorBuffer, startX + 4, startY + 5,
                              "2. Register");
      }

      std::string prompt = "Select option: [ ";
      prompt += m_inputLine.str();
      prompt += " ]";
      TextureUtil::drawText(colorBuffer, startX + 4, startY + 7, prompt);

      std::string inst = "Press [Enter] to select";
      int instX = startX + (boxW - (int)inst.length()) / 2;
      TextureUtil::drawText(colorBuffer, instX, startY + 9, inst);
      break;
    }

    case State::InputUsername: {
      TextureUtil::drawBox(colorBuffer, startX, startY, boxW, boxH);

      std::string title = m_isRegisterMode ? "User Registration" : "User Login";
      int titleX = startX + (boxW - (int)title.length()) / 2;
      TextureUtil::drawText(colorBuffer, titleX, startY + 1, title);
      TextureUtil::drawText(colorBuffer, startX, startY + 2,
                            "+" + std::string(boxW - 2, '-') + "+");

      TextureUtil::drawText(colorBuffer, startX + 4, startY + 4,
                            "Enter Username:");

      std::string inputField = "[ " + m_inputLine.str();
      int fieldW = boxW - 8;
      if ((int)inputField.length() < fieldW - 2) {
        inputField += std::string(fieldW - 2 - inputField.length(), ' ');
      }
      inputField += " ]";
      TextureUtil::drawText(colorBuffer, startX + 4, startY + 6, inputField);

      std::string inst = "Press [Enter] to submit";
      int instX = startX + (boxW - (int)inst.length()) / 2;
      TextureUtil::drawText(colorBuffer, instX, startY + 9, inst);
      break;
    }

    case State::InputPassword: {
      TextureUtil::drawBox(colorBuffer, startX, startY, boxW, boxH);

      std::string title = m_isRegisterMode ? "User Registration" : "User Login";
      int titleX = startX + (boxW - (int)title.length()) / 2;
      TextureUtil::drawText(colorBuffer, titleX, startY + 1, title);
      TextureUtil::drawText(colorBuffer, startX, startY + 2,
                            "+" + std::string(boxW - 2, '-') + "+");

      TextureUtil::drawText(colorBuffer, startX + 4, startY + 4,
                            "Username: " + m_username);
      TextureUtil::drawText(colorBuffer, startX + 4, startY + 5,
                            "Enter Password:");

      std::string masked(m_inputLine.str().length(), '*');
      std::string inputField = "[ " + masked;
      int fieldW = boxW - 8;
      if ((int)inputField.length() < fieldW - 2) {
        inputField += std::string(fieldW - 2 - inputField.length(), ' ');
      }
      inputField += " ]";
      TextureUtil::drawText(colorBuffer, startX + 4, startY + 7, inputField);

      std::string inst = "Press [Enter] to submit";
      int instX = startX + (boxW - (int)inst.length()) / 2;
      TextureUtil::drawText(colorBuffer, instX, startY + 9, inst);
      break;
    }

    case State::Verifying: {
      TextureUtil::drawBox(colorBuffer, startX, startY, boxW, boxH);

      std::string title = "Please Wait";
      int titleX = startX + (boxW - (int)title.length()) / 2;
      TextureUtil::drawText(colorBuffer, titleX, startY + 1, title);
      TextureUtil::drawText(colorBuffer, startX, startY + 2,
                            "+" + std::string(boxW - 2, '-') + "+");

      char spinner[] = {'|', '/', '-', '\\'};
      char current_spinner = spinner[(m_animFrame / 3) % 4];
      std::string status =
          std::string(1, current_spinner) + " Verifying credentials...";
      int statusX = startX + (boxW - (int)status.length()) / 2;
      TextureUtil::drawText(colorBuffer, statusX, startY + 4, status);

      int barMaxW = boxW - 12;
      int barProgress = (m_animFrame / 2) % (barMaxW + 1);
      std::string bar = "[";
      if (barProgress > 0) {
        bar += std::string(barProgress - 1, '=') + ">";
      }
      bar += std::string(barMaxW - barProgress, ' ') + "]";
      int barX = startX + (boxW - (int)bar.length()) / 2;
      TextureUtil::drawText(colorBuffer, barX, startY + 6, bar);

      std::string msg = "Processing secure hash...";
      int msgX = startX + (boxW - (int)msg.length()) / 2;
      TextureUtil::drawText(colorBuffer, msgX, startY + 8, msg);
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
  int m_animFrame = 0;

  astream::util::TextInputLine
      m_inputLine; // チャットでも使っている一行入力バッファ
};
