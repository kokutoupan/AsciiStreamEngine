#include "AsciiRasterizer.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

AsciiRasterizer::AsciiRasterizer() {
  size_t header_len = 3;
  size_t total_size = header_len + (WIDTH + 1) * HEIGHT;
  send_buffer.resize(total_size);
  // ヘッダーと改行コードの初期化
  std::memcpy(send_buffer.data(), "\x1b[H", 3);
  for (int y = 0; y < HEIGHT; ++y) {
    send_buffer[header_len + y * (WIDTH + 1) + WIDTH] = '\n';
  }
}

void AsciiRasterizer::clear() {
  std::memset(grid, ' ', sizeof(grid));
  for (int y = 0; y < HEIGHT; y++)
    for (int x = 0; x < WIDTH; x++)
      zbuffer[y][x] = std::numeric_limits<float>::max();
}

void AsciiRasterizer::flushToBuffer() {
  size_t header_len = 3;
  char *dst = send_buffer.data() + header_len;
  for (int y = 0; y < HEIGHT; ++y) {
    std::memcpy(dst + y * (WIDTH + 1), grid[y], WIDTH);
  }
}

float AsciiRasterizer::edgeFunction(const Vec3 &a, const Vec3 &b,
                                    const Vec3 &c) {
  return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

void AsciiRasterizer::rasterizeTriangle(const Vertex &v0, const Vertex &v1,
                                        const Vertex &v2, FragmentShader fs) {
  int minX =
      std::max(0, (int)std::min({v0.position.x, v1.position.x, v2.position.x}));
  int minY =
      std::max(0, (int)std::min({v0.position.y, v1.position.y, v2.position.y}));
  int maxX = std::min(
      WIDTH - 1,
      (int)std::max({v0.position.x, v1.position.x, v2.position.x}) + 1);
  int maxY = std::min(
      HEIGHT - 1,
      (int)std::max({v0.position.y, v1.position.y, v2.position.y}) + 1);

  float area = edgeFunction(v0.position, v1.position, v2.position);
  // if (area <= 0)
  //   return; // Back-face culling

  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      Vec3 p((float)x, (float)y, 0);

      float w0 = edgeFunction(v1.position, v2.position, p);
      float w1 = edgeFunction(v2.position, v0.position, p);
      float w2 = edgeFunction(v0.position, v1.position, p);

      if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
        w0 /= area;
        w1 /= area;
        w2 /= area;

        // 重心座標補間: 頂点属性(法線など)をブレンド
        Vertex interpolated = v0 * w0 + v1 * w1 + v2 * w2;

        // Z-Test
        if (interpolated.position.z < zbuffer[y][x]) {
          zbuffer[y][x] = interpolated.position.z;
          // Fragment Shader 呼び出し
          grid[y][x] = fs(interpolated);
        }
      }
    }
  }
}

void AsciiRasterizer::draw(const std::vector<Vertex> &vertices,
                           const std::vector<int> &indices, VertexShader vs,
                           FragmentShader fs) {
  // 1. Vertex Shader Stage
  std::vector<Vertex> processed_verts;
  processed_verts.reserve(vertices.size());
  for (const auto &v : vertices) {
    processed_verts.push_back(vs(v));
  }

  // 2. Rasterization Stage
  for (size_t i = 0; i < indices.size(); i += 3) {
    if (i + 2 >= indices.size())
      break;
    int idx0 = indices[i];
    int idx1 = indices[i + 1];
    int idx2 = indices[i + 2];

    rasterizeTriangle(processed_verts[idx0], processed_verts[idx1],
                      processed_verts[idx2], fs);
  }

  flushToBuffer();
}
