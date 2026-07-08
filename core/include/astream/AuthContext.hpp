#pragma once
#include <astream/GraphicsDevice.hpp>
#include <astream/InputDevice.hpp>
#include <astream/UserStore.hpp>
#include <astream/util/TextInputLine.hpp>
#include <string>

struct AuthResult {
  bool success = false;
  std::string username = "";
};

class AuthContext {
public:
  enum class State { SelectMode, InputUsername, InputPassword };

  AuthContext(UserStore *_userStore) : userStore(_userStore) {
    // モード選択用の初期メッセージ
    m_menuText = "=================================\n"
                 "    AsciiStreamEngine Login\n"
                 "=================================\n"
                 "1. Login\n"
                 "2. Register\n\n"
                 "Select mode (1-2): ";
  }

  // 毎フレーム、Engineから入力を受け取って描画と状態遷移を行う
  AuthResult update(int fd, const InputDevice &input,
                    TextureView<char> colorBuffer) {
    AuthResult result;
    // 1. 画面のクリア（ログイン画面全体の背景を空白に）
    for (int y = 0; y < colorBuffer.height(); ++y) {
      for (int x = 0; x < colorBuffer.width(); ++x) {
        colorBuffer.at(x, y) = ' ';
      }
    }

    // 2. 状態に応じた入力処理とテキスト描画
    switch (m_state) {
    case State::SelectMode: {
      // メニュー描画
      drawText(colorBuffer, m_menuText, 2, 2);

      // TextInputLine を使って1文字（または改行まで）受ける
      m_inputLine.update(input);
      std::string currentInput = m_inputLine.str();
      drawText(colorBuffer, currentInput, 21, 8); // 入力中の文字をエコーバック

      if (input.getKeyDown(Key::Enter)) {
        if (currentInput == "1") {
          m_isRegisterMode = false;
          m_state = State::InputUsername;
          m_inputLine.clear();
        } else if (currentInput == "2") {
          m_isRegisterMode = true;
          m_state = State::InputUsername;
          m_inputLine.clear();
        } else {
          m_inputLine.clear(); // 1, 2 以外はリセット
        }
      }
      break;
    }

    case State::InputUsername: {
      std::string title =
          m_isRegisterMode ? "[User Registration]" : "[User Login]";
      drawText(colorBuffer, title, 2, 2);
      drawText(colorBuffer, "Username: ", 2, 4);

      m_inputLine.update(input);
      std::string currentInput = m_inputLine.str();
      drawText(colorBuffer, currentInput, 12, 4); // 入力中のユーザー名を表示

      if (input.getKeyDown(Key::Enter)) {
        if (!currentInput.empty()) {
          m_username = currentInput;
          m_state = State::InputPassword;
          m_inputLine.clear();
        }
      }
      break;
    }

    case State::InputPassword: {
      std::string title =
          m_isRegisterMode ? "[User Registration]" : "[User Login]";
      drawText(colorBuffer, title, 2, 2);
      drawText(colorBuffer, "Username: " + m_username, 2, 4);
      drawText(colorBuffer, "Password: ", 2, 5);

      m_inputLine.update(input);
      std::string currentInput = m_inputLine.str();

      // パスワードなのでアスタリスクでマスク表示
      std::string masked(currentInput.length(), '*');
      drawText(colorBuffer, masked, 12, 5);

      if (input.getKeyDown(Key::Enter)) {
        m_password = currentInput;
        m_inputLine.clear();

        if (m_isRegisterMode) {
          // 【登録モード】
          // 本来はここでUserStoreに登録する
          // 例: bool ok = userStore.registerUser(m_username, m_password);
          bool registerOk = userStore->registerUser(m_username, m_password);

          if (registerOk) {
            result.success = true;
            result.username = m_username;
          } else {
            // 失敗したら最初に戻る（エラーメッセージを出すとなお良い）
            m_state = State::SelectMode;
          }
        } else {
          // 【ログインモード】
          // 本来はここでUserStoreの照合をする
          bool authOk = userStore->authenticate(m_username, m_password);
          // bool authOk = true;

          if (authOk) {
            result.success = true;
            result.username = m_username;
          } else {
            m_state = State::SelectMode; // 弾かれたら最初へ
          }
        }
      }
      break;
    }
    }

    return result;
  }

private:
  UserStore *userStore;
  State m_state = State::SelectMode;
  bool m_isRegisterMode = false;
  std::string m_username = "";
  std::string m_password = "";
  std::string m_menuText = "";

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
