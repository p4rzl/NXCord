#!/usr/bin/env python3
"""Host regression tests for the production SAX parser and zlib stream.

Run: python3 tests/test_gateway_stream.py (requires g++ and system zlib).
Only Discord callbacks/model types are stubbed; parser code is taken from
the vendored source so these tests exercise the code used by the sysmodule.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "externals/sleepy-discord/sleepy_discord/client.cpp").read_text()
parser = source[source.index("        struct MessageHandler"):
                source.index("\n\tvoid BaseDiscordClient::processMessage")]
client_source = (ROOT / "nxcord/src/discord_client.cpp").read_text()
startup = client_source[client_source.index("void DiscordClient::startConnection()"):
                        client_source.index("void DiscordClient::tick()")]

harness = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <functional>
#include <sstream>
#include <vector>
#include <nxcord/websocket_zlib_stream.hpp>
#include <nxcord/gateway_events.hpp>
#include <sleepy_discord/rapidjson/reader.h>
namespace Logger { void write(const char *, ...) {} }
namespace SleepyDiscord {
using rapidjson::SizeType;
struct User {};
template<class T> struct Snowflake {
  std::string value;
  Snowflake &operator=(const std::string &v) { value = v; return *this; }
  const std::string &string() const { return value; }
};
struct VoiceState {
  Snowflake<User> userID, channelID;
  std::string sessionID;
  struct { std::string nick; struct { std::string username; } user; } serverMember;
};
struct Server {
  Snowflake<Server> ID;
  std::string name;
  std::vector<VoiceState> voiceStates;
};
class BaseDiscordClient {
 public:
  enum { DISPATCH = 0, ERROR_NOTE = 1 };
  int lastSReceived = 0, consecutiveReconnectsCount = 0;
  std::string sessionID, last_message;
  Snowflake<User> userID;
  bool bot = false, ready = false;
  int ready_calls = 0, message_calls = 0, skipped = 0;
  int server_calls = 0;
  size_t server_voices = 0;
  std::string server_name;
  void onReady() { ++ready_calls; }
  void onEditVoiceState(const VoiceState &) {}
  void onError(int, const std::string &) { ++skipped; }
  void onServer(Server server) { ++server_calls; server_name = server.name; server_voices = server.voiceStates.size(); }
  void onEditServer(Server server) { onServer(std::move(server)); }
  void processMessage(const std::string &s) { ++message_calls; last_message = s; }
  void processStream(JsonInputStream &);
  bool shouldProcessDispatch(const std::string &event) const { return nxcord_gateway_event(event); }
  bool useLightweightServerEvents() const { return true; }
};
'''

startup_harness = r'''
}
class DiscordClient {
 public:
  bool connection = false, _start_pending = false;
  std::string _token;
  std::vector<std::function<void()>> tasks;
  int starts = 0;
  void startConnection();
  void schedule(std::function<void()> f, int) { tasks.push_back(f); }
  void start(const std::string &, int) { ++starts; connection = true; }
};
'''

checks = r'''
int main() {
  DiscordClient startup;
  startup.startConnection();
  assert(startup.tasks.empty()); // no token, no connection attempt.
  startup._token = "test-token";
  startup.startConnection();
  startup.startConnection(); // two IPC/UI calls before the scheduler tick.
  assert(startup.tasks.size() == 1);
  startup.tasks.front()();
  assert(startup.starts == 1 && !startup._start_pending);
  startup.startConnection();
  assert(startup.tasks.size() == 1); // already connected.
  // Discord zlib-stream keeps one compression context across all messages.
  z_stream deflater{};
  assert(deflateInit(&deflater, Z_DEFAULT_COMPRESSION) == Z_OK);
  ZlibWrapper inflater;
  SleepyDiscord::BaseDiscordClient client;
  auto process = [&](const std::string &json) {
    std::string compressed;
    deflater.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(json.data()));
    deflater.avail_in = json.size();
    do {
      char out[1024];
      deflater.next_out = reinterpret_cast<Bytef *>(out);
      deflater.avail_out = sizeof(out);
      assert(deflate(&deflater, Z_SYNC_FLUSH) == Z_OK);
      compressed.append(out, sizeof(out) - deflater.avail_out);
    } while (deflater.avail_in || !deflater.avail_out);
    std::istringstream input(compressed);
    inflater.set_stream(&input);
    WebsocketZlibStream stream(inflater);
    client.processStream(stream);
    assert(stream.Peek() == '\0');
    assert(stream.Tell() == json.size());
    assert(stream.GetCache().size() <= WebsocketZlibStream::max_cache_size);
  };
  process(R"({"op":10,"d":{"heartbeat_interval":41250},"s":null,"t":null})");
  assert(client.message_calls == 1); // HELLO must reach identity handling.
  process(R"({"op":0,"d":{"user":{"id":"42","bot":true},"session_id":"session"},"s":7,"t":"READY"})");
  assert(client.ready_calls == 1 && client.ready && client.bot);
  assert(client.userID.string() == "42" && client.sessionID == "session");
  assert(client.lastSReceived == 7); // sequence can follow the READY body.
  std::string large = "[";
  for (int i = 0; i < 100000; ++i) large += "\"ignored\",";
  large += "null]";
  process("{\"op\":0,\"d\":{" + std::string("\"extra\":") + large +
          ",\"session_id\":\"second\",\"user\":{\"id\":\"43\"}},\"s\":8,\"t\":\"READY\"}");
  assert(client.ready_calls == 2 && client.lastSReceived == 8);
  assert(client.userID.string() == "43" && !client.bot);
  process("{\"op\":0,\"d\":" + large + ",\"s\":9,\"t\":\"READY_SUPPLEMENTAL\"}");
  assert(client.skipped == 0 && client.lastSReceived == 9);
  process("{\"op\":0,\"d\":" + large + ",\"s\":10,\"t\":\"CHANNEL_CREATE\"}");
  assert(client.skipped == 1 && client.lastSReceived == 10);
  process(R"({"op":11,"d":null,"s":null,"t":null})");
  assert(client.message_calls == 2); // oversized event must not break zlib.
  assert(client.last_message == R"({"op":11,"d":null,"s":null,"t":null})");
  process(R"({"op":0,"t":"READY","s":10,"d":{"session_id":"third","user":{"id":"44"}}})");
  assert(client.ready_calls == 3 && client.lastSReceived == 10);
  // A single multi-megabyte string must not grow RapidJSON's scratch buffer.
  process("{\"op\":0,\"d\":{\"settings\":\"" + std::string(4 * 1024 * 1024, 'x') +
          "\\\"ignored\\\\text\",\"session_id\":\"fourth\",\"user\":{\"id\":\"45\"}},\"s\":11,\"t\":\"READY\"}");
  assert(client.ready_calls == 4 && client.lastSReceived == 11);
  assert(client.sessionID == "fourth" && client.userID.string() == "45");
  process(R"({"op":0,"d":{"content":"ignored chat"},"s":12,"t":"MESSAGE_CREATE"})");
  assert(client.message_calls == 2 && client.lastSReceived == 12);
  process(R"({"op":0,"d":{"guild_id":"1","token":"test","endpoint":"voice"},"s":13,"t":"VOICE_SERVER_UPDATE"})");
  assert(client.message_calls == 3 && client.lastSReceived == 13);
  process("{\"op\":0,\"d\":{\"settings\":\"" + std::string(8187, 'x') +
          "\\uD83D\\uDE00more\",\"session_id\":\"fifth\",\"user\":{\"id\":\"46\"}},\"s\":14,\"t\":\"READY\"}");
  assert(client.ready_calls == 5 && client.lastSReceived == 14);
  process("{\"op\":0,\"d\":{\"settings\":\"" + std::string(8191, 'x') +
          u8"😀more\",\"session_id\":\"sixth\",\"user\":{\"id\":\"47\"}},\"s\":15,\"t\":\"READY\"}");
  assert(client.ready_calls == 6 && client.lastSReceived == 15);
  process("{\"op\":0,\"d\":{\"members\":" + large +
          ",\"id\":\"123\",\"name\":\"Large guild\",\"voice_states\":[{\"user_id\":\"1\",\"channel_id\":\"2\",\"session_id\":\"voice\"}]},\"s\":16,\"t\":\"GUILD_CREATE\"}");
  assert(client.server_calls == 1 && client.server_name == "Large guild");
  assert(client.server_voices == 1 && client.lastSReceived == 16);
  // READY with nested sessions/relationships/users arrays must never overwrite userID or sessionID.
  process(R"({"op":0,"t":"READY","s":17,"d":{"user":{"id":"777777","bot":false},"session_id":"real_sess","sessions":[{"session_id":"all"}],"relationships":[{"id":"111","user":null},{"id":"222","bot":true}],"users":[{"id":"333"}]}})");
  assert(client.ready_calls == 7 && client.lastSReceived == 17);
  assert(client.userID.string() == "777777" && client.sessionID == "real_sess" && !client.bot);
  deflateEnd(&deflater);
  puts("Gateway regression tests passed");
}
'''

with tempfile.TemporaryDirectory(prefix="nxcord-gateway-") as temp:
    temp = Path(temp)
    cpp = temp / "gateway.cpp"
    cpp.write_text(harness + parser + startup_harness + startup + checks)
    binary = temp / "gateway"
    subprocess.run([
        "g++", "-std=c++17", "-Wno-deprecated-declarations",
        "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "externals/sleepy-discord/include"),
        "-I" + str(ROOT / "externals/sleepy-discord/include/sleepy_discord"),
        str(cpp), str(ROOT / "nxcord/src/zlib_wrapper.cpp"),
        "-lz", "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
