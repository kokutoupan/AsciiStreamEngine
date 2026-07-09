#include "ChatApp.hpp"
#include "astream/UserStore.hpp"
#include <astream/Engine.hpp>

int main() {
  // Launch engine with the duck-typed ChatWorld and ChatSession classes (no
  // inheritance)
  Engine<ChatWorld, ChatSession> engine(12345, 30, true,
                                        RegisterPolicy::AllowAll);
  engine.run();
  return 0;
}
