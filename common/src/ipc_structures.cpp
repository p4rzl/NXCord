#include <common/ipc_structures.hpp>

namespace IPCStruct {
namespace {
template <size_t N>
void copy_name(char (&out)[N], const std::string &name) {
  // Fixed IPC buffers must not cut a multibyte UTF-8 name: Plutonium's
  // UTF-16 conversion would otherwise throw and terminate the NRO.
  size_t src = 0, dst = 0;
  while (src < name.size() && dst < N - 1) {
    const auto lead = static_cast<unsigned char>(name[src]);
    size_t bytes = lead < 0x80 ? 1 : lead >= 0xC2 && lead <= 0xDF ? 2 :
                   lead >= 0xE0 && lead <= 0xEF ? 3 :
                   lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
    bool valid = bytes != 0 && bytes <= name.size() - src;
    for (size_t i = 1; valid && i < bytes; ++i) {
      auto next = static_cast<unsigned char>(name[src + i]);
      valid = (next & 0xC0) == 0x80;
    }
    if (valid && bytes >= 3) {
      auto next = static_cast<unsigned char>(name[src + 1]);
      valid = !(lead == 0xE0 && next < 0xA0) && !(lead == 0xED && next >= 0xA0) &&
              !(lead == 0xF0 && next < 0x90) && !(lead == 0xF4 && next >= 0x90);
    }
    if (!valid) { out[dst++] = '?'; ++src; continue; }
    if (bytes > N - 1 - dst) break;
    std::memcpy(out + dst, name.data() + src, bytes);
    dst += bytes;
    src += bytes;
  }
  out[dst] = '\0';
}
}  // namespace

Login create_login(const std::string &email, const std::string &password) {
  IPCStruct::Login login{};
  std::strncpy(login.email, email.c_str(), sizeof(login.email) - 1);
  std::strncpy(login.password, password.c_str(), sizeof(login.password) - 1);
  return login;
}

DiscordServer create_discord_server(const std::string &name, int64_t id) {
  IPCStruct::DiscordServer discord_server{};
  copy_name(discord_server.name, name);
  discord_server.id = id;
  return discord_server;
}

DiscordChannel create_discord_channel(const std::string &name, int64_t serverId,
                                      int64_t id,
                                      IPCStruct::DiscordChannelType type) {
  IPCStruct::DiscordChannel discord_channel{};
  copy_name(discord_channel.name, name);
  discord_channel.serverId = serverId;
  discord_channel.id = id;
  discord_channel.type = type;
  return discord_channel;
}

DiscordVoiceState create_discord_voice_state(int64_t userId,
                                             const std::string &name) {
  DiscordVoiceState voiceState{};
  voiceState.userId = userId;
  copy_name(voiceState.name, name);
  return voiceState;
}
}  // namespace IPCStruct
