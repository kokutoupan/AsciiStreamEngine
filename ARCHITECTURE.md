# AsciiStreamEngine アーキテクチャ仕様書 (AI・開発者向けガイド)

本フレームワークは、サーバーサイドでECS駆動のシミュレーションを行い、その結果を各クライアント視点で高速に並列ストリーミング送信するマルチプレイヤーゲームエンジンです。

---

## 1. エンジンコア・パイプライン (Engine.hpp)

エンジンは1フレームを以下の一本道のデータ流動パイプラインとして処理します。シミュレーションと送信フェーズの境界線を綺麗に引くことで、データ競合のない超高速な並列処理を実現しています。

1. **Inputフェーズ (I/O)**
   - クライアントからの生キーボード入力を `InputDevice` でパース（矢印キー等のエスケープシーケンス対応）。
2. **Simulationフェーズ (ECS更新)**
   - `Registry::update()` を実行。1フレーム内で以下の順序でシステムが実行されます。
     - **`pre_systems`**: 入力の適用、状態リセット、外力の加算など。
     - **`systems` (Main)**: 通常の物理移動、メインのゲームロジック。
     - **`post_systems`**: 衝突判定、接触応答、描画用データの最終確定。
3. **Render & Sendフェーズ (マルチスレッド並列処理コア)**
   - シミュレーション完了後、世界データ（`m_world`）を**完全な読み取り専用 (`const`)** として扱います。
   - `std::execution::par` を用いて、全クライアントセッションの描画・バッファコピー・`zlib`圧縮・ソケット送信までを**マルチスレッドで一斉に並列実行**します。

---

## 2. ハイブリッドECSストレージ (Registry.hpp)

### 2.1 連続メモリ配置とスパースセット

- 各コンポーネントは `ComponentPool<T>` 内部の「スパースセット (Sparse Set)」で管理されます。
- コンポーネントの実体は密配列（`std::vector<T> dense_data`）に格納され、連続メモリに配置されるためキャッシュ局所性が最大化されます。

### 2.2 【超高速】基本コンポーネントのインデックスアクセス (O(1))

エンジンコアに最初から組み込まれている**基本コンポーネント群**は、システムループ内での `std::views::zip` 等によるイテレーション時に、エンティティIDを経由せず、プール内の密配列インデックス（`index`）から直接リファレンスを最速で引っこ抜く専用APIが用意されています。

**インデックスから直接取得可能な基本コンポーネント一覧:**

- `reg.get_transform_by_index(index)` -> `Transform&` （位置・回転・スケールの操作用）
- `reg.get_velocity_by_index(index)` -> `Velocity&` （移動速度ベクトルの操作用）
- `reg.get_acceleration_by_index(index)` -> `Acceleration&` （加速度ベクトルの操作用）
- `reg.get_vertex_by_index(index)` -> `VertexComponent&` （描画用メッシュアセットへの参照）
- `reg.get_collider_by_index(index)` -> `Collider&` （AABB衝突判定バッファと接触リスト）
- `reg.get_tag_by_index(index)` -> `std::uint64_t` （オブジェクト識別用のビットマスク等 ※値渡し）

### 2.3 【汎用】その他のカスタムコンポーネント・外部参照 (Entity IDアクセス)

ゲーム側で独自に型定義して追加した**カスタムコンポーネント**や、イベント応答等で特定のエンティティを指定してランダムアクセスを行う場合は、一律で **`entity_id` (Entity ID)** をキーとしてストレージから取得します。

**Entity ID を用いる汎用API一覧:**

- `reg.get_component<T>(entity_id)` -> `T&` (任意のコンポーネントの取得)
- `reg.has_component<T>(entity_id)` -> `bool` (コンポーネントの存在チェック)
- `reg.add_component<T>(entity_id, component)` (動的な追加)
- `reg.remove_component<T>(entity_id)` (動的な削除)

### 2.4 禁止事項・AIへの注意点

- 独自に追加したカスタムコンポーネント `MyComponent` に対して、`reg.get_mycomponent_by_index(index)` のような関数を捏造しないこと。それは存在しない。必ず `entity_id` を用いて `reg.get_component<MyComponent>(entity_id)` でアクセスすること。

---

## 3. アプリケーション実装例 (examples/cube_multiplayer/EcsSampleApp.hpp)

本フレームワークは描画ロジックを強制しません。`ConnectionContext::render` を実装することで自由な描画が可能です。
サンプル（`EcsSampleApp.hpp`）では、アスキーアートでありながら現代的なGPUに近い**「ディファード・ラスタライズ・レンダリング」**をソフトウェアで実装しています。

### 【実装例】サンプルの3パス描画パイプライン

1. **PASS 1: シャドウパス (Shadow Pass)**
   - ライト視点の行列をベースに、`shadowDepth` (深度バッファ) のみをラスタライズ構築。
2. **PASS 2: ジオメトリパス (Geometry Pass)**
   - クライアント固有のカメラ視点に基づき、中間バッファである `albedoBuffer`, `normalBuffer`, `worldPosBuffer` (Gバッファ) に法線や座標情報をパースペクティブ・コレクトに書き込み。
3. **PASS 3: ライティング・合成パス (Lighting Compute Pass)**
   - 生成されたGバッファをピクセル単位で走査し、`shadowDepth` を**バイリニアサンプリング**して影（シャドウファクター）を計算。ランバート拡散反射から輝度を求め、最終的に `mapIntensityToChar` でアスキーアート文字に変換して出力バッファへ書き込み。

このように、エンジン自体は純粋なデータシミュレータと送信基盤に徹し、表現部分はアプリケーション側でいくらでもリッチに拡張できる構造になっています。
