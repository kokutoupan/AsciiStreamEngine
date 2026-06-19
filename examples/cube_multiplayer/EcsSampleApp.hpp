#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <ranges>
#include <vector>

#include <astream/ConnectionContext.hpp>
#include <astream/GameWorld.hpp>
#include <astream/GraphicsDevice.hpp>
#include <astream/Registry.hpp>
#include <astream/Texture2D.hpp>
#include <astream/TextureUtil.hpp>
#include <astream/shaders/DefaultShaders.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// =============================================================================
// 1. 特殊な動き (床と平行な円軌道) を担当する AI (EntityBehavior)
// =============================================================================

/**
 * @brief エンティティの円軌道運動 (XZ平面) と自転を制御する振る舞いクラス。
 */
class OrbitBehavior : public EntityBehavior {
private:
  glm::vec3 m_center; ///< 円軌道の中心座標
  float m_radius;     ///< 軌道半径
  float m_speed;      ///< 周回速度

public:
  OrbitBehavior(glm::vec3 center, float radius, float speed)
      : m_center(center), m_radius(radius), m_speed(speed) {}

  /**
   * @brief
   * 毎フレーム呼び出され、サイン・コサイン波を用いて座標を円軌道上に更新します。
   */
  void update(std::size_t index, std::uint32_t entityId,
              Registry &reg) override {
    // インデックスによるO(1)アクセスで対象トランスフォームを取得
    auto &trans = reg.get_transform_by_index(index);

    static float ticks = 0.0f;
    ticks += 0.02f;

    // XZ平面上での円軌道座標を計算
    trans.position.x = m_center.x + std::cos(ticks * m_speed) * m_radius;
    trans.position.z = m_center.z + std::sin(ticks * m_speed) * m_radius;
    trans.position.y = m_center.y; // 高さは基準点を維持

    // 進行方向に対応するようY軸回転させ、XZ回転はゼロにリセット
    trans.rotation.y -= 0.05f;
    trans.rotation.x = 0.0f;
    trans.rotation.z = 0.0f;
  }
};

// =============================================================================
// 2. 世界の共通状態を統括する GameWorld 実装
// =============================================================================

/**
 * @brief ハイブリッドECS (Registry)
 * を内包し、エンティティの生成とシステム実行を管理するゲームワールド。
 */
class EcsGameWorld : public GameWorld {
private:
  Registry m_registry; ///< ECSのレジストリ実体

  // システムが処理対象を識別するためのタグ定義
  static constexpr std::uint64_t TAG_NORMAL_FLOAT =
      1; ///< 通常の浮遊オブジェクト用タグ
  static constexpr std::uint64_t TAG_SPECIAL_ORBIT =
      2; ///< 特殊な円軌道オブジェクト用タグ

public:
  EcsGameWorld() {
    std::cout << "[ECS Engine] Initializing EcsGameWorld..." << std::endl;

    // ---------------------------------------------------------------------
    // システムの登録: 通常キューブの一斉浮遊制御
    // ---------------------------------------------------------------------
    m_registry.add_system([](Registry &reg) {
      auto &transforms = reg.get_raw_data<Transform>();
      auto &tags = reg.get_raw_data<std::uint64_t>();

      static float time = 0.0f;
      time += 0.05f;

      // タグ判定を行いながら各エンティティのトランスフォームを更新
      for (std::size_t i = 0; i < transforms.size(); ++i) {
        if (tags[i] == TAG_NORMAL_FLOAT) {
          // インデックス値に応じて波の位相と初期高さをずらす
          float phase = static_cast<float>(i) * 0.5f;
          float baseY = static_cast<float>((i % 4) - 1) * 2.0f;

          transforms[i].position.y = baseY + std::sin(time + phase) * 1.0f;

          // 緩やかな自転
          transforms[i].rotation.x += 0.01f;
          transforms[i].rotation.y += 0.02f;
        }
      }
    });

    // ---------------------------------------------------------------------
    // アセット準備: 立方体メッシュデータの作成
    // ---------------------------------------------------------------------
    auto cubeMesh = std::make_shared<MeshAsset<Shaders::DefaultVertex>>();
    cubeMesh->vertices = {{{1, -1, -1}, {0, 0, -1}, {0.0f, 1.0f}},
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
    cubeMesh->indices = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,
                         8,  9,  10, 8,  10, 11, 12, 13, 14, 12, 14, 15,
                         16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};
    VertexComponent cubeVertexComp{.asset = cubeMesh};

    // ---------------------------------------------------------------------
    // エンティティの生成
    // ---------------------------------------------------------------------

    // 1. システム駆動される通常キューブを30個生成 (スクリプト動作は無し)
    for (int i = 0; i < 30; ++i) {
      float x = static_cast<float>((i % 6) - 3) * 3.0f;
      float z = static_cast<float>((i / 6) - 2) * -4.0f;

      Transform trans{.position = glm::vec3(x, 0.0f, z),
                      .rotation = glm::vec3(0.0f),
                      .scale = glm::vec3(0.5f)};

      m_registry.create_entity(trans, Velocity{}, cubeVertexComp,
                               TAG_NORMAL_FLOAT, nullptr);
    }

    // 2. スクリプト駆動されるボス級ボスモデルを1個生成
    Transform specialTrans{.position = glm::vec3(0.0f, 0.0f, 0.0f),
                           .rotation = glm::vec3(0.0f),
                           .scale = glm::vec3(1.5f)};

    // 円軌道ビヘイビアを生成 (中心: (0, 3, -5), 半径: 6.0, 速度: 1.0)
    auto orbitBehavior = std::make_unique<OrbitBehavior>(
        glm::vec3(0.0f, 3.0f, -5.0f), 6.0f, 1.0f);

    m_registry.create_entity(specialTrans, Velocity{}, cubeVertexComp,
                             TAG_SPECIAL_ORBIT, std::move(orbitBehavior));
  }

  void processPlayerInput(int clientId, const InputDevice &input) override {}

  /**
   * @brief
   * ワールドの更新処理。登録されたスクリプトおよびシステムを一括アップデートします。
   */
  void globalUpdate() override { m_registry.update(); }

  /**
   * @brief ECSレジストリへの定数参照を取得します。
   */
  const Registry &get_registry() const { return m_registry; }
};

// =============================================================================
// 3. 各クライアントごとの個別描画セッション
// =============================================================================

/**
 * @brief
 * クライアントごとのカメラ制御、インプット処理、遅延シェーディングおよび描画パイプラインを処理するセッションクラス。
 */
class EcsPlayerSession : public ConnectionContext<EcsGameWorld> {
private:
  int m_clientId = -1;
  int w = 80, h = 24;
  float aspect = 1.0f;

  glm::vec3 m_cameraPos = glm::vec3(0.0f, 10.0f, 12.0f); ///< カメラ位置
  float m_yaw = -90.0f;                                  ///< 水平回転角 (ヨー)
  float m_pitch = -25.0f; ///< 垂直回転角 (ピッチ)

  GraphicsDevice device;
  std::unique_ptr<Texture2D<float>> cameraDepth;
  std::unique_ptr<Texture2D<float>> shadowDepth;
  std::unique_ptr<Texture2D<char>> albedoBuffer;
  std::unique_ptr<Texture2D<glm::vec3>> normalBuffer;
  std::unique_ptr<Texture2D<glm::vec3>> worldPosBuffer;

  glm::mat4 lightSpaceMatrix;
  glm::vec3 lightDir;

  std::vector<Shaders::DefaultVertex> planeVertices;
  std::vector<int> planeIndices;

  std::chrono::steady_clock::time_point m_lastFrameTime;

  const int sw = 128;
  const int sh = 128;

public:
  EcsPlayerSession() {
    m_lastFrameTime = std::chrono::steady_clock::now();
    lightDir = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
    planeVertices = {{{-15, -2.0f, -25}, {0, 1, 0}, {0.0f, 10.0f}},
                     {{15, -2.0f, -25}, {0, 1, 0}, {10.0f, 10.0f}},
                     {{15, -2.0f, 10}, {0, 1, 0}, {10.0f, 0.0f}},
                     {{-15, -2.0f, 10}, {0, 1, 0}, {0.0f, 0.0f}}};
    planeIndices = {0, 2, 1, 0, 3, 2};
  }

  void init(int clientId, int width, int height, EcsGameWorld &world) override {
    m_lastFrameTime = std::chrono::steady_clock::now();

    m_clientId = clientId;
    w = width;
    h = height;
    aspect = (float)w / (float)h * 0.5f;

    float pi_half = 3.14159265f / 2.0f;
    glm::mat4 lightView =
        glm::lookAt(lightDir * 20.0f, glm::vec3(0, 0, -5), glm::vec3(0, 1, 0));
    glm::mat4 lightProj =
        glm::ortho(-15.0f * aspect, 15.0f * aspect, -15.0f, 15.0f, 0.1f, 50.0f);
    lightSpaceMatrix = lightProj * lightView;

    cameraDepth = std::make_unique<Texture2D<float>>(
        w, h, std::numeric_limits<float>::max());
    shadowDepth = std::make_unique<Texture2D<float>>(
        sw, sh, std::numeric_limits<float>::max());
    albedoBuffer = std::make_unique<Texture2D<char>>(w, h, 0);
    normalBuffer =
        std::make_unique<Texture2D<glm::vec3>>(w, h, glm::vec3(0, 0, 0));
    worldPosBuffer =
        std::make_unique<Texture2D<glm::vec3>>(w, h, glm::vec3(0, 0, 0));
  }

  void onDisconnect(EcsGameWorld &world) override {}

  /**
   * @brief 入力デバイスの入力状況に応じてカメラの位置と視線を移動・調整します。
   */
  void update(int clientId, const InputDevice &input,
              EcsGameWorld &world) override {
    // J/L/I/K キーで視線回転
    float lookSpeed = 90.0f * input.getDeltaTime();
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

    glm::vec3 front;
    front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front.y = sin(glm::radians(m_pitch));
    front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    glm::vec3 cameraFront = glm::normalize(front);
    glm::vec3 cameraRight =
        glm::normalize(glm::cross(cameraFront, glm::vec3(0.0f, 1.0f, 0.0f)));

    // W/A/S/D/Q/E キーでカメラ平行移動
    float camSpeed = 5.0f * input.getDeltaTime();
    if (input.getKey(Key::w) || input.getKey(Key::W))
      m_cameraPos += cameraFront * camSpeed;
    if (input.getKey(Key::s) || input.getKey(Key::S))
      m_cameraPos -= cameraFront * camSpeed;
    if (input.getKey(Key::a) || input.getKey(Key::A))
      m_cameraPos -= cameraRight * camSpeed;
    if (input.getKey(Key::d) || input.getKey(Key::D))
      m_cameraPos += cameraRight * camSpeed;
    if (input.getKey(Key::e) || input.getKey(Key::E))
      m_cameraPos += glm::vec3(0, 1, 0) * camSpeed;
    if (input.getKey(Key::q) || input.getKey(Key::Q))
      m_cameraPos -= glm::vec3(0, 1, 0) * camSpeed;
  }

  /**
   * @brief
   * 画面のレンダリング処理。シャドウマップ生成、Gバッファへの描画、ディファードライティング、UIテキスト重ね合わせを行います。
   */
  void render(Texture2D<char> &outputTexture,
              const EcsGameWorld &world) override {
    auto renderStart = std::chrono::steady_clock::now();
    double frameDeltaMs =
        std::chrono::duration<double, std::milli>(renderStart - m_lastFrameTime)
            .count();
    m_lastFrameTime = renderStart;

    outputTexture.clear(' ');
    cameraDepth->clear(std::numeric_limits<float>::max());
    shadowDepth->clear(std::numeric_limits<float>::max());
    albedoBuffer->clear(0);
    normalBuffer->clear(glm::vec3(0, 0, 0));
    worldPosBuffer->clear(glm::vec3(0, 0, 0));

    glm::vec3 front;
    front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front.y = sin(glm::radians(m_pitch));
    front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    glm::mat4 view =
        glm::lookAt(m_cameraPos, m_cameraPos + glm::normalize(front),
                    glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(0.8f, aspect, 0.1f, 100.0f);

    const auto &reg = world.get_registry();
    auto &transforms = reg.get_raw_data<Transform>();
    auto &vertexComps = reg.get_raw_data<VertexComponent>();

    // 1. シャドウマップ生成パス
    auto shadowPass =
        device.create_rasterize_pass<Shaders::DefaultVertex, glm::vec3, float>(
            *shadowDepth);
    glm::mat4 identityModel(1.0f);
    shadowPass.draw(
        planeVertices, planeIndices,
        std::bind_back(Shaders::shadowVS, identityModel, lightSpaceMatrix));

    for (auto &&[trans, vComp] : std::views::zip(transforms, vertexComps)) {
      const auto &mesh = vComp.cast<Shaders::DefaultVertex>();
      shadowPass.draw(mesh.vertices, mesh.indices,
                      std::bind_back(Shaders::shadowVS, trans.to_matrix(),
                                     lightSpaceMatrix));
    }

    // 2. Gバッファ生成（ジオメトリ）パス
    auto geometryPass = device.create_rasterize_pass<
        Shaders::DefaultVertex, Shaders::DefaultVarying, float>(*cameraDepth);
    glm::mat4 planeMVP = proj * view * identityModel;
    geometryPass.draw(
        planeVertices, planeIndices,
        std::bind_back(Shaders::geometryVS, identityModel, planeMVP),
        [&](int x, int y, const Shaders::DefaultVarying &in) {
          albedoBuffer->at(x, y) = 127;
          normalBuffer->at(x, y) = in.normal;
          worldPosBuffer->at(x, y) = in.worldPos;
        });

    for (auto &&[trans, vComp] : std::views::zip(transforms, vertexComps)) {
      const auto &mesh = vComp.cast<Shaders::DefaultVertex>();
      glm::mat4 model = trans.to_matrix();
      glm::mat4 mvp = proj * view * model;

      geometryPass.draw(mesh.vertices, mesh.indices,
                        std::bind_back(Shaders::geometryVS, model, mvp),
                        [&](int x, int y, const Shaders::DefaultVarying &in) {
                          albedoBuffer->at(x, y) = 80;
                          normalBuffer->at(x, y) = in.normal;
                          worldPosBuffer->at(x, y) = in.worldPos;
                        });
    }

    // 3. ライティング・合成パス
    auto lightingPass = device.create_compute_pass();
    lightingPass.execute(
        w, h,
        std::bind_back(Shaders::deferredLightingCS, std::ref(outputTexture),
                       std::cref(*albedoBuffer), std::cref(*normalBuffer),
                       std::cref(*worldPosBuffer), std::cref(*shadowDepth),
                       lightSpaceMatrix, lightDir, w, h));

    auto renderEnd = std::chrono::steady_clock::now();
    double renderTimeMs =
        std::chrono::duration<double, std::milli>(renderEnd - renderStart)
            .count();

    // 4. デバッグ情報のHUDテキスト描画
    std::string debugStr = std::format(
        "Hybrid ECS | Total Entities: {}\nFPS: "
        "{:.1f} ({:.2f} ms)\nRender: {:.2f} ms",
        transforms.size(), (frameDeltaMs > 0.0 ? 1000.0 / frameDeltaMs : 0.0),
        frameDeltaMs, renderTimeMs);

    Texture2D<char> textTex = TextureUtil::strToTexture(debugStr, ' ');
    TextureUtil::blit_texture(outputTexture, textTex, 0, 0);
  }
};
