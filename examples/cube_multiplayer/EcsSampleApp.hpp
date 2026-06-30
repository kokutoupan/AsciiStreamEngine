#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <ranges>
#include <unordered_map>
#include <vector>

#include <astream/ConnectionContext.hpp>
#include <astream/GameWorld.hpp>
#include <astream/GraphicsDevice.hpp>
#include <astream/MeshUtil.hpp>
#include <astream/Registry.hpp>
#include <astream/Texture2D.hpp>
#include <astream/TextureUtil.hpp>
#include <astream/shaders/DefaultShaders.hpp>

#include <astream/PhysicsUtil.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "cube.hpp" // cube.obj から自動生成

/**
 * @brief 円軌道運動に必要なパラメータを保持するコンポーネント
 */
struct OrbitComponent {
  glm::vec3 center;   ///< 円軌道の中心座標
  float radius;       ///< 軌道半径
  float speed;        ///< 周回速度
  float ticks = 0.0f; ///< エンティティ個別の時間経過（バグの修正）
};

// プレイヤーの入力状態（あるいはコマンド）を保持するコンポーネント
struct PlayerInputComponent {
  int clientId;
  glm::vec3 move_intention{0.0f};
  float yaw = 0.0f;
  float pitch = 0.0f;
  bool action_trigger_space = false;
  bool action_trigger_r = false;
};

// =============================================================================
// 1. 世界の共通状態を統括する GameWorld 実装
// =============================================================================

/**
 * @brief ハイブリッドECS (Registry)
 * を内包し、エンティティの生成とシステム実行を管理するゲームワールド。
 */
class EcsGameWorld : public GameWorld {
private:
  Registry m_registry; ///< ECSのレジストリ実体

public:
  std::unordered_map<int, EntityId> player_entities; // ClientID -> EntityId
  bool m_resetRequested = false;

  void onPlayerConnect(int clientId) {
    Transform trans{.position = glm::vec3(0.0f, 10.0f, 12.0f),
                    .rotation = glm::vec3(0.0f),
                    .scale = glm::vec3(1.0f)};
    auto id = m_registry.create_entity(trans, Velocity{}, Acceleration{},
                                       VertexComponent{}, 0, nullptr);
    m_registry.add_component<PlayerInputComponent>(
        id, PlayerInputComponent{
                .clientId = clientId, .yaw = -90.0f, .pitch = -25.0f});
    player_entities[clientId] = id;
  }

  void onPlayerDisconnect(int clientId) {
    auto it = player_entities.find(clientId);
    if (it != player_entities.end()) {
      m_registry.destroy_entity(it->second);
      player_entities.erase(it);
    }
  }

  // システムが処理対象を識別するためのタグ定義
  static constexpr std::uint64_t TAG_NORMAL_PHYSICS =
      1; ///< 通常の物理オブジェクト用タグ
  static constexpr std::uint64_t TAG_SPECIAL_ORBIT =
      2; ///< 特殊な円軌道オブジェクト用タグ
  static constexpr std::uint64_t TAG_GROUND =
      3; ///< 床（静的コライダー）識別用タグ
  static constexpr std::uint64_t OBJECT_TYPE_MASK = 0x3;

  // 床の高さの基準
  static constexpr float GROUND_Y = -2.0f;

  struct GravityTag {};

  void create_entitys() {
    // ---------------------------------------------------------------------
    // アセット準備: 立方体メッシュデータの作成
    // ---------------------------------------------------------------------

    // 動的な場合の例
    // auto cube_vertex = MeshUtil::create_vertices(
    //     cube_vertex_count, cube_positions, cube_normals, cube_texcoords);
    // ;
    // auto cube_index = MeshUtil::create_indices(cube_index_count,
    // cube_indices);
    //
    // std::shared_ptr<VectorMeshHolder<Shaders::DefaultVertex>> mesh_asset =
    //     std::make_shared<VectorMeshHolder<Shaders::DefaultVertex>>(
    //         VectorMeshHolder<Shaders::DefaultVertex>{cube_vertex,
    //         cube_index});
    //
    // VertexComponent cubeVertexComp{
    //     .vertices = std::as_bytes(std::span(mesh_asset->vertices)),
    //     .indices = mesh_asset->indices,
    //     .stride = sizeof(Shaders::DefaultVertex),
    //     .life_support = mesh_asset};

    static constexpr auto cube_vertices =
        MeshUtil::create_static_vertices<cube_vertex_count>(
            cube_positions, cube_normals, cube_texcoords);
    static constexpr auto cube_index =
        MeshUtil::create_static_indices<cube_index_count>(cube_indices);
    VertexComponent cubeVertexComp{.vertices =
                                       std::as_bytes(std::span(cube_vertices)),
                                   .indices = cube_index,
                                   .stride = sizeof(Shaders::DefaultVertex)};

    // ---------------------------------------------------------------------
    // エンティティの生成
    // ---------------------------------------------------------------------

    Transform planeTrans{.position = glm::vec3(0.0f, GROUND_Y, -7.5f)};
    Velocity planeVel{};
    VertexComponent
        planeVertexComp{}; // 描画はセッション側で静的におこなうため空でOK
    Collider planeCollider{
        .centerOffset = glm::vec3(0.0f),
        .halfExtents = glm::vec3(
            15.0f, 0.1f, 17.5f) // 幅30, 厚み0.2, 奥行き35 の巨大な壁を床化
    };
    // 床として世界に固定配置
    m_registry.create_entity(planeTrans, planeVel, Acceleration{},
                             planeVertexComp, TAG_GROUND, nullptr,
                             planeCollider);

    // 1. 通常キューブを30個生成 (コライダーを付与)
    for (int i = 0; i < 30; ++i) {
      float x = static_cast<float>((i % 6) - 3) * 3.0f;
      float z = static_cast<float>((i / 6) - 2) * -4.0f;
      float initialY = 4.0f + static_cast<float>(i % 3) *
                                  2.0f; // 初期高度を散らしてカオス感を出す

      Transform trans{.position = glm::vec3(x, initialY, z),
                      .rotation = glm::vec3(0.0f),
                      .scale = glm::vec3(0.5f)};

      // スケール0.5fに合わせたAABBハーフサイズ
      Collider cubeCollider{.centerOffset = glm::vec3(0.0f),
                            .halfExtents = glm::vec3(0.5f)};

      const auto new_id = m_registry.create_entity(
          trans, Velocity{}, Acceleration{}, cubeVertexComp, TAG_NORMAL_PHYSICS,
          nullptr, cubeCollider);
      m_registry.add_component<GravityTag>(new_id, GravityTag{});
    }

    // 2. 特殊な軌道運動を行うモデルを1個生成
    Transform specialTrans{.position = glm::vec3(0.0f, 0.0f, 0.0f),
                           .rotation = glm::vec3(0.0f),
                           .scale = glm::vec3(1.5f)};

    // 円軌道ビヘイビアを生成 (中心: (0, 3, -5), 半径: 6.0, 速度: 1.0)
    const auto orbit_id =
        m_registry.create_entity(specialTrans, Velocity{}, Acceleration{},
                                 cubeVertexComp, TAG_SPECIAL_ORBIT);
    m_registry.add_component<OrbitComponent>(
        orbit_id, OrbitComponent{glm::vec3(0.0f, 3.0f, -5.0f), 6.0f, 1.0f});
  }

  EcsGameWorld() {
    std::cout << "[ECS Engine] Initializing EcsGameWorld..." << std::endl;

    // ---------------------------------------------------------------------
    // デフォルトシステムの登録 (物理移動と衝突判定)
    // ---------------------------------------------------------------------
    m_registry.add_default_systems();

    // 2. 外部重力システム: TAG_NORMAL_PHYSICS のみに重力を適用
    m_registry.add_system([](Registry &reg, float dt) {
      glm::vec3 gravity(0.0f, -9.8f, 0.0f);
      for (auto &&[tag, accel] : reg.view<GravityTag, Acceleration>()) {
        accel.value = gravity;
      }
    });

    // 3. プレイヤー入力処理システム
    m_registry.add_system([this](Registry &reg, float dt) {
      for (auto &&[inputComp, trans] :
           reg.view<PlayerInputComponent, Transform>()) {
        // 視線ベクトルの計算
        glm::vec3 front;
        front.x = cos(glm::radians(inputComp.yaw)) *
                  cos(glm::radians(inputComp.pitch));
        front.y = sin(glm::radians(inputComp.pitch));
        front.z = sin(glm::radians(inputComp.yaw)) *
                  cos(glm::radians(inputComp.pitch));
        glm::vec3 cameraFront = glm::normalize(front);
        glm::vec3 cameraRight = glm::normalize(
            glm::cross(cameraFront, glm::vec3(0.0f, 1.0f, 0.0f)));

        // 移動の適用
        float camSpeed = 5.0f * dt;
        if (glm::length(inputComp.move_intention) > 0.0f) {
          trans.position +=
              cameraRight * (inputComp.move_intention.x * camSpeed);
          trans.position += glm::vec3(0.0f, 1.0f, 0.0f) *
                            (inputComp.move_intention.y * camSpeed);
          trans.position +=
              cameraFront * (inputComp.move_intention.z * camSpeed);
        }

        // スペースキーでレイキャスト＆破壊
        if (inputComp.action_trigger_space) {
          auto hit = raycast_now(reg, trans.position, cameraFront, 100.0f);
          if (hit.hit && reg.is_valid(hit.entity)) {
            std::uint64_t tag = reg.get_component<std::uint64_t>(hit.entity);
            if ((tag & EcsGameWorld::OBJECT_TYPE_MASK) ==
                EcsGameWorld::TAG_NORMAL_PHYSICS) {
              reg.destroy_entity_deferred(hit.entity);
            }
          }
        }

        // Rキーでリセットリクエスト
        if (inputComp.action_trigger_r) {
          this->m_resetRequested = true;
        }
      }
    });

    // ---------------------------------------------------------------------
    // 【接触応答システム】
    // 標準判定が埋めた hitEntities
    // から床(TAG_GROUND)との接触を検知してVelocityを反転
    // ---------------------------------------------------------------------
    m_registry.add_system([](Registry &reg, float dt) {
      for (auto &&[tag, collider, vec, acc] :
           reg.view<GravityTag, Collider, Velocity, Acceleration>()) {

        if (collider.hitEntities.empty())
          continue;

        for (std::uint32_t hitId : collider.hitEntities) {
          // 衝突相手のタグを安全に確認
          if (reg.is_valid(hitId)) {
            std::uint64_t hitTag = reg.get_component<std::uint64_t>(hitId);

            // 相手が「床」であり、自分が現在落下中の場合のみ反発（反発係数1.0）
            if ((hitTag & OBJECT_TYPE_MASK) == TAG_GROUND &&
                vec.value.y < 0.0f) {
              acc.value = glm::vec3(0.0f);
              vec.value.y = std::abs(vec.value.y) * 1.0f;
              break; // 床との衝突処理が確定したらこのオブジェクトのチェックを抜ける
            }
          }
        }
      }
    });

    m_registry.add_system([](Registry &reg, float dt) {
      // OrbitComponent と Transform を持つエンティティだけを最速走査
      for (auto &&[orbit, trans] : reg.view<OrbitComponent, Transform>()) {

        orbit.ticks += dt;

        // XZ平面上での円軌道座標を計算
        trans.position.x =
            orbit.center.x + std::cos(orbit.ticks * orbit.speed) * orbit.radius;
        trans.position.z =
            orbit.center.z + std::sin(orbit.ticks * orbit.speed) * orbit.radius;
        trans.position.y = orbit.center.y;

        // 回転の更新
        trans.rotation.y -= 0.05f;
        trans.rotation.x = 0.0f;
        trans.rotation.z = 0.0f;
      }
    });

    create_entitys();
  }

  void processPlayerInput(int clientId, const InputDevice &input) override {}

  /**
   * @brief
   * ワールドの更新処理。登録されたシステムを一括アップデートします。
   */
  void globalUpdate(float delta_time) override {
    m_registry.update(delta_time);
    if (m_resetRequested) {
      resetWorld();
      m_resetRequested = false;
    }
  }

  /**
   * @brief ECSレジストリへの参照を取得します。
   */
  Registry &get_registry() { return m_registry; }
  const Registry &get_registry() const { return m_registry; }

  /**
   * @brief ワールドをリセットし、通常キューブを再生成します。
   */
  void resetWorld() {
    // 1. TAG_NORMAL_PHYSICS のエンティティをすべて集める
    std::vector<EntityId> to_destroy;
    auto &tags = m_registry.get_raw_data<std::uint64_t>();
    auto &entities = m_registry.get_raw_entities<std::uint64_t>();
    for (std::size_t i = 0; i < tags.size(); ++i) {
      if ((tags[i] & OBJECT_TYPE_MASK) == TAG_NORMAL_PHYSICS ||
          (tags[i] & OBJECT_TYPE_MASK) == TAG_SPECIAL_ORBIT) {
        to_destroy.push_back(entities[i]);
      }
    }

    // 2. 破壊する
    for (auto id : to_destroy) {
      m_registry.destroy_entity(id);
    }

    create_entitys();
  }
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

    world.onPlayerConnect(clientId);
  }

  void onDisconnect(EcsGameWorld &world) override {
    world.onPlayerDisconnect(m_clientId);
  }

  /**
   * @brief
   * 入力デバイスの入力状況に応じてカメラの位置と視線を移動・調整します。
   */
  void update(int clientId, const InputDevice &input,
              EcsGameWorld &world) override {
    auto it = world.player_entities.find(clientId);
    if (it == world.player_entities.end()) {
      return;
    }
    EntityId playerEntity = it->second;
    auto &reg = world.get_registry();
    if (!reg.is_valid(playerEntity))
      return;

    auto &inputComp = reg.get_component<PlayerInputComponent>(playerEntity);

    // J/L/I/K キーで視線回転
    float lookSpeed = 90.0f * input.getDeltaTime();
    if (input.getKey(Key::j) || input.getKey(Key::J))
      inputComp.yaw -= lookSpeed;
    if (input.getKey(Key::l) || input.getKey(Key::L))
      inputComp.yaw += lookSpeed;
    if (input.getKey(Key::i) || input.getKey(Key::I))
      inputComp.pitch += lookSpeed;
    if (input.getKey(Key::k) || input.getKey(Key::K))
      inputComp.pitch -= lookSpeed;

    if (inputComp.pitch > 89.0f)
      inputComp.pitch = 89.0f;
    if (inputComp.pitch < -89.0f)
      inputComp.pitch = -89.0f;

    // W/A/S/D/Q/E キーで移動インテンションを設定
    glm::vec3 move_intention{0.0f};
    if (input.getKey(Key::w) || input.getKey(Key::W))
      move_intention.z += 1.0f;
    if (input.getKey(Key::s) || input.getKey(Key::S))
      move_intention.z -= 1.0f;
    if (input.getKey(Key::a) || input.getKey(Key::A))
      move_intention.x -= 1.0f;
    if (input.getKey(Key::d) || input.getKey(Key::D))
      move_intention.x += 1.0f;
    if (input.getKey(Key::e) || input.getKey(Key::E))
      move_intention.y += 1.0f;
    if (input.getKey(Key::q) || input.getKey(Key::Q))
      move_intention.y -= 1.0f;
    inputComp.move_intention = move_intention;

    // アクションのトリガー
    inputComp.action_trigger_space = input.getKeyDown(Key::Space);
    inputComp.action_trigger_r =
        input.getKeyDown(Key::r) || input.getKeyDown(Key::R);
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

    glm::vec3 cameraPos = glm::vec3(0.0f, 10.0f, 12.0f);
    float yaw = -90.0f;
    float pitch = -25.0f;

    const auto &reg = world.get_registry();
    auto it = world.player_entities.find(m_clientId);
    if (it != world.player_entities.end()) {
      EntityId playerEntity = it->second;
      if (reg.is_valid(playerEntity)) {
        cameraPos = reg.get_component<Transform>(playerEntity).position;
        const auto &inputComp =
            reg.get_component<PlayerInputComponent>(playerEntity);
        yaw = inputComp.yaw;
        pitch = inputComp.pitch;
      }
    }

    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + glm::normalize(front),
                                 glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(0.8f, aspect, 0.1f, 100.0f);

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
      if (vComp.vertices.empty())
        continue;
      std::span<const Shaders::DefaultVertex> vertices(
          reinterpret_cast<const Shaders::DefaultVertex *>(
              vComp.vertices.data()),
          vComp.vertices.size() / vComp.stride);
      shadowPass.draw(vertices, vComp.indices,
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
      if (vComp.vertices.empty())
        continue;
      glm::mat4 model = trans.to_matrix();
      glm::mat4 mvp = proj * view * model;

      std::span<const Shaders::DefaultVertex> vertices(
          reinterpret_cast<const Shaders::DefaultVertex *>(
              vComp.vertices.data()),
          vComp.vertices.size() / vComp.stride);
      geometryPass.draw(vertices, vComp.indices,
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
