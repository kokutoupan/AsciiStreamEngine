#pragma once
#include "Math.hpp"
#include <functional>
#include <vector>

// 頂点データ
struct Vertex {
  Vec3 position; // 座標
  Vec3 normal;   // 法線

  // 属性の線形補間 (ラスタライザーで使う)
  Vertex operator+(const Vertex &r) const {
    return {position + r.position, normal + r.normal};
  }
  Vertex operator*(float s) const { return {position * s, normal * s}; }
};

// シェーダー定義
// VS: 入力頂点を受け取り、変換後の頂点を返す
using VertexShader = std::function<Vertex(const Vertex &)>;
// FS: 補間された頂点を受け取り、出力する文字を返す
using FragmentShader = std::function<char(const Vertex &)>;

class AsciiRasterizer {
private:
  static const int WIDTH = 80;
  static const int HEIGHT = 24;

  char grid[HEIGHT][WIDTH];
  float zbuffer[HEIGHT][WIDTH];
  std::vector<char> send_buffer;

public:
  AsciiRasterizer();

  // 描画クリア
  void clear();

  // ドローコール: 頂点、インデックス、シェーダーを受け取って描画
  void draw(const std::vector<Vertex> &vertices,
            const std::vector<int> &indices, VertexShader vs,
            FragmentShader fs);

  // バッファ取得
  const char *getBuffer() const { return send_buffer.data(); }
  size_t getBufferSize() const { return send_buffer.size(); }

private:
  void flushToBuffer();
  float edgeFunction(const Vec3 &a, const Vec3 &b, const Vec3 &c);
  void rasterizeTriangle(const Vertex &v0, const Vertex &v1, const Vertex &v2,
                         FragmentShader fs);
};
