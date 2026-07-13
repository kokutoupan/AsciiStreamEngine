#pragma once
#include <cstddef>

namespace astream {
namespace config {
// デフォルトの最大フレームサイズ（用途に応じて書き換えてビルド）
constexpr std::size_t MAX_RECEIVE_FRAME_SIZE = 128;
} // namespace config
} // namespace astream
