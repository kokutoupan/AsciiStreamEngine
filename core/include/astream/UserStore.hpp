#pragma once

#include <string>
#include <unordered_map>

class UserStore {
public:
  // ユーザー登録（成否を返す）
  bool registerUser(const std::string &username, const std::string &password) {
    if (m_users.contains(username))
      return false; // 既に存在する

    // TODO: 本格的にやるならここでソルト+ハッシュ
    m_users[username] = password;
    saveToFile(); // オプションでファイル永続化
    return true;
  }

  // 認証チェック
  bool authenticate(const std::string &username, const std::string &password) {
    auto it = m_users.find(username);
    if (it == m_users.end())
      return false;
    return it->second == password;
  }

private:
  std::unordered_map<std::string, std::string> m_users;

  void saveToFile() {}
  void loadFromFile() {}
};
