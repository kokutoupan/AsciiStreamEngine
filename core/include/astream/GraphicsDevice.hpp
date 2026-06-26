#pragma once

#include <algorithm>
#include <concepts>
#include <memory_resource>
#include <span>
#include <utility>

#include <glm/glm.hpp>

#include <astream/Texture2D.hpp>

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
    void draw(std::span<const InputVertex> vertices,
              std::span<const int> indices, VertexShader &&vertexShader,
              FragmentShader &&fragmentShader) {

      int targetWidth = m_depthBuffer.getWidth();
      int targetHeight = m_depthBuffer.getHeight();

      // 補間計算用に、1/W でスケールされた状態の Varying
      // 属性を保持する内部構造体
      struct VSOutput {
        glm::vec4 clipPos;
        Varying var;
      };

      // 256KBのスタックを確保
      alignas(VSOutput) std::byte buffer[256 * 1024];
      std::pmr::monotonic_buffer_resource mem_pool(buffer, sizeof(buffer));
      std::pmr::vector<VSOutput> vsOutputs(&mem_pool);

      vsOutputs.reserve(vertices.size());

      for (const auto &v : vertices) {
        auto result = vertexShader(v);
        vsOutputs.push_back({result.first, result.second});
      }
      // 補間計算用に、1/W でスケールされた状態の属性を保持する構造体
      struct ShadedVertex {
        glm::vec4 screenPos;
        float invW;
        Varying varInvW;
      };

      const float W_PLANE = 0.001f;
      // エッジがニアプレーンと交差する点の補間計算
      auto clipEdge = [W_PLANE](const VSOutput &in,
                                const VSOutput &out) -> VSOutput {
        float t = (in.clipPos.w - W_PLANE) / (in.clipPos.w - out.clipPos.w);
        VSOutput res;
        // 座標とVaryingを線形補間
        res.clipPos = in.clipPos * (1.0f - t) + out.clipPos * t;
        res.var = in.var * (1.0f - t) + out.var * t;
        return res;
      };

      // クリップ処理を通過した「安全な三角形」を画面に描画するローカル関数
      auto rasterizeTriangle = [&](const VSOutput &v0_in, const VSOutput &v1_in,
                                   const VSOutput &v2_in) {
        ShadedVertex sv[3];
        const VSOutput *tri[3] = {&v0_in, &v1_in, &v2_in};

        // NDC -> スクリーン空間座標への変換と、1/W 乗算
        for (int j = 0; j < 3; ++j) {
          float invW = 1.0f / tri[j]->clipPos.w;
          glm::vec4 sPos;
          sPos.x =
              (tri[j]->clipPos.x * invW + 1.0f) * 0.5f * (float)targetWidth;
          sPos.y =
              (1.0f - tri[j]->clipPos.y * invW) * 0.5f * (float)targetHeight;
          sPos.z = tri[j]->clipPos.z * invW;
          sPos.w = invW;
          sv[j] = {sPos, invW, tri[j]->var * invW};
        }

        const auto &v0 = sv[0];
        const auto &v1 = sv[1];
        const auto &v2 = sv[2];

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
          return; // バックフェースカリング

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

              float interpolatedInvW =
                  v0.invW * w0 + v1.invW * w1 + v2.invW * w2;
              float pixelW =
                  1.0f / (interpolatedInvW != 0.0f ? interpolatedInvW : 1.0f);

              float var0 = v0.screenPos.z;
              float var1 = v1.screenPos.z;
              float var2 = v2.screenPos.z;
              DepthT z = (var0 * w0 + var1 * w1 + var2 * w2) * pixelW;

              if (z < m_depthBuffer.at(x, y)) {
                m_depthBuffer.at(x, y) = z;
                Varying interpolatedVarInvW =
                    v0.varInvW * w0 + v1.varInvW * w1 + v2.varInvW * w2;
                Varying correctVarying = interpolatedVarInvW * pixelW;
                fragmentShader(x, y, correctVarying);
              }
            }
          }
        }
      };

      // 2. 三角形ごとのクリッピング＆ラスタライズ
      for (size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size())
          break;

        VSOutput inputTri[3] = {vsOutputs[indices[i]],
                                vsOutputs[indices[i + 1]],
                                vsOutputs[indices[i + 2]]};
        VSOutput outputPoly[4]; // クリッピング後の多角形（最大で四角形=4頂点）
        int outCount = 0;

        // ニアプレーンに対するサザーランド・ホッジマン・クリッピング
        for (int j = 0; j < 3; ++j) {
          int next = (j + 1) % 3;
          const VSOutput &currV = inputTri[j];
          const VSOutput &nextV = inputTri[next];

          bool currInside = currV.clipPos.w >= W_PLANE;
          bool nextInside = nextV.clipPos.w >= W_PLANE;

          if (currInside) {
            // 現在の頂点が内側なら追加
            outputPoly[outCount++] = currV;
          }

          if (currInside != nextInside) {
            // 境界をまたいだら交点を計算して追加
            outputPoly[outCount++] = clipEdge(currV, nextV);
          }
        }

        // 構築された多角形（3〜4頂点）を三角形に分割してラスタライズ
        if (outCount == 3) {
          // そのままの三角形、または1点だけがカメラ前の小さな三角形
          rasterizeTriangle(outputPoly[0], outputPoly[1], outputPoly[2]);
        } else if (outCount == 4) {
          // 四角形になった場合は2つの三角形に分割 (Triangle Fan方式)
          rasterizeTriangle(outputPoly[0], outputPoly[1], outputPoly[2]);
          rasterizeTriangle(outputPoly[0], outputPoly[2], outputPoly[3]);
        }
      }
    }

    template <typename VertexShader>
      requires IsVertexShader<VertexShader, InputVertex, Varying>
    void draw(std::span<const InputVertex> vertices,
              std::span<const int> indices, VertexShader &&vertexShader) {

      int targetWidth = m_depthBuffer.getWidth();
      int targetHeight = m_depthBuffer.getHeight();

      struct VSOutput {
        glm::vec4 clipPos;
      };

      // 256KBのスタックを確保
      alignas(VSOutput) std::byte buffer[256 * 1024];
      std::pmr::monotonic_buffer_resource mem_pool(buffer, sizeof(buffer));
      std::pmr::vector<VSOutput> vsOutputs(&mem_pool);

      vsOutputs.reserve(vertices.size());
      for (const auto &v : vertices) {
        auto result = vertexShader(v);
        vsOutputs.push_back({result.first});
      }
      // 補間計算用に、1/W でスケールされた状態の属性を保持する構造体
      struct ShadedVertex {
        glm::vec4 screenPos;
        float invW;
      };

      const float W_PLANE = 0.001f;

      // エッジがニアプレーンと交差する点の補間計算
      auto clipEdge = [W_PLANE](const VSOutput &in,
                                const VSOutput &out) -> VSOutput {
        float t = (in.clipPos.w - W_PLANE) / (in.clipPos.w - out.clipPos.w);
        VSOutput res;
        // 座標を線形補間
        res.clipPos = in.clipPos * (1.0f - t) + out.clipPos * t;
        return res;
      };

      // クリップ処理を通過した「安全な三角形」を画面に描画するローカル関数
      auto rasterizeTriangle = [&](const VSOutput &v0_in, const VSOutput &v1_in,
                                   const VSOutput &v2_in) {
        ShadedVertex sv[3];
        const VSOutput *tri[3] = {&v0_in, &v1_in, &v2_in};

        // NDC -> スクリーン空間座標への変換と、1/W 乗算
        for (int j = 0; j < 3; ++j) {
          float invW = 1.0f / tri[j]->clipPos.w;
          glm::vec4 sPos;
          sPos.x =
              (tri[j]->clipPos.x * invW + 1.0f) * 0.5f * (float)targetWidth;
          sPos.y =
              (1.0f - tri[j]->clipPos.y * invW) * 0.5f * (float)targetHeight;
          sPos.z = tri[j]->clipPos.z * invW;
          sPos.w = invW;
          sv[j] = {sPos, invW};
        }

        const auto &v0 = sv[0];
        const auto &v1 = sv[1];
        const auto &v2 = sv[2];

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
          return; // バックフェースカリング

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

              float interpolatedInvW =
                  v0.invW * w0 + v1.invW * w1 + v2.invW * w2;
              float pixelW =
                  1.0f / (interpolatedInvW != 0.0f ? interpolatedInvW : 1.0f);

              float var0 = v0.screenPos.z;
              float var1 = v1.screenPos.z;
              float var2 = v2.screenPos.z;
              DepthT z = (var0 * w0 + var1 * w1 + var2 * w2) * pixelW;

              if (z < m_depthBuffer.at(x, y)) {
                m_depthBuffer.at(x, y) = z;
              }
            }
          }
        }
      };

      // 2. 三角形ごとのクリッピング＆ラスタライズ
      for (size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size())
          break;

        VSOutput inputTri[3] = {vsOutputs[indices[i]],
                                vsOutputs[indices[i + 1]],
                                vsOutputs[indices[i + 2]]};
        VSOutput outputPoly[4]; // クリッピング後の多角形（最大で四角形=4頂点）
        int outCount = 0;

        // ニアプレーンに対するサザーランド・ホッジマン・クリッピング
        for (int j = 0; j < 3; ++j) {
          int next = (j + 1) % 3;
          const VSOutput &currV = inputTri[j];
          const VSOutput &nextV = inputTri[next];

          bool currInside = currV.clipPos.w >= W_PLANE;
          bool nextInside = nextV.clipPos.w >= W_PLANE;

          if (currInside) {
            // 現在の頂点が内側なら追加
            outputPoly[outCount++] = currV;
          }

          if (currInside != nextInside) {
            // 境界をまたいだら交点を計算して追加
            outputPoly[outCount++] = clipEdge(currV, nextV);
          }
        }

        // 構築された多角形（3〜4頂点）を三角形に分割してラスタライズ
        if (outCount == 3) {
          // そのままの三角形、または1点だけがカメラ前の小さな三角形
          rasterizeTriangle(outputPoly[0], outputPoly[1], outputPoly[2]);
        } else if (outCount == 4) {
          // 四角形になった場合は2つの三角形に分割 (Triangle Fan方式)
          rasterizeTriangle(outputPoly[0], outputPoly[1], outputPoly[2]);
          rasterizeTriangle(outputPoly[0], outputPoly[2], outputPoly[3]);
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
