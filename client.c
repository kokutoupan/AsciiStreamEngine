
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define HOST_PORT "12345"
#define HOST_DOMAIN "localhost"

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

  // 構造体に詰めて送る
  // (ネットワークバイトオーダー変換は省略するが、本当はhtonsすべき)
  // 今回は同一マシンやリトルエンディアン同士と仮定してそのまま送る
  uint16_t size_packet[2] = {w, h};
  write(fd, size_packet, sizeof(size_packet));

  char buffer[4096];

  while (1) {

    int len = recv(fd, buffer, 4096, 0);
    if (len <= 0) {
      break;
    }
    write(1, buffer, len);
  }

  close(fd);
  freeaddrinfo(res);
  return 0;
}
