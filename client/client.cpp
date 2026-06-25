#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <print>
#include <string_view>
#include <random>
#include <format>
#include <string>

// =============================================================================
// OS固有のヘッダー定義とシステムコール/ネイティブAPIのマッピング
// =============================================================================
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#define close closesocket

// Windows用のグローバル状態
DWORD orig_console_mode;
HANDLE hStdin, hStdout;
// クライアント固有のウィンドウタイトルを保持する変数
std::string g_unique_title;
#else
// Linux
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;
#endif

// zlib は双方共通
#include <zconf.h>
#include <zlib.h>

#define HOST_PORT "12345"
#define HOST_DOMAIN "localhost"


#if defined(_WIN32)
// Windows用のキーマッピングと送信処理
void scan_and_send_keys(SOCKET fd) {
    HWND hFg = GetForegroundWindow();
    if (hFg == NULL) return;

    if (hFg != GetConsoleWindow()) {
        // Windows ターミナル環境のタブ判定
        char title[512] = {0};
        GetWindowTextA(hFg, title, sizeof(title));

        if (!std::string_view(title).contains(g_unique_title)) {
          return;
        }
    }

    // 1. Shiftキーの押下状態を取得（大文字・小文字、および記号の判定用）
    bool is_shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    // 2. アルファベットキー（'A' ~ 'Z'）のチェック
    for (int vk = 'A'; vk <= 'Z'; vk++) {
        // GetAsyncKeyStateの最上位ビットが1なら、現在押されている
        if (GetAsyncKeyState(vk) & 0x8000) {
            char ch;
            if (is_shift) {
                ch = (char)vk; // 大文字 'A' ~ 'Z'
            }
            else {
                ch = (char)(vk + ('a' - 'A')); // 小文字 'a' ~ 'z'
            }
            send(fd, &ch, 1, 0);
        }
    }

    // 3. 数字キー（'0' ~ '9'）のチェック
    for (int vk = '0'; vk <= '9'; vk++) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            char ch = (char)vk;
            send(fd, &ch, 1, 0);
        }
    }

    // 4. 特殊キーのチェック（Space, Enter, Escape）
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) {
        char ch = ' ';
        send(fd, &ch, 1, 0);
    }
    if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
        char ch = '\n';
        send(fd, &ch, 1, 0);
    }
    if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
        char ch = 0x1b; // \x1b 単体は Escape キー
        send(fd, &ch, 1, 0);
    }

    // 5. 矢印キーのチェック（Linuxのエスケープシーケンスに翻訳して送信）
    // Up: \x1b[A, Down: \x1b[B, Right: \x1b[C, Left: \x1b[D
    if (GetAsyncKeyState(VK_UP) & 0x8000) {
        const char seq[3] = { 0x1b, '[', 'A' };
        send(fd, seq, 3, 0);
    }
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
        const char seq[3] = { 0x1b, '[', 'B' };
        send(fd, seq, 3, 0);
    }
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
        const char seq[3] = { 0x1b, '[', 'C' };
        send(fd, seq, 3, 0);
    }
    if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
        const char seq[3] = { 0x1b, '[', 'D' };
        send(fd, seq, 3, 0);
    }

    // 6. 安全な記号類のチェック
    // WindowsのJIS/US配列の違いを考慮しつつ、一般的なキーコードからマッピング
    struct KeyMap { int vk; char normal_ch; char shift_ch; };
    static const KeyMap gem_symbols[] = {
        { VK_OEM_MINUS,  '-', '=' }, // US/JISでズレる可能性がありますが、代表的な配置
        { VK_OEM_PLUS,   '+', ';' },
        { VK_OEM_PERIOD, '.', '>' },
        { VK_OEM_COMMA,  ',', '<' },
        { VK_OEM_2,      '/', '?' }, // スラッシュ・クエスチョン
        { '1',           '1', '!' }, // 1の裏のビックリマーク
        { '8',           '8', '*' }, // 配列依存対策で直打ちできるように
    };

    for (const auto& mapping : gem_symbols) {
        if (GetAsyncKeyState(mapping.vk) & 0x8000) {
            char ch = is_shift ? mapping.shift_ch : mapping.normal_ch;

            // サーバーのInputDeviceが拾える「安全な記号」のみをフィルタリングして送信
            if (ch == '-' || ch == '+' || ch == '=' || ch == '*' ||
                ch == '/' || ch == '.' || ch == ',' || ch == '?' || ch == '!') {
                send(fd, &ch, 1, 0);
            }
        }
    }
}
#endif

// =============================================================================
// ターミナル制御 (Rawモード切り替え)
// =============================================================================
void disable_raw_mode() {
#if defined(_WIN32)
    SetConsoleMode(hStdin, orig_console_mode);
#else
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
#endif
}

void enable_raw_mode() {
#if defined(_WIN32)
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    GetConsoleMode(hStdin, &orig_console_mode);
    atexit(disable_raw_mode);

    // カノニカルモード（行単位入力）と入力を画面に表示するエコーをオフにする
    DWORD raw_mode = orig_console_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    // 仮想ターミナル入力を有効化 (エスケープシーケンス等の解析に必要)
    raw_mode |= ENABLE_VIRTUAL_TERMINAL_INPUT;
    SetConsoleMode(hStdin, raw_mode);

    // 出力側もエスケープシーケンス（画面クリア等）を有効化
    DWORD out_mode;
    GetConsoleMode(hStdout, &out_mode);
    SetConsoleMode(hStdout, out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
}

// =============================================================================
// ターミナル画面サイズ取得
// =============================================================================
void get_terminal_size(uint16_t& w, uint16_t& h) {
    w = 80; h = 24; // デフォルト値
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != -1) {
        w = ws.ws_col;
        h = ws.ws_row;
    }
#endif
}

// =============================================================================
// 受信ヘルパー (正確なバイト数を取得するまでループ)
// =============================================================================
int recv_exact(int fd, void* buf, size_t len) {
    size_t recvd = 0;
    char* p = (char*)buf;

    while (recvd < len) {
        int n = recv(fd, p + recvd, (int)(len - recvd), 0);
        if (n == 0) return 0;  // closed
        if (n < 0)  return -1; // error
        recvd += n;
    }
    return (int)recvd;
}

// =============================================================================
// メイン関数
// =============================================================================
int main(int argc, char* argv[]) {
#if defined(_WIN32)
    // 1. ハードウェア由来のシードを使ってメルセンヌ・ツイスタ乱数生成器を初期化
    std::random_device rd;
    std::mt19937 gen(rd());
    // 2. 32ビット整数の範囲でランダムな値を生成
    std::uniform_int_distribution<uint32_t> dis;

    g_unique_title = std::format("AsciiStreamEngine_Client_{:08X}", dis(gen));

    SetConsoleTitleA(g_unique_title.c_str());
#endif

    const char* port = HOST_PORT;
    const char* host = HOST_DOMAIN;

    // Windows環境対応の簡易引数解析 
    for (int i = 1; i < argc; i++) {
        std::string_view arg(argv[i]);
        if (arg == "-p" && i + 1 < argc) {
            port = argv[++i];
        }
        else if (arg == "-h" && i + 1 < argc) {
            host = argv[++i];
        }
        else {
            std::println(std::cerr, "usage: {} [-p port] [-h domain]", argv[0]);
            return 1;
        }
    }

#if defined(_WIN32)
    // Windows固有：ネットワークサブシステムの初期化システムコール呼び出し
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::println(std::cerr, "WSAStartup failed");
        return -1;
    }
#endif

    struct addrinfo hints = {}, * res;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;
    int result = getaddrinfo(host, port, &hints, &res);
    if (result) {
        std::println(std::cerr, "info error: {}", result);
        return -1;
    }

    int fd = (int)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
#if defined(_WIN32)
        std::println(std::cerr, "error socket:{}", WSAGetLastError());
#else
        std::println(std::cerr, "error socket:{}", errno);
#endif
        return -1;
    }

    if (connect(fd, res->ai_addr, (int)res->ai_addrlen) < 0) {
#if defined(_WIN32)
        std::println(std::cerr, "failed connect: {};", WSAGetLastError());
#else
        std::println(std::cerr, "failed connect: {};", errno);
#endif
        return -1;
    }

    // 画面サイズの取得
    uint16_t w, h;
    get_terminal_size(w, h);

    uint16_t size_packet[2] = { htons(w), htons(h) };
    send(fd, (const char*)size_packet, sizeof(size_packet), 0);

    // 接続が確立したらターミナルをRawモード化
    enable_raw_mode();

    unsigned char buffer[65536];
    unsigned char out_buf[65536];

#if defined(_WIN32)
    // Windows用多重化：WSAPollにはソケットのみ登録 (1つのイベントを監視)
    WSAPOLLFD fds[1];
    fds[0].fd = fd;
    fds[0].events = POLLIN;
#else
    // Linux用多重化：標準入力とソケットの2つを監視
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = fd;
    fds[1].events = POLLIN;
#endif

    while (1) {
#if defined(_WIN32)
        // --- Windows 側の入力検知ロジック ---
        // Engine側のInputDeviceが想定しているキーマップ(W,A,S,D,矢印キー等)をWin32非同期APIでスキャンして送信
        // 仮想キーコード: 矢印(0x25~0x28), アルファベットはそのまま大文字
        scan_and_send_keys(fd);

        // Windows側は30FPS相当（33ms）のタイムアウトでノンブロッキングにサーバーからのパケットを監視
        int ret = WSAPoll(fds, 1, 33);
        if (ret < 0) break;
#else
        // --- Linux 側のブロッキング多重化ロジック ---
        int ret = poll(fds, 2, -1);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        // イベント1: キーボードから入力があった場合、サーバーへ即転送
        if (fds[0].revents & POLLIN) {
            char ch;
            if (read(STDIN_FILENO, &ch, 1) > 0) {
                send(fd, &ch, 1, 0);
            }
        }
#endif

        // イベント2: サーバーから描画データが届いた場合、デコードして出力
        // Windowsの場合はインデックス0、Linuxの場合はインデックス1がサーバー用fd
        auto& server_fd_event = fds[
#if defined(_WIN32)
            0
#else
            1
#endif
        ];

        if (server_fd_event.revents & POLLIN) {
            uint32_t net_len;
            if (recv(fd, (char*)&net_len, 4, 0) <= 0) {
                break; // サーバー切断
            }

            int len_oder = ntohl(net_len);
            int len = recv_exact(fd, buffer, len_oder);
            if (len <= 0)
                break;

            uLongf out_len = sizeof(out_buf);
            int res = uncompress(out_buf, &out_len, buffer, len);

            if (res == Z_OK) {
#if defined(_WIN32)
                DWORD written;
                WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), out_buf, (DWORD)out_len, &written, NULL);
#else
                write(1, out_buf, out_len);
#endif
            }
        }
    }

    close(fd);
    freeaddrinfo(res);

#if defined(_WIN32)
    WSACleanup();
#endif

    return 0;
}
