#pragma once

#include "Texture2D.hpp"
#include <algorithm>
#include <concepts>
#include <glm/glm.hpp>
#include <limits>
#include <utility>
#include <vector>

template <typename T>
concept IsVarying = requires(T a, T b, float s) {
  { a + b } -> std::same_as<T>;
  { a * s } -> std::same_as<T>;
};
// VertexShaderの制約 (InputVertex -> std::pair<glm::vec4, Varying>)
template <typename F, typename InputVertex, typename Varying>
concept IsVertexShader = requires(F &&shader, const InputVertex &vertex) {
  {
    std::forward<F>(shader)(vertex)
  } -> std::convertible_to<std::pair<glm::vec4, Varying>>;
};

// FragmentShaderの制約 (int, int, const Varying& -> void)
template <typename F, typename Varying>
concept IsFragmentShader =
    requires(F &&shader, int x, int y, const Varying &var) {
      { std::forward<F>(shader)(x, y, var) } -> std::same_as<void>;
    };

// ComputeShaderの制約 (int, int -> void)
template <typename F>
concept IsComputeShader = requires(F &&shader, int x, int y) {
  { std::forward<F>(shader)(x, y) } -> std::same_as<void>;
};

class GraphicsDevice {
public:
  GraphicsDevice() = default;

  // --- RasterizePass の定義 ---
  // Varyingは operator + と operator * が必要 (補間計算のため)
  template <typename InputVertex, IsVarying Varying, typename DepthT = float>
  class RasterizePass {
  private:
    Texture2D<DepthT> &m_depthBuffer;

    // エッジ関数の内部計算用
    inline float edgeFunction(const glm::vec2 &a, const glm::vec2 &b,
                              const glm::vec2 &c) {
      return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
    }

  public:
    RasterizePass(Texture2D<DepthT> &db) : m_depthBuffer(db) {}

    template <typename VertexShader, typename FragmentShader>
      requires IsVertexShader<VertexShader, InputVertex, Varying> &&
               IsFragmentShader<FragmentShader, Varying>
    void draw(const std::vector<InputVertex> &vertices,
              const std::vector<int> &indices, VertexShader &&vertexShader,
              FragmentShader &&fragmentShader) {

      int targetWidth = m_depthBuffer.getWidth();
      int targetHeight = m_depthBuffer.getHeight();

      // 補間計算用に、1/W でスケールされた状態の Varying
      // 属性を保持する内部構造体
      struct ShadedVertex {
        glm::vec4 screenPos; // X, Y はスクリーン空間座標、Z は深度テスト用、W
                             // は 1/W 保持用
        float invW;          // 1 / W
        Varying varInvW;     // Varying * (1 / W)
      };

      std::vector<ShadedVertex> shadedVertices;
      shadedVertices.reserve(vertices.size());

      for (const auto &v : vertices) {
        auto result = vertexShader(v);
        glm::vec4 clipPos = result.first;
        Varying origVar = result.second;

        // 簡易ニアプレーン・クリッピングガード
        if (clipPos.w <= 0.001f) {
          clipPos.w = 0.001f;
        }

        float invW = 1.0f / clipPos.w;

        // NDC -> スクリーン空間座標への変換
        glm::vec4 sPos;
        sPos.x = (clipPos.x * invW + 1.0f) * 0.5f * (float)targetWidth;
        sPos.y = (1.0f - clipPos.y * invW) * 0.5f * (float)targetHeight;
        sPos.z = clipPos.z * invW; // 深度テスト用のZバッファ値
        sPos.w = invW;             // 補間用に格納

        // 属性に 1/W を乗算しておく（パースペクティブ・コレクトの準備）
        Varying varInvW = origVar * invW;

        shadedVertices.push_back({sPos, invW, varInvW});
      }

      // 2. ラスタライズステージ
      for (size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size())
          break;

        const auto &v0 = shadedVertices[indices[i]];
        const auto &v1 = shadedVertices[indices[i + 1]];
        const auto &v2 = shadedVertices[indices[i + 2]];

        // バウンディングボックスの計算
        int minX = std::max(
            0, (int)std::min({v0.screenPos.x, v1.screenPos.x, v2.screenPos.x}));
        int minY = std::max(
            0, (int)std::min({v0.screenPos.y, v1.screenPos.y, v2.screenPos.y}));
        int maxX = std::min(
            targetWidth - 1,
            (int)std::max({v0.screenPos.x, v1.screenPos.x, v2.screenPos.x}) +
                1);
        int maxY = std::min(
            targetHeight - 1,
            (int)std::max({v0.screenPos.y, v1.screenPos.y, v2.screenPos.y}) +
                1);

        glm::vec2 sp0 = {v0.screenPos.x, v0.screenPos.y};
        glm::vec2 sp1 = {v1.screenPos.x, v1.screenPos.y};
        glm::vec2 sp2 = {v2.screenPos.x, v2.screenPos.y};

        float area = edgeFunction(sp0, sp1, sp2);
        if (area <= 0)
          continue; // バックフェースカリング

        for (int y = minY; y <= maxY; ++y) {
          for (int x = minX; x <= maxX; ++x) {
            glm::vec2 p = {(float)x, (float)y};

            float w0 = edgeFunction(sp1, sp2, p);
            float w1 = edgeFunction(sp2, sp0, p);
            float w2 = edgeFunction(sp0, sp1, p);

            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
              w0 /= area;
              w1 /= area;
              w2 /= area;

              // 1. スクリーン空間上で線形補間しても問題ない擬似深度Zの計算
              DepthT z = v0.screenPos.z * w0 + v1.screenPos.z * w1 +
                         v2.screenPos.z * w2;

              // 固定機能：Zテスト
              if (z < m_depthBuffer.at(x, y)) {
                m_depthBuffer.at(x, y) = z;

                // 2.パースペクティブ・コレクト・インタポレーションによる属性復元
                float interpolatedInvW =
                    v0.invW * w0 + v1.invW * w1 + v2.invW * w2;

                float pixelW =
                    1.0f / (interpolatedInvW != 0.0f ? interpolatedInvW : 1.0f);

                // 次に、スクリーン上で線形変化する (Varying * 1/W) を補間
                Varying interpolatedVarInvW =
                    v0.varInvW * w0 + v1.varInvW * w1 + v2.varInvW * w2;

                Varying correctVarying = interpolatedVarInvW * pixelW;

                // ユーザーアクションの実行
                fragmentShader(x, y, correctVarying);
              }
            }
          }
        }
      }
    }

    template <typename VertexShader>
      requires IsVertexShader<VertexShader, InputVertex, Varying>
    void draw(const std::vector<InputVertex> &vertices,
              const std::vector<int> &indices, VertexShader &&vertexShader) {

      int targetWidth = m_depthBuffer.getWidth();
      int targetHeight = m_depthBuffer.getHeight();

      // 深度描画用に座標と1/Wのみを保持する内部構造体
      struct ShadedVertex {
        glm::vec4 screenPos; // X, Y はスクリーン空間座標、Z は深度テスト用、W
                             // は 1/W 保持用
        float invW;          // 1 / W
      };

      std::vector<ShadedVertex> shadedVertices;
      shadedVertices.reserve(vertices.size());

      for (const auto &v : vertices) {
        auto result = vertexShader(v);
        glm::vec4 clipPos = result.first;

        // 簡易ニアプレーン・クリッピングガード
        if (clipPos.w <= 0.001f) {
          clipPos.w = 0.001f;
        }

        float invW = 1.0f / clipPos.w;

        // NDC -> スクリーン空間座標への変換
        glm::vec4 sPos;
        sPos.x = (clipPos.x * invW + 1.0f) * 0.5f * (float)targetWidth;
        sPos.y = (1.0f - clipPos.y * invW) * 0.5f * (float)targetHeight;
        sPos.z = clipPos.z * invW; // 深度テスト用のZバッファ値
        sPos.w = invW;             // 補間用に格納

        shadedVertices.push_back({sPos, invW});
      }

      // 2. ラスタライズステージ
      for (size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size())
          break;

        const auto &v0 = shadedVertices[indices[i]];
        const auto &v1 = shadedVertices[indices[i + 1]];
        const auto &v2 = shadedVertices[indices[i + 2]];

        // バウンディングボックスの計算
        int minX = std::max(
            0, (int)std::min({v0.screenPos.x, v1.screenPos.x, v2.screenPos.x}));
        int minY = std::max(
            0, (int)std::min({v0.screenPos.y, v1.screenPos.y, v2.screenPos.y}));
        int maxX = std::min(
            targetWidth - 1,
            (int)std::max({v0.screenPos.x, v1.screenPos.x, v2.screenPos.x}) +
                1);
        int maxY = std::min(
            targetHeight - 1,
            (int)std::max({v0.screenPos.y, v1.screenPos.y, v2.screenPos.y}) +
                1);

        glm::vec2 sp0 = {v0.screenPos.x, v0.screenPos.y};
        glm::vec2 sp1 = {v1.screenPos.x, v1.screenPos.y};
        glm::vec2 sp2 = {v2.screenPos.x, v2.screenPos.y};

        float area = edgeFunction(sp0, sp1, sp2);
        if (area <= 0)
          continue; // バックフェースカリング

        for (int y = minY; y <= maxY; ++y) {
          for (int x = minX; x <= maxX; ++x) {
            glm::vec2 p = {(float)x, (float)y};

            float w0 = edgeFunction(sp1, sp2, p);
            float w1 = edgeFunction(sp2, sp0, p);
            float w2 = edgeFunction(sp0, sp1, p);

            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
              w0 /= area;
              w1 /= area;
              w2 /= area;

              // 1. スクリーン空間上で線形補間しても問題ない擬似深度Zの計算
              DepthT z = v0.screenPos.z * w0 + v1.screenPos.z * w1 +
                         v2.screenPos.z * w2;

              // 固定機能：Zテスト
              if (z < m_depthBuffer.at(x, y)) {
                m_depthBuffer.at(x, y) = z;
              }
            }
          }
        }
      }
    }
  };

  class ComputePass {
  private:
  public:
    ComputePass() = default;

    template <typename ComputeShader>
      requires IsComputeShader<ComputeShader>
    void execute(int width, int height, ComputeShader computeShader) {

      // TODO: 後に#pragma omp parallel for collapse(2)
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          computeShader(x, y);
        }
      }
    }
  };

  // --- パス生成ファクトリ関数 ---
  template <typename InputVertex, typename Varying, typename DepthT = float>
  RasterizePass<InputVertex, Varying, DepthT>
  create_rasterize_pass(Texture2D<DepthT> &depthBuffer) {
    return RasterizePass<InputVertex, Varying, DepthT>(depthBuffer);
  }

  ComputePass create_compute_pass() { return ComputePass(); }
};
