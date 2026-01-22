#pragma once
#include <vector>
#include <cstring>

class WireframeRenderer {
private:
    // 描画用 (ロジック用)
    static const int WIDTH = 80;
    static const int HEIGHT = 24;
    char grid[HEIGHT][WIDTH];

    // 送信用 (ネットワークに流すバイト列そのもの)
    std::vector<char> send_buffer;

    struct Vec3 { float x, y, z; };
    std::vector<Vec3> vertices;
    std::vector<std::pair<int, int>> edges;

public:
    WireframeRenderer();
    
    // バッファのポインタとサイズをペアで返す (C++17 string_viewでも可だが、古いやつでまずそうなのでとりあえずポインタ)
    void render(float angleX, float angleY, const char** out_ptr, size_t* out_size);
    
private:
    void clear();
    void drawLine(int x0, int y0, int x1, int y1);
    // バッファへの転送用
    void flushToBuffer();
};
