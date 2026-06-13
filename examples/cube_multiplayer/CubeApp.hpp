#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include "ConnectionContext.hpp"
#include "GameWorld.hpp"
#include "GraphicsDevice.hpp"
#include "Math.hpp"
#include "Texture2D.hpp"

// =============================================================================
// 1. グローバルな世界の共有状態クラス (矢印キーでキューブが同期回転)
// =============================================================================
class MyGameWorld : public GameWorld {
public:
  float angleX = 0.0f;
  float angleY = 0.0f;

  void processPlayerInput(int clientId, const InputDevice &input) override {
    // 矢印キーでグローバルな角度を更新
    if (input.getKey(Key::Left))
      angleY -= 0.05f;
    if (input.getKey(Key::Right))
      angleY += 0.05f;
    if (input.getKey(Key::Up))
      angleX -= 0.05f;
    if (input.getKey(Key::Down))
      angleX += 0.05f;

    // スペースキーで一斉リセット
    if (input.getKeyDown(Key::Space)) {
      angleX = 0.0f;
      angleY = 0.0f;
    }
  }

  void globalUpdate() override {
    // 全入力が処理された後の共通物理やNPC制御などがあればここに記述
  }
};

// =============================================================================
// 2. 各クライアントごとの個別セッションクラス (WASDキーで自分だけのカメラ移動)
// =============================================================================

inline char mapIntensityToChar(float intensity) {
  const char *palette = " .'`^\",:;Il!i~+_-?][}{1)(|\\/"
                        "tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
  int len = strlen(palette);
  int idx = (int)(intensity * (len - 1));
  if (idx < 0)
    idx = 0;
  if (idx >= len)
    idx = len - 1;
  return palette[idx];
}

class MyPlayerSession : public ConnectionContext<MyGameWorld> {
private:
  int m_clientId = -1;
  int w = 80, h = 24;
  float aspect = 1.0f;

  // 各クライアントが個別に持つローカルなカメラ座標
  Vec3 m_cameraPos = Vec3(0.0f, 0.0f, -4.5f);

  GraphicsDevice device;
  std::unique_ptr<Texture2D<float>> cameraDepth;
  std::unique_ptr<Texture2D<float>> shadowDepth;
  std::unique_ptr<Texture2D<char>> albedoBuffer;
  std::unique_ptr<Texture2D<Vec3>> normalBuffer;
  std::unique_ptr<Texture2D<Vec3>> worldPosBuffer;

  Mat4 lightSpaceMatrix;
  Vec3 lightDir;

  struct InputVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
  };
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

  std::vector<InputVertex> cubeVertices;
  std::vector<int> cubeIndices;
  std::vector<InputVertex> planeVertices;
  std::vector<int> planeIndices;

public:
  MyPlayerSession() {
    lightDir = Vec3(0.0f, 1.0f, 0.0f).normalize();
    float pi_half = 3.14159265f / 2.0f;
    Mat4 lightView = Mat4::translate(0, 0, -5.0f) * Mat4::rotateX(pi_half) *
                     Mat4::rotateY(0.0f);
    float boxSize = 8.0f;

    cubeVertices = {{{1, -1, -1}, {0, 0, -1}, {0.0f, 1.0f}},
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
    cubeIndices = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,
                   8,  9,  10, 8,  10, 11, 12, 13, 14, 12, 14, 15,
                   16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};
    planeVertices = {{{-10, -1.5f, -15}, {0, 1, 0}, {0.0f, 10.0f}},
                     {{10, -1.5f, -15}, {0, 1, 0}, {10.0f, 10.0f}},
                     {{10, -1.5f, 2}, {0, 1, 0}, {10.0f, 0.0f}},
                     {{-10, -1.5f, 2}, {0, 1, 0}, {0.0f, 0.0f}}};
    planeIndices = {0, 2, 1, 0, 3, 2};

    aspect = (float)w / (float)h *
             0.5f; // アスペクト比補正 (フォントの縦横比1:2を考慮)
    Mat4 lightProj = Mat4::orthographic(-8.0f * aspect, 8.0f * aspect, -8.0f,
                                        8.0f, 0.1f, 10.0f);
    lightSpaceMatrix = lightProj * lightView;
  }

  void init(int clientId, int width, int height, MyGameWorld &world) override {
    m_clientId = clientId;
    w = width;
    h = height;
    aspect = (float)w / (float)h *
             0.5f; // アスペクト比補正 (フォントの縦横比1:2を考慮)

    float pi_half = 3.14159265f / 2.0f;
    Mat4 lightView = Mat4::translate(0, 0, -5.0f) * Mat4::rotateX(pi_half) *
                     Mat4::rotateY(0.0f);
    Mat4 lightProj = Mat4::orthographic(-8.0f * aspect, 8.0f * aspect, -8.0f,
                                        8.0f, 0.1f, 10.0f);
    lightSpaceMatrix = lightProj * lightView;

    cameraDepth = std::make_unique<Texture2D<float>>(
        w, h, std::numeric_limits<float>::max());
    shadowDepth = std::make_unique<Texture2D<float>>(
        w, h, std::numeric_limits<float>::max());
    albedoBuffer = std::make_unique<Texture2D<char>>(w, h, ' ');
    normalBuffer = std::make_unique<Texture2D<Vec3>>(w, h, Vec3(0, 0, 0));
    worldPosBuffer = std::make_unique<Texture2D<Vec3>>(w, h, Vec3(0, 0, 0));
  }

  void onDisconnect(MyGameWorld &world) override {}

  void update(int clientId, const InputDevice &input,
              MyGameWorld &world) override {
    // 1. 矢印操作要求をグローバルな共有世界へ転送
    world.processPlayerInput(clientId, input);

    // 2. WASD操作は自分自身のローカルカメラの座標移動に適用
    float camSpeed = 0.1f;
    if (input.getKey(Key::a) || input.getKey(Key::A))
      m_cameraPos.x -= camSpeed;
    if (input.getKey(Key::d) || input.getKey(Key::D))
      m_cameraPos.x += camSpeed;
    if (input.getKey(Key::w) || input.getKey(Key::W))
      m_cameraPos.z += camSpeed;
    if (input.getKey(Key::s) || input.getKey(Key::S))
      m_cameraPos.z -= camSpeed;
  }

  void render(Texture2D<char> &outputTexture,
              const MyGameWorld &world) override {
    outputTexture.clear(' ');
    cameraDepth->clear(std::numeric_limits<float>::max());
    shadowDepth->clear(std::numeric_limits<float>::max());
    albedoBuffer->clear(' ');
    normalBuffer->clear(Vec3(0, 0, 0));
    worldPosBuffer->clear(Vec3(0, 0, 0));

    // 個別のカメラ位置を反映してビュー行列を作る
    Mat4 view = Mat4::translate(m_cameraPos.x, m_cameraPos.y, m_cameraPos.z);
    Mat4 proj = Mat4::perspective(1.0f, aspect, 0.1f, 100.0f);

    auto shadowVS = [](const InputVertex &in, const Mat4 &model,
                       const Mat4 &lightSpace) -> std::pair<Vec4, Vec3> {
      Vec3 worldPos = model.transform(in.position);
      return {
          lightSpace.transform(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f)),
          worldPos};
    };

    auto geometryVS = [](const InputVertex &in, const Mat4 &model,
                         const Mat4 &mvp) -> std::pair<Vec4, MyVarying> {
      MyVarying outVar;
      outVar.normal = model.transform(in.normal).normalize();
      outVar.uv = in.uv;
      outVar.worldPos = model.transform(in.position);
      return {mvp.transform(
                  Vec4(in.position.x, in.position.y, in.position.z, 1.0f)),
              outVar};
    };

    auto deferredLightingCS = [](int x, int y, Texture2D<char> &colorBuf,
                                 const Texture2D<char> &albedoBuf,
                                 const Texture2D<Vec3> &normalBuf,
                                 const Texture2D<Vec3> &worldPosBuf,
                                 const Texture2D<float> &shadowDepth,
                                 const Mat4 &lightSpace, const Vec3 &lightDir,
                                 int w, int h) {
      char mtl = albedoBuf.at(x, y);
      if (mtl == ' ')
        return;

      Vec3 worldPos = worldPosBuf.at(x, y);
      Vec3 normal = normalBuf.at(x, y);

      Vec4 posInLightSpace =
          lightSpace.transform(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f));
      float shadowX = (posInLightSpace.x + 1.0f) * 0.5f * (float)w;
      float shadowY = (1.0f - posInLightSpace.y) * 0.5f * (float)h;

      float shadowFactor = 1.0f;
      if (shadowX >= 0 && shadowX < w && shadowY >= 0 && shadowY < h) {
        float closestDepth = shadowDepth.at((int)shadowX, (int)shadowY);
        if (posInLightSpace.z > closestDepth + 0.002f)
          shadowFactor = 0.2f;
      }

      float diff = std::max(0.0f, normal.dot(lightDir));
      float col = std::min(1.0f, (diff * shadowFactor) + 0.1f);

      if (mtl == 'C')
        colorBuf.at(x, y) = mapIntensityToChar(col);
      else if (mtl == 'F')
        colorBuf.at(x, y) =
            (shadowFactor < 1.0f) ? ':' : mapIntensityToChar(col * 0.5f);
    };

    // --- PASS 1: シャドウパス ---
    auto shadowPass =
        device.create_rasterize_pass<InputVertex, Vec3, float>(*shadowDepth);
    Mat4 currentModel;
    currentModel.identity();
    shadowPass.draw(planeVertices, planeIndices,
                    std::bind_back(shadowVS, currentModel, lightSpaceMatrix));

    // 【共有空間との同期】世界の回転角 (world.angleX / world.angleY)
    // をモデル行列にバインドする！
    currentModel = Mat4::translate(0.0f, 0.3f, 0.0f) *
                   Mat4::rotateY(world.angleY) * Mat4::rotateX(world.angleX);
    shadowPass.draw(cubeVertices, cubeIndices,
                    std::bind_back(shadowVS, currentModel, lightSpaceMatrix));

    // --- PASS 2: ジオメトリパス ---
    auto geometryPass =
        device.create_rasterize_pass<InputVertex, MyVarying, float>(
            *cameraDepth);
    currentModel.identity();
    Mat4 currentMVP = proj * view * currentModel;
    geometryPass.draw(planeVertices, planeIndices,
                      std::bind_back(geometryVS, currentModel, currentMVP),
                      [&](int x, int y, const MyVarying &in) {
                        albedoBuffer->at(x, y) = 'F';
                        normalBuffer->at(x, y) = in.normal;
                        worldPosBuffer->at(x, y) = in.worldPos;
                      });

    // ジオメトリパス側にも世界の回転角を同期
    currentModel = Mat4::translate(0.0f, 0.3f, 0.0f) *
                   Mat4::rotateY(world.angleY) * Mat4::rotateX(world.angleX);
    currentMVP = proj * view * currentModel;
    geometryPass.draw(cubeVertices, cubeIndices,
                      std::bind_back(geometryVS, currentModel, currentMVP),
                      [&](int x, int y, const MyVarying &in) {
                        albedoBuffer->at(x, y) = 'C';
                        normalBuffer->at(x, y) = in.normal;
                        worldPosBuffer->at(x, y) = in.worldPos;
                      });

    // --- PASS 3: ライティングパス ---
    auto lightingPass = device.create_compute_pass();
    lightingPass.execute(
        w, h,
        std::bind_back(deferredLightingCS, std::ref(outputTexture),
                       std::cref(*albedoBuffer), std::cref(*normalBuffer),
                       std::cref(*worldPosBuffer), std::cref(*shadowDepth),
                       std::cref(lightSpaceMatrix), lightDir, w, h));
  }
};
