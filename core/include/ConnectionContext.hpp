#pragma once

#include "InputDevice.hpp"
#include "Texture2D.hpp"

// 利用者が実装するアプリケーションのインターフェース
class ConnectionContext {
public:
  virtual ~ConnectionContext() = default;
  virtual void init(int w, int h) = 0;
  virtual void update(const InputDevice &input) = 0;
  virtual void render(Texture2D<char> &output) = 0;
};
