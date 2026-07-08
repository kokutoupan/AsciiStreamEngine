#pragma once

#include <string>
#include <unordered_map>

#include <cctype>
#include <string>
#include <unordered_map>

class UserStore {
public:
  // ユーザー登録（成否を返す）
  bool registerUser(std::string username, std::string password) {
    if (!cleanAndValidate(username) || !cleanAndValidate(password)) {
      return false;
    }

    if (m_users.contains(username)) {
      return false; // 既に存在する
    }

    // TODO: 本格的にやるならここでソルト+ハッシュ
    m_users[username] = password;
    saveToFile();
    return true;
  }

  // 認証チェック
  bool authenticate(std::string username, std::string password) {
    // 入力値のクリーンアップ
    if (!cleanAndValidate(username) || !cleanAndValidate(password)) {
      return false;
    }

    auto it = m_users.find(username);
    if (it == m_users.end()) {
      return false;
    }

    return it->second == password;
  }

private:
  std::unordered_map<std::string, std::string> m_users;

  // 空白を除去し、有効な文字列（空じゃない）かチェックする
  bool cleanAndValidate(std::string &str) {
    std::erase_if(str, [](unsigned char c) { return std::isspace(c); });

    return !str.empty();
  }

  void saveToFile() {}
  void loadFromFile() {}
};
