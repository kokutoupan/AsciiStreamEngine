#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

// 数学ライブラリとしてGLMを使用
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Registry;
class EntityBehavior;

// =============================================================================
// 1. 基本コンポーネントの定義
// =============================================================================

/**
 * @brief
 * エンティティの位置、回転、スケールを表すトランスフォームコンポーネント。
 */
struct Transform {
  glm::vec3 position{0.0f}; ///< 位置 (X, Y, Z)
  glm::vec3 rotation{0.0f}; ///< 回転 (ラジアン角: ピッチ, ヨー, ロール)
  glm::vec3 scale{1.0f};    ///< スケール (X, Y, Z)

  /**
   * @brief トランスフォーム情報から変換行列 (Model Matrix) を計算します。
   *
   * 単位行列に対して 移動 -> 回転 (Z -> Y -> X の順) -> 拡大縮小
   * の順に適用します。
   *
   * @return glm::mat4 4x4変換行列
   */
  glm::mat4 to_matrix() const {
    glm::mat4 m(1.0f);

    // 1. 移動 (Translation)
    m = glm::translate(m, position);

    // 2. 回転 (Rotation) - Z(ロール) -> Y(ヨー) -> X(ピッチ) の順で回転を適用
    m = glm::rotate(m, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::rotate(m, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));

    // 3. 拡大縮小 (Scale)
    m = glm::scale(m, scale);

    return m;
  }
};

/**
 * @brief エンティティの速度を表すコンポーネント。
 */
struct Velocity {
  glm::vec3 value{0.0f}; ///< 速度ベクトル
};

/**
 * @brief
 * ユーザー定義の頂点バッファアセットを型消去して一律に扱うための基底インターフェース。
 */
class IBufferAsset {
public:
  virtual ~IBufferAsset() = default;
};

/**
 * @brief ゲーム側やAIが定義した具体的な頂点型 (InputVertex)
 * を保持するメッシュアセットの具象クラス。
 * @tparam InputVertex ユーザー定義の頂点構造体
 */
template <typename InputVertex> struct MeshAsset : public IBufferAsset {
  std::vector<InputVertex> vertices; ///< 頂点データ配列
  std::vector<int> indices;          ///< インデックスデータ配列
};

/**
 * @brief メッシュアセットの参照を保持するコンポーネント。
 */
struct VertexComponent {
  std::shared_ptr<IBufferAsset> asset =
      nullptr; ///< 型消去されたメッシュアセットへの共有ポインタ

  /**
   * @brief アセットを指定した具体的な頂点型の MeshAsset にキャストします。
   * @tparam InputVertex キャスト先の頂点構造体
   * @return const MeshAsset<InputVertex>& キャストされたアセットへの定数参照
   */
  template <typename InputVertex> const MeshAsset<InputVertex> &cast() const {
    return *static_cast<const MeshAsset<InputVertex> *>(asset.get());
  }
};

/**
 * @brief
 * エンティティの動的な振る舞い（スクリプト動作）を定義する基底インターフェース。
 */
class EntityBehavior {
public:
  virtual ~EntityBehavior() = default;

  /**
   * @brief 毎フレーム実行される更新処理。
   *
   * Registry の前方参照定義の後に実装されているため、Registry
   * の実体を安全に操作できます。
   *
   * @param index プール内でのインデックス
   * @param entityId 対象のエンティティID
   * @param reg 呼び出し元のレジストリ実体
   * @param dt 前フレームからの経過時間 (デルタタイム)
   */
  virtual void update(std::size_t index, std::uint32_t entityId, Registry &reg,
                      float dt) = 0;
};

/**
 * @brief エンティティに動的な振る舞いを紐付けるスクリプトコンポーネント。
 */
struct ScriptComponent {
  std::unique_ptr<EntityBehavior> behavior =
      nullptr; ///< 振る舞いを定義するオブジェクトへの所有ポインタ
};

/**
 * @brief 衝突判定を行うためのAABB (Axis-Aligned Bounding Box) コライダー。
 */
struct Collider {
  glm::vec3 centerOffset{0.0f}; ///< 親トランスフォームの位置からのオフセット
  glm::vec3 halfExtents{
      0.0f}; ///< AABBの各軸の半幅サイズ (幅/2, 高さ/2, 奥行き/2)

  std::vector<std::uint32_t>
      hitEntities; ///< このフレームで接触した他エンティティのIDリスト
};

// =============================================================================
// 2. スパースセット・ストレージ基盤 (型消去)
// =============================================================================

/**
 * @brief 型消去されたコンポーネントプールの基底インターフェース。
 */
class IPool {
public:
  virtual ~IPool() = default;

  /**
   * @brief
   * 指定したエンティティに関連付けられたコンポーネントをプールから削除します。
   * @param entity_idx 削除対象のエンティティインデックス
   */
  virtual void remove_entity(std::uint32_t entity_idx) = 0;
};

/**
 * @brief スパースセット (Sparse Set) を用いたコンポーネントのストレージプール。
 *
 * メモリの連続性を保ちつつ、O(1)
 * でのランダムアクセス、挿入、削除をサポートします。
 * @tparam T 格納するコンポーネントの型
 */
template <typename T> class ComponentPool : public IPool {
private:
  std::vector<T> dense_data; ///< 密配列: コンポーネントの実体を連続メモリに格納
  std::vector<std::uint32_t> dense_to_entity; ///< 密から粗へのインデックス写像
                                              ///< (dense_idx -> entity_idx)
  std::vector<std::uint32_t> entity_to_dense; ///< 粗から密へのインデックス写像
                                              ///< (entity_idx -> dense_idx)

public:
  /**
   * @brief
   * エンティティにコンポーネントを割り当てます。既に存在する場合は上書きされます。
   * @param entity_idx 対象のエンティティインデックス
   * @param component 割り当てるコンポーネントの実体
   */
  void assign(std::uint32_t entity_idx, T component) {
    if (entity_idx >= entity_to_dense.size()) {
      entity_to_dense.resize(entity_idx + 1, 0xFFFFFFFF);
    }
    entity_to_dense[entity_idx] = static_cast<std::uint32_t>(dense_data.size());
    dense_to_entity.push_back(entity_idx);
    dense_data.push_back(std::move(component));
  }

  /**
   * @brief 指定したエンティティのコンポーネントを削除します。
   * @param entity_idx 対象のエンティティインデックス
   */
  void remove(std::uint32_t entity_idx) {
    if (entity_idx >= entity_to_dense.size() ||
        entity_to_dense[entity_idx] == 0xFFFFFFFF)
      return;

    std::uint32_t target_dense_idx = entity_to_dense[entity_idx];
    std::uint32_t last_dense_idx =
        static_cast<std::uint32_t>(dense_data.size() - 1);

    // 順序を保持しない高速削除 (末尾要素と入れ替えて pop_back)
    if (target_dense_idx != last_dense_idx) {
      dense_data[target_dense_idx] = std::move(dense_data[last_dense_idx]);
      std::uint32_t moved_entity = dense_to_entity[last_dense_idx];
      dense_to_entity[target_dense_idx] = moved_entity;
      entity_to_dense[moved_entity] = target_dense_idx;
    }

    dense_data.pop_back();
    dense_to_entity.pop_back();
    entity_to_dense[entity_idx] = 0xFFFFFFFF;
  }

  /**
   * @brief
   * 指定したエンティティに関連付けられたコンポーネントをプールから削除します
   * (インターフェース実装)。
   * @param entity_idx 削除対象のエンティティインデックス
   */
  void remove_entity(std::uint32_t entity_idx) override { remove(entity_idx); }

  /**
   * @brief
   * 指定したエンティティがこのプールにコンポーネントを持っているか判定します。
   * @param entity_idx 対象のエンティティインデックス
   * @return true 持っている場合
   * @return false 持っていない場合
   */
  bool has(std::uint32_t entity_idx) const {
    if (entity_idx >= entity_to_dense.size())
      return false;
    return entity_to_dense[entity_idx] != 0xFFFFFFFF;
  }

  /**
   * @brief 指定したエンティティに関連付けられたコンポーネントを取得します。
   * @param entity_idx 対象のエンティティインデックス
   * @return T& コンポーネントへの参照
   */
  T &get(std::uint32_t entity_idx) {
    return dense_data[entity_to_dense[entity_idx]];
  }

  /**
   * @brief
   * 指定したエンティティに関連付けられたコンポーネントを定数参照で取得します。
   * @param entity_idx 対象のエンティティインデックス
   * @return const T& コンポーネントへの定数参照
   */
  const T &get(std::uint32_t entity_idx) const {
    return dense_data[entity_to_dense[entity_idx]];
  }

  /**
   * @brief 格納されているコンポーネントの密配列を取得します。
   * @return std::vector<T>& 密配列への参照
   */
  std::vector<T> &get_data() { return dense_data; }

  /**
   * @brief 格納されているコンポーネントの密配列を定数参照で取得します。
   * @return const std::vector<T>& 密配列への定数参照
   */
  const std::vector<T> &get_data() const { return dense_data; }

  /**
   * @brief
   * コンポーネントが割り当てられているエンティティIDの密配列を取得します。
   * @return const std::vector<std::uint32_t>& エンティティID配列への参照
   */
  const std::vector<std::uint32_t> &get_entities() const {
    return dense_to_entity;
  }
};

// =============================================================================
// 3. レジストリ (コアエンジンインターフェース)
// =============================================================================

/**
 * @brief
 * エンティティとコンポーネントのライフサイクルおよびシステムを統合管理するレジストリクラス。
 */
class Registry {
private:
  std::unordered_map<std::type_index, std::unique_ptr<IPool>>
      pools; ///< 型ごとのコンポーネントプール

  // C++23 の std::move_only_function
  // を使用し、コピー不可なキャプチャ付きラムダも高速かつ安全に保持します
  std::vector<std::move_only_function<void(Registry &, float dt)>>
      systems; ///< 登録されたシステム関数のリスト

  std::uint32_t next_entity_id = 0;    ///< 次に割り当てる新規エンティティID
  std::vector<std::uint32_t> free_ids; ///< 再利用可能な解放済みエンティティID

  /**
   * @brief 使用可能なエンティティIDを生成または再利用キューから取得します。
   * @return std::uint32_t 生成または再利用されたエンティティID
   */
  std::uint32_t generate_raw_id() {
    if (!free_ids.empty()) {
      auto id = free_ids.back();
      free_ids.pop_back();
      return id;
    }
    return next_entity_id++;
  }

public:
  Registry() = default;
  ~Registry() = default;

  // コピー禁止、ムーブのみ許可
  Registry(const Registry &) = delete;
  Registry &operator=(const Registry &) = delete;
  Registry(Registry &&) noexcept = default;
  Registry &operator=(Registry &&) noexcept = default;

  /**
   * @brief 指定した型のコンポーネントプールを取得または新規生成します。
   * @tparam T コンポーネントの型
   * @return ComponentPool<T>& プールへの参照
   */
  template <typename T> ComponentPool<T> &get_pool() {
    auto type_idx = std::type_index(typeid(T));
    if (!pools.contains(type_idx)) {
      pools[type_idx] = std::make_unique<ComponentPool<T>>();
    }
    return *static_cast<ComponentPool<T> *>(pools[type_idx].get());
  }

  /**
   * @brief
   * 指定した型のコンポーネントプールを定数参照で取得します。存在しない場合は例外をスローします。
   * @tparam T コンポーネントの型
   * @return const ComponentPool<T>& プールへの定数参照
   * @throws std::runtime_error プールが存在しない場合
   */
  template <typename T> const ComponentPool<T> &get_pool() const {
    auto type_idx = std::type_index(typeid(T));
    auto it = pools.find(type_idx);
    if (it == pools.end()) {
      throw std::runtime_error("Requested pool does not exist.");
    }
    return *static_cast<const ComponentPool<T> *>(it->second.get());
  }

  /**
   * @brief
   * 基本コンポーネント群およびコライダーを紐付けたエンティティを一括生成します。
   *
   * @param transform 初期トランスフォーム
   * @param velocity 初期速度
   * @param vertex 頂点アセット情報
   * @param tag ユーザー定義のタグ値 (デフォルトは 0)
   * @param behavior エンティティの動的動作 (デフォルトは nullptr)
   * @param collider コライダー設定 (デフォルトは空コライダー)
   * @return std::uint32_t 生成されたエンティティID
   */
  std::uint32_t
  create_entity(Transform transform, Velocity velocity, VertexComponent vertex,
                std::uint64_t tag = 0,
                std::unique_ptr<EntityBehavior> behavior = nullptr,
                Collider collider = Collider{}) {
    std::uint32_t id = generate_raw_id();

    // 各コンポーネントを一括して割り当て
    add_component<Transform>(id, std::move(transform));
    add_component<Velocity>(id, std::move(velocity));
    add_component<VertexComponent>(id, std::move(vertex));
    add_component<std::uint64_t>(id, tag);
    add_component<ScriptComponent>(id, ScriptComponent{std::move(behavior)});
    add_component<Collider>(id, std::move(collider));

    return id;
  }

  /**
   * @brief
   * エンティティを動的に削除し、全プールから関連コンポーネントを剥ぎ取り、IDを再利用可能にします。
   * @param entity_id 削除対象のエンティティID
   */
  void destroy_entity(std::uint32_t entity_id) {
    for (auto &[_, pool] : pools) {
      pool->remove_entity(entity_id);
    }
    free_ids.push_back(entity_id);
  }

  /**
   * @brief 指定したエンティティに任意のコンポーネントを追加します。
   * @tparam T コンポーネントの型
   * @param entity_id 対象のエンティティID
   * @param component 追加するコンポーネント実体
   */
  template <typename T>
  void add_component(std::uint32_t entity_id, T component) {
    get_pool<T>().assign(entity_id, std::move(component));
  }

  /**
   * @brief 指定したエンティティから任意のコンポーネントを削除します。
   * @tparam T コンポーネントの型
   * @param entity_id 対象のエンティティID
   */
  template <typename T> void remove_component(std::uint32_t entity_id) {
    get_pool<T>().remove(entity_id);
  }

  /**
   * @brief 指定したエンティティのコンポーネントを取得します。
   * @tparam T コンポーネントの型
   * @param entity_id 対象のエンティティID
   * @return T& コンポーネントへの参照
   */
  template <typename T> T &get_component(std::uint32_t entity_id) {
    return get_pool<T>().get(entity_id);
  }

  /**
   * @brief 指定したエンティティのコンポーネントを定数参照で取得します。
   * @tparam T コンポーネントの型
   * @param entity_id 対象のエンティティID
   * @return const T& コンポーネントへの定数参照
   */
  template <typename T> const T &get_component(std::uint32_t entity_id) const {
    return get_pool<T>().get(entity_id);
  }

  /**
   * @brief 指定したエンティティが対象コンポーネントを持っているか判定します。
   * @tparam T コンポーネントの型
   * @param entity_id 対象のエンティティID
   * @return true コンポーネントを持っている場合
   * @return false コンポーネントを持っていない場合
   */
  template <typename T> bool has_component(std::uint32_t entity_id) const {
    auto type_idx = std::type_index(typeid(T));
    if (!pools.contains(type_idx))
      return false;
    return get_pool<T>().has(entity_id);
  }

  /**
   * @brief 更新時に実行されるカスタムシステム関数を登録します。
   * @tparam Func コールバック関数の型
   * @param system_func 登録するシステム関数 (Registry &
   * を引数に取る関数オブジェクト)
   */
  template <typename Func> void add_system(Func &&system_func) {
    systems.emplace_back(std::forward<Func>(system_func));
  }

  /**
   * @brief 指定したコンポーネント型の内部密配列 (連続バッファ) を取得します。
   * @tparam T コンポーネントの型
   * @return auto& コンポーネント配列への参照
   */
  template <typename T> auto &get_raw_data() {
    return get_pool<T>().get_data();
  }

  /**
   * @brief 指定したコンポーネント型の内部密配列 (連続バッファ)
   * を定数参照で取得します。
   * @tparam T コンポーネントの型
   * @return const auto& コンポーネント配列への定数参照
   */
  template <typename T> const auto &get_raw_data() const {
    return get_pool<T>().get_data();
  }

  /**
   * @brief
   * 指定したコンポーネントが割り当てられているエンティティの連続配列を定数参照で取得します。
   * @tparam T コンポーネントの型
   * @return const auto& エンティティID配列への定数参照
   */
  template <typename T> const auto &get_raw_entities() const {
    return get_pool<T>().get_entities();
  }

  /**
   * @brief プール内の密配列のインデックスから、Transform
   * コンポーネントを取得します。
   * @param index プール内のインデックス
   * @return Transform& Transformコンポーネントへの参照
   */
  Transform &get_transform_by_index(std::size_t index) {
    return get_raw_data<Transform>()[index];
  }

  /**
   * @brief プール内の密配列のインデックスから、Transform
   * コンポーネントを定数参照で取得します。
   * @param index プール内のインデックス
   * @return const Transform& Transformコンポーネントへの定数参照
   */
  const Transform &get_transform_by_index(std::size_t index) const {
    return get_raw_data<Transform>()[index];
  }

  /**
   * @brief プール内の密配列のインデックスから、Velocity
   * コンポーネントを取得します。
   * @param index プール内のインデックス
   * @return Velocity& Velocityコンポーネントへの参照
   */
  Velocity &get_velocity_by_index(std::size_t index) {
    return get_raw_data<Velocity>()[index];
  }

  /**
   * @brief プール内の密配列のインデックスから、Velocity
   * コンポーネントを定数参照で取得します。
   * @param index プール内のインデックス
   * @return const Velocity& Velocityコンポーネントへの定数参照
   */
  const Velocity &get_velocity_by_index(std::size_t index) const {
    return get_raw_data<Velocity>()[index];
  }

  /**
   * @brief プール内の密配列のインデックスから、VertexComponent を取得します。
   * @param index プール内のインデックス
   * @return VertexComponent& VertexComponentへの参照
   */
  VertexComponent &get_vertex_by_index(std::size_t index) {
    return get_raw_data<VertexComponent>()[index];
  }

  /**
   * @brief プール内の密配列のインデックスから、VertexComponent
   * を定数参照で取得します。
   * @param index プール内のインデックス
   * @return const VertexComponent& VertexComponentへの定数参照
   */
  const VertexComponent &get_vertex_by_index(std::size_t index) const {
    return get_raw_data<VertexComponent>()[index];
  }

  /**
   * @brief プール内の密配列のインデックスから、タグを取得します。
   * @param index プール内のインデックス
   * @return std::uint64_t& タグへの参照
   */
  std::uint64_t &get_tag_by_index(std::size_t index) {
    return get_raw_data<std::uint64_t>()[index];
  }

  /**
   * @brief プール内の密配列 of インデックスから、タグを値渡しで取得します。
   * @param index プール内のインデックス
   * @return std::uint64_t タグの値
   */
  std::uint64_t get_tag_by_index(std::size_t index) const {
    return get_raw_data<std::uint64_t>()[index];
  }

  /**
   * @brief プール内の密配列のインデックスから、ScriptComponent を取得します。
   * @param index プール内のインデックス
   * @return ScriptComponent& ScriptComponentへの参照
   */
  ScriptComponent &get_script_by_index(std::size_t index) {
    return get_raw_data<ScriptComponent>()[index];
  }

  /**
   * @brief プール内の密配列のインデックスから、ScriptComponent
   * を定数参照で取得します。
   * @param index プール内のインデックス
   * @return const ScriptComponent& ScriptComponentへの定数参照
   */
  const ScriptComponent &get_script_by_index(std::size_t index) const {
    return get_raw_data<ScriptComponent>()[index];
  }

  /**
   * @brief プール内の密配列のインデックスから、Collider
   * コンポーネントを取得します。
   * @param index プール内のインデックス
   * @return Collider& Colliderコンポーネントへの参照
   */
  Collider &get_collider_by_index(std::size_t index) {
    return get_raw_data<Collider>()[index];
  }

  /**
   * @brief プール内の密配列のインデックスから、Collider
   * コンポーネントを定数参照で取得します。
   * @param index プール内のインデックス
   * @return const Collider& Colliderコンポーネントへの定数参照
   */
  const Collider &get_collider_by_index(std::size_t index) const {
    return get_raw_data<Collider>()[index];
  }

  /**
   * @brief 物理移動 (Inertia Movement) と衝突判定 (AABB Collision)
   * などのデフォルトシステムを追加します。
   */
  void add_default_systems() {
    // 1. 物理移動システム
    add_system([](Registry &reg, float dt) {
      auto &transforms = reg.get_raw_data<Transform>();
      auto &velocities = reg.get_raw_data<Velocity>();

      for (auto &&[trans, vel] : std::views::zip(transforms, velocities)) {
        trans.position += vel.value * dt;
      }
    });

    // 2. AABB衝突判定システム (O(N^2) 総当たりチェック)
    add_system([](Registry &reg, float /*dt*/) {
      auto &transforms = reg.get_raw_data<Transform>();
      auto &colliders = reg.get_raw_data<Collider>();
      auto &entities = reg.get_raw_entities<Collider>();

      std::size_t count = transforms.size();

      // 各フレームの開始時に衝突リストをリセット
      for (auto &col : colliders) {
        col.hitEntities.clear();
      }

      for (std::size_t i = 0; i < count; ++i) {
        if (colliders[i].halfExtents.x <= 0.0f ||
            colliders[i].halfExtents.y <= 0.0f ||
            colliders[i].halfExtents.z <= 0.0f) {
          continue;
        }

        glm::vec3 centerA = transforms[i].position + colliders[i].centerOffset;
        glm::vec3 minA = centerA - colliders[i].halfExtents;
        glm::vec3 maxA = centerA + colliders[i].halfExtents;

        for (std::size_t j = i + 1; j < count; ++j) {
          if (colliders[j].halfExtents.x <= 0.0f ||
              colliders[j].halfExtents.y <= 0.0f ||
              colliders[j].halfExtents.z <= 0.0f) {
            continue;
          }

          glm::vec3 centerB =
              transforms[j].position + colliders[j].centerOffset;
          glm::vec3 minB = centerB - colliders[j].halfExtents;
          glm::vec3 maxB = centerB + colliders[j].halfExtents;

          // AABB交差判定 (すべての軸で重なりがあるか)
          bool overlapX = (minA.x <= maxB.x) && (maxA.x >= minB.x);
          bool overlapY = (minA.y <= maxB.y) && (maxA.y >= minB.y);
          bool overlapZ = (minA.z <= maxB.z) && (maxA.z >= minB.z);

          if (overlapX && overlapY && overlapZ) {
            colliders[i].hitEntities.push_back(entities[j]);
            colliders[j].hitEntities.push_back(entities[i]);
          }
        }
      }
    });
  }

  /**
   * @brief 毎フレーム呼び出されるメイン更新ループ。
   *
   * 全ての ScriptComponent に登録された EntityBehavior の update を実行した後、
   * 登録された全システム関数を実行します。
   *
   * @param dt 前フレームからの経過時間 (デルタタイム)
   */
  void update(float dt) {
    auto &scripts = get_raw_data<ScriptComponent>();
    auto &entities = get_raw_entities<ScriptComponent>();

    // 1. 各エンティティに登録されたスクリプトの更新処理を実行
    for (std::size_t i = 0; i < scripts.size(); ++i) {
      if (scripts[i].behavior) {
        scripts[i].behavior->update(i, entities[i], *this, dt);
      }
    }

    // 2. 登録された全システム関数の実行
    for (auto &system : systems) {
      system(*this, dt);
    }
  }
};
