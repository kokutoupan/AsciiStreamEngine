#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace astream::graphics {

struct Transform {
  glm::vec3 position{0.0f}; ///< 位置 (X, Y, Z)
  glm::vec3 rotation{0.0f}; ///< 回転 (ラジアン角: ピッチ, ヨー, ロール)
  glm::vec3 scale{1.0f};    ///< スケール (X, Y, Z)

  glm::mat4 to_matrix() const {
    glm::mat4 m(1.0f);
    m = glm::translate(m, position);
    m = glm::rotate(m, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::rotate(m, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    m = glm::scale(m, scale);
    return m;
  }
};

} // namespace astream::graphics
