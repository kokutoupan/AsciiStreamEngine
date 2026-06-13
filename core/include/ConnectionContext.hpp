#pragma once

#include "InputDevice.hpp"
#include "Texture2D.hpp"

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

  // 自分のカメラバッファに対するレンダリング
  virtual void render(Texture2D<char> &colorBuffer, const WorldType &world) = 0;
};
