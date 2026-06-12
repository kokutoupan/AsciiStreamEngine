#include <algorithm>
#include <arpa/inet.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <math.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>
#include <zconf.h>
#include <zlib.h>

#include "GraphicsDevice.hpp"
#include "Math.hpp"
#include "Texture2D.hpp"

constexpr int PORT = 12345;

int send_frame_compressed(int sock, const char *raw_data, size_t raw_len,
                          int is_raw_send) {
  if (is_raw_send) {
    return send(sock, raw_data, raw_len, 0);
  }
  uLongf comp_len = compressBound(raw_len);
  std::vector<Bytef> comp_buf(comp_len);

  int res =
      compress(comp_buf.data(), &comp_len, (const Bytef *)raw_data, raw_len);
  if (res != Z_OK) {
    return -1;
  }

  uint32_t net_len = htonl((uint32_t)comp_len);
  res = send(sock, &net_len, sizeof(net_len), 0);

  if (res <= 0) {
    return res;
  }

  return send(sock, comp_buf.data(), comp_len, 0);
}

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

struct MyVarying {
  Vec3 normal;
  Vec2 uv;
  Vec3 worldPos;

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
std::vector<InputVertex> cubeVertices = {
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

std::vector<InputVertex> planeVertices = {
    {{-10, -1.5f, -15}, {0, 1, 0}, {0.0f, 10.0f}},
    {{10, -1.5f, -15}, {0, 1, 0}, {10.0f, 10.0f}},
    {{10, -1.5f, 2}, {0, 1, 0}, {10.0f, 0.0f}},
    {{-10, -1.5f, 2}, {0, 1, 0}, {0.0f, 0.0f}}};
std::vector<int> planeIndices = {0, 2, 1, 0, 3, 2};

// ==========================================
// SHADER FUNCTIONS
// ==========================================

// シャドウパス用頂点シェーダー
auto shadowVS = [](const InputVertex &in, const Mat4 &model,
                   const Mat4 &lightSpace) -> std::pair<Vec4, Vec3> {
  Vec3 worldPos = model.transform(in.position);
  Vec4 pos =
      lightSpace.transform(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f));
  return {pos, worldPos};
};

// ジオメトリパス用頂点シェーダー
auto geometryVS = [](const InputVertex &in, const Mat4 &model,
                     const Mat4 &mvp) -> std::pair<Vec4, MyVarying> {
  Vec4 pos =
      mvp.transform(Vec4(in.position.x, in.position.y, in.position.z, 1.0f));
  MyVarying outVar;
  outVar.normal = model.transform(in.normal).normalize();
  outVar.uv = in.uv;
  outVar.worldPos = model.transform(in.position);
  return {pos, outVar};
};

// ディファードライティング用コンピュート（ピクセル）シェーダー
auto deferredLightingCS =
    [](int x, int y, Texture2D<char> &colorBuf,
       const Texture2D<char> &albedoBuf, const Texture2D<Vec3> &normalBuf,
       const Texture2D<Vec3> &worldPosBuf, const Texture2D<float> &shadowDepth,
       const Mat4 &lightSpace, const Vec3 &lightDir, int w, int h) {
      char mtl = albedoBuf.at(x, y);
      if (mtl == ' ')
        return;

      Vec3 worldPos = worldPosBuf.at(x, y);
      Vec3 normal = normalBuf.at(x, y);

      // 1. シャドウ判定
      Vec4 posInLightSpace =
          lightSpace.transform(Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f));
      float shadowX = (posInLightSpace.x + 1.0f) * 0.5f * (float)w;
      float shadowY = (1.0f - posInLightSpace.y) * 0.5f * (float)h;

      float shadowFactor = 1.0f;
      if (shadowX >= 0 && shadowX < w && shadowY >= 0 && shadowY < h) {
        float closestDepth = shadowDepth.at((int)shadowX, (int)shadowY);
        float currentDepth = posInLightSpace.z;
        if (currentDepth > closestDepth + 0.002f) {
          shadowFactor = 0.2f;
        }
      }

      // 2. ライティング
      float diff = std::max(0.0f, normal.dot(lightDir));
      float col = (diff * shadowFactor) + 0.1f;
      col = std::min(1.0f, col);

      if (mtl == 'C') {
        colorBuf.at(x, y) = mapIntensityToChar(col);
      } else if (mtl == 'F') {
        colorBuf.at(x, y) =
            (shadowFactor < 1.0f) ? ':' : mapIntensityToChar(col * 0.5f);
      }
    };

void handle_client(int client_sock) {
  int w = 80, h = 24; // デフォルト値

  int is_raw_send = 1;
  // 1. タイムアウト設定 (0.1秒)
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 100000; // 100ms
  setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv,
             sizeof(tv));

  // 2. サイズ受信を試みる
  uint16_t size_packet[2] = {0, 0};
  int len = recv(client_sock, size_packet, sizeof(size_packet), 0);

  if (len == sizeof(size_packet)) {
    // データが正しく来たら採用
    w = ntohs(size_packet[0]);
    h = ntohs(size_packet[1]);
    // バカでかい値や0が来ないようにガード
    if (w < 1)
      w = 80;
    if (h < 1)
      h = 24;
    printf("Client requested size: %d x %d\n", w, h);

    // 接続が来る = 多分圧縮ok
    is_raw_send = 0;
  } else {
    // ncなどが接続した場合 (タイムアウト or データなし)
    printf("Using default size: %d x %d\n", w, h);
  }

  // 3. タイムアウト解除 (ブロッキングモードに戻す)
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv,
             sizeof(tv));

  auto device = GraphicsDevice();

  // 出力
  auto colorBuffer = Texture2D<char>(w, h, ' ');
  auto cameraDepth = Texture2D<float>(w, h, std::numeric_limits<float>::max());
  auto shadowDepth = Texture2D<float>(w, h, std::numeric_limits<float>::max());

  // Gバッファ群
  auto albedoBuffer = Texture2D<char>(w, h, ' ');
  auto normalBuffer = Texture2D<Vec3>(w, h, Vec3(0, 0, 0));
  auto worldPosBuffer = Texture2D<Vec3>(w, h, Vec3(0, 0, 0));

  float angleX = 0.0f, angleY = 0.0f;
  const char *clear_seq = "\x1b[2J";

  Vec3 lightDir = Vec3(0.0f, 1.0f, 0.0f).normalize();
  float pi_half = 3.14159265f / 2.0f;
  Mat4 lightView = Mat4::translate(0, 0, -5.0f) * Mat4::rotateX(pi_half) *
                   Mat4::rotateY(0.0f);
  float boxSize = 8.0f;
  float aspect = (float)w / (float)h * 0.5f;
  Mat4 lightProj = Mat4::orthographic(-boxSize * aspect, boxSize * aspect,
                                      -boxSize, boxSize, 0.1f, 10.0f);
  Mat4 lightSpaceMatrix = lightProj * lightView;

  std::vector<char> outputStream((w + 1) * h + 1 + 4, '\0');
  std::copy(clear_seq, clear_seq + 4, outputStream.begin());
  for (int y = 0; y < h; ++y) {
    outputStream[4 + (w + 1) * y + w] = '\n';
  }
  // 最末尾をヌル文字に
  outputStream[outputStream.size() - 1] = '\0';

  Mat4 currentModel;
  Mat4 currentMVP;

  while (true) {

    colorBuffer.clear(' ');
    cameraDepth.clear(std::numeric_limits<float>::max());
    shadowDepth.clear(std::numeric_limits<float>::max());
    albedoBuffer.clear(' ');
    normalBuffer.clear(Vec3(0, 0, 0));
    worldPosBuffer.clear(Vec3(0, 0, 0));

    Mat4 view = Mat4::translate(0, 0, -4.5f);
    Mat4 proj = Mat4::perspective(1.0f, aspect, 0.1f, 100.0f);

    // ==========================================
    // PASS 1: シャドウパス
    // ==========================================
    auto shadowPass =
        device.create_rasterize_pass<InputVertex, Vec3, float>(shadowDepth);

    currentModel.identity();
    shadowPass.draw(planeVertices, planeIndices,
                    std::bind_back(shadowVS, currentModel, lightSpaceMatrix));

    currentModel = Mat4::translate(0.0f, 0.3f, 0.0f) * Mat4::rotateY(angleY) *
                   Mat4::rotateX(angleX);
    shadowPass.draw(cubeVertices, cubeIndices,
                    std::bind_back(shadowVS, currentModel, lightSpaceMatrix));

    // ==========================================
    // PASS 2: ジオメトリパス (Gバッファへの書き込み)
    // ==========================================
    auto geometryPass =
        device.create_rasterize_pass<InputVertex, MyVarying, float>(
            cameraDepth);

    // 2-1. 床

    currentModel.identity();
    currentMVP = proj * view * currentModel;
    geometryPass.draw(planeVertices, planeIndices,
                      std::bind_back(geometryVS, currentModel, currentMVP),
                      [&](int x, int y, const MyVarying &in) {
                        albedoBuffer.at(x, y) = 'F';
                        normalBuffer.at(x, y) = in.normal;
                        worldPosBuffer.at(x, y) = in.worldPos;
                      });

    // 2-2. キューブ
    currentModel = Mat4::translate(0.0f, 0.3f, 0.0f) * Mat4::rotateY(angleY) *
                   Mat4::rotateX(angleX);
    currentMVP = proj * view * currentModel;
    geometryPass.draw(cubeVertices, cubeIndices,
                      std::bind_back(geometryVS, currentModel, currentMVP),
                      [&](int x, int y, const MyVarying &in) {
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
        std::bind_back(deferredLightingCS, std::ref(colorBuffer),
                       std::cref(albedoBuffer), std::cref(normalBuffer),
                       std::cref(worldPosBuffer), std::cref(shadowDepth),
                       lightSpaceMatrix, lightDir, w, h));
    size_t streamIdx = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        outputStream[streamIdx++] = colorBuffer.at(x, y);
      }
      ++streamIdx;
    }
    outputStream[streamIdx] = '\0';

    // 送信
    // if (send(client_sock, rasterizer.getBuffer(), rasterizer.getBufferSize(),
    //          0) <= 0)
    //   break;
    // 出力

    if (send_frame_compressed(client_sock, outputStream.data(),
                              outputStream.size(), is_raw_send) <= 0)
      break;

    angleX += 0.05f;
    angleY += 0.03f;
    usleep(50000);
  }
  close(client_sock);
}

int main() {
  signal(SIGPIPE, SIG_IGN);
  int server_sock, client_sock;
  struct sockaddr_in server_addr, client_addr;
  socklen_t addr_len = sizeof(client_addr);

  // 1. ソケット作成
  if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("socket failed");
    return -1;
  }

  // ソケットオプション設定 (TIME_WAIT状態でもポートを即再利用できるようにする)
  int opt = 1;
  setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // 2. アドレス設定
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(PORT);

  // 3. Bind
  if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind failed");
    return -1;
  }

  // 4. Listen
  if (listen(server_sock, 3) < 0) {
    perror("listen failed");
    return -1;
  }

  printf("Server listening on port %d...\n", PORT);
  printf("Try running: nc 127.0.0.1 or client -p  %d\n", PORT);

  // 5. Accept Loop
  while (1) {
    if ((client_sock = accept(server_sock, (struct sockaddr *)&client_addr,
                              &addr_len)) < 0) {
      perror("accept failed");
      continue;
    }

    printf("New connection from %s\n", inet_ntoa(client_addr.sin_addr));

    // 子プロセスを作って処理を任せる
    if (fork() == 0) {
      close(server_sock); // 子プロセスにはリスニングソケットは不要
      handle_client(client_sock);
      fprintf(stderr, "Connection closed.\n");
      exit(0);
    } else {
      // 親は，子供を閉じる
      close(client_sock);
    }
  }

  return 0;
}
