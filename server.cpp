#include <arpa/inet.h>
#include <cstdint>
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

#include "AsciiRasterizer.hpp"

constexpr int PORT = 12345;

int send_frame_compressed(int sock, const char *raw_data, size_t raw_len) {
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

  // 70文字の超高解像度パレット (黒背景用: 暗→明)
  const char *palette = " .'`^\",:;Il!i~+_-?][}"
                        "{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";

  // 文字数 - 1 (null文字分)
  int len = strlen(palette);

  // 0.0~1.0 を インデックスにマッピング
  int idx = (int)(intensity * (len - 1));

  if (idx < 0)
    idx = 0;
  if (idx >= len)
    idx = len - 1;

  return palette[idx];
}

// 頂点データ
struct MyVertex {
  Vec3 normal; // 法線
  Vec2 uv;

  // 属性の線形補間 (ラスタライザーで使う)
  MyVertex operator+(const MyVertex &r) const {
    return {normal + r.normal, uv + r.uv};
  }
  MyVertex operator*(float s) const { return {normal * s, uv * s}; }
};
// [Attribute] モデルデータとして保持する頂点構造
struct InputVertex {
  Vec3 position; // 座標
  Vec3 normal;   // 法線
  Vec2 uv;
};

// 頂点データ (座標 + 法線 + UV)
// 24頂点 (面ごとに独立)
std::vector<InputVertex> cubeVertices = {
    // --- Back Face (Z = -1) ---
    // Pos                      Normal              UV
    {{1, -1, -1}, {0, 0, -1}, {0.0f, 1.0f}},  // 0: Right Bottom
    {{-1, -1, -1}, {0, 0, -1}, {1.0f, 1.0f}}, // 1: Left Bottom
    {{-1, 1, -1}, {0, 0, -1}, {1.0f, 0.0f}},  // 2: Left Top
    {{1, 1, -1}, {0, 0, -1}, {0.0f, 0.0f}},   // 3: Right Top

    // --- Front Face (Z = +1) ---
    {{-1, -1, 1}, {0, 0, 1}, {0.0f, 1.0f}}, // 4: Left Bottom
    {{1, -1, 1}, {0, 0, 1}, {1.0f, 1.0f}},  // 5: Right Bottom
    {{1, 1, 1}, {0, 0, 1}, {1.0f, 0.0f}},   // 6: Right Top
    {{-1, 1, 1}, {0, 0, 1}, {0.0f, 0.0f}},  // 7: Left Top

    // --- Left Face (X = -1) ---
    {{-1, -1, -1}, {-1, 0, 0}, {0.0f, 1.0f}}, // 8:  Back Bottom
    {{-1, -1, 1}, {-1, 0, 0}, {1.0f, 1.0f}},  // 9:  Front Bottom
    {{-1, 1, 1}, {-1, 0, 0}, {1.0f, 0.0f}},   // 10: Front Top
    {{-1, 1, -1}, {-1, 0, 0}, {0.0f, 0.0f}},  // 11: Back Top

    // --- Right Face (X = +1) ---
    {{1, -1, 1}, {1, 0, 0}, {0.0f, 1.0f}},  // 12: Front Bottom
    {{1, -1, -1}, {1, 0, 0}, {1.0f, 1.0f}}, // 13: Back Bottom
    {{1, 1, -1}, {1, 0, 0}, {1.0f, 0.0f}},  // 14: Back Top
    {{1, 1, 1}, {1, 0, 0}, {0.0f, 0.0f}},   // 15: Front Top

    // --- Top Face (Y = +1) ---
    {{-1, 1, 1}, {0, 1, 0}, {0.0f, 1.0f}},  // 16: Left Front
    {{1, 1, 1}, {0, 1, 0}, {1.0f, 1.0f}},   // 17: Right Front
    {{1, 1, -1}, {0, 1, 0}, {1.0f, 0.0f}},  // 18: Right Back
    {{-1, 1, -1}, {0, 1, 0}, {0.0f, 0.0f}}, // 19: Left Back

    // --- Bottom Face (Y = -1) ---
    {{-1, -1, -1}, {0, -1, 0}, {0.0f, 1.0f}}, // 20: Left Back
    {{1, -1, -1}, {0, -1, 0}, {1.0f, 1.0f}},  // 21: Right Back
    {{1, -1, 1}, {0, -1, 0}, {1.0f, 0.0f}},   // 22: Right Front
    {{-1, -1, 1}, {0, -1, 0}, {0.0f, 0.0f}}   // 23: Left Front
};

// インデックスデータ (24頂点対応版)
// すべて反時計回り (Counter-Clockwise)
std::vector<int> cubeIndices = {
    0,  1,  2,  0,  2,  3,  // Back
    4,  5,  6,  4,  6,  7,  // Front
    8,  9,  10, 8,  10, 11, // Left
    12, 13, 14, 12, 14, 15, // Right
    16, 17, 18, 16, 18, 19, // Top
    20, 21, 22, 20, 22, 23  // Bottom
};

void handle_client(int client_sock) {
  int w = 80, h = 24; // デフォルト値

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
  } else {
    // ncなどが接続した場合 (タイムアウト or データなし)
    printf("Using default size: %d x %d\n", w, h);
  }

  // 3. タイムアウト解除 (ブロッキングモードに戻す)
  tv.tv_sec = 0;
  tv.tv_usec = 0;
  setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv,
             sizeof(tv));

  AsciiRasterizer<MyVertex> rasterizer(w, h); // テンプレート型を指定

  float angleX = 0.0f, angleY = 0.0f;
  const char *clear_seq = "\x1b[2J";

  //send(client_sock, clear_seq, strlen(clear_seq), 0);
  send_frame_compressed(client_sock,clear_seq, strlen(clear_seq));

  // 1. shaderに共有する変数
  Mat4 mvp;
  Mat4 model;

  // --- 2. シェーダーの定義 (Programmable Shader) ---

  // Vertex Shader: InputVertexを受け取り、pair<Vec4, MyVertex>を返す
  auto vs = [&](const InputVertex &in) -> std::pair<Vec4, MyVertex> {
    // 1. システム座標 (SV_Position)
    Vec4 pos =
        mvp.transform(Vec4(in.position.x, in.position.y, in.position.z, 1.0f));

    // 2. ユーザー属性 (Varying)
    MyVertex outVar;
    outVar.normal = model.transform(in.normal).normalize();
    outVar.uv = in.uv;

    return {pos, outVar};
  };

  // 平行光源
  Vec3 lightDir = Vec3(0.5f, 1.0f, 1.0f).normalize();
  float paraL = 1.0;

  float ambient = 0.1;
  // 8x8 の "C++" ロゴテクスチャ (ASCIIアート)
  const char texture[8][9] = {"########", "##    ##", "# C  C #", "#      #",
                              "# +  + #", "#      #", "##    ##", "########"};
  // 向き確認用テクスチャ (矢印とか文字とか)
  const char dev_texture[8][9] = {
      "########", // 0
      "##11  ##", // 1
      "##  2 ##", // 2
      "##   3##", // 3
      "##  4 ##", // 4
      "## 5  ##", // 5
      "##66  ##", // 6
      "########"  // 7
  };
  // Fragment Shader: MyVertex (補間済み) を受け取る
  auto fs = [&](const MyVertex &in) -> char {
    float diff = std::max(0.0f, in.normal.dot(lightDir));

    float col = diff * paraL + ambient;
    col = std::min(1.0f, col);
    // 2. テクスチャサンプリング (UV座標を使う)
    // UVは 0.0~1.0 なので、配列インデックス 0~7 に変換
    int u = (int)(in.uv.x * 8.0f) % 8;
    int v = (int)(in.uv.y * 8.0f) % 8;

    // 負の値ケア
    if (u < 0)
      u += 8;
    if (v < 0)
      v += 8;

    char texChar = texture[v][u];

    // 3. テクスチャとライティングのブレンド
    // テクスチャが空白なら陰影のみ、文字があればそれを表示
    if (texChar != ' ') {
      return texChar;
    } else {
      // 背景部分は陰影文字
      return mapIntensityToChar(col);
    }
  };

  while (true) {

    float currentW = (float)rasterizer.getWidth();
    float currentH = (float)rasterizer.getHeight();

    float aspect = (currentH > 0) ? (currentW / currentH * 0.5f) : 1.0f;

    // Model Matrix: オブジェクト自身の変換 (回転)
    model = Mat4::rotateY(angleY) * Mat4::rotateX(angleX);

    // View Matrix: カメラの位置 (Z = -4.0f)
    Mat4 view = Mat4::translate(0, 0, -4.0f);

    // Projection Matrix: カメラのレンズ設定
    Mat4 proj = Mat4::perspective(1.0f, aspect, 0.1f, 100.0f);

    // MVP行列: シェーダーに渡すための合成行列
    mvp = proj * view * model;

    // --- 2. 描画 (Draw Call) ---
    rasterizer.clear();
    rasterizer.draw<InputVertex>(cubeVertices, cubeIndices, vs, fs);

    // 送信
    // if (send(client_sock, rasterizer.getBuffer(), rasterizer.getBufferSize(),
    //          0) <= 0)
    //   break;
    if (send_frame_compressed(client_sock, rasterizer.getBuffer(),
                              rasterizer.getBufferSize()) <= 0)
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
