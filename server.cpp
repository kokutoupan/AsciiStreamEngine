#include <arpa/inet.h>
#include <math.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "Renderer.hpp"

constexpr int PORT = 12345;

void handle_client(int client_sock) {
  WireframeRenderer renderer;

  float angleX = 0.0f, angleY = 0.0f;
  const char *clear_seq = "\x1b[2J";

  send(client_sock, clear_seq, strlen(clear_seq), 0);

  while (true) {
    const char *out;
    size_t len;
    renderer.render(angleX, angleY, &out, &len);

    // send client
    ssize_t sent = send(client_sock, out, len, 0);
    if (sent <= 0)
      break; // Broken pipe or error

    angleX += 0.05f;
    angleY += 0.03f;
    usleep(50000); // 20 FPS
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
