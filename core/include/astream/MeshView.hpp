#pragma once
#include <cstddef>
#include <memory>
#include <span>

namespace astream {

// ラスタライザが直接シークするための、最速・型消去されたメッシュ参照データ
struct MeshView {
  std::span<const std::byte> vertices; // 型消去された頂点配列への窓 (16バイト)
  std::span<const int> indices;        // インデックス配列への窓 (16バイト)
  std::size_t stride = 0;              // 頂点1個のバイトサイズ (8バイト)
  std::shared_ptr<const void> life_support =
      nullptr; // メッシュ実体の寿命を延ばす安全弁
};

} // namespace astream
