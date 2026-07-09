#pragma once

#include <algorithm>
#include <cctype>
#include <ranges>
#include <string>
#include <string_view>

namespace astream::util {

/**
 * @brief C++23基準の超軽量トリム関数
 * @param sv トリーム対象の string_view
 * @return 前後の空白（スペース、タブ、改行等）を除去した string_view
 */
[[nodiscard]] constexpr std::string_view trim(std::string_view sv) noexcept {
  // 前方の空白をスキップ
  auto start = std::ranges::find_if_not(
      sv, [](unsigned char c) { return std::isspace(c); });
  // 後方の空白をスキップ（逆方向イテレータ）
  auto end =
      std::ranges::find_if_not(sv | std::views::reverse,
                               [](unsigned char c) { return std::isspace(c); });

  if (start == sv.end()) {
    return {}; // 全て空白だった場合
  }

  // イテレータ間の部分文字列を切り出す
  return sv.substr(std::distance(sv.begin(), start),
                   std::distance(start, end.base()));
}

/**
 * @brief 元の文字列を直接書き換えるトリム関数（破壊的）
 * @param s トリーム対象の std::string（参照）
 */
constexpr void trim_inplace(std::string &s) noexcept {
  std::string_view sv = s;
  auto trimmed = trim(sv);

  if (trimmed.empty()) {
    s.clear();
    return;
  }

  auto keep_end_idx = trimmed.end() - sv.begin();
  s.erase(s.begin() + keep_end_idx, s.end());

  auto keep_start_idx = trimmed.begin() - sv.begin();
  s.erase(s.begin(), s.begin() + keep_start_idx);
}

} // namespace astream::util
