#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <unistd.h>
#include <vector>

#include "GraphicsDevice.hpp"
#include "Math.hpp"
#include "Texture2D.hpp"

inline char mapIntensityToChar(float intensity) {
  const char *palette = " .'`^\",:;Il!i~+_-?][}"
                        "{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
  int len = strlen(palette);
  int idx = (int)(intensity * (len - 1));
  if (idx < 0)
    idx = 0;
  if (idx >= len)
    idx = len - 1;
  return palette[idx];
}

// 深度値を可視化するためのパレットマッピング（近い＝明るい、遠い＝暗い）
inline char mapDepthToChar(float z, float zNear, float zFar) {
  if (z >= std::numeric_limits<float>::max() - 1.0f) {
    return ' '; // 何も描画されていない背景は空白
  }

  // 深度を 0.0 (Near) 〜 1.0 (Far) に正規化
  float normalizedZ = (z - zNear) / (zFar - zNear);
  if (normalizedZ < 0.0f)
    normalizedZ = 0.0f;
  if (normalizedZ > 1.0f)
    normalizedZ = 1.0f;

  // 反転させて「近いほど明るい（パレットの後半の文字）」にする
  float intensity = 1.0f - normalizedZ;

  const char *palette = " .'`^\",:;Il!i~+_-?][}"
                        "{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
  int len = strlen(palette);
  int idx = (int)(intensity * (len - 1));

  if (idx < 0)
    idx = 0;
  if (idx >= len)
    idx = len - 1;

  return palette[idx];
}

// 頂点シェーダーからフラグメントへ渡す Varying 構造体
struct MyVarying {
  Vec3 normal;
  Vec2 uv;
  Vec3 worldPos; // 影判定のために必須

  MyVarying operator+(const MyVarying &r) const {
    return {normal + r.normal, uv + r.uv, worldPos + r.worldPos};
  }
  MyVarying operator*(float s) const {
    return {normal * s, uv * s, worldPos * s};
  }
};

struct InputVertex {
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
};

// --- アセット定義 ---
// 1. キューブ (既存データ)
std::vector<InputVertex> cubeVertices = {
    // Back, Front, Left, Right, Top, Bottom の24頂点
    {{1, -1, -1}, {0, 0, -1}, {0.0f, 1.0f}},
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
std::vector<int> cubeIndices = {0,  1,  2,  0,  2,  3,  4,  5,  6,  4,  6,  7,
                                8,  9,  10, 8,  10, 11, 12, 13, 14, 12, 14, 15,
                                16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23};

// 2. 床 (Plane) の定義: Y = -1.5 の位置に広がる大きな平面
std::vector<InputVertex> planeVertices = {
    {{-10, -1.5f, -15}, {0, 1, 0}, {0.0f, 1.0f}}, // 左奥
    {{10, -1.5f, -15}, {0, 1, 0}, {1.0f, 1.0f}},  // 右奥
    {{10, -1.5f, 2}, {0, 1, 0}, {1.0f, 0.0f}}, // 右手前 (Zを 10 から 2 に変更)
    {{-10, -1.5f, 2}, {0, 1, 0}, {0.0f, 0.0f}} // 左手前 (Zを 10 から 2 に変更)
};
std::vector<int> planeIndices = {
    0, 2, 1, 0, 3, 2 // 反時計回り
};

int main() {
  int w = 80, h = 24;

  auto device = GraphicsDevice();
  auto colorBuffer = Texture2D<char>(w, h, ' ');
  auto cameraDepth = Texture2D<float>(w, h, std::numeric_limits<float>::max());

  // シャドウマップ用の解像度バッファ (メイン画面と同じサイズにする)
  auto shadowDepth = Texture2D<float>(w, h, std::numeric_limits<float>::max());

  float angleX = 0.0f, angleY = 0.0f;
  const char *clear_seq = "\x1b[2J";

  // 1. ライトの方向ベクトル（真上から真下へ照らす）
  Vec3 lightDir = Vec3(0.0f, 1.0f, 0.0f).normalize();

  // 2. ライトView: シーンの少し上空（Z =
  // -5.0f）から真下（rotateX(90度)）を見下ろす
  float pi_half = 3.14159265f / 2.0f;
  Mat4 lightView = Mat4::translate(0, 0, -10.0f) * Mat4::rotateX(pi_half) *
                   Mat4::rotateY(0.0f);

  // 3. ライトProj: 正投影行列
  float boxSize = 8.0f;
  // アスペクト比を考慮して横幅を調整（メイン表示の比率に合わせる）
  float aspect = (float)w / (float)h * 0.5f;
  float orthoW = boxSize * aspect;
  float orthoH = boxSize;

  Mat4 lightProj =
      Mat4::orthographic(-orthoW, orthoW, -orthoH, orthoH, 0.1f, 10.0f);

  Mat4 lightSpaceMatrix = lightProj * lightView;

  const char texture[8][9] = {"########", "##    ##", "# C  C #", "#      #",
                              "# +  + #", "#      #", "##    ##", "########"};
  std::vector<char> outputStream((w + 1) * h + 1, '\0');

  // 行列共有用のポインタ/参照をキャプチャ
  Mat4 currentModel;
  Mat4 currentMVP;

  // シャドウパス用の共通VS
  auto shadowVS = [&](const InputVertex &in) -> std::pair<Vec4, Vec3> {
    // 1. まずローカル座標を currentModel で世界座標に変換する！
    Vec3 worldPos = currentModel.transform(in.position);

    // 2. 世界座標をライト空間に変換する
    Vec4 pos = lightSpaceMatrix.transform(
        Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f));

    return {pos, worldPos};
  };

  // ジオメトリパス用の共通VS
  auto geometryVS = [&](const InputVertex &in) -> std::pair<Vec4, MyVarying> {
    Vec4 pos = currentMVP.transform(
        Vec4(in.position.x, in.position.y, in.position.z, 1.0f));
    MyVarying outVar;
    outVar.normal = currentModel.transform(in.normal).normalize();
    outVar.uv = in.uv;
    outVar.worldPos = currentModel.transform(in.position);
    return {pos, outVar};
  };

  // ジオメトリパス用の共通FS（ピクセル合格時アクション）
  // シャドウバッファとカラーバッファをキャプチャしてブレンド処理を行う
  auto geometryFS = [&](int x, int y, const MyVarying &in) {
    // 1. シャドウ判定
    Vec4 posInLightSpace = lightSpaceMatrix.transform(
        Vec4(in.worldPos.x, in.worldPos.y, in.worldPos.z, 1.0f));

    // ライト空間の NDC (-1.0 ~ 1.0) を シャドウバッファの座標 (0 ~ w, 0 ~ h)
    // へマッピング
    float shadowX = (posInLightSpace.x + 1.0f) * 0.5f * (float)w;
    float shadowY = (1.0f - posInLightSpace.y) * 0.5f * (float)h;

    float shadowFactor = 1.0f; // 1.0: 日向, 0.2: 影
    if (shadowX >= 0 && shadowX < w && shadowY >= 0 && shadowY < h) {
      float closestDepth = shadowDepth.at((int)shadowX, (int)shadowY);
      float currentDepth = posInLightSpace.z;

      float bias = 0.002f; // アスキーの解像度粗さによるアクネ対策
      if (currentDepth > closestDepth + bias) {
        shadowFactor = 0.2f; // 影の中
      }
    }

    // 2. ライティング計算
    float diff = std::max(0.0f, in.normal.dot(lightDir));
    float col = (diff * shadowFactor) + 0.1f; // 環境光 0.1f
    col = std::min(1.0f, col);

    // 3. テクスチャ
    // int u = (int)(in.uv.x * 8.0f) % 8;
    // int v = (int)(in.uv.y * 8.0f) % 8;
    // if (u < 0)
    //   u += 8;
    // if (v < 0)
    //   v += 8;

    // char texChar = texture[v][u];
    char texChar = ' ';

    // 影の中の壁文字をちょっと変調させる(影っぽさを強調)
    if (texChar != ' ') {
      colorBuffer.at(x, y) = (shadowFactor < 1.0f) ? '.' : texChar;
    } else {
      colorBuffer.at(x, y) = mapIntensityToChar(col);
    }
  };

  while (true) {
    colorBuffer.clear(' ');
    cameraDepth.clear(std::numeric_limits<float>::max());
    shadowDepth.clear(std::numeric_limits<float>::max());

    float aspect = ((float)w / (float)h * 0.5f);
    Mat4 view = Mat4::translate(0, 0, -4.5f);
    Mat4 proj = Mat4::perspective(1.0f, aspect, 0.1f, 100.0f);

    // ==========================================
    // PASS 1: シャドウパス (ライト視点でデプスを焼く)
    // ==========================================
    auto shadowPass =
        device.create_rasterize_pass<InputVertex, Vec3, float>(shadowDepth);
    shadowPass.set_vertex_shader(shadowVS);
    // フラグメントは設定しない（＝Zバッファだけ更新される固定機能）

    // 1-1. 床の描画登録 (床は静止)
    currentModel.identity();

    shadowPass.draw(planeVertices, planeIndices);

    // 1-2. キューブの描画登録 (キューブは少し上に浮かせ、回転させる)
    currentModel = Mat4::translate(0.0f, 0.3f, 0.0f) * Mat4::rotateY(angleY) *
                   Mat4::rotateX(angleX);
    shadowPass.draw(cubeVertices, cubeIndices);

    // ==========================================
    // PASS 2: ジオメトリ ＆ ライティングパス (メイン表示)
    // ==========================================
    auto renderPass =
        device.create_rasterize_pass<InputVertex, MyVarying, float>(
            cameraDepth);
    renderPass.set_vertex_shader(geometryVS);
    renderPass.set_on_pixel_fragment(geometryFS);

    // 2-1. 床の描画
    currentModel.identity(); // 床はトランスフォームなし
    currentMVP = proj * view * currentModel;
    renderPass.draw(planeVertices, planeIndices);

    // 2-2. キューブの描画 (浮遊・回転)
    currentModel = Mat4::translate(0.0f, 0.3f, 0.0f) * Mat4::rotateY(angleY) *
                   Mat4::rotateX(angleX);
    currentMVP = proj * view * currentModel;
    renderPass.draw(cubeVertices, cubeIndices);

    // ==========================================
    // PRESENT: 画面出力
    // ==========================================
    std::cout << clear_seq;
    size_t streamIdx = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        outputStream[streamIdx++] = colorBuffer.at(x, y);
        // outputStream[streamIdx++] =
        //     mapDepthToChar(shadowDepth.at(x, y), 0.1, 10);
      }
      outputStream[streamIdx++] = '\n';
    }
    outputStream[streamIdx] = '\0';
    std::cout << outputStream.data() << std::flush;

    angleX += 0.05f;
    angleY += 0.03f;
    usleep(50000);
  }
  return 0;
}
