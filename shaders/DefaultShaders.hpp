#pragma once

#include "Math.hpp"
#include "ShaderUtil.hpp"
#include "Texture2D.hpp"
#include <algorithm>
#include <utility>

namespace Shaders {

/**
 * @brief Default vertex layout representing position, normal vector, and UV
 * coordinates.
 */
struct DefaultVertex {
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
};

/**
 * @brief Default varying attributes interpolated across triangles and passed to
 * the fragment shader.
 */
struct DefaultVarying {
  Vec3 normal;
  Vec2 uv;
  Vec3 worldPos;

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
inline std::pair<Vec4, Vec3>
shadowVS(const DefaultVertex &in, const Mat4 &model, const Mat4 &lightSpace) {
  Vec3 worldPos = model.transform(in.position);
  return {lightSpace.transform(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f)),
          worldPos};
}

/**
 * @brief Default Vertex Shader for the geometry/G-buffer pass.
 * Transforms the position to MVP space and calculates varying attributes in
 * world space.
 */
inline std::pair<Vec4, DefaultVarying>
geometryVS(const DefaultVertex &in, const Mat4 &model, const Mat4 &mvp) {
  DefaultVarying outVar;
  outVar.normal = model.transform(in.normal).normalize();
  outVar.uv = in.uv;
  outVar.worldPos = model.transform(in.position);
  return {
      mvp.transform(Vec4(in.position.x, in.position.y, in.position.z, 1.0f)),
      outVar};
}

/**
 * @brief Default Compute/Pixel Shader for deferred lighting.
 * Performs shadow mapping, diffuse shading, and character palette mapping.
 */
inline void deferredLightingCS(int x, int y, Texture2D<char> &colorBuf,
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

  colorBuf.at(x, y) = mapIntensityToChar(col);
}

} // namespace Shaders
