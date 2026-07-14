#pragma once

#include <algorithm>
#include <utility>

#include <astream/graphics/Texture2D.hpp>
#include <astream/graphics/shaders/ShaderUtil.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace astream::graphics::shaders {

/**
 * @brief Default vertex layout representing position, normal vector, and UV
 * coordinates.
 */
struct DefaultVertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;
};

/**
 * @brief Default varying attributes interpolated across triangles and passed to
 * the fragment shader.
 */
struct DefaultVarying {
  glm::vec3 normal;
  glm::vec2 uv;
  glm::vec3 worldPos;

  DefaultVarying operator+(const DefaultVarying &r) const {
    return {normal + r.normal, uv + r.uv, worldPos + r.worldPos};
  }
  DefaultVarying operator*(float s) const {
    return {normal * s, uv * s, worldPos * s};
  }
};

/**
 * @brief Default Vertex Shader for the shadow pass.
 * Transforms the vertex into light clip space and outputs the world position.
 */
inline std::pair<glm::vec4, glm::vec3> shadowVS(const DefaultVertex &in,
                                                const glm::mat4 &model,
                                                const glm::mat4 &lightSpace) {
  glm::vec3 worldPos = glm::vec3(model * glm::vec4(in.position, 1.0f));
  return {lightSpace * glm::vec4(worldPos, 1.0f), worldPos};
}

/**
 * @brief Default Vertex Shader for the geometry/G-buffer pass.
 * Transforms the position to MVP space and calculates varying attributes in
 * world space.
 */
inline std::pair<glm::vec4, DefaultVarying> geometryVS(const DefaultVertex &in,
                                                       const glm::mat4 &model,
                                                       const glm::mat4 &mvp) {
  DefaultVarying outVar;
  outVar.normal = glm::normalize(glm::vec3(model * glm::vec4(in.normal, 0.0f)));
  outVar.uv = in.uv;
  outVar.worldPos = glm::vec3(model * glm::vec4(in.position, 1.0f));
  return {mvp * glm::vec4(in.position, 1.0f), outVar};
}

/**
 * @brief Default Compute/Pixel Shader for deferred lighting.
 * Performs shadow mapping, diffuse shading, and character palette mapping.
 */
inline void deferredLightingCS(int x, int y, TextureView<char> colorBuf,
                               TextureView<const char> albedoBuf,
                               TextureView<const glm::vec3> normalBuf,
                               TextureView<const glm::vec3> worldPosBuf,
                               TextureView<const float> shadowDepth,
                               const glm::mat4 &lightSpace,
                               const glm::vec3 &lightDir, int w, int h) {
  char mtl = albedoBuf.at(x, y);
  if (mtl == 0)
    return;

  glm::vec3 worldPos = worldPosBuf.at(x, y);
  glm::vec3 normal = normalBuf.at(x, y);

  glm::vec4 posInLightSpace = lightSpace * glm::vec4(worldPos, 1.0f);
  if (posInLightSpace.w != 0.0f) {
    posInLightSpace /= posInLightSpace.w;
  }

  float shadowX = (posInLightSpace.x + 1.0f) * 0.5f;
  float shadowY = (1.0f - posInLightSpace.y) * 0.5f;

  float shadowFactor = 1.0f;
  if (shadowX >= 0 && shadowX < 1 && shadowY >= 0 && shadowY < 1) {
    float closestDepth = shadowDepth.sampleBilinear(shadowX, shadowY);
    if (posInLightSpace.z > closestDepth + 0.002f)
      shadowFactor = 0.2f;
  }

  float diff = std::max(0.0f, glm::dot(normal, lightDir));
  float col = std::min(1.0f, (diff * shadowFactor * (mtl / 128.0f)) + 0.1f);

  colorBuf.at(x, y) = mapIntensityToChar(col);
}

} // namespace astream::graphics::shaders

namespace Shaders {
using astream::graphics::shaders::DefaultVarying;
using astream::graphics::shaders::DefaultVertex;
using astream::graphics::shaders::deferredLightingCS;
using astream::graphics::shaders::geometryVS;
using astream::graphics::shaders::shadowVS;
} // namespace Shaders
