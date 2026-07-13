#include "ChatApp.hpp"
#include "astream/UserStore.hpp"
#include <astream/Engine.hpp>
#include <string_view>

int main(int argc, char *argv[]) {
  RegisterPolicy policy = RegisterPolicy::AllowAll;
  bool encrypt = false;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);
    if (arg == "--admin-only") {
      policy = RegisterPolicy::AdminOnly;
    } else if (arg == "--encrypt") {
      encrypt = true;
    }
  }
  // Launch engine with the duck-typed ChatWorld and ChatSession classes (no
  // inheritance)
  Engine<ChatWorld, ChatSession> engine(12345, 30, true, policy, encrypt);
  engine.run();
  return 0;
}
