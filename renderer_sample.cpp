#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <unistd.h>
#include <vector>

#include "GraphicsDevice.hpp"
#include "Math.hpp"
#include "Texture2D.hpp"

inline char mapIntensityToChar(float intensity) {
  const char *palette = " .'`^\",:;Il!i~+_-?][}"
                        "{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
  int len = strlen(palette);
  int idx = (int)(intensity * (len - 1));
  if (idx < 0)
    idx = 0;
  if (idx >= len)
    idx = len - 1;
  return palette[idx];
}

struct MyVarying {
  Vec3 normal;
  Vec2 uv;
  Vec3 worldPos;

  MyVarying operator+(const MyVarying &r) const {
    return {normal + r.normal, uv + r.uv, worldPos + r.worldPos};
  }
  MyVarying operator*(float s) const {
    return {normal * s, uv * s, worldPos * s};
  }
};

struct InputVertex {
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
};

// --- アセット定義 ---
std::vector<InputVertex> cubeVertices = {
    {{1, -1, -1}, {0, 0, -1}, {0.0f, 1.0f}},
    {{-1, -1, -1}, {0, 0, -1}, {1.0f, 1.0f}},
    {{-1, 1, -1}, {0, 0, -1}, {1.0f, 0.0f}},
    {{1, 1, -1}, {0, 0, -1}, {0.0f, 0.0f}},
    {{-1, -1, 1}, {0, 0, 1}, {0.0f, 1.0f}},
    {{1, -1, 1}, {0, 0, 1}, {1.0f, 1.0f}},
    {{1, 1, 1}, {0, 0, 1}, {1.0f, 0.0f}},
    {{-1, 1, 1}, {0, 0, 1}, {0.0f, 0.0f}},
    {{-1, -1, -1}, {-1, 0, 0}, {0.0f, 1.0f}},
    {{-1, -1, 1}, {-1, 0, 0}, {1.0f, 1.0f}},
    {{-1, 1, 1}, {-1, 0, 0}, {1.0f, 0.0f}},
    {{-1, 1, -1}, {-1, 0, 0}, {0.0f, 0.0f}},
    {{1, -1, 1}, {1, 0, 0}, {0.0f, 1.0f}},
    {{1, -1, -1}, {1, 0, 0}, {1.0f, 1.0f}},
    {{1, 1, -1}, {1, 0, 0}, {1.0f, 0.0f}},
    {{1, 1, 1}, {1, 0, 0}, {0.0f, 0.0f}},
    {{-1, 1, 1}, {0, 1, 0}, {0.0f, 1.0f}},
    {{1, 1, 1}, {0, 1, 0}, {1.0f, 1.0f}},
    {{1, 1, -1}, {0, 1, 0}, {1.0f, 0.0f}},
    {{-1, 1, -1}, {0, 1, 0}, {0.0f, 0.0f}},
    {{-1, -1, -1}, {0, -1, 0}, {0.0f, 1.0f}},
    {{1, -1, -1}, {0, -1, 0}, {1.0f, 1.0f}},
    {{1, -1, 1}, {0, -1, 0}, {1.0f, 0.0f}},
    {{-1, -1, 1}, {0, -1, 0}, {0.0f, 0.0f}}};
std::vector<int> cubeIndices = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,
                                8,  9,  10, 8,  10, 11, 12, 13, 14, 12, 14, 15,
                                16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

std::vector<InputVertex> planeVertices = {
    {{-10, -1.5f, -15}, {0, 1, 0}, {0.0f, 10.0f}},
    {{10, -1.5f, -15}, {0, 1, 0}, {10.0f, 10.0f}},
    {{10, -1.5f, 2}, {0, 1, 0}, {10.0f, 0.0f}},
    {{-10, -1.5f, 2}, {0, 1, 0}, {0.0f, 0.0f}}};
std::vector<int> planeIndices = {0, 2, 1, 0, 3, 2};

// ==========================================
// SHADER FUNCTIONS
// ==========================================

// シャドウパス用頂点シェーダー
auto shadowVS = [](const InputVertex &in, const Mat4 &model,
                   const Mat4 &lightSpace) -> std::pair<Vec4, Vec3> {
  Vec3 worldPos = model.transform(in.position);
  Vec4 pos =
      lightSpace.transform(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f));
  return {pos, worldPos};
};

// ジオメトリパス用頂点シェーダー
auto geometryVS = [](const InputVertex &in, const Mat4 &model,
                     const Mat4 &mvp) -> std::pair<Vec4, MyVarying> {
  Vec4 pos =
      mvp.transform(Vec4(in.position.x, in.position.y, in.position.z, 1.0f));
  MyVarying outVar;
  outVar.normal = model.transform(in.normal).normalize();
  outVar.uv = in.uv;
  outVar.worldPos = model.transform(in.position);
  return {pos, outVar};
};

// ディファードライティング用コンピュート（ピクセル）シェーダー
auto deferredLightingCS =
    [](int x, int y, Texture2D<char> &colorBuf,
       const Texture2D<char> &albedoBuf, const Texture2D<Vec3> &normalBuf,
       const Texture2D<Vec3> &worldPosBuf, const Texture2D<float> &shadowDepth,
       const Mat4 &lightSpace, const Vec3 &lightDir, int w, int h) {
      char mtl = albedoBuf.at(x, y);
      if (mtl == ' ')
        return;

      Vec3 worldPos = worldPosBuf.at(x, y);
      Vec3 normal = normalBuf.at(x, y);

      // 1. シャドウ判定
      Vec4 posInLightSpace =
          lightSpace.transform(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f));
      float shadowX = (posInLightSpace.x + 1.0f) * 0.5f * (float)w;
      float shadowY = (1.0f - posInLightSpace.y) * 0.5f * (float)h;

      float shadowFactor = 1.0f;
      if (shadowX >= 0 && shadowX < w && shadowY >= 0 && shadowY < h) {
        float closestDepth = shadowDepth.at((int)shadowX, (int)shadowY);
        float currentDepth = posInLightSpace.z;
        if (currentDepth > closestDepth + 0.002f) {
          shadowFactor = 0.2f;
        }
      }

      // 2. ライティング
      float diff = std::max(0.0f, normal.dot(lightDir));
      float col = (diff * shadowFactor) + 0.1f;
      col = std::min(1.0f, col);

      if (mtl == 'C') {
        colorBuf.at(x, y) = mapIntensityToChar(col);
      } else if (mtl == 'F') {
        colorBuf.at(x, y) =
            (shadowFactor < 1.0f) ? ':' : mapIntensityToChar(col * 0.5f);
      }
    };

int main() {
  int w = 80, h = 24;
  auto device = GraphicsDevice();

  // 出力
  auto colorBuffer = Texture2D<char>(w, h, ' ');
  auto cameraDepth = Texture2D<float>(w, h, std::numeric_limits<float>::max());
  auto shadowDepth = Texture2D<float>(w, h, std::numeric_limits<float>::max());

  // Gバッファ群
  auto albedoBuffer = Texture2D<char>(w, h, ' ');
  auto normalBuffer = Texture2D<Vec3>(w, h, Vec3(0, 0, 0));
  auto worldPosBuffer = Texture2D<Vec3>(w, h, Vec3(0, 0, 0));

  float angleX = 0.0f, angleY = 0.0f;
  const char *clear_seq = "\x1b[2J";

  Vec3 lightDir = Vec3(0.0f, 1.0f, 0.0f).normalize();
  float pi_half = 3.14159265f / 2.0f;
  Mat4 lightView = Mat4::translate(0, 0, -5.0f) * Mat4::rotateX(pi_half) *
                   Mat4::rotateY(0.0f);
  float boxSize = 8.0f;
  float aspect = (float)w / (float)h * 0.5f;
  Mat4 lightProj = Mat4::orthographic(-boxSize * aspect, boxSize * aspect,
                                      -boxSize, boxSize, 0.1f, 10.0f);
  Mat4 lightSpaceMatrix = lightProj * lightView;

  std::vector<char> outputStream((w + 1) * h + 1, '\0');

  Mat4 currentModel;
  Mat4 currentMVP;

  while (true) {
    colorBuffer.clear(' ');
    cameraDepth.clear(std::numeric_limits<float>::max());
    shadowDepth.clear(std::numeric_limits<float>::max());
    albedoBuffer.clear(' ');
    normalBuffer.clear(Vec3(0, 0, 0));
    worldPosBuffer.clear(Vec3(0, 0, 0));

    Mat4 view = Mat4::translate(0, 0, -4.5f);
    Mat4 proj = Mat4::perspective(1.0f, aspect, 0.1f, 100.0f);

    // ==========================================
    // PASS 1: シャドウパス
    // ==========================================
    auto shadowPass =
        device.create_rasterize_pass<InputVertex, Vec3, float>(shadowDepth);

    currentModel.identity();
    shadowPass.draw(planeVertices, planeIndices,
                    std::bind_back(shadowVS, currentModel, lightSpaceMatrix));

    currentModel = Mat4::translate(0.0f, 0.3f, 0.0f) * Mat4::rotateY(angleY) *
                   Mat4::rotateX(angleX);
    shadowPass.draw(cubeVertices, cubeIndices,
                    std::bind_back(shadowVS, currentModel, lightSpaceMatrix));

    // ==========================================
    // PASS 2: ジオメトリパス (Gバッファへの書き込み)
    // ==========================================
    auto geometryPass =
        device.create_rasterize_pass<InputVertex, MyVarying, float>(
            cameraDepth);

    // 2-1. 床

    currentModel.identity();
    currentMVP = proj * view * currentModel;
    geometryPass.draw(planeVertices, planeIndices,
                      std::bind_back(geometryVS, currentModel, currentMVP),
                      [&](int x, int y, const MyVarying &in) {
                        albedoBuffer.at(x, y) = 'F';
                        normalBuffer.at(x, y) = in.normal;
                        worldPosBuffer.at(x, y) = in.worldPos;
                      });

    // 2-2. キューブ
    currentModel = Mat4::translate(0.0f, 0.3f, 0.0f) * Mat4::rotateY(angleY) *
                   Mat4::rotateX(angleX);
    currentMVP = proj * view * currentModel;
    geometryPass.draw(cubeVertices, cubeIndices,
                      std::bind_back(geometryVS, currentModel, currentMVP),
                      [&](int x, int y, const MyVarying &in) {
                        albedoBuffer.at(x, y) = 'C';
                        normalBuffer.at(x, y) = in.normal;
                        worldPosBuffer.at(x, y) = in.worldPos;
                      });

    // ==========================================
    // PASS 3: ライティングパス
    // ==========================================
    auto lightingPass = device.create_compute_pass();

    lightingPass.execute(
        w, h,
        std::bind_back(deferredLightingCS, std::ref(colorBuffer),
                       std::cref(albedoBuffer), std::cref(normalBuffer),
                       std::cref(worldPosBuffer), std::cref(shadowDepth),
                       lightSpaceMatrix, lightDir, w, h));

    // ==========================================
    // PRESENT: 画面出力
    // ==========================================
    std::cout << clear_seq;
    size_t streamIdx = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        outputStream[streamIdx++] = colorBuffer.at(x, y);
      }
      outputStream[streamIdx++] = '\n';
    }
    outputStream[streamIdx] = '\0';
    std::cout << outputStream.data() << std::flush;

    angleX += 0.05f;
    angleY += 0.03f;
    usleep(50000);
  }
  return 0;
}
