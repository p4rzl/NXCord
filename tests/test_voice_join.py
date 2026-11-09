#!/usr/bin/env python3
"""Exercise the production join/cancel/timeout/status code paths without Discord."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "nxcord/src/nxcord_client.cpp").read_text()
methods = source[source.index("void NXCordClient::joinVoiceChannel"):
                 source.index("IPCStruct::DiscordServer NXCordClient::getServer")]
closed = source[source.index("  void onClosed("):
                source.index("  inline void onHeartbeat(")]

harness = r'''
#include <cassert>
#include <map>
#include <vector>
#include <common/ipc_structures.hpp>
#include <sleepy_discord/snowflake.h>
namespace Logger { void write(const char *, ...) {} }
namespace Utils {
  time_t now=100;
  time_t current_time_millis() { return now; }
}
namespace SleepyDiscord {
struct Server {};
struct Channel {};
struct VoiceConnection {};
struct BaseVoiceEventHandler {
  virtual ~BaseVoiceEventHandler() = default;
  virtual void onClosed(VoiceConnection &, int) {}
  virtual void onCryptoState(VoiceConnection &, unsigned int, bool) {}
};
struct VoiceContext {
  bool handler_attached=false;
  Snowflake<Server> server;
  Snowflake<Channel> channel;
  template<class T, class Client> void startVoiceHandler(Client &) { handler_attached=true; }
};
}
class DiscordClient { public: void tick() {} };
class VoiceEventHandler;
class NXCordClient : public DiscordClient {
 public:
  bool gateway_connected=true;
  IPCStruct::VoiceStatus _voice_status{};
  time_t _voice_started=0;
  struct LocalVoiceContext {};
  LocalVoiceContext _current_voice_context;
  SleepyDiscord::VoiceConnection *_current_voice_connection=nullptr;
  SleepyDiscord::VoiceContext context;
  std::map<int64_t,std::vector<IPCStruct::DiscordChannel>> _channels;
  int requests=0,cancels=0;
  std::vector<int> voiceConnections;
  bool isConnected() const { return gateway_connected; }
  bool isConnectedVoiceChannel() const { return _current_voice_connection != nullptr; }
  auto &createVoiceContext(SleepyDiscord::Snowflake<SleepyDiscord::Server> server,
                           SleepyDiscord::Snowflake<SleepyDiscord::Channel> channel) {
    context={}; context.server=server; context.channel=channel; return context;
  }
  void connectToVoiceChannel(SleepyDiscord::VoiceContext &voice) {
    assert(voice.handler_attached); ++requests;
  }
  void disconnectAllVoiceChannels() { ++cancels; voiceConnections.clear(); }
  void stopScreenShare() { _voice_status.screen_sharing = false; }
  bool isScreenSharing() const { return _voice_status.screen_sharing; }
  void joinVoiceChannel(int64_t,int64_t);
  void disconnectVoiceChannel();
  void tick();
};
class VoiceEventHandler : public SleepyDiscord::BaseVoiceEventHandler {
  NXCordClient &_client;
 public:
  explicit VoiceEventHandler(NXCordClient &client) : _client(client) {}
'''

checks = r'''
int main() {
  using State=IPCStruct::VoiceConnectionState;
  NXCordClient client;
  client._channels[1].push_back(IPCStruct::create_discord_channel(
      "The selected channel",1,42,IPCStruct::DiscordChannelType::SERVER_VOICE));
  client.joinVoiceChannel(1,42);
  assert(client.requests == 1 && client._voice_status.state == State::Connecting);
  assert(client.context.server.number() == 1 && client.context.channel.number() == 42);
  assert(std::string(client._voice_status.channel.name) == "The selected channel");
  client.joinVoiceChannel(1,42);
  assert(client.requests == 1); // repeated A must not enqueue duplicate sessions.
  client.joinVoiceChannel(1,43);
  assert(client.requests == 2 && client._voice_status.channel.id == 43);
  client.disconnectVoiceChannel();
  assert(client._voice_status.state == State::Disconnected && !client.isConnectedVoiceChannel());
  client.joinVoiceChannel(1,42);
  Utils::now+=30001;
  client.tick();
  assert(client._voice_status.state == State::Failed && client._voice_status.channel.id == 42);
  assert(std::strlen(client._voice_status.detail) > 0);
  client.joinVoiceChannel(1,42); // retry after timeout.
  assert(client._voice_status.state == State::Connecting);
  VoiceEventHandler events(client);
  SleepyDiscord::VoiceConnection voice;
  events.onClosed(voice,4017);
  assert(client._voice_status.close_code == 4017 && client._voice_status.state == State::Failed);
  assert(std::string(client._voice_status.detail).find("DAVE") != std::string::npos);
  client.voiceConnections.push_back(1);
  client.tick();
  assert(client.voiceConnections.empty() && client._voice_status.close_code == 4017);
  client.gateway_connected=false;
  int requests=client.requests;
  client.joinVoiceChannel(1,42);
  assert(client.requests == requests && client._voice_status.state == State::Failed);
  puts("Voice join/status regression tests passed");
}
'''

with tempfile.TemporaryDirectory(prefix="nxcord-voice-") as temp:
    temp = Path(temp)
    cpp = temp / "voice.cpp"
    cpp.write_text(harness + closed + "};\n" + methods + checks)
    binary = temp / "voice"
    subprocess.run([
        "g++", "-std=c++17", "-O1", "-Wno-deprecated-declarations",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "externals/sleepy-discord/include"),
        "-I" + str(ROOT / "externals/sleepy-discord/include/sleepy_discord"),
        str(cpp), str(ROOT / "common/src/ipc_structures.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
