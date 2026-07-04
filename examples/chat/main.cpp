#include "ChatApp.hpp"
#include <astream/Engine.hpp>

int main() {
  // Launch engine with the duck-typed ChatWorld and ChatSession classes (no
  // inheritance)
  Engine<ChatWorld, ChatSession> engine(12345);
  engine.run();
  return 0;
}
