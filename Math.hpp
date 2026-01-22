#pragma once
#include <cmath>
#include <cstring>

struct Vec2 {
  float x, y;
  Vec2() : x(0), y(0) {}
  Vec2(float x, float y) : x(x), y(y) {}

  Vec2 operator+(const Vec2 &r) const { return {x + r.x, y + r.y}; }
  Vec2 operator-(const Vec2 &r) const { return {x - r.x, y - r.y}; }
  Vec2 operator*(float s) const { return {x * s, y * s}; }

  float dot(const Vec2 &r) const { return x * r.x + y * r.y; }
};

struct Vec3 {
  float x, y, z;
  Vec3() : x(0), y(0), z(0) {}
  Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

  Vec3 operator+(const Vec3 &r) const { return {x + r.x, y + r.y, z + r.z}; }
  Vec3 operator-(const Vec3 &r) const { return {x - r.x, y - r.y, z - r.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

  float dot(const Vec3 &r) const { return x * r.x + y * r.y + z * r.z; }

  Vec3 normalize() const {
    float len = std::sqrt(x * x + y * y + z * z);
    return len > 0 ? *this * (1.0f / len) : *this;
  }
};

struct Vec4 {
  float x, y, z, w;

  // wのデフォルトは1.0
  Vec4() : x(0), y(0), z(0), w(1.0f) {}
  Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

  Vec4 operator+(const Vec4 &r) const {
    return {x + r.x, y + r.y, z + r.z, w + r.w};
  }
  Vec4 operator-(const Vec4 &r) const {
    return {x - r.x, y - r.y, z - r.z, w - r.w};
  }
  Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
};

struct Mat4 {
  float m[4][4];
  Mat4() { identity(); }

  void identity() {
    std::memset(m, 0, sizeof(m));
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
  }

  Vec3 transform(const Vec3 &v) const {
    float x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3];
    float y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3];
    float z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3];
    float w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
    // 透視除算 (Perspective Divide)
    if (w != 0.0f) {
      return {x / w, y / w, z / w};
    }
    return {x, y, z};
  }

  Vec4 transform(const Vec4 &v) const {
    float x_out = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w;
    float y_out = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w;
    float z_out = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w;
    float w_out = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w;
    return Vec4(x_out, y_out, z_out, w_out);
  }

  Vec4 transformVec4(const Vec3 &v) const {
    return transform(Vec4(v.x, v.y, v.z, 1.0f));
  }

  Mat4 operator*(const Mat4 &r) const {
    Mat4 res;
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 4; j++) {
        res.m[i][j] = 0;
        for (int k = 0; k < 4; k++)
          res.m[i][j] += m[i][k] * r.m[k][j];
      }
    }
    return res;
  }

  static Mat4 rotateY(float rad) {
    Mat4 r;
    float c = cos(rad), s = sin(rad);
    r.m[0][0] = c;
    r.m[0][2] = s;
    r.m[2][0] = -s;
    r.m[2][2] = c;
    return r;
  }
  static Mat4 rotateX(float rad) {
    Mat4 r;
    float c = cos(rad), s = sin(rad);
    r.m[1][1] = c;
    r.m[1][2] = -s;
    r.m[2][1] = s;
    r.m[2][2] = c;
    return r;
  }
  static Mat4 translate(float x, float y, float z) {
    Mat4 r;
    r.m[0][3] = x;
    r.m[1][3] = y;
    r.m[2][3] = z;
    return r;
  }
  static Mat4 perspective(float fov, float aspect, float n, float f) {
    Mat4 r;
    std::memset(r.m, 0, sizeof(r.m));
    float t = 1.0f / std::tan(fov / 2);
    r.m[0][0] = t / aspect;
    r.m[1][1] = t;
    r.m[2][2] = (f + n) / (n - f);
    r.m[2][3] = (2 * f * n) / (n - f);
    r.m[3][2] = -1.0f;
    return r;
  }
};
