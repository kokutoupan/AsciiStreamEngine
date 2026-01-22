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

// モデルデータ (立方体)
std::vector<Vertex> cubeVertices = {
    {{-1, -1, -1}, {-0.57, -0.57, -0.57}}, {{1, -1, -1}, {0.57, -0.57, -0.57}},
    {{1, 1, -1}, {0.57, 0.57, -0.57}},     {{-1, 1, -1}, {-0.57, 0.57, -0.57}},
    {{-1, -1, 1}, {-0.57, -0.57, 0.57}},   {{1, -1, 1}, {0.57, -0.57, 0.57}},
    {{1, 1, 1}, {0.57, 0.57, 0.57}},       {{-1, 1, 1}, {-0.57, 0.57, 0.57}}};
std::vector<int> cubeIndices = {
    // Back (修正: 順序反転)
    2, 1, 0, 3, 2, 0,
    // Front (OK)
    4, 5, 6, 4, 6, 7,
    // Left (OK)
    0, 4, 7, 0, 7, 3,
    // Right (修正: 順序反転)
    6, 5, 1, 2, 6, 1,
    // Top (修正: 順序反転)
    6, 2, 3, 7, 6, 3,
    // Bottom (OK)
    0, 1, 5, 0, 5, 4};

void handle_client(int client_sock) {
  AsciiRasterizer rasterizer;

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

    // Vertex Shader: ラムダ式で記述
    // [キャプチャ]で外部の行列(mvp, model)を取り込む
    VertexShader vs = [&](const Vertex &in) -> Vertex {
      Vertex out;
      // 座標変換: Model -> View -> Proj
      out.position = mvp.transform(in.position);

      // ビューポート変換: NDC [-1, 1] -> Screen [80, 24]
      out.position.x = (out.position.x + 1.0f) * 0.5f * 80.0f;
      out.position.y = (1.0f - out.position.y) * 0.5f * 24.0f; // Y反転

      // 法線変換: 回転だけ適用 (Model行列)
      out.normal = model.transform(in.normal).normalize();
      return out;
    };

    // Fragment Shader: ラムダ式で記述
    Vec3 lightDir = Vec3(0.0f, 0.0f, -2.0f).normalize();
    const char *palette = " .:-=+*#%@";

    FragmentShader fs = [&](const Vertex &in) -> char {
      // ライティング計算: 法線とライトの内積 (Lambert)
      float diff = std::max(0.0f, in.normal.dot(lightDir));
      int idx = (int)(diff * 9.0f);
      if (idx > 8)
        idx = 8;
      if (idx == 0) {
        idx = 1;
      }
      return palette[idx];
    };

    // --- 3. 描画 (Draw Call) ---
    rasterizer.clear();
    rasterizer.draw(cubeVertices, cubeIndices, vs, fs);

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
