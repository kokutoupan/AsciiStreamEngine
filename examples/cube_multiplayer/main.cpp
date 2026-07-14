// #include "CubeApp.hpp"
#include "EcsSampleApp.hpp"
#include <astream/Engine.hpp>

int main() {
  // 共有世界と個別表示セッションの型を渡してポート12345でエンジンを起動
  // Engine<MyGameWorld, MyPlayerSession> engine(12345);
  astream::Engine<EcsGameWorld, EcsPlayerSession> engine(12345);

  engine.run();
  return 0;
}
