#include <arpa/inet.h>
#include <math.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "AsciiRasterizer.hpp"

constexpr int PORT = 12345;

// 頂点データ
struct MyVertex {
  Vec3 normal; // 法線

  // 属性の線形補間 (ラスタライザーで使う)
  MyVertex operator+(const MyVertex &r) const { return {normal + r.normal}; }
  MyVertex operator*(float s) const { return {normal * s}; }
};
// [Attribute] モデルデータとして保持する頂点構造
struct InputVertex {
  Vec3 position; // 座標
  Vec3 normal;   // 法線
};

// 頂点データ (座標 + 法線)
std::vector<InputVertex> cubeVertices = {
    // x, y, z      nx, ny, nz
    {{-1, -1, -1}, {-0.577f, -0.577f, -0.577f}}, // 0: Left-Bottom-Back
    {{1, -1, -1}, {0.577f, -0.577f, -0.577f}},   // 1: Right-Bottom-Back
    {{1, 1, -1}, {0.577f, 0.577f, -0.577f}},     // 2: Right-Top-Back
    {{-1, 1, -1}, {-0.577f, 0.577f, -0.577f}},   // 3: Left-Top-Back
    {{-1, -1, 1}, {-0.577f, -0.577f, 0.577f}},   // 4: Left-Bottom-Front
    {{1, -1, 1}, {0.577f, -0.577f, 0.577f}},     // 5: Right-Bottom-Front
    {{1, 1, 1}, {0.577f, 0.577f, 0.577f}},       // 6: Right-Top-Front
    {{-1, 1, 1}, {-0.577f, 0.577f, 0.577f}}      // 7: Left-Top-Front
};

// インデックスデータ (半時計回り)
std::vector<int> cubeIndices = {
    // Back Face
    2, 1, 0, 3, 2, 0,
    // Front Face
    4, 5, 6, 4, 6, 7,
    // Left Face
    0, 4, 7, 0, 7, 3,
    // Right Face
    6, 5, 1, 2, 6, 1,
    // Top Face
    3, 7, 6, 2, 3, 6,
    // Bottom Face
    0, 1, 5, 0, 5, 4};

void handle_client(int client_sock) {
  AsciiRasterizer<MyVertex> rasterizer; // テンプレート型を指定

  float angleX = 0.0f, angleY = 0.0f;
  const char *clear_seq = "\x1b[2J";

  send(client_sock, clear_seq, strlen(clear_seq), 0);

  while (true) {
    // Model Matrix: オブジェクト自身の変換 (回転)
    Mat4 model = Mat4::rotateY(angleY) * Mat4::rotateX(angleX);

    // View Matrix: カメラの位置 (Z = -4.0f)
    Mat4 view = Mat4::translate(0, 0, -4.0f);

    // Projection Matrix: カメラのレンズ設定
    Mat4 proj = Mat4::perspective(1.0f, 80.0f / 24.0f * 0.5f, 0.1f, 100.0f);

    // MVP行列: シェーダーに渡すための合成行列
    Mat4 mvp = proj * view * model;

    // --- 2. シェーダーの定義 (Programmable Shader) ---

    // Vertex Shader: InputVertexを受け取り、pair<Vec4, MyVertex>を返す
    auto vs = [&](const InputVertex &in) -> std::pair<Vec4, MyVertex> {
      // 1. システム座標 (SV_Position)
      Vec4 pos = mvp.transform(
          Vec4(in.position.x, in.position.y, in.position.z, 1.0f));

      // 2. ユーザー属性 (Varying)
      MyVertex outVar;
      outVar.normal = model.transform(in.normal).normalize();

      return {pos, outVar};
    };

    // 平行光源
    Vec3 lightDir = Vec3(0.5f, 1.0f, 1.0f).normalize();

    // Fragment Shader: MyVertex (補間済み) を受け取る
    auto fs = [&](const MyVertex &in) -> char {
      float diff = std::max(0.0f, in.normal.dot(lightDir));

      const char *palette = " .:-=+*#%@";
      int idx = (int)(diff * 9.0f) + 1;
      if (idx > 8)
        idx = 8;

      return palette[idx];
    };

    // --- 3. 描画 (Draw Call) ---
    rasterizer.clear();
    rasterizer.draw<InputVertex>(cubeVertices, cubeIndices, vs, fs);

    // 送信
    if (send(client_sock, rasterizer.getBuffer(), rasterizer.getBufferSize(),
             0) <= 0)
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
