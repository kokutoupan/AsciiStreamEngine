#pragma once
#include <astream/InputDevice.hpp>

class GameWorld {
public:
  virtual ~GameWorld() = default;

  // 各クライアントの入力をワールド側で一括して処理するアクセスポイント
  virtual void processPlayerInput(int clientId, const InputDevice &input) = 0;

  // 全プレイヤーの入力処理が完了した後に実行される世界共通の更新ロジック
  virtual void globalUpdate() = 0;
};
