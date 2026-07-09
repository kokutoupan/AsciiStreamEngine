#pragma once

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>

#include <astream/util/StringUtil.hpp>

// ユーザー登録のポリシー
enum class RegisterPolicy {
  AllowAll,  // 誰でも自由に登録可能
  AdminOnly, // 一般クライアントからの登録は拒否（サーバー内部でのみ事前登録）
  Disabled   // 登録機能を完全に無効化（読み込み専用）
};

class UserStore {
public:
  // ポリシーはコンストラクタやセッターで外部から指定可能に
  explicit UserStore(RegisterPolicy policy = RegisterPolicy::AdminOnly)
      : m_policy(policy) {}

  // 認証
  [[nodiscard]] bool authenticate(std::string_view username,
                                  std::string_view password) const noexcept {
    auto clean_name = astream::util::trim(username);
    if (clean_name.empty() || password.empty()) {
      return false;
    }

    auto it = m_users.find(std::string(clean_name));
    if (it == m_users.end()) {
      // タイミング攻撃対策：存在しない場合もダミーのハッシュ/比較処理を走らせて時間を一定に
      verifyPassword(password, "dummy_hash_for_timing_attack_protection");
      return false;
    }

    return verifyPassword(password, it->second);
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
    if (clean_name.empty() || password.empty()) {
      return false;
    }

    std::string name_str(clean_name);
    if (m_users.contains(name_str)) {
      return false; // 既に存在する
    }

    // パスワードは内部でハッシュ化して保持（ファイル漏洩対策）
    m_users[name_str] = hashPassword(password);
    saveToFile();
    return true;
  }

  // ポリシーの動的変更
  void setRegisterPolicy(RegisterPolicy policy) noexcept { m_policy = policy; }

  constexpr RegisterPolicy getRegisterPolicy() const noexcept {
    return m_policy;
  };

private:
  std::unordered_map<std::string, std::string>
      m_users; // username -> hashedPassword
  RegisterPolicy m_policy;

  // パスワードハッシュ化ラッパー (bcryptやargon2などのライブラリをそのうち使う)
  std::string hashPassword(std::string_view password) const {
    return "hashed_" + std::string(password); // 概念用のダミー
  }

  bool verifyPassword(std::string_view password,
                      std::string_view hash) const noexcept {
    return hash == ("hashed_" + std::string(password)); // 概念用のダミー
  }

  void saveToFile() {}
  void loadFromFile() {}
};
