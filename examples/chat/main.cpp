#include "ChatApp.hpp"
#include <astream/astream.hpp>
#include <string_view>

int main(int argc, char *argv[]) {
  astream::RegisterPolicy policy = astream::RegisterPolicy::AllowAll;
  bool encrypt = false;
  for (int i = 1; i < argc; ++i) {
    std::string_view arg(argv[i]);
    if (arg == "--admin-only") {
      policy = astream::RegisterPolicy::AdminOnly;
    } else if (arg == "--encrypt") {
      encrypt = true;
    }
  }
  // Launch engine with the duck-typed ChatWorld and ChatSession classes (no
  // inheritance)
  astream::Engine<ChatWorld, ChatSession> engine(12345, 30, true, policy,
                                                 encrypt);
  engine.run();
  return 0;
}
