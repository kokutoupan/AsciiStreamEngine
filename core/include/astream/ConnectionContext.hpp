#pragma once

#include <astream/InputDevice.hpp>
#include <astream/Texture2D.hpp>

// これを継承してもいいし，ちゃんとメゾットを満たしてさえいれば継承する必要はない
template <typename WorldType> class ConnectionContext {
public:
  virtual ~ConnectionContext() = default;

  // 初期化フック
  virtual void init(int clientId, int w, int h, WorldType &world) = 0;

  // 切断時のクリーンアップフック
  virtual void onDisconnect(WorldType &world) = 0;

  // 毎フレームの入力更新イベント（通常は world の入力メソッドへ委託）
  virtual void update(int clientId, const InputDevice &input,
                      WorldType &world) = 0;

  // 【Optional】毎フレーム、ワールドのglobal updateの直後に呼び出される
  // virtual void postUpdate(int clientID, WorldType& world);

  // 自分のカメラバッファに対するレンダリング
  virtual bool render(TextureView<char> colorBuffer,
                      const WorldType &world) = 0;

  // 【Optional】毎フレーム、すべての処理（render/送信）が完了した直後に呼び出される（フラグリセット等）
  // virtual void endFrame(int clientId, WorldType& world);
};
