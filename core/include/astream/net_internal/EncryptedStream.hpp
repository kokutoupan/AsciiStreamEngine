#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

#include <monocypher.h>

namespace astream::net {

class EncryptedStream {
public:
  enum class Mode {
    Plaintext, // 暗号化なし（生の通信）
    Encrypted  // XChaCha20-Poly1305 フレーム暗号化
  };

  EncryptedStream() : mode(Mode::Plaintext), tx_counter(0), rx_counter(0) {}

  // サーバーからの初期応答に応じて有効化する
  void initialize_encryption(Mode selected_mode, const uint8_t shared_key[32],
                             const uint8_t tx_base_nonce[24],
                             const uint8_t rx_base_nonce[24]) {
    mode = selected_mode;
    if (mode == Mode::Encrypted) {
      std::memcpy(this->key, shared_key, 32);
      std::memcpy(this->tx_nonce, tx_base_nonce, 24);
      std::memcpy(this->rx_nonce, rx_base_nonce, 24);
      tx_counter = 0;
      rx_counter = 0;
    }
  }

  Mode get_mode() const { return mode; }

  /**
   * @brief 送信データのラップ処理
   * @return 実際にソケット経由で送信すべきバイト列
   */
  std::vector<uint8_t> wrap_outgoing(const uint8_t *data, uint16_t size) {
    if (mode == Mode::Plaintext) {
      // 暗号化なしなら、そのままのデータを vector にコピーして返す
      return std::vector<uint8_t>(data, data + size);
    }

    // --- 暗号化あり（フレーム化） ---
    std::vector<uint8_t> packet(2 + size + 16);
    packet[0] = static_cast<uint8_t>((size >> 8) & 0xFF);
    packet[1] = static_cast<uint8_t>(size & 0xFF);

    // ナンスの末尾8バイトをカウンターで更新
    for (int i = 0; i < 8; ++i) {
      tx_nonce[16 + i] = static_cast<uint8_t>((tx_counter >> (i * 8)) & 0xFF);
    }

    crypto_aead_lock(packet.data() + 2, packet.data() + 2 + size, key, tx_nonce,
                     packet.data(), 2, // AADとしてヘッダーを含める
                     data, size);

    tx_counter++;
    return packet;
  }

  /**
   * @brief 受信データのアンラップ処理
   * @param raw_packet ソケットから読み込んだ1フレーム分のデータ
   * @param out_plain 復号・検証されたプレーンテキストの出力先
   * @return true: 成功 / false: 認証失敗（改ざん・リプレイ攻撃）
   */
  bool unwrap_incoming(const std::vector<uint8_t> &raw_packet,
                       std::vector<uint8_t> &out_plain) {
    if (mode == Mode::Plaintext) {
      out_plain = raw_packet;
      return true;
    }

    // --- 暗号化あり（フレーム解凍・検証） ---
    if (raw_packet.size() < 2 + 16)
      return false;

    uint16_t size = (static_cast<uint16_t>(raw_packet[0]) << 8) | raw_packet[1];
    if (raw_packet.size() != 2 + size + 16)
      return false;

    out_plain.resize(size);

    // ナンスの末尾8バイトをカウンターで更新
    for (int i = 0; i < 8; ++i) {
      rx_nonce[16 + i] = static_cast<uint8_t>((rx_counter >> (i * 8)) & 0xFF);
    }

    int result = crypto_aead_unlock(
        out_plain.data(), raw_packet.data() + 2 + size, key, rx_nonce,
        raw_packet.data(), 2, raw_packet.data() + 2, size);

    if (result != 0) {
      out_plain.clear();
      return false; // 改ざん検知
    }

    rx_counter++;
    return true;
  }

private:
  Mode mode;
  uint8_t key[32];
  uint8_t tx_nonce[24];
  uint8_t rx_nonce[24];
  uint64_t tx_counter;
  uint64_t rx_counter;
};

} // namespace astream::net
