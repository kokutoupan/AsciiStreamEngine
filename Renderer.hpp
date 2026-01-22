#pragma once
#include <cstring>
#include <vector>

#include "Math.hpp"

class WireframeRenderer {
private:
  // 描画用 (ロジック用)
  static const int WIDTH = 80;
  static const int HEIGHT = 24;
  char grid[HEIGHT][WIDTH];
  // zbuffer
  float zbuffer[HEIGHT][WIDTH];

  // 送信用 (ネットワークに流すバイト列そのもの)
  std::vector<char> send_buffer;

  std::vector<Vec3> vertices;
  std::vector<int> indices;

public:
  WireframeRenderer();

  // バッファのポインタとサイズをペアで返す (C++17
  // string_viewでも可だが、古いやつでまずそうなのでとりあえずポインタ)
  void render(float angleX, float angleY, const char **out_ptr,
              size_t *out_size);

private:
  void clear();
  void flushToBuffer();

  void fillTriangle(const Vec3 &v0, const Vec3 &v1, const Vec3 &v2, char shade);

  // 重心計算
  float edgeFunction(const Vec3 &a, const Vec3 &b, const Vec3 &c);
};
