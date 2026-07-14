#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <astream/Texture2D.hpp>

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
 * @brief 既存の Texture2D<char>
 * に対して、自動折り返しを考慮しながら文字列を書き込む
 * @param view 描画対象のテクスチャ参照
 * @param startX 書き込み開始位置のX座標
 * @param startY 書き込み開始位置のY座標
 * @param text 描画する文字列
 * @return 領域内にすべての文字が収まれば true、縦方向にオーバーフローしたら
 * false
 */
inline bool drawText(TextureView<char> view, int startX, int startY,
                     const std::string_view &text) noexcept {
  int cx = startX;
  int cy = startY;

  const int texW = view.width();
  const int texH = view.height();

  for (char ch : text) {
    // キャリッジリターン (\r) はスキップして \n に統一して処理
    if (ch == '\r') [[unlikely]] {
      continue;
    }

    if (ch == '\n') {
      cy++;
      cx = 0;
      continue;
    }

    if (cx >= texW) {
      cy++;
      cx = 0;
    }

    if (cy >= texH) [[unlikely]] {
      return false;
    }

    if (cx >= 0 && cy >= 0 && cx < texW && cy < texH) [[likely]] {
      view[cy, cx] = ch;
    }

    cx++;
  }

  // ループ終了後、最終的な行が縦幅を超えていなければ成功
  return cy < texH;
}

/**
 * @brief 境界枠（ボックス）を既存のテクスチャに描画する
 * @param view 描画対象のテクスチャ参照
 * @param x 開始X座標
 * @param y 開始Y座標
 * @param w 幅
 * @param h 高さ
 */
inline void drawBox(TextureView<char> view, int x, int y, int w, int h) {
  if (w <= 2 || h <= 2)
    return;
  drawText(view, x, y, "+" + std::string(w - 2, '-') + "+");
  drawText(view, x, y + h - 1, "+" + std::string(w - 2, '-') + "+");
  for (int row = y + 1; row < y + h - 1; ++row) {
    drawText(view, x, row, "|");
    drawText(view, x + w - 1, row, "|");
  }
}

namespace detail {
template <typename T>
void blit_texture_impl(TextureView<T> dst, TextureView<const T> src, int posX,
                       int posY) {
  int dstW = dst.width();
  int dstH = dst.height();
  int srcW = src.width();
  int srcH = src.height();

  if (posX >= dstW || posY >= dstH || posX + srcW <= 0 || posY + srcH <= 0) {
    return;
  }

  int srcStartX = std::max(0, -posX);
  int srcStartY = std::max(0, -posY);
  int srcEndX = std::min(srcW, dstW - posX);
  int srcEndY = std::min(srcH, dstH - posY);
  int copyWidth = srcEndX - srcStartX;

  for (int srcY = srcStartY; srcY < srcEndY; ++srcY) {
    int dstY = posY + srcY;
    T *dstRow = &dst[dstY, posX + srcStartX];
    const T *srcRow = &src[srcY, srcStartX];

    std::copy_n(srcRow, copyWidth, dstRow);
  }
}
} // namespace detail

/**
 * @brief テクスチャを別のテクスチャへ等倍で合成 (Blit) する（行単位コピー版）
 * @param dst 転送先のテクスチャ (例: メインのカラーバッファ)
 * @param src 転送元のテクスチャ (例: 文字列テクスチャ)
 * @param posX 転送先における左上のX座標
 * @param posY 転送先における左上のY座標
 */
template <typename Dst, typename Src>
  requires convertible_to_texture_view<
               Dst, typename std::remove_cvref_t<
                        decltype(*std::declval<Dst>().data())>> &&
           convertible_to_texture_view<
               Src, typename std::remove_cvref_t<
                        decltype(*std::declval<Src>().data())>>
inline void blit_texture(Dst &&dst, Src &&src, int posX, int posY) {
  using DstValue = std::remove_cvref_t<decltype(*std::declval<Dst>().data())>;
  using SrcValue = std::remove_cvref_t<decltype(*std::declval<Src>().data())>;

  TextureView<DstValue> dstView = std::forward<Dst>(dst);
  TextureView<const SrcValue> srcView = std::forward<Src>(src);

  detail::blit_texture_impl(dstView, srcView, posX, posY);
}

} // namespace TextureUtil
