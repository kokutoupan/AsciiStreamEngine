#pragma once
#include <algorithm>
#include <astream/InputDevice.hpp>
#include <astream/Texture2D.hpp>
#include <astream/util/TextInputLine.hpp>
#include <astream/util/TextureUtil.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// Duck-typing design complying with IsGameWorld and IsConnectionSession
// concepts. No inheritance from GameWorld or ConnectionContext. No usage of
// GraphicsDevice.

class ChatWorld {
private:
  bool m_frameChanged = false; // このフレーム内で変更があったか
public:
  struct PlayerState {
    std::string name;
    astream::util::TextInputLine inputLine;
  };

  std::unordered_map<int, PlayerState> m_players;
  std::vector<std::string> m_chatLog;

  ChatWorld() {
    m_chatLog.push_back("=== System: Chat room initialized ===");
    m_frameChanged = true;
  }

  void clearFrameChangedFlag() { m_frameChanged = false; }

  bool hasChangedThisFrame() const { return m_frameChanged; }

  void markChanged() { m_frameChanged = true; }

  void postUpdate() { clearFrameChangedFlag(); };

  // Matches IsGameWorld requirement
  void processPlayerInput(int clientId, const InputDevice &input) {
    auto it = m_players.find(clientId);
    if (it == m_players.end())
      return;

    // Update TextInputLine state
    it->second.inputLine.update(input);

    // If Enter is pressed, dispatch the message
    if (input.getKeyDown(Key::Enter)) {
      std::string msg = it->second.inputLine.str();
      // Remove any carriage return/line feed characters
      msg.erase(std::remove(msg.begin(), msg.end(), '\n'), msg.end());
      msg.erase(std::remove(msg.begin(), msg.end(), '\r'), msg.end());

      if (!msg.empty()) {
        m_chatLog.push_back(it->second.name + ": " + msg);
        markChanged();
      }
      it->second.inputLine.clear();
    }
  }

  // Matches IsGameWorld requirement
  void globalUpdate(float dt) {
    // Prevent the chat log from growing infinitely
    if (m_chatLog.size() > 100) {
      m_chatLog.erase(m_chatLog.begin(),
                      m_chatLog.begin() + (m_chatLog.size() - 100));
      markChanged();
    }
  }

  void addPlayer(int clientId, const std::string &name) {
    PlayerState p;
    p.name = name;
    m_players.emplace(clientId, std::move(p));
    m_chatLog.push_back("*** System: " + m_players[clientId].name +
                        " entered the chat ***");
    markChanged();
  }

  void removePlayer(int clientId) {
    auto it = m_players.find(clientId);
    if (it != m_players.end()) {
      m_chatLog.push_back("*** System: " + it->second.name +
                          " left the chat ***");
      m_players.erase(it);
      markChanged();
    }
  }
};

class ChatSession {
private:
  int m_clientId = -1;
  int m_width = 80;
  int m_height = 24;
  int m_scrollOffset = 0;
  bool m_dirty = true;

public:
  ChatSession() = default;

  // Matches IsConnectionSession requirement
  void init(int clientId, int w, int h, const std::string &user_name,
            ChatWorld &world) {
    m_clientId = clientId;
    m_width = w;
    m_height = h;
    world.addPlayer(clientId, user_name);
  }

  // Matches IsConnectionSession requirement
  void onDisconnect(ChatWorld &world) { world.removePlayer(m_clientId); }

  // Matches IsConnectionSession requirement
  void update(int clientId, const InputDevice &input, ChatWorld &world) {
    auto it = world.m_players.find(clientId);
    std::string prevStr;
    if (it != world.m_players.end()) {
      prevStr = it->second.inputLine.str();
    }

    world.processPlayerInput(clientId, input);

    if (it != world.m_players.end()) {
      if (it->second.inputLine.str() != prevStr) {
        m_dirty = true;
      }
    }

    if (input.getKeyDown(Key::Up)) {
      m_scrollOffset++;
      m_dirty = true;
    }
    if (input.getKeyDown(Key::Down)) {
      if (m_scrollOffset > 0) {
        m_scrollOffset--;
        m_dirty = true;
      }
    }
  }

  // Optional session hook: called after global update
  void postUpdate(int clientId, ChatWorld &world) {
    if (world.hasChangedThisFrame()) {
      m_dirty = true;
    }
  }

  // Matches IsConnectionSession requirement
  bool render(TextureView<char> buf, const ChatWorld &world) {

    if (!m_dirty) {
      return false; // Skip drawing and transmission
    }

    buf.clear(' ');

    // Header UI
    std::string scrollInfo = "";
    if (m_scrollOffset > 0) {
      scrollInfo = " [Scrolled: " + std::to_string(m_scrollOffset) + "]";
    }
    std::string header = " --- C++23 ASCII Chat for AsciiStreamEngine (" +
                         std::to_string(m_width) + "x" +
                         std::to_string(m_height) + ")" + scrollInfo + " --- ";
    TextureUtil::drawText(buf, 2, 0, header);

    std::string rule = std::string(std::max(0, m_width - 4), '=');
    TextureUtil::drawText(buf, 2, 1, rule);

    // Message Logs
    int startY = 3;
    int endY = m_height - 4;
    int displayLines = endY - startY + 1;

    if (displayLines > 0) {
      int logSize = static_cast<int>(world.m_chatLog.size());

      // Clamp scroll offset to valid bounds
      int maxScroll = std::max(0, logSize - displayLines);
      if (m_scrollOffset > maxScroll) {
        m_scrollOffset = maxScroll;
      }

      int endIdx = logSize - m_scrollOffset;
      int startIdx = std::max(0, endIdx - displayLines);
      int curY = startY;
      for (int i = startIdx; i < endIdx; ++i) {
        TextureUtil::drawText(buf, 2, curY++, world.m_chatLog[i]);
      }
    }

    // Footer UI
    TextureUtil::drawText(buf, 2, m_height - 3, rule);

    // Active Input Line
    auto it = world.m_players.find(m_clientId);
    if (it != world.m_players.end()) {
      std::string prompt =
          "[" + it->second.name + "]: " + it->second.inputLine.str() + "_";
      TextureUtil::drawText(buf, 2, m_height - 2, prompt);
    }

    return true; // Sent frame
  }

  // Optional session hook: called at the end of the frame
  void endFrame(int clientId, ChatWorld &world) { m_dirty = false; }
};
