#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include <zconf.h>
#include <zlib.h>

#define HOST_PORT "12345"
#define HOST_DOMAIN "localhost"

struct termios orig_termios;

// 終了時にターミナルの設定を元に戻す
void disable_raw_mode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

// ターミナルを Raw モード（Enterなしで1文字ずつ即時取得）に変える
void enable_raw_mode() {
  tcgetattr(STDIN_FILENO, &orig_termios);
  atexit(disable_raw_mode); // プログラム終了時に必ず復元

  struct termios raw = orig_termios;
  // カノニカルモード（行単位入力）と、入力のエコー（画面表示）をオフにする
  raw.c_lflag &= ~(ECHO | ICANON);

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int recv_exact(int fd, void *buf, size_t len) {
  size_t recvd = 0;
  char *p = (char *)buf;

  while (recvd < len) {
    int n = recv(fd, p + recvd, len - recvd, 0);
    if (n == 0)
      return 0; // closed
    if (n < 0)
      return -1; // error
    recvd += n;
  }
  return recvd;
}

int main(int argc, char *argv[]) {
  const char *port = HOST_PORT;
  const char *host = HOST_DOMAIN;
  int opt;
  while ((opt = getopt(argc, argv, "p:h:")) != -1) {
    switch (opt) {
    case 'p':
      port = optarg;
      break;
    case 'h':
      host = optarg;
      break;
    default:
      fprintf(stderr, "usage: %s [-p port] [-h domain]\n", argv[0]);
      return 1;
    }
  }

  struct addrinfo hints = {}, *res;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_INET;
  int result = getaddrinfo(host, port, &hints, &res);
  if (result) {
    fprintf(stderr, "info error: %d\n", result);
    return -1;
  }

  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0) {
    fprintf(stderr, "error socket:%d", errno);
    return -1;
  }

  if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
    fprintf(stderr, "failed connect: %d;\n", errno);
    return -1;
  }

  struct winsize ws;
  uint16_t w = 80, h = 24;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
    w = ws.ws_col;
    h = ws.ws_row;
  }

  uint16_t size_packet[2] = {htons(w), htons(h)};
  write(fd, size_packet, sizeof(size_packet));

  // 接続が確立したらターミナルをRawモード化
  enable_raw_mode();

  unsigned char buffer[65536];
  unsigned char out_buf[65536];

  // I/O多重化のための poll 設定
  struct pollfd fds[2];
  fds[0].fd = STDIN_FILENO; // ユーザーのキーボード入力
  fds[0].events = POLLIN;
  fds[1].fd = fd; // サーバーからの画面パケット
  fds[1].events = POLLIN;

  while (1) {
    int ret = poll(fds, 2, -1); // イベントが来るまで無限ブロック
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    // イベント1: キーボードから入力があった場合、サーバーへ即転送
    if (fds[0].revents & POLLIN) {
      char ch;
      if (read(STDIN_FILENO, &ch, 1) > 0) {
        // サーバー側へ送信！
        send(fd, &ch, 1, 0);
      }
    }

    // イベント2: サーバーから描画データが届いた場合、デコードして出力
    if (fds[1].revents & POLLIN) {
      uint32_t net_len;
      if (recv(fd, &net_len, 4, 0) <= 0) {
        break; // サーバー切断
      }

      int len_oder = ntohl(net_len);
      int len = recv_exact(fd, buffer, len_oder);
      if (len <= 0)
        break;

      uLongf out_len = sizeof(out_buf);
      int res = uncompress(out_buf, &out_len, buffer, len);

      if (res == Z_OK) {
        write(1, out_buf, out_len);
      }
    }
  }

  close(fd);
  freeaddrinfo(res);
  return 0;
}
