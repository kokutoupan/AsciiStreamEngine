#pragma once

#include <astream/Registry.hpp>
#include <cstdint>

struct RaycastHit {
  std::uint32_t entity = 0;
  float distance = -1.0f;
  bool hit = false;
};

// レイとAABBの交差判定を行うヘルパー関数（スラブ法）
inline bool intersect_ray_aabb(const glm::vec3 &origin, const glm::vec3 &dir,
                               const glm::vec3 &minA, const glm::vec3 &maxA,
                               float &t_out) {
  float tmin = 0.0f; // レイの起点（0.0）より前のみを判定
  float tmax = std::numeric_limits<float>::max();

  // X, Y, Z の3軸に対してスラブ判定を行う
  for (int i = 0; i < 3; ++i) {
    // レイの方向ベクトルがその軸に対して平行に近い場合
    if (std::abs(dir[i]) < 1e-6f) {
      // レイの起点がAABBの外側にあれば衝突しない
      if (origin[i] < minA[i] || origin[i] > maxA[i]) {
        return false;
      }
    } else {
      // 各軸の平面との交差時間を計算
      float invDir = 1.0f / dir[i];
      float t1 = (minA[i] - origin[i]) * invDir;
      float t2 = (maxA[i] - origin[i]) * invDir;

      if (t1 > t2)
        std::swap(t1, t2);

      tmin = std::max(tmin, t1);
      tmax = std::min(tmax, t2);

      // 衝突範囲が逆転したらヒットしていない
      if (tmin > tmax)
        return false;
    }
  }

  t_out = tmin;
  return true;
}

RaycastHit raycast_now(Registry &registry, const glm::vec3 &origin,
                       const glm::vec3 &direction, float max_distance) {
  RaycastHit closest_hit;
  closest_hit.distance = max_distance;
  glm::vec3 norm_dir = glm::normalize(direction);

  // registry内のColliderを持つエンティティを線形走査
  auto &colliders = registry.get_raw_data<Collider>();
  auto &transforms = registry.get_raw_data<Transform>();
  auto &entities = registry.get_raw_entities<Collider>();

  const int count = transforms.size();
  for (std::size_t i = 0; i < count; ++i) {
    if (colliders[i].halfExtents.x <= 0.0f ||
        colliders[i].halfExtents.y <= 0.0f ||
        colliders[i].halfExtents.z <= 0.0f) {
      continue;
    }

    glm::vec3 centerA = transforms[i].position + colliders[i].centerOffset;
    glm::vec3 minA = centerA - colliders[i].halfExtents;
    glm::vec3 maxA = centerA + colliders[i].halfExtents;

    // スラブ法による交差判定
    float t = 0.0f;
    if (intersect_ray_aabb(origin, norm_dir, minA, maxA, t)) {
      // 設定された最大距離以内で、かつこれまでの最安値（最も近い衝突）を更新した場合
      if (t > 0.0f && t < closest_hit.distance) {
        closest_hit.hit = true;
        closest_hit.distance = t;
        closest_hit.entity = entities[i];
      }
    }
  }
  return closest_hit;
}
