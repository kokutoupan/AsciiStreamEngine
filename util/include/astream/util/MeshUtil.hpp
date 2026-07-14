#pragma once

#include <astream/graphics/shaders/DefaultShaders.hpp>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace Shaders {
using astream::graphics::shaders::DefaultVertex;
}

namespace MeshUtil {

/**
 * @brief アセットコンパイラが書き出した独立配列から、
 * レンダラーが要求するインターリーブ構造体の vector を一撃で組み立てる関数
 */
inline std::vector<Shaders::DefaultVertex>
create_vertices(uint32_t vertex_count, const float *positions,
                const float *normals, const float *texcoords) {
  std::vector<Shaders::DefaultVertex> vertices;
  vertices.reserve(vertex_count);

  for (uint32_t i = 0; i < vertex_count; ++i) {
    vertices.push_back(Shaders::DefaultVertex{
        .position = glm::vec3(positions[i * 3 + 0], positions[i * 3 + 1],
                              positions[i * 3 + 2]),
        .normal = glm::vec3(normals[i * 3 + 0], normals[i * 3 + 1],
                            normals[i * 3 + 2]),
        .uv = glm::vec2(texcoords[i * 2 + 0], texcoords[i * 2 + 1])});
  }

  return vertices;
}

/**
 * @brief インデックス配列を std::vector<int> に変換するヘルパー
 */
inline std::vector<int> create_indices(uint32_t index_count,
                                       const int *indices) {
  return std::vector<int>(indices, indices + index_count);
}

/**
 * @brief コンパイル時にフラット配列から std::array
 * のインターリーブ構造体を組み立てる constexpr 関数
 */
template <uint32_t VertexCount>
constexpr std::array<Shaders::DefaultVertex, VertexCount>
create_static_vertices(const float *positions, const float *normals,
                       const float *texcoords) {
  std::array<Shaders::DefaultVertex, VertexCount> vertices{};

  for (uint32_t i = 0; i < VertexCount; ++i) {
    vertices[i] = Shaders::DefaultVertex{
        .position = glm::vec3(positions[i * 3 + 0], positions[i * 3 + 1],
                              positions[i * 3 + 2]),
        .normal = glm::vec3(normals[i * 3 + 0], normals[i * 3 + 1],
                            normals[i * 3 + 2]),
        .uv = glm::vec2(texcoords[i * 2 + 0], texcoords[i * 2 + 1])};
  }

  return vertices;
}

/**
 * @brief インデックス用の std::array 変換ヘルパー
 */
template <uint32_t IndexCount>
constexpr std::array<int, IndexCount>
create_static_indices(const int *indices) {
  std::array<int, IndexCount> result{};
  for (uint32_t i = 0; i < IndexCount; ++i) {
    result[i] = indices[i];
  }
  return result;
}

} // namespace MeshUtil
