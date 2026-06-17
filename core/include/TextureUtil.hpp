#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "Texture2D.hpp"

namespace TextureUtil {

/**
 * @brief 改行を含む文字列から Texture2D<char> を生成する
 * @param text 対象の文字列 (\n 区切りに対応)
 * @param background 空白部分を埋めるデフォルト文字
 */
inline Texture2D<char> strToTexture(const std::string_view &text,
                                    char background = ' ') {
  std::vector<std::string_view> lines;
  std::string line;
  std::size_t maxWidth = 0;

  std::size_t pos = 0;
  while (pos < text.length()) {
    std::size_t next_nl = text.find('\n', pos);
    std::string_view line = (next_nl == std::string_view::npos)
                                ? text.substr(pos)
                                : text.substr(pos, next_nl - pos);

    // キャリッジリターンを除去
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    maxWidth = std::max(maxWidth, line.length());
    lines.push_back(line);

    if (next_nl == std::string_view::npos)
      break;
    pos = next_nl + 1;
  }

  int w = static_cast<int>(maxWidth);
  int h = static_cast<int>(lines.size());

  // 最低限のサイズを保証
  if (w == 0)
    w = 1;
  if (h == 0)
    h = 1;

  Texture2D<char> tex(w, h, background);

  for (int y = 0; y < h; ++y) {
    const auto &currentLine = lines[y];
    for (int x = 0; x < w; ++x) {
      if (x < static_cast<int>(currentLine.length())) {
        tex.at(x, y) = currentLine[x];
      }
    }
  }

  return tex;
}

/**
 * @brief テクスチャを別のテクスチャへ等倍で合成 (Blit) する（行単位コピー版）
 * @param dst 転送先のテクスチャ (例: メインのカラーバッファ)
 * @param src 転送元のテクスチャ (例: 文字列テクスチャ)
 * @param posX 転送先における左上のX座標
 * @param posY 転送先における左上のY座標
 */
template <typename T>
inline void blit_texture(Texture2D<T> &dst, const Texture2D<T> &src, int posX,
                         int posY) {
  int dstW = dst.getWidth();
  int dstH = dst.getHeight();
  int srcW = src.getWidth();
  int srcH = src.getHeight();

  // 完全に画面外なら即終了
  if (posX >= dstW || posY >= dstH || posX + srcW <= 0 || posY + srcH <= 0) {
    return;
  }

  // クラッピング（転送可能な矩形範囲を計算）
  int srcStartX = std::max(0, -posX);
  int srcStartY = std::max(0, -posY);
  int srcEndX = std::min(srcW, dstW - posX);
  int srcEndY = std::min(srcH, dstH - posY);

  int copyWidth = srcEndX - srcStartX;

  for (int srcY = srcStartY; srcY < srcEndY; ++srcY) {
    int dstY = posY + srcY;

    T *dstRow = dst.getData() + (dstY * dstW) + (posX + srcStartX);
    const T *srcRow = src.getData() + (srcY * srcW) + srcStartX;

    // 透過処理がない場合は、1行丸ごと一撃でコピー
    std::copy_n(srcRow, copyWidth, dstRow);
  }
}

} // namespace TextureUtil
