#pragma once

#include <cmath>
#include <vector>

template <typename T> class Texture2D {
private:
  int width, height;
  std::vector<T> data;

public:
  Texture2D(int w, int h, T initialValue)
      : width(w), height(h), data(w * h, initialValue) {}

  void resize(int w, int h, T initialValue) {
    width = w;
    height = h;
    data.assign(w * h, initialValue);
  }

  void clear(T value) { std::fill(data.begin(), data.end(), value); }

  T &at(int x, int y) { return data[y * width + x]; }
  const T &at(int x, int y) const { return data[y * width + x]; }

  int getWidth() const { return width; }
  int getHeight() const { return height; }
  T *getData() { return data.data(); }
  const T *getData() const { return data.data(); }

  T sampleBilinear(float u, float v) const {
    // 1. 座標を 0.0 ~ 1.0 にクランプ
    u = std::max(0.0f, std::min(1.0f, u));
    v = std::max(0.0f, std::min(1.0f, v));

    // 2. テクスチャ空間のピクセル座標（連続値）に変換
    // ピクセルの中心を考慮するため、サイズを掛けて 0.5 引く設計が一般的です
    float texX = u * (width - 1);
    float texY = v * (height - 1);

    // 3. 周辺4ピクセルのインデックスと、その間の重み（フランク部分）を計算
    int x0 = static_cast<int>(std::floor(texX));
    int y0 = static_cast<int>(std::floor(texY));
    int x1 = std::min(width - 1, x0 + 1);
    int y1 = std::min(height - 1, y0 + 1);

    float tx = texX - x0;
    float ty = texY - y0;

    // 4. 周辺4ピクセルの値をサンプリング
    const T &s00 = at(x0, y0);
    const T &s10 = at(x1, y0);
    const T &s01 = at(x0, y1);
    const T &s11 = at(x1, y1);

    // 5. バイリニア補間
    T s0 = s00 * (1.0f - tx) + s10 * tx;
    T s1 = s01 * (1.0f - tx) + s11 * tx;

    return s0 * (1.0f - ty) + s1 * ty;
  }
};
