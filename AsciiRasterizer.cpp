#include "AsciiRasterizer.hpp"

#include <algorithm>
#include <cmath>

AsciiRasterizer::AsciiRasterizer() {
  // キューブの頂点定義
  vertices = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
              {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};

  // 1つの面につき2つの三角形 × 6面 = 12三角形 (36インデックス)
  indices = {
      0, 1, 2, 0, 2, 3, // Back
      4, 5, 6, 4, 6, 7, // Front
      0, 4, 7, 0, 7, 3, // Left
      1, 5, 6, 1, 6, 2, // Right
      3, 2, 6, 3, 6, 7, // Top
      0, 1, 5, 0, 5, 4  // Bottom
  };

  // 送信データの総サイズを計算
  // ヘッダー("\x1b[H") + (幅 + 改行) * 高さ
  size_t header_len = 3;
  size_t frame_len = (WIDTH + 1) * HEIGHT;
  size_t total_size = header_len + frame_len;

  // メモリを一度だけ確保
  send_buffer.resize(total_size);

  // ヘッダー部分は固定なので最初に書き込んでおく
  std::memcpy(send_buffer.data(), "\x1b[H", 3);

  // 改行コードの位置も固定なので、最初に書き込んでおく
  // 構造: [H][W][W]...[\n][W][W]...[\n]
  for (int y = 0; y < HEIGHT; ++y) {
    size_t newline_pos = header_len + (y * (WIDTH + 1)) + WIDTH;
    send_buffer[newline_pos] = '\n';
  }
}

void AsciiRasterizer::clear() {
  // gridをスペースで埋める
  std::memset(grid, ' ', sizeof(grid));
  // Zバッファを無限遠で初期化
  for (int y = 0; y < HEIGHT; y++)
    for (int x = 0; x < WIDTH; x++)
      zbuffer[y][x] = std::numeric_limits<float>::max();
}

void AsciiRasterizer::flushToBuffer() {
  // gridの内容をsend_bufferにコピー
  size_t header_len = 3;
  char *dst_base = send_buffer.data() + header_len;

  for (int y = 0; y < HEIGHT; ++y) {
    // dst_base + (1行の長さ * y) の位置に、WIDTH分だけコピー
    std::memcpy(dst_base + (y * (WIDTH + 1)), grid[y], WIDTH);
  }
}

// 重心座標計算用のヘルパー
float AsciiRasterizer::edgeFunction(const Vec3 &a, const Vec3 &b,
                                      const Vec3 &c) {
  return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

// 三角形のラスタライズ (塗りつぶし + Zバッファ)
void AsciiRasterizer::fillTriangle(const Vec3 &v0, const Vec3 &v1,
                                     const Vec3 &v2, char shade) {
  // 1. バウンディングボックスを求める (描画範囲を絞るため)
  int minX = std::max(0, (int)std::min({v0.x, v1.x, v2.x}));
  int minY = std::max(0, (int)std::min({v0.y, v1.y, v2.y}));
  int maxX = std::min(WIDTH - 1, (int)std::max({v0.x, v1.x, v2.x}) + 1);
  int maxY = std::min(HEIGHT - 1, (int)std::max({v0.y, v1.y, v2.y}) + 1);

  // 2. ピクセルごとに三角形の中か判定
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      Vec3 p = {(float)x, (float)y, 0};

      // 重心座標 (Barycentric Coordinates)
      // 三角形の面積の比率を使って、点が内側にあるか判定する
      float area = edgeFunction(v0, v1, v2);
      if (area == 0)
        continue; // 縮退した三角形

      float w0 = edgeFunction(v1, v2, p);
      float w1 = edgeFunction(v2, v0, p);
      float w2 = edgeFunction(v0, v1, p);

      // 3. もし三角形の内側なら
      // (時計回りの場合、全部負になることもあるので符号一致で判定)
      if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0)) {
        // 正規化
        w0 /= area;
        w1 /= area;
        w2 /= area;

        // 4. Z深度の補間
        float z = w0 * v0.z + w1 * v1.z + w2 * v2.z;

        // 5. Zテスト (手前にある場合のみ描画)
        if (z < zbuffer[y][x]) {
          zbuffer[y][x] = z;
          grid[y][x] = shade; // 文字で塗る
        }
      }
    }
  }
}

void AsciiRasterizer::render(float angleX, float angleY, const char **out_ptr,
                               size_t *out_size) {
  clear();
  float cx = cos(angleX), sx = sin(angleX);
  float cy = cos(angleY), sy = sin(angleY);

  // 1. 行列の構築 (Model -> View -> Projection)
  Mat4 model = Mat4::rotateY(angleY) * Mat4::rotateX(angleX);
  Mat4 view = Mat4::translate(0, 0, -4.0f); // カメラを少し引く
  Mat4 proj =
      Mat4::perspective(1.0f, (float)WIDTH / HEIGHT * 0.5f, 0.1f, 100.0f);

  // MVP行列作成
  Mat4 mvp = proj * view * model;

  std::vector<Vec3> projected(vertices.size());
  // 1. 頂点シェーダー相当 (回転 + 投影)
  for (size_t i = 0; i < vertices.size(); ++i) {
    projected[i] = mvp.transform(vertices[i]);

    // NDC [-1, 1] を Screen [0, WIDTH], [0, HEIGHT] に変換
    projected[i].x = (projected[i].x + 1.0f) * 0.5f * WIDTH;

    // Y軸は3Dとスクリーン(文字)で上下逆なので反転させる (1.0 - y)
    projected[i].y = (1.0f - projected[i].y) * 0.5f * HEIGHT;
  }

  // 2. ラスタライザ相当 (三角形ループ)
  // indicesを3つずつ進める (TRIANGLES)
  const char *shades = ".:-=+*#%@"; // 明るさテーブル

  for (size_t i = 0; i < indices.size(); i += 3) {
    int idx0 = indices[i];
    int idx1 = indices[i + 1];
    int idx2 = indices[i + 2];

    Vec3 v0 = projected[idx0];
    Vec3 v1 = projected[idx1];
    Vec3 v2 = projected[idx2];

    // 簡易ライティング (法線ベクトルのZ成分を見る)
    // カリングも兼ねるなら (area > 0) のチェックを入れる
    float area = edgeFunction(v0, v1, v2);
    if (area > 0) { // 背面カリング (時計回りが表の場合)
      // 簡易的に法線を計算せず、インデックス順序で明るさを決めてみる
      // 本来は法線とライトの内積を取る
      int color_idx = (i / 3) % 9;
      fillTriangle(v0, v1, v2, shades[color_idx]);
    }
  }

  flushToBuffer();
  *out_ptr = send_buffer.data();
  *out_size = send_buffer.size();
}
