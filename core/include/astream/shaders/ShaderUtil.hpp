#pragma once

#include <string_view>

namespace Shaders {

/**
 * @brief Maps a lighting intensity float value [0.0, 1.0] to a corresponding
 * ASCII character.
 *
 * @param intensity Normalized light/color intensity.
 * @return char Corresponding ASCII character from the palette.
 */
inline char mapIntensityToChar(float intensity) {
  constexpr std::string_view palette =
      " .'`^\",:;Il!i~+_-?][}{1)(|\\/"
      "tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$";
  int len = static_cast<int>(palette.length());
  int idx = (int)(intensity * (len - 1));
  if (idx < 0)
    idx = 0;
  if (idx >= len)
    idx = len - 1;
  return palette[idx];
}

} // namespace Shaders
