#pragma once

#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include <astream/ConnectionContext.hpp>
#include <astream/GameWorld.hpp>
#include <astream/GraphicsDevice.hpp>
#include <astream/MeshView.hpp>
#include <astream/Texture2D.hpp>
#include <astream/Transform.hpp>
#include <astream/shaders/DefaultShaders.hpp>

#include <astream/util/MeshUtil.hpp>
#include <astream/util/TextureUtil.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <entt/entt.hpp>

// Jolt Physics Headers
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "cube.hpp" // cube.obj から自動生成

using astream::MeshView;
using astream::Transform;

#include "EcsPhysics.hpp"

// =============================================================================
// EcsGameWorld 実装 (EnTT + Jolt Physics)
// =============================================================================
class EcsGameWorld : public GameWorld {
private:
  entt::registry m_registry;

  // Jolt 物理エンジンのコンポーネント
  JPH::TempAllocatorImpl *m_tempAllocator = nullptr;
  JPH::JobSystemThreadPool *m_jobSystem = nullptr;
  BPLayerInterfaceImpl m_bpLayerInterface;
  ObjectVsBroadPhaseLayerFilterImpl m_objVsBpFilter;
  ObjectLayerPairFilterImpl m_objVsObjFilter;
  JPH::PhysicsSystem m_physicsSystem;

  JPH::ShapeRefC m_cubeShape;
  JPH::ShapeRefC m_floorShape;

  std::vector<entt::entity> deferred_destroy_queue;

public:
  std::unordered_map<int, entt::entity> player_entities;
  bool m_resetRequested = false;

  static constexpr float GROUND_Y = -2.0f;

  EcsGameWorld() {
    std::cout << "[EnTT + Jolt Physics] Initializing EcsGameWorld..."
              << std::endl;

    // Jolt 物理エンジンのグローバル初期化
    static bool joltInitialized = false;
    if (!joltInitialized) {
      JPH::RegisterDefaultAllocator();
      JPH::Factory::sInstance = new JPH::Factory();
      JPH::RegisterTypes();
      joltInitialized = true;
    }

    m_tempAllocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    m_jobSystem = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        std::thread::hardware_concurrency() - 1);

    const JPH::uint cMaxBodies = 1024;
    const JPH::uint cNumBodyMutexes = 0;
    const JPH::uint cMaxBodyPairs = 1024;
    const JPH::uint cMaxContactConstraints = 1024;

    m_physicsSystem.Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs,
                         cMaxContactConstraints, m_bpLayerInterface,
                         m_objVsBpFilter, m_objVsObjFilter);

    // 重力の設定 (-9.8 m/s^2)
    m_physicsSystem.SetGravity(JPH::Vec3(0.0f, -9.8f, 0.0f));

    // 形状アセットの事前ロード
    JPH::BoxShapeSettings floorShapeSettings(JPH::Vec3(15.0f, 0.1f, 17.5f));
    floorShapeSettings.SetEmbedded();
    m_floorShape = floorShapeSettings.Create().Get();

    JPH::BoxShapeSettings cubeShapeSettings(JPH::Vec3(0.5f, 0.5f, 0.5f));
    cubeShapeSettings.SetEmbedded();
    m_cubeShape = cubeShapeSettings.Create().Get();

    // 床 (静的コライダー) のセットアップ
    setupFloor();

    // 動的オブジェクトの生成
    create_entitys();
  }

  ~EcsGameWorld() override {
    // 物理剛体の削除
    auto &bodyInterface = m_physicsSystem.GetBodyInterface();
    m_registry.view<PhysicsBodyComponent>().each(
        [&](auto entity, auto &bodyComp) {
          if (!bodyComp.bodyId.IsInvalid()) {
            bodyInterface.RemoveBody(bodyComp.bodyId);
            bodyInterface.DestroyBody(bodyComp.bodyId);
          }
        });

    delete m_tempAllocator;
    delete m_jobSystem;
  }

  void setupFloor() {
    auto &bodyInterface = m_physicsSystem.GetBodyInterface();
    JPH::BodyCreationSettings floorSettings(
        m_floorShape, JPH::RVec3(0.0f, GROUND_Y, -7.5f), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::NON_MOVING);
    floorSettings.mRestitution = 1.0f; // バウンドさせるための高反発設定
    floorSettings.mFriction = 0.2f;

    JPH::Body *floorBody = bodyInterface.CreateBody(floorSettings);
    bodyInterface.AddBody(floorBody->GetID(), JPH::EActivation::DontActivate);

    auto floorEntity = m_registry.create();
    m_registry.emplace<Transform>(
        floorEntity, Transform{.position = glm::vec3(0.0f, GROUND_Y, -7.5f)});
    m_registry.emplace<MeshView>(floorEntity, MeshView{});
    m_registry.emplace<ObjectTypeComponent>(
        floorEntity, ObjectTypeComponent{ObjectType::Ground});
    m_registry.emplace<PhysicsBodyComponent>(
        floorEntity, PhysicsBodyComponent{floorBody->GetID()});
  }

  void create_entitys() {
    static constexpr auto cube_vertices =
        MeshUtil::create_static_vertices<cube_vertex_count>(
            cube_positions, cube_normals, cube_texcoords);
    static constexpr auto cube_index =
        MeshUtil::create_static_indices<cube_index_count>(cube_indices);
    MeshView cubeVertexComp{.vertices = std::as_bytes(std::span(cube_vertices)),
                            .indices = cube_index,
                            .stride = sizeof(Shaders::DefaultVertex)};

    auto &bodyInterface = m_physicsSystem.GetBodyInterface();

    // 1. 通常キューブを30個生成 (物理エンジンに登録)
    for (int i = 0; i < 30; ++i) {
      float x = static_cast<float>((i % 6) - 3) * 3.0f;
      float z = static_cast<float>((i / 6) - 2) * -4.0f;
      float initialY = 4.0f + static_cast<float>(i % 3) * 2.0f;

      JPH::BodyCreationSettings cubeSettings(
          m_cubeShape, JPH::RVec3(x, initialY, z), JPH::Quat::sIdentity(),
          JPH::EMotionType::Dynamic, Layers::MOVING);
      cubeSettings.mRestitution = 1.0f; // 反発係数1.0
      cubeSettings.mFriction = 0.2f;

      JPH::Body *cubeBody = bodyInterface.CreateBody(cubeSettings);
      bodyInterface.AddBody(cubeBody->GetID(), JPH::EActivation::Activate);

      auto new_id = m_registry.create();
      m_registry.emplace<Transform>(
          new_id, Transform{.position = glm::vec3(x, initialY, z),
                            .rotation = glm::vec3(0.0f),
                            .scale = glm::vec3(0.5f)});
      m_registry.emplace<MeshView>(new_id, cubeVertexComp);
      m_registry.emplace<ObjectTypeComponent>(
          new_id, ObjectTypeComponent{ObjectType::NormalPhysics});
      m_registry.emplace<PhysicsBodyComponent>(
          new_id, PhysicsBodyComponent{cubeBody->GetID()});
    }

    // 2. 特殊な軌道運動を行うモデルを1個生成
    Transform specialTrans{.position = glm::vec3(0.0f, 0.0f, 0.0f),
                           .rotation = glm::vec3(0.0f),
                           .scale = glm::vec3(1.5f)};

    auto orbit_id = m_registry.create();
    m_registry.emplace<Transform>(orbit_id, specialTrans);
    m_registry.emplace<MeshView>(orbit_id, cubeVertexComp);
    m_registry.emplace<ObjectTypeComponent>(
        orbit_id, ObjectTypeComponent{ObjectType::SpecialOrbit});
    m_registry.emplace<OrbitComponent>(
        orbit_id, OrbitComponent{glm::vec3(0.0f, 3.0f, -5.0f), 6.0f, 1.0f});
  }

  void onPlayerConnect(int clientId) {
    Transform trans{.position = glm::vec3(0.0f, 10.0f, 12.0f),
                    .rotation = glm::vec3(0.0f),
                    .scale = glm::vec3(1.0f)};
    auto id = m_registry.create();
    m_registry.emplace<Transform>(id, trans);
    m_registry.emplace<ObjectTypeComponent>(
        id, ObjectTypeComponent{ObjectType::Player});
    m_registry.emplace<PlayerInputComponent>(
        id, PlayerInputComponent{
                .clientId = clientId, .yaw = -90.0f, .pitch = -25.0f});
    player_entities[clientId] = id;
  }

  void onPlayerDisconnect(int clientId) {
    auto it = player_entities.find(clientId);
    if (it != player_entities.end()) {
      m_registry.destroy(it->second);
      player_entities.erase(it);
    }
  }

  void processPlayerInput(int clientId, const InputDevice &input) override {}

  void globalUpdate(float delta_time) override {
    // 1. Jolt 物理演算の更新
    m_physicsSystem.Update(delta_time, 1, m_tempAllocator, m_jobSystem);

    // 2. Jolt 剛体結果から Transform への同期
    auto &bodyInterface = m_physicsSystem.GetBodyInterface();
    m_registry.view<PhysicsBodyComponent, Transform>().each(
        [&](auto entity, auto &bodyComp, auto &trans) {
          if (!bodyComp.bodyId.IsInvalid() &&
              bodyInterface.IsActive(bodyComp.bodyId)) {
            JPH::RVec3 pos =
                bodyInterface.GetCenterOfMassPosition(bodyComp.bodyId);
            JPH::Quat rot = bodyInterface.GetRotation(bodyComp.bodyId);
            trans.position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
            glm::quat q(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
            trans.rotation = glm::eulerAngles(q);
          }
        });

    // 3. プレイヤー入力処理システム
    m_registry.view<PlayerInputComponent, Transform>().each([&](auto entity,
                                                                auto &inputComp,
                                                                auto &trans) {
      glm::vec3 front;
      front.x =
          cos(glm::radians(inputComp.yaw)) * cos(glm::radians(inputComp.pitch));
      front.y = sin(glm::radians(inputComp.pitch));
      front.z =
          sin(glm::radians(inputComp.yaw)) * cos(glm::radians(inputComp.pitch));
      glm::vec3 cameraFront = glm::normalize(front);
      glm::vec3 cameraRight =
          glm::normalize(glm::cross(cameraFront, glm::vec3(0.0f, 1.0f, 0.0f)));

      float camSpeed = 5.0f * delta_time;
      if (glm::length(inputComp.move_intention) > 0.0f) {
        trans.position += cameraRight * (inputComp.move_intention.x * camSpeed);
        trans.position += glm::vec3(0.0f, 1.0f, 0.0f) *
                          (inputComp.move_intention.y * camSpeed);
        trans.position += cameraFront * (inputComp.move_intention.z * camSpeed);
      }

      // スペースキーでレイキャスト＆破壊
      if (inputComp.action_trigger_space) {
        JPH::RRayCast ray{
            JPH::RVec3(trans.position.x, trans.position.y, trans.position.z),
            JPH::Vec3(cameraFront.x * 100.0f, cameraFront.y * 100.0f,
                      cameraFront.z * 100.0f)};
        JPH::RayCastResult hit;
        if (m_physicsSystem.GetNarrowPhaseQuery().CastRay(ray, hit)) {
          JPH::BodyID hitBodyId = hit.mBodyID;
          entt::entity targetEntity = entt::null;
          m_registry.view<PhysicsBodyComponent>().each(
              [&](auto ent, auto &bComp) {
                if (bComp.bodyId == hitBodyId) {
                  targetEntity = ent;
                }
              });
          if (targetEntity != entt::null && m_registry.valid(targetEntity)) {
            auto &typeComp = m_registry.get<ObjectTypeComponent>(targetEntity);
            if (typeComp.type == ObjectType::NormalPhysics) {
              deferred_destroy_queue.push_back(targetEntity);
            }
          }
        }
      }

      if (inputComp.action_trigger_r) {
        this->m_resetRequested = true;
      }
    });

    // 4. OrbitComponent更新システム
    m_registry.view<OrbitComponent, Transform>().each(
        [&](auto entity, auto &orbit, auto &trans) {
          orbit.ticks += delta_time;
          trans.position.x = orbit.center.x +
                             std::cos(orbit.ticks * orbit.speed) * orbit.radius;
          trans.position.z = orbit.center.z +
                             std::sin(orbit.ticks * orbit.speed) * orbit.radius;
          trans.position.y = orbit.center.y;
          trans.rotation.y -= 0.05f;
          trans.rotation.x = 0.0f;
          trans.rotation.z = 0.0f;
        });

    // 遅延破壊の処理
    for (auto ent : deferred_destroy_queue) {
      if (m_registry.valid(ent)) {
        if (m_registry.all_of<PhysicsBodyComponent>(ent)) {
          auto &bComp = m_registry.get<PhysicsBodyComponent>(ent);
          if (!bComp.bodyId.IsInvalid()) {
            bodyInterface.RemoveBody(bComp.bodyId);
            bodyInterface.DestroyBody(bComp.bodyId);
          }
        }
        m_registry.destroy(ent);
      }
    }
    deferred_destroy_queue.clear();

    if (m_resetRequested) {
      resetWorld();
      m_resetRequested = false;
    }
  }

  entt::registry &get_registry() { return m_registry; }
  const entt::registry &get_registry() const { return m_registry; }

  void resetWorld() {
    auto &bodyInterface = m_physicsSystem.GetBodyInterface();
    std::vector<entt::entity> to_destroy;
    m_registry.view<ObjectTypeComponent>().each(
        [&](auto entity, auto &typeComp) {
          if (typeComp.type == ObjectType::NormalPhysics ||
              typeComp.type == ObjectType::SpecialOrbit) {
            to_destroy.push_back(entity);
          }
        });

    for (auto ent : to_destroy) {
      if (m_registry.all_of<PhysicsBodyComponent>(ent)) {
        auto &bComp = m_registry.get<PhysicsBodyComponent>(ent);
        if (!bComp.bodyId.IsInvalid()) {
          bodyInterface.RemoveBody(bComp.bodyId);
          bodyInterface.DestroyBody(bComp.bodyId);
        }
      }
      m_registry.destroy(ent);
    }

    create_entitys();
  }
};

// =============================================================================
// 各クライアントごとの個別描画セッション (EnTT への移行対応)
// =============================================================================
class EcsPlayerSession : public ConnectionContext<EcsGameWorld> {
private:
  int m_clientId = -1;
  int w = 80, h = 24;
  float aspect = 1.0f;

  GraphicsDevice device;
  std::optional<Texture2D<float>> cameraDepth;
  std::optional<Texture2D<float>> shadowDepth;
  std::optional<Texture2D<char>> albedoBuffer;
  std::optional<Texture2D<glm::vec3>> normalBuffer;
  std::optional<Texture2D<glm::vec3>> worldPosBuffer;

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

    glm::mat4 lightView =
        glm::lookAt(lightDir * 20.0f, glm::vec3(0, 0, -5), glm::vec3(0, 1, 0));
    glm::mat4 lightProj =
        glm::ortho(-15.0f * aspect, 15.0f * aspect, -15.0f, 15.0f, 0.1f, 50.0f);
    lightSpaceMatrix = lightProj * lightView;

    cameraDepth.emplace(w, h, std::numeric_limits<float>::max());
    shadowDepth.emplace(sw, sh, std::numeric_limits<float>::max());
    albedoBuffer.emplace(w, h, 0);
    normalBuffer.emplace(w, h, glm::vec3(0, 0, 0));
    worldPosBuffer.emplace(w, h, glm::vec3(0, 0, 0));

    world.onPlayerConnect(clientId);
  }

  void onDisconnect(EcsGameWorld &world) override {
    world.onPlayerDisconnect(m_clientId);
  }

  void update(int clientId, const InputDevice &input,
              EcsGameWorld &world) override {
    auto it = world.player_entities.find(clientId);
    if (it == world.player_entities.end()) {
      return;
    }
    entt::entity playerEntity = it->second;
    auto &reg = world.get_registry();
    if (!reg.valid(playerEntity))
      return;

    auto &inputComp = reg.get<PlayerInputComponent>(playerEntity);

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

    inputComp.action_trigger_space = input.getKeyDown(Key::Space);
    inputComp.action_trigger_r =
        input.getKeyDown(Key::r) || input.getKeyDown(Key::R);
  }

  void render(TextureView<char> outputTexture,
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
      entt::entity playerEntity = it->second;
      if (reg.valid(playerEntity)) {
        cameraPos = reg.get<Transform>(playerEntity).position;
        const auto &inputComp = reg.get<PlayerInputComponent>(playerEntity);
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

    // 1. シャドウマップ生成パス
    auto shadowPass =
        device.create_rasterize_pass<Shaders::DefaultVertex, glm::vec3, float>(
            shadowDepth->view());
    glm::mat4 identityModel(1.0f);
    shadowPass.draw(
        planeVertices, planeIndices,
        std::bind_back(Shaders::shadowVS, identityModel, lightSpaceMatrix));

    // EnTT のビュー走査
    auto enttView = reg.view<const Transform, const MeshView>();
    for (auto entity : enttView) {
      const auto &trans = enttView.get<const Transform>(entity);
      const auto &vComp = enttView.get<const MeshView>(entity);
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
    auto geometryPass =
        device.create_rasterize_pass<Shaders::DefaultVertex,
                                     Shaders::DefaultVarying, float>(
            cameraDepth->view());
    glm::mat4 planeMVP = proj * view * identityModel;
    geometryPass.draw(
        planeVertices, planeIndices,
        std::bind_back(Shaders::geometryVS, identityModel, planeMVP),
        [&](int x, int y, const Shaders::DefaultVarying &in) {
          albedoBuffer->at(x, y) = 127;
          normalBuffer->at(x, y) = in.normal;
          worldPosBuffer->at(x, y) = in.worldPos;
        });

    for (auto entity : enttView) {
      const auto &trans = enttView.get<const Transform>(entity);
      const auto &vComp = enttView.get<const MeshView>(entity);
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
        std::bind_back(Shaders::deferredLightingCS, outputTexture,
                       albedoBuffer->view(), normalBuffer->view(),
                       worldPosBuffer->view(), shadowDepth->view(),
                       lightSpaceMatrix, lightDir, w, h));

    auto renderEnd = std::chrono::steady_clock::now();
    double renderTimeMs =
        std::chrono::duration<double, std::milli>(renderEnd - renderStart)
            .count();

    // 4. デバッグ情報のHUDテキスト描画
    std::string debugStr =
        std::format("EnTT+Jolt ECS | Total Entities: {}\nFPS: "
                    "{:.1f} ({:.2f} ms)\nRender: {:.2f} ms",
                    reg.view<Transform>().size(),
                    (frameDeltaMs > 0.0 ? 1000.0 / frameDeltaMs : 0.0),
                    frameDeltaMs, renderTimeMs);

    Texture2D<char> textTex = TextureUtil::strToTexture(debugStr, ' ');
    TextureUtil::blit_texture(outputTexture, textTex, 0, 0);

    // TextureViewのサンプル
    TextureUtil::drawText(outputTexture.subView(0, 3, 20, 2), 0, 0,
                          "TextureView is good");
  }
};
