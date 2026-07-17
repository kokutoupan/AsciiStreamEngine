#pragma once

#include <argon2.h>

#include <fstream>
#include <iostream>
#include <mutex>
#include <print>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>

#include <astream/auth/RegisterPolicy.hpp>
#include <astream/util/StringUtil.hpp>

namespace astream::detail::auth {

using astream::auth::RegisterPolicy;

enum class AccountStatus { Active, Banned };

class UserStore {
public:
  // ポリシーはコンストラクタやセッターで外部から指定可能に
  explicit UserStore(RegisterPolicy policy = RegisterPolicy::AdminOnly,
                     std::string filepath = "users.txt")
      : m_policy(policy), m_filePath(std::move(filepath)) {
    // インスタンス化のタイミングで自動ロードする
    loadFromFile();
  }

  // 認証
  [[nodiscard]] bool authenticate(std::string_view username,
                                  std::string_view password) const noexcept {
    auto clean_name = astream::util::trim(username);
    if (clean_name.empty() || password.empty()) {
      return false;
    }

    std::string hashed_password;
    bool user_found = false;
    bool is_banned = false;

    {
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      auto it = m_users.find(std::string(clean_name));
      if (it != m_users.end()) {
        user_found = true;
        hashed_password = it->second.hashed_password;
        is_banned = (it->second.status == AccountStatus::Banned);
      }
    }

    if (!user_found) {
      // タイミング攻撃対策：存在しない場合もダミーのハッシュ/比較処理を走らせて時間を一定に
      verifyPassword(password, "dummy_hash_for_timing_attack_protection");
      return false;
    }

    if (is_banned) {
      return false;
    }

    return verifyPassword(password, hashed_password);
  }

  // ユーザー登録（成否を返す）
  // m_policy の状態によって挙動が変化する
  bool registerUser(std::string_view username, std::string_view password,
                    bool is_internal_call = false) {

    // 内部呼び出し（サーバー起動時のファイル読み込みや、管理者コマンド）でない場合、ポリシーをチェック
    if (!is_internal_call) {
      if (m_policy == RegisterPolicy::Disabled ||
          m_policy == RegisterPolicy::AdminOnly) {
        return false; // クライアント側からの自由登録を拒否
      }
    }

    auto clean_name = astream::util::trim(username);
    if (clean_name.empty() || password.empty())
      return false;
    if (clean_name.contains(':') || clean_name.contains('\n') ||
        clean_name.contains('\r'))
      return false;
    if (password.contains('\n') || password.contains('\r'))
      return false;

    std::string name_str(clean_name);
    {
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      if (m_users.contains(name_str)) {
        return false; // 既に存在する
      }
    }

    // パスワードは内部でハッシュ化して保持（ファイル漏洩対策）
    std::string hashed = hashPassword(password);

    {
      std::lock_guard<std::recursive_mutex> lock(m_mutex);
      if (m_users.contains(name_str)) {
        return false; // ハッシュ化中に他のスレッドに登録された
      }
      m_users[name_str].hashed_password = hashed;
      m_users[name_str].status = AccountStatus::Active;
      saveToFile();
    }
    return true;
  }

  bool banUser(std::string_view username) {
    auto clean_name = astream::util::trim(username);
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto it = m_users.find(std::string(clean_name));
    if (it == m_users.end()) {
      return false; // ユーザーが存在しない
    }

    it->second.status = AccountStatus::Banned;
    saveToFile();
    return true;
  }

  // ポリシーの動的変更
  void setRegisterPolicy(RegisterPolicy policy) noexcept { m_policy = policy; }

  constexpr RegisterPolicy getRegisterPolicy() const noexcept {
    return m_policy;
  };

  void loadFromFile() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::ifstream file(m_filePath);
    if (!file.is_open()) {
      // ファイルがない場合は新規作成される想定なのでエラーにはしない
      return;
    }

    m_users.clear();
    std::string line;

    // 1行ずつ読み込む（改行でレコードが区切られているため安全）
    while (std::getline(file, line)) {
      if (line.empty())
        continue;

      std::stringstream ss(line);
      std::string name, hash, status_str;

      // コロン（:）で分割してパース
      if (std::getline(ss, name, ':') && std::getline(ss, hash, ':') &&
          std::getline(ss, status_str, ':')) {

        AccountStatus status = (status_str == "Banned") ? AccountStatus::Banned
                                                        : AccountStatus::Active;

        m_users[name] = UserData{.hashed_password = hash, .status = status};
      }
    }
    std::println("UserStore: Loaded {} users from {}", m_users.size(),
                 m_filePath);
  }

  // テキスト形式でファイルに保存する
  void saveToFile() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::ofstream file(m_filePath, std::ios::trunc); // 常に上書き
    if (!file.is_open()) {
      std::println(std::cerr, "UserStore Error: Could not open {} for writing.",
                   m_filePath);
      return;
    }

    for (const auto &[name, data] : m_users) {
      std::string_view status_str =
          (data.status == AccountStatus::Banned) ? "Banned" : "Active";
      // 「ユーザー名:ハッシュ:ステータス」の形式で書き出す
      std::println(file, "{}:{}:{}", name, data.hashed_password, status_str);
    }
  }

private:
  struct UserData {
    std::string hashed_password;
    AccountStatus status = AccountStatus::Active;
  };

  std::unordered_map<std::string, UserData> m_users;
  RegisterPolicy m_policy;

  std::string m_filePath;
  mutable std::recursive_mutex m_mutex;

  // パスワードハッシュ化
  std::string hashPassword(std::string_view password) const {
    // 1. OSの乱数生成器でセキュアなソルト（16バイト）を生成
    uint8_t salt[16];
    std::random_device rd;
    auto *salt_u32 = reinterpret_cast<uint32_t *>(salt);
    for (size_t i = 0; i < 4; ++i) {
      salt_u32[i] = rd();
    }

    // 2. ハッシュ化（Argon2idを使用）
    char encoded[256]; // エンコード結果を入れる十分なサイズのバッファ
    int result = argon2id_hash_encoded(
        1, 16384, 1, // 反復回数, メモリ(KB), スレッド数
        password.data(), password.size(), salt, sizeof(salt),
        32, // 生成されるハッシュ長
        encoded, sizeof(encoded));

    if (result != ARGON2_OK) {
      throw std::runtime_error("Hashing failed");
    }
    return std::string(encoded);
  }

  bool verifyPassword(std::string_view password,
                      std::string_view hash) const noexcept {

    std::string hash_str(hash);

    return argon2id_verify(hash_str.c_str(), password.data(),
                           password.size()) == ARGON2_OK;
  }
};

} // namespace astream::detail::auth
