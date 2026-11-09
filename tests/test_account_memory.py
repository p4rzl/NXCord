#!/usr/bin/env python3
"""Host stress tests for bounded account caches and lightweight REST parsing."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "nxcord/src/nxcord_client.cpp").read_text()
methods = source[source.index("inline IPCStruct::DiscordChannel create_ipc_channel"):
                 source.index("void NXCordClient::joinVoiceChannel")]

harness = r'''
#include <algorithm>
#include <cassert>
#include <codecvt>
#include <locale>
#include <map>
#include <sleepy_discord/snowflake.h>
#include <nxcord/discord_list_parser.hpp>
#include <nxcord/gateway_events.hpp>
namespace Logger { void write(const char *, ...) {} }
namespace SleepyDiscord {
enum { OK = 200 };
struct Server {};
struct User {};
struct Channel {
  enum { SERVER_VOICE = 2 };
  Snowflake<Channel> ID;
  Snowflake<Server> serverID;
  std::string name;
  int type = 2;
};
struct VoiceState {
  Snowflake<User> userID;
  Snowflake<Channel> channelID;
  struct { std::string nick; struct { std::string username; } user; } serverMember;
};
}
class NXCordClient {
 public:
  std::map<int64_t, std::vector<IPCStruct::DiscordChannel>> _channels;
  std::map<int64_t, std::vector<IPCStruct::DiscordVoiceState>> _voice_states;
  struct {
    SleepyDiscord::Snowflake<SleepyDiscord::Server> serverID;
    SleepyDiscord::Snowflake<SleepyDiscord::Channel> channelID;
  } _current_voice_context;
  int fetches = 0;
  struct Response { int statusCode; std::string text; };
  Response getServerChannels(const SleepyDiscord::Snowflake<SleepyDiscord::Server> &id) {
    ++fetches;
    return id.number() == 99999 ? Response{500, "{}"} :
        Response{200, R"([{"id":"42","name":"Voice","type":2}])"};
  }
  std::vector<IPCStruct::DiscordChannel> *getChannelsPtr(
      const SleepyDiscord::Snowflake<SleepyDiscord::Server> &id) {
    auto it = _channels.find(id.number());
    return it == _channels.end() ? nullptr : &it->second;
  }
  std::vector<IPCStruct::DiscordChannel> *getChannelsPtr(const SleepyDiscord::Channel &channel) {
    return getChannelsPtr(channel.serverID);
  }
  void onChannel(SleepyDiscord::Channel);
  void onEditChannel(SleepyDiscord::Channel);
  void onDeleteChannel(SleepyDiscord::Channel);
  bool fillChannels(const SleepyDiscord::Snowflake<SleepyDiscord::Server> &);
  const std::vector<IPCStruct::DiscordChannel> &getCachedChannels(int64_t);
  void onEditVoiceState(const SleepyDiscord::VoiceState &);
};
'''

checks = r'''
int main() {
  using ID = SleepyDiscord::Snowflake<SleepyDiscord::Server>;
  assert(ID().number() == 0 && ID("bad").number() == 0);
  assert(ID("9223372036854775808").number() == 0);
  assert(ID("1234junk").number() == 0 && ID("-1").number() == 0);
  assert(ID("9223372036854775807").number() == INT64_MAX);
  NXCordClient client;
  SleepyDiscord::Channel channel;
  channel.ID = std::string("7");
  // No guild ID is normal for DMs. No exceptions or REST requests permitted.
  client.onChannel(channel);
  client.onEditChannel(channel);
  client.onDeleteChannel(channel);
  for (int i = 1; i <= 10000; ++i) {
    channel.serverID = std::to_string(i);
    client.onChannel(channel);
    client.onEditChannel(channel);
    client.onDeleteChannel(channel);
  }
  assert(client.fetches == 0 && client._channels.empty());
  client._current_voice_context.serverID = std::string("1");
  assert(client.getCachedChannels(1).size() == 1);
  for (int i = 2; i <= 200; ++i) {
    assert(client.getCachedChannels(i).size() == 1);
    assert(client._channels.size() <= NXCordLimits::cached_guilds);
    assert(client._channels.count(1) == 1);
  }
  int fetches = client.fetches;
  assert(client.getCachedChannels(0).empty() && client.fetches == fetches);
  assert(client.getCachedChannels(99999).empty());
  assert(client._channels.count(99999) == 0); // errors do not poison the cache.
  client.getCachedChannels(99999);
  assert(client.fetches == fetches + 2); // retries still possible.
  SleepyDiscord::VoiceState state;
  client._current_voice_context.channelID = std::string("1");
  state.channelID = std::string("1");
  state.userID = std::string("1");
  client.onEditVoiceState(state);
  for (int i = 2; i <= 10000; ++i) {
    state.channelID = std::to_string(i);
    state.userID = std::to_string(i);
    client.onEditVoiceState(state);
    assert(client._voice_states.size() <= NXCordLimits::voice_channels);
    assert(client._voice_states.count(1) == 1);
  }
  state.channelID = std::string("1");
  for (int i = 10001; i <= 11000; ++i) {
    state.userID = std::to_string(i);
    client.onEditVoiceState(state);
  }
  assert(client._voice_states[1].size() == NXCordLimits::users_per_channel);
  // A 200-guild account is represented by compact IPC rows, not Server models.
  std::string guilds = "[";
  for (int i = 1; i <= 250; ++i) {
    if (i > 1) guilds += ",";
    guilds += "{\"id\":\"" + std::to_string(i) + "\",\"name\":\"Guild\",\"roles\":[{\"id\":\"999\",\"name\":\"ignore\"}]}";
  }
  guilds += "]";
  std::vector<IPCStruct::DiscordServer> servers;
  assert(DiscordLists::parse(guilds, servers, IPCStruct::max_servers));
  assert(servers.size() == 200 && servers.back().id == 200);
  std::string channels = "[";
  for (int i = 1; i <= 1000; ++i) {
    if (i > 1) channels += ",";
    channels += "{\"id\":\"" + std::to_string(i) + "\",\"name\":\"Channel\",\"type\":" +
                std::to_string(i % 2 ? 0 : 2) + ",\"permission_overwrites\":[{\"id\":\"999\",\"type\":0}]}";
  }
  channels += "]";
  std::vector<IPCStruct::DiscordChannel> voices;
  assert(DiscordLists::parse(channels, voices, 500, 123));
  assert(voices.size() == 500 && voices.back().serverId == 123 && voices.back().id == 1000);
  assert(!DiscordLists::parse("{\"message\":\"error\"}", voices, 500) && voices.empty());
  assert(!DiscordLists::parse("[{\"id\":\"1\"}", servers, 200) && servers.empty());
  std::string buffer;
  assert(NXCordLimits::append(buffer, "1234", 4, 4));
  assert(!NXCordLimits::append(buffer, "5", 1, 4) && buffer == "1234");
  // IPC names must remain valid UTF-8 at every 31-byte truncation boundary.
  std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> convert;
  for (int prefix = 0; prefix <= 40; ++prefix) {
    std::string name(prefix, 'x');
    name += u8"😀猫è vocal channel";
    auto server = IPCStruct::create_discord_server(name, 1);
    auto voice = IPCStruct::create_discord_channel(name, 1, 2, IPCStruct::DiscordChannelType::SERVER_VOICE);
    auto user = IPCStruct::create_discord_voice_state(3, name);
    convert.from_bytes(server.name);
    convert.from_bytes(voice.name);
    convert.from_bytes(user.name);
    assert(std::strlen(server.name) <= 31);
  }
  assert(!nxcord_gateway_event("MESSAGE_CREATE"));
  assert(nxcord_gateway_event("VOICE_STATE_UPDATE") && nxcord_gateway_event("VOICE_SERVER_UPDATE"));
  puts("Account memory regression tests passed");
}
'''

with tempfile.TemporaryDirectory(prefix="nxcord-account-") as temp:
    temp = Path(temp)
    cpp = temp / "account.cpp"
    cpp.write_text(harness + methods + checks)
    binary = temp / "account"
    subprocess.run([
        "g++", "-std=c++17", "-O1", "-Wno-deprecated-declarations",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "externals/sleepy-discord/include"),
        "-I" + str(ROOT / "externals/sleepy-discord/include/sleepy_discord"),
        str(cpp), str(ROOT / "common/src/ipc_structures.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
