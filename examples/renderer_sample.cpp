#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#include <astream/astream.hpp>
using namespace astream;
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// --- アセット定義 ---
std::vector<Shaders::DefaultVertex> cubeVertices = {
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

std::vector<Shaders::DefaultVertex> planeVertices = {
    {{-10, -1.5f, -15}, {0, 1, 0}, {0.0f, 10.0f}},
    {{10, -1.5f, -15}, {0, 1, 0}, {10.0f, 10.0f}},
    {{10, -1.5f, 2}, {0, 1, 0}, {10.0f, 0.0f}},
    {{-10, -1.5f, 2}, {0, 1, 0}, {0.0f, 0.0f}}};
std::vector<int> planeIndices = {0, 2, 1, 0, 3, 2};

int main() {
  int w = 80, h = 24;
  auto device = GraphicsDevice();

  // 出力
  auto colorBuffer = Texture2D<char>(w, h, ' ');
  auto cameraDepth = Texture2D<float>(w, h, std::numeric_limits<float>::max());
  auto shadowDepth = Texture2D<float>(w, h, std::numeric_limits<float>::max());

  // Gバッファ群
  auto albedoBuffer = Texture2D<char>(w, h, ' ');
  auto normalBuffer = Texture2D<glm::vec3>(w, h, glm::vec3(0, 0, 0));
  auto worldPosBuffer = Texture2D<glm::vec3>(w, h, glm::vec3(0, 0, 0));

  float angleX = 0.0f, angleY = 0.0f;
  const char *clear_seq = "\x1b[2J";

  glm::vec3 lightDir = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f));
  float pi_half = 3.14159265f / 2.0f;
  glm::mat4 lightView =
      glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -5.0f)) *
      glm::rotate(glm::mat4(1.0f), pi_half, glm::vec3(1.0f, 0.0f, 0.0f)) *
      glm::rotate(glm::mat4(1.0f), 0.0f, glm::vec3(0.0f, 1.0f, 0.0f));
  float boxSize = 8.0f;
  float aspect = (float)w / (float)h * 0.5f;
  glm::mat4 lightProj = glm::ortho(-boxSize * aspect, boxSize * aspect,
                                   -boxSize, boxSize, 0.1f, 10.0f);
  glm::mat4 lightSpaceMatrix = lightProj * lightView;

  std::vector<char> outputStream((w + 1) * h + 1, '\0');

  glm::mat4 currentModel(1.0f);
  glm::mat4 currentMVP(1.0f);

  while (true) {
    colorBuffer.clear(' ');
    cameraDepth.clear(std::numeric_limits<float>::max());
    shadowDepth.clear(std::numeric_limits<float>::max());
    albedoBuffer.clear(' ');
    normalBuffer.clear(glm::vec3(0, 0, 0));
    worldPosBuffer.clear(glm::vec3(0, 0, 0));

    glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -4.5f));
    glm::mat4 proj = glm::perspective(1.0f, aspect, 0.1f, 100.0f);

    // ==========================================
    // PASS 1: シャドウパス
    // ==========================================
    auto shadowPass =
        device.create_rasterize_pass<Shaders::DefaultVertex, glm::vec3, float>(
            shadowDepth.view());

    currentModel = glm::mat4(1.0f);
    shadowPass.draw(
        planeVertices, planeIndices,
        std::bind_back(Shaders::shadowVS, currentModel, lightSpaceMatrix));

    currentModel =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.3f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), angleY, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), angleX, glm::vec3(1.0f, 0.0f, 0.0f));
    shadowPass.draw(
        cubeVertices, cubeIndices,
        std::bind_back(Shaders::shadowVS, currentModel, lightSpaceMatrix));

    // ==========================================
    // PASS 2: ジオメトリパス (Gバッファへの書き込み)
    // ==========================================
    auto geometryPass =
        device.create_rasterize_pass<Shaders::DefaultVertex,
                                     Shaders::DefaultVarying, float>(
            cameraDepth.view());

    // 2-1. 床

    currentModel = glm::mat4(1.0f);
    currentMVP = proj * view * currentModel;
    geometryPass.draw(
        planeVertices, planeIndices,
        std::bind_back(Shaders::geometryVS, currentModel, currentMVP),
        [&](int x, int y, const Shaders::DefaultVarying &in) {
          albedoBuffer.at(x, y) = 'F';
          normalBuffer.at(x, y) = in.normal;
          worldPosBuffer.at(x, y) = in.worldPos;
        });

    // 2-2. キューブ
    currentModel =
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.3f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), angleY, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), angleX, glm::vec3(1.0f, 0.0f, 0.0f));
    currentMVP = proj * view * currentModel;
    geometryPass.draw(
        cubeVertices, cubeIndices,
        std::bind_back(Shaders::geometryVS, currentModel, currentMVP),
        [&](int x, int y, const Shaders::DefaultVarying &in) {
          albedoBuffer.at(x, y) = 'C';
          normalBuffer.at(x, y) = in.normal;
          worldPosBuffer.at(x, y) = in.worldPos;
        });

    // ==========================================
    // PASS 3: ライティングパス
    // ==========================================
    auto lightingPass = device.create_compute_pass();

    lightingPass.execute(
        w, h,
        std::bind_back(Shaders::deferredLightingCS, colorBuffer.view(),
                       albedoBuffer.view(), normalBuffer.view(),
                       worldPosBuffer.view(), shadowDepth.view(),
                       lightSpaceMatrix, lightDir, w, h));

    // ==========================================
    // PRESENT: 画面出力
    // ==========================================
    std::cout << clear_seq;
    size_t streamIdx = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        outputStream[streamIdx++] = colorBuffer.at(x, y);
      }
      outputStream[streamIdx++] = '\n';
    }
    outputStream[streamIdx] = '\0';
    std::cout << outputStream.data() << std::flush;

    angleX += 0.05f;
    angleY += 0.03f;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return 0;
}
