#include "ChatApp.hpp"
#include <astream/astream.hpp>
#include <string_view>

int main(int argc, char *argv[]) {

  constexpr astream::EngineConfig config{
      .max_receive_frame_size = 128,
      .enable_encryption = true,
      .enable_auth = true,
      .register_policy = astream::RegisterPolicy::AllowAll,
  };
  // Launch engine with the duck-typed ChatWorld and ChatSession classes (no
  // inheritance)
  astream::Engine<ChatWorld, ChatSession, config> engine(12345, 30);
  engine.run();
  return 0;
}
