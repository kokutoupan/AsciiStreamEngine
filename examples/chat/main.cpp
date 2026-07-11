#include "ChatApp.hpp"
#include "astream/UserStore.hpp"
#include <astream/Engine.hpp>
#include <string_view>

int main(int argc, char *argv[]) {
  RegisterPolicy policy = RegisterPolicy::AllowAll;
  if (argc > 1 && std::string_view(argv[1]) == "--admin-only") {
    policy = RegisterPolicy::AdminOnly;
  }
  // Launch engine with the duck-typed ChatWorld and ChatSession classes (no
  // inheritance)
  Engine<ChatWorld, ChatSession> engine(12345, 30, true, policy);
  engine.run();
  return 0;
}
