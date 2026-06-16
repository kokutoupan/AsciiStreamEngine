#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

#include "ConnectionContext.hpp"
#include "DefaultShaders.hpp"
#include "GameWorld.hpp"
#include "GraphicsDevice.hpp"
#include "Texture2D.hpp"
#include "TextureUtil.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// =============================================================================
// 1. グローバルな世界の共有状態クラス (矢印キーでキューブが同期回転)
// =============================================================================
class MyGameWorld : public GameWorld {
public:
  float angleX = 0.0f;
  float angleY = 0.0f;

  void processPlayerInput(int clientId, const InputDevice &input) override {
    float rotSpeed = 1.5f * input.getDeltaTime();
    // 矢印キーでグローバルな角度を更新
    if (input.getKey(Key::Left))
      angleY -= rotSpeed;
    if (input.getKey(Key::Right))
      angleY += rotSpeed;
    if (input.getKey(Key::Up))
      angleX -= rotSpeed;
    if (input.getKey(Key::Down))
      angleX += rotSpeed;

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

class MyPlayerSession : public ConnectionContext<MyGameWorld> {
private:
  int m_clientId = -1;
  int w = 80, h = 24;
  float aspect = 1.0f;

  // 各クライアントが個別に持つローカルなカメラ座標
  glm::vec3 m_cameraPos = glm::vec3(0.0f, 0.0f, 4.5f);
  float m_yaw = -90.0f;
  float m_pitch = 0.0f;

  GraphicsDevice device;
  std::unique_ptr<Texture2D<float>> cameraDepth;
  std::unique_ptr<Texture2D<float>> shadowDepth;
  std::unique_ptr<Texture2D<char>> albedoBuffer;
  std::unique_ptr<Texture2D<glm::vec3>> normalBuffer;
  std::unique_ptr<Texture2D<glm::vec3>> worldPosBuffer;

  glm::mat4 lightSpaceMatrix;
  glm::vec3 lightDir;

  std::vector<Shaders::DefaultVertex> cubeVertices;
  std::vector<int> cubeIndices;
  std::vector<Shaders::DefaultVertex> planeVertices;
  std::vector<int> planeIndices;

  std::chrono::steady_clock::time_point m_lastFrameTime;

public:
  MyPlayerSession() {
    m_lastFrameTime = std::chrono::steady_clock::now();
    m_cameraPos = glm::vec3(0.0f, 0.0f, 4.5f);
    m_yaw = -90.0f;
    m_pitch = 0.0f;
    lightDir = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f));
    float pi_half = 3.14159265f / 2.0f;
    glm::mat4 lightView =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f)) *
        glm::rotate(glm::mat4(1.0f), pi_half, glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), 0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
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
    glm::mat4 lightProj =
        glm::ortho(-8.0f * aspect, 8.0f * aspect, -8.0f, 8.0f, 0.1f, 10.0f);
    lightSpaceMatrix = lightProj * lightView;
  }

  void init(int clientId, int width, int height, MyGameWorld &world) override {
    m_clientId = clientId;
    w = width;
    h = height;
    aspect = (float)w / (float)h *
             0.5f; // アスペクト比補正 (フォントの縦横比1:2を考慮)
    m_lastFrameTime = std::chrono::steady_clock::now();
    m_cameraPos = glm::vec3(0.0f, 0.0f, 4.5f);
    m_yaw = -90.0f;
    m_pitch = 0.0f;

    float pi_half = 3.14159265f / 2.0f;
    glm::mat4 lightView =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f)) *
        glm::rotate(glm::mat4(1.0f), pi_half, glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), 0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 lightProj =
        glm::ortho(-8.0f * aspect, 8.0f * aspect, -8.0f, 8.0f, 0.1f, 10.0f);
    lightSpaceMatrix = lightProj * lightView;

    cameraDepth = std::make_unique<Texture2D<float>>(
        w, h, std::numeric_limits<float>::max());
    shadowDepth = std::make_unique<Texture2D<float>>(
        w, h, std::numeric_limits<float>::max());
    albedoBuffer = std::make_unique<Texture2D<char>>(w, h, ' ');
    normalBuffer =
        std::make_unique<Texture2D<glm::vec3>>(w, h, glm::vec3(0, 0, 0));
    worldPosBuffer =
        std::make_unique<Texture2D<glm::vec3>>(w, h, glm::vec3(0, 0, 0));
  }

  void onDisconnect(MyGameWorld &world) override {}

  void update(int clientId, const InputDevice &input,
              MyGameWorld &world) override {
    // 1. 矢印操作要求をグローバルな共有世界へ転送
    world.processPlayerInput(clientId, input);

    // 2. IJKL操作によるカメラ視線方向の変更
    float lookSpeed = 90.0f * input.getDeltaTime(); // 90度/秒
    if (input.getKey(Key::j) || input.getKey(Key::J))
      m_yaw -= lookSpeed;
    if (input.getKey(Key::l) || input.getKey(Key::L))
      m_yaw += lookSpeed;
    if (input.getKey(Key::i) || input.getKey(Key::I))
      m_pitch += lookSpeed;
    if (input.getKey(Key::k) || input.getKey(Key::K))
      m_pitch -= lookSpeed;

    if (m_pitch > 89.0f)
      m_pitch = 89.0f;
    if (m_pitch < -89.0f)
      m_pitch = -89.0f;

    // 3. 視線方向ベクトルと右方向ベクトルの算出
    glm::vec3 front;
    front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front.y = sin(glm::radians(m_pitch));
    front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    glm::vec3 cameraFront = glm::normalize(front);
    glm::vec3 cameraRight =
        glm::normalize(glm::cross(cameraFront, glm::vec3(0.0f, 1.0f, 0.0f)));

    // 4. WASD操作はカメラ視線方向を基準にした相対移動に適用
    float camSpeed = 3.0f * input.getDeltaTime();
    if (input.getKey(Key::w) || input.getKey(Key::W))
      m_cameraPos += cameraFront * camSpeed;
    if (input.getKey(Key::s) || input.getKey(Key::S))
      m_cameraPos -= cameraFront * camSpeed;
    if (input.getKey(Key::a) || input.getKey(Key::A))
      m_cameraPos -= cameraRight * camSpeed;
    if (input.getKey(Key::d) || input.getKey(Key::D))
      m_cameraPos += cameraRight * camSpeed;
  }

  void render(Texture2D<char> &outputTexture,
              const MyGameWorld &world) override {
    auto renderStart = std::chrono::steady_clock::now();
    double frameDeltaMs =
        std::chrono::duration<double, std::milli>(renderStart - m_lastFrameTime)
            .count();
    m_lastFrameTime = renderStart;

    outputTexture.clear(' ');
    cameraDepth->clear(std::numeric_limits<float>::max());
    shadowDepth->clear(std::numeric_limits<float>::max());
    albedoBuffer->clear(' ');
    normalBuffer->clear(glm::vec3(0, 0, 0));
    worldPosBuffer->clear(glm::vec3(0, 0, 0));

    // 個別のカメラ位置と視線方向を反映してビュー行列を作る
    glm::vec3 front;
    front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front.y = sin(glm::radians(m_pitch));
    front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    glm::vec3 cameraFront = glm::normalize(front);

    glm::mat4 view = glm::lookAt(m_cameraPos, m_cameraPos + cameraFront,
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(1.0f, aspect, 0.1f, 100.0f);

    // --- PASS 1: シャドウパス ---
    auto shadowPass =
        device.create_rasterize_pass<Shaders::DefaultVertex, glm::vec3, float>(
            *shadowDepth);
    glm::mat4 currentModel(1.0f);
    shadowPass.draw(
        planeVertices, planeIndices,
        std::bind_back(Shaders::shadowVS, currentModel, lightSpaceMatrix));

    // 【共有空間との同期】世界の回転角 (world.angleX / world.angleY)
    // をモデル行列にバインドする！
    currentModel =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.3f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), world.angleY,
                    glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), world.angleX, glm::vec3(1.0f, 0.0f, 0.0f));
    shadowPass.draw(
        cubeVertices, cubeIndices,
        std::bind_back(Shaders::shadowVS, currentModel, lightSpaceMatrix));

    // --- PASS 2: ジオメトリパス ---
    auto geometryPass = device.create_rasterize_pass<
        Shaders::DefaultVertex, Shaders::DefaultVarying, float>(*cameraDepth);
    currentModel = glm::mat4(1.0f);
    glm::mat4 currentMVP = proj * view * currentModel;
    geometryPass.draw(
        planeVertices, planeIndices,
        std::bind_back(Shaders::geometryVS, currentModel, currentMVP),
        [&](int x, int y, const Shaders::DefaultVarying &in) {
          albedoBuffer->at(x, y) = 'F';
          normalBuffer->at(x, y) = in.normal;
          worldPosBuffer->at(x, y) = in.worldPos;
        });

    // ジオメトリパス側にも世界の回転角を同期
    currentModel =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.3f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), world.angleY,
                    glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), world.angleX, glm::vec3(1.0f, 0.0f, 0.0f));
    currentMVP = proj * view * currentModel;
    geometryPass.draw(
        cubeVertices, cubeIndices,
        std::bind_back(Shaders::geometryVS, currentModel, currentMVP),
        [&](int x, int y, const Shaders::DefaultVarying &in) {
          albedoBuffer->at(x, y) = 'C';
          normalBuffer->at(x, y) = in.normal;
          worldPosBuffer->at(x, y) = in.worldPos;
        });

    // --- PASS 3: ライティングパス ---
    auto lightingPass = device.create_compute_pass();
    lightingPass.execute(
        w, h,
        std::bind_back(Shaders::deferredLightingCS, std::ref(outputTexture),
                       std::cref(*albedoBuffer), std::cref(*normalBuffer),
                       std::cref(*worldPosBuffer), std::cref(*shadowDepth),
                       std::cref(lightSpaceMatrix), lightDir, w, h));

    auto renderEnd = std::chrono::steady_clock::now();
    double renderTimeMs =
        std::chrono::duration<double, std::milli>(renderEnd - renderStart)
            .count();

    char infoBuf[128];
    std::snprintf(infoBuf, sizeof(infoBuf),
                  "FPS: %.1f (%.2f ms)\nRender: %.2f ms",
                  (frameDeltaMs > 0.0 ? 1000.0 / frameDeltaMs : 0.0),
                  frameDeltaMs, renderTimeMs);

    Texture2D<char> textTex = TextureUtil::strToTexture(infoBuf, ' ');
    TextureUtil::blit_texture(outputTexture, textTex, 0, 0);
  }
};
