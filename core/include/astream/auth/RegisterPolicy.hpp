#pragma once

namespace astream::auth {

// ユーザー登録のポリシー
enum class RegisterPolicy {
  AllowAll,  // 誰でも自由に登録可能
  AdminOnly, // 一般クライアントからの登録は拒否（サーバー内部でのみ事前登録）
  Disabled   // 登録機能を完全に無効化（読み込み専用）
};

} // namespace astream::auth
