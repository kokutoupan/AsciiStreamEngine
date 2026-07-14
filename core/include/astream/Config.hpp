#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <astream/auth/RegisterPolicy.hpp>

namespace astream {

/// @brief エンジン全体のコンパイル時設定を一元管理する構造体
struct EngineConfig {
  // -------------------------------------------------------------------------
  // 1. システム・リソース制限
  // -------------------------------------------------------------------------
  size_t max_players = 4; // 最大同時接続プレイヤー数
  size_t max_receive_frame_size =
      128;                          // 1パケットの最大受信バッファサイズ(bytes)
  float default_target_fps = 30.0f; // デフォルトのターゲットFPS

  // -------------------------------------------------------------------------
  // 2. 暗号化・セキュリティ設定
  // -------------------------------------------------------------------------
  bool enable_encryption =
      false; // トランスポート層の暗号化(X25519/Blake2b等)を有効にするか

  // -------------------------------------------------------------------------
  // 3. 認証・ユーザー管理設定
  // -------------------------------------------------------------------------
  bool enable_auth = false; // ユーザー認証(Argon2id)を有効にするか
  auth::RegisterPolicy register_policy =
      auth::RegisterPolicy::AdminOnly; // アカウント登録のポリシー

  // -------------------------------------------------------------------------
  // 4. DoS / 認証重撃対策（防壁設定）
  // -------------------------------------------------------------------------
  bool enable_rate_limit = true; // 同一IPからの短時間大量接続・認証試行を弾くか
  size_t max_auth_attempts = 3;  // 指定時間内に許容する最大認証試行回数
  int rate_limit_window_sec = 10; // 制限を判定する時間枠（秒）
};

/// @brief エンジンがデフォルトで使用する構成定数
constexpr EngineConfig DefaultConfig{};

} // namespace astream
