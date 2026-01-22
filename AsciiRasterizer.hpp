#pragma once
#include "Math.hpp"
#include <algorithm>
#include <functional>
#include <vector>

// テンプレート引数 T (=Varying) は、operator+ と operator*
// を持っている必要がある
template <typename T> class AsciiRasterizer {
private:
  static const int WIDTH = 80;
  static const int HEIGHT = 24;

  char grid[HEIGHT][WIDTH];
  float zbuffer[HEIGHT][WIDTH];
  std::vector<char> send_buffer;

public:
  // シェーダー定義
  // VS: 入力頂点を受け取り、変換後の頂点を返す
  using VertexShaderOutput = std::pair<Vec4, T>;
  // FS: 補間された頂点を受け取り、出力する文字を返す
  using FragmentShader = std::function<char(const T &)>;

  AsciiRasterizer() {
    size_t header_len = 3;
    size_t total_size = header_len + (WIDTH + 1) * HEIGHT;
    send_buffer.resize(total_size);
    // ヘッダーと改行コードの初期化
    std::memcpy(send_buffer.data(), "\x1b[H", 3);
    for (int y = 0; y < HEIGHT; ++y) {
      send_buffer[header_len + y * (WIDTH + 1) + WIDTH] = '\n';
    }
  }

  // 描画クリア
  void clear() {
    std::memset(grid, ' ', sizeof(grid));
    for (int y = 0; y < HEIGHT; y++)
      for (int x = 0; x < WIDTH; x++)
        zbuffer[y][x] = std::numeric_limits<float>::max();
  }

  // ドローコール: 頂点、インデックス、シェーダーを受け取って描画
  template <typename InputVertexT>
  void draw(const std::vector<InputVertexT> &vertices,
            const std::vector<int> &indices,
            std::function<std::pair<Vec4, T>(const InputVertexT &)> vs, // VS
            FragmentShader fs)                                          // FS
  {
    // 1. Vertex Shader Stage
    // システム座標とユーザー属性を分けて保持する
    struct ShadedVertex {
      Vec4 pos; // システム用 (Zテストに使う)
      T var;    // ユーザー用 (補間される)
    };
    std::vector<ShadedVertex> shadedVertices;
    shadedVertices.reserve(vertices.size());

    for (const auto &v : vertices) {
      auto result = vs(v);
      Vec4 &pos = result.first;

      // A. 透視除算 (Clip Space -> NDC)
      if (pos.w != 0.0f) {
        float invW = 1.0f / pos.w;
        pos.x *= invW;
        pos.y *= invW;
        pos.z *= invW;
      }

      // B. ビューポート変換 (NDC -> Screen Space)
      // -1.0 ~ +1.0 を 0 ~ WIDTH/HEIGHT に変換
      pos.x = (pos.x + 1.0f) * 0.5f * (float)WIDTH;
      pos.y = (1.0f - pos.y) * 0.5f * (float)HEIGHT; // Y反転も含める

      shadedVertices.push_back({result.first, result.second});
    }

    // 2. Rasterizer Stage
    for (size_t i = 0; i < indices.size(); i += 3) {
      if (i + 2 >= indices.size())
        break;
      // 3頂点を取り出す
      const auto &v0 = shadedVertices[indices[i]];
      const auto &v1 = shadedVertices[indices[i + 1]];
      const auto &v2 = shadedVertices[indices[i + 2]];

      // 三角形ループへ
      // ここで渡すのは「システム座標(Vec4)」と「ユーザー属性(T)」
      rasterizeTriangle(v0.pos, v1.pos, v2.pos, v0.var, v1.var, v2.var, fs);
    }
    flushToBuffer();
  }

  // バッファ
  const char *getBuffer() const { return send_buffer.data(); }
  size_t getBufferSize() const { return send_buffer.size(); }

private:
  void flushToBuffer() {
    size_t header_len = 3;
    char *dst = send_buffer.data() + header_len;
    for (int y = 0; y < HEIGHT; ++y) {
      std::memcpy(dst + y * (WIDTH + 1), grid[y], WIDTH);
    }
  }

  float edgeFunction(const Vec2 &a, const Vec2 &b, const Vec2 &c) {
    return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
  }

  void rasterizeTriangle(const Vec4 &p0, const Vec4 &p1, const Vec4 &p2, // 座標
                         const T &v0, const T &v1, const T &v2,          // 属性
                         FragmentShader fs) {
    // スクリーン座標(X,Y)だけでバウンディングボックス計算
    int minX = std::max(0, (int)std::min({p0.x, p1.x, p2.x}));
    int minY = std::max(0, (int)std::min({p0.y, p1.y, p2.y}));
    int maxX = std::min(WIDTH - 1, (int)std::max({p0.x, p1.x, p2.x}) + 1);
    int maxY = std::min(HEIGHT - 1, (int)std::max({p0.y, p1.y, p2.y}) + 1);

    Vec2 sp0 = {p0.x, p0.y}, sp1 = {p1.x, p1.y}, sp2 = {p2.x, p2.y};

    float area = edgeFunction(sp0, sp1, sp2);
    if (area <= 0)
      return; // Cull backface

    for (int y = minY; y <= maxY; ++y) {
      for (int x = minX; x <= maxX; ++x) {
        Vec2 p = {(float)x, (float)y};

        float w0 = edgeFunction(sp1, sp2, p);
        float w1 = edgeFunction(sp2, sp0, p);
        float w2 = edgeFunction(sp0, sp1, p);

        if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
          w0 /= area;
          w1 /= area;
          w2 /= area;

          // 【Zテスト】
          // システム座標のZを補間してチェックする
          // (パースペクティブ補正なしの簡易線形補間)
          float z = p0.z * w0 + p1.z * w1 + p2.z * w2;

          if (z < zbuffer[y][x]) {
            zbuffer[y][x] = z;

            // 【属性補間】
            // ユーザー型 T が何であろうと、ここで補間される
            // T は operator+, operator* を持っている必要がある
            T interpolated = v0 * w0 + v1 * w1 + v2 * w2;

            grid[y][x] = fs(interpolated);
          }
        }
      }
    }
  }
};
