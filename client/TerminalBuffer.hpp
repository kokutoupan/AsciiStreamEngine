#pragma once

#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <thread>


class TerminalBuffer {
private:
  uint8_t current_width = 0;
  uint8_t current_height = 0;
  std::string output_buffer;

  // 1. フォーマット（\x1b[H と \n の再配置）を専門に行う非公開関数
  void reformat(uint8_t w, uint8_t h) {
    current_width = w;
    current_height = h;

    size_t required_size = 3 + (w * h) + h - 1;
    output_buffer.resize(required_size);

    // 制御文字の初期配置
    output_buffer[0] = '\x1b';
    output_buffer[1] = '[';
    output_buffer[2] = 'H';

    for (uint8_t y = 0; y < h - 1; ++y) {
      size_t newline_pos = 3 + (y * (w + 1)) + w;
      output_buffer[newline_pos] = '\n';
    }
  }

public:
  // 1. サイズ変更が必要かチェックして適宜リサイズする関数
  void ensure_size(uint8_t w, uint8_t h) {
    if (w != current_width || h != current_height) {
      reformat(w, h);
    }
  }

  // 2. 純粋にコピーして画面に出力する関数
  void draw(std::span<const unsigned char> raw_chars) {
    // 現在の w と h の情報を使って安全にコピー
    char *dest_ptr = output_buffer.data() + 3;

    for (uint8_t y = 0; y < current_height; ++y) {
      const unsigned char *row_start = raw_chars.data() + (y * current_width);

      // std::copy で隙間に安全に流し込む
      std::copy(row_start, row_start + current_width, dest_ptr);

      dest_ptr += current_width + 1;
    }

    // 一括出力
    std::cout.write(output_buffer.data(), output_buffer.size());
  }

  // 必要に応じて現在のサイズを取得できるゲッター
  uint8_t width() const { return current_width; }
  uint8_t height() const { return current_height; }
};
