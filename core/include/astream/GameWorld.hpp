#pragma once
#include <astream/InputDevice.hpp>

// これを継承してもいいし，ちゃんとメゾットを満たしてさえいれば継承する必要はない
class GameWorld {
public:
  virtual ~GameWorld() = default;

  // 【Optional】毎フレーム、全セッションの入力処理（update）が始まる前に呼ばれる前処理
  // virtual void preUpdate() = 0;

  // 各クライアントの入力をワールド側で一括して処理するアクセスポイント
  virtual void processPlayerInput(int clientId, const InputDevice &input) = 0;

  // 全プレイヤーの入力処理が完了した後に実行される世界共通の更新ロジック
  virtual void globalUpdate(float delta_time) = 0;

  // 【Optional】globalUpdateや全セッションの描画が終わった後に呼ばれる後処理
  // virtual void postUpdate() = 0;
};
