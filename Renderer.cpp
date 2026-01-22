
#include "Renderer.hpp"

#include <cmath>

// --- 3D Engine Part (C++) ---

WireframeRenderer::WireframeRenderer() {
  // キューブの頂点定義
  vertices = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
              {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
  // エッジ定義
  edges = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0}, // Back
      {4, 5}, {5, 6}, {6, 7}, {7, 4}, // Front
      {0, 4}, {1, 5}, {2, 6}, {3, 7}  // Connections
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

void WireframeRenderer::clear() {
  // gridをスペースで埋める
  std::memset(grid, ' ', sizeof(grid));
}

void WireframeRenderer::flushToBuffer() {
  // gridの内容をsend_bufferにコピー
  // send_bufferはヘッダーと改行を含んでいるので、行ごとにコピーする
  size_t header_len = 3;
  char *dst_base = send_buffer.data() + header_len;

  for (int y = 0; y < HEIGHT; ++y) {
    // dst_base + (1行の長さ * y) の位置に、WIDTH分だけコピー
    // 改行文字は上書きしないように注意
    std::memcpy(dst_base + (y * (WIDTH + 1)), grid[y], WIDTH);
  }
}

// ブレゼンハムの直線アルゴリズム
void WireframeRenderer::drawLine(int x0, int y0, int x1, int y1) {
  int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy, e2;

  while (true) {
    if (x0 >= 0 && x0 < WIDTH && y0 >= 0 && y0 < HEIGHT) {
      grid[y0][x0] = '#';
    }
    if (x0 == x1 && y0 == y1)
      break;
    e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// レンダリングしてバッファを返す
void WireframeRenderer::render(float angleX, float angleY, const char **out_ptr,
                               size_t *out_size) {
  clear();
  float cx = cos(angleX), sx = sin(angleX);
  float cy = cos(angleY), sy = sin(angleY);

  std::vector<Vec3> projected(vertices.size());

  // 回転と投影
  for (size_t i = 0; i < vertices.size(); ++i) {
    float x = vertices[i].x;
    float y = vertices[i].y;
    float z = vertices[i].z;

    // X Rotation
    float ty = y * cx - z * sx;
    float tz = y * sx + z * cx;
    y = ty;
    z = tz;

    // Y Rotation
    float tx = x * cy - z * sy;
    tz = x * sy + z * cy;
    x = tx;
    z = tz;

    // Projection (Camera dist = 4.0)
    float dist = 4.0f;
    float z_inv = 1.0f / (z + dist);

    projected[i].x = (x * z_inv * 30.0f + WIDTH / 2.0f);
    projected[i].y = (y * z_inv * 15.0f + HEIGHT / 2.0f);
  }

  // 描画
  for (const auto &edge : edges) {
    drawLine((int)projected[edge.first].x, (int)projected[edge.first].y,
             (int)projected[edge.second].x, (int)projected[edge.second].y);
  }

  // 計算し終わった grid を送信用バッファに転送
  flushToBuffer();

  // 呼び出し元にはバッファの実体を渡す
  *out_ptr = send_buffer.data();
  *out_size = send_buffer.size();
}
