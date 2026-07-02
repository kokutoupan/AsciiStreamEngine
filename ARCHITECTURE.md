# AsciiStreamEngine アーキテクチャ仕様書 (AI・開発者向けガイド)

**AsciiStreamEngine** は、**サーバーサイドで重い計算（物理演算・マルチパス描画・Gバッファ相当のソフトウェアラスタライズ）を一手に引き受け**、クライアントには**最小限の圧縮アスキーフレーム**だけをストリーミングするマルチプレイヤー・アスキーアートゲームフレームワークです。

C++23 + 現代的な並列処理（`std::execution::par`）を活用し、高速・スケーラブルなサーバーサイドレンダリングを実現しています。

---

## 1. 全体アーキテクチャ概要

### コア設計原則

- **Engine** がネットワーク・ループ・並列描画を一元管理
- **GameWorld**（ユーザー実装）がゲーム状態・物理・ロジックを担当
- **ConnectionContext**（ユーザー実装）が各クライアント固有の視点描画を担当
- シミュレーション結果（`const World`）を**読み取り専用**で全クライアントに並列レンダリング → データ競合ゼロ

### フレームワークの責務分離

- **コア（framework）**: ネットワーク、入力集約、並列レンダリング・圧縮送信、ライフサイクル管理
- **サンプル側**: EnTT（ECS） + Jolt Physics + 独自描画ロジック

---

## 2. エンジンコア (Engine.hpp)

`Engine<WorldType, SessionType>` はテンプレートで、以下のコンセプトを満たす型を受け取ります。

### 主要コンセプト

```cpp
template <typename T>
concept IsGameWorld = /* processPlayerInput + globalUpdate */;

template <typename Session, typename World>
concept IsConnectionSession = /* init, update, render, onDisconnect */;
```

### 1フレームの処理フロー

1. **新規接続処理**（accept + 初期化）
2. **入力受信**（非同期 / 非ブロッキング）
3. **入力適用**（各セッションの `context->update()`）
4. **グローバル更新**（`world.globalUpdate(deltaTime)`）
5. **並列描画・送信**（`std::execution::par` で全クライアント同時処理）
   - 各 `context->render(buffer, const_world)`
   - zlib 圧縮
   - ソケット送信（width/height + 圧縮データ）
6. **入力状態リセット**（次フレームへ）

**特徴**: 描画・圧縮・送信が完全に並列化され、サーバーCPUを最大限活用。

---

## 3. 拡張ポイント（ユーザー実装側）

### 3.1 GameWorld（GameWorld.hpp を継承 or コンセプト準拠）

```cpp
class EcsGameWorld : public GameWorld {
    void processPlayerInput(int clientId, const InputDevice& input) override;
    void globalUpdate(float deltaTime) override;
};
```

- 物理更新（Jolt）
- EnTT registry の更新
- 共有ゲームロジック全般

### 3.2 ConnectionContext（ConnectionContext.hpp を継承  or コンセプト準拠）

```cpp
template<typename WorldType>
class MyContext : public ConnectionContext<WorldType> {
    void init(int clientId, int w, int h, WorldType& world) override;
    void update(int clientId, const InputDevice& input, WorldType& world) override;
    void render(Texture2D<char>& colorBuffer, const WorldType& world) override;
    void onDisconnect(WorldType& world) override;
};
```

- **各クライアント固有**のカメラ視点
- ソフトウェアレンダリング（Gバッファ相当、影、ライティング → ASCII変換）
- 描画結果を `Texture2D<char>` に書き込む

---

## 4. 主要コンポーネント

- **InputDevice**: 生キー入力 → フレーム内状態管理
- **Texture2D<T>**: シンプル2Dバッファ（アスキー出力用）
- **GraphicsDevice / MeshView / Transform**: 描画補助ユーティリティ
- **shaders/**: 共通シェーダー関数（ライティング、ASCIIマッピングなど）
- **util/**: Mesh/Texture ローディング支援

---

## 5. サンプル実装 (examples/cube_multiplayer/)

- **EnTT** で ECS 管理
- **Jolt Physics** で本格物理（重力・衝突・RayCast）
- マルチパスソフトウェアレンダリング（深度・法線・ライティング）
- プレイヤーごとの独立カメラ + 共有オブジェクト
- 破壊可能オブジェクト、軌道運動オブジェクトなど

---

## 6. パフォーマンス設計

- **const World** を全描画スレッドに共有 → ロック不要
- zlib 圧縮を並列実行
- 最小限のネットワークペイロード（圧縮後アスキーバッファ + メタデータ）
- フレームレート制御（デルタタイム考慮）

---

## 7. 将来拡張の方向性

- より高度なシェーダー体系
- 状態同期最適化（差分圧縮）
- 認証・暗号化レイヤー

---

**注意**: まだ開発中のプロジェクトです。APIは予告なく変更される可能性があります。

最新のコードを参照しながら実装してください。
