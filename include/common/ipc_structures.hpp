#pragma once

#include <cstring>
#include <cstdint>
#include <string>

#define SERVICE_NAME "nx:cord"

#define IPC_COMMAND_ENUM      \
  enum class CommandId {      \
    Ping = 0,                 \
    IsConnected,              \
    IsConnecting,             \
    AttemptLogin,             \
    Submit2faCode,            \
    TokenAvailable,           \
    StartConnection,          \
    StopConnection,           \
    GetServers,               \
    GetChannels,              \
    JoinVoiceChannel,         \
    DisconnectVoiceChannel,   \
    IsConnectedVoiceChannel,  \
    Logout,                   \
    SetMicrophoneAmplifier,   \
    GetMicrophoneAmplifier,   \
    SetGlobalAudioVolume,     \
    GetGlobalAudioVolume,     \
    GetMicrophoneVolume,      \
    SetMicrophoneThreshold,   \
    GetMicrophoneThreshold,   \
    GetVoiceStates,           \
    GetUserID,                \
    GetServer,                \
    GetConnectedVoiceChannel, \
    SetVoiceUserMultiplier,   \
    GetVoiceUserMultiplier,   \
    GetVoiceStatus,           \
    SetScreenSharing,         \
    IsScreenSharing,          \
  };

namespace IPCStruct {
struct Login {
  char email[0x100];
  char password[0x100];
};

struct LoginResult {
  bool success;
  bool has2fa;
  char error_message[0x100];
};

struct MfaCode {
  char code[0x20];
};

struct DiscordServer {
  char name[0x20];
  int64_t id;
};

enum class DiscordChannelType {
  CHANNEL_TYPE_NONE = -1,
  SERVER_TEXT = 0,
  DM = 1,
  SERVER_VOICE = 2,
  GROUP_DM = 3,
  SERVER_CATEGORY = 4,
  GUILD_NEWS = 5,
  GUILD_STORE = 6
};

struct DiscordChannel {
  char name[0x20];
  int64_t serverId;
  int64_t id;
  DiscordChannelType type;
};

enum class VoiceConnectionState : uint32_t { Disconnected, Connecting, Connected, Failed };
struct VoiceStatus {
  VoiceConnectionState state = VoiceConnectionState::Disconnected;
  DiscordChannel channel{};
  int32_t close_code = 0;
  bool screen_sharing = false;
  char detail[160]{};
};

struct DiscordChannelsRequest {
  size_t offset;
  int64_t serverId;
};

struct DiscordVoiceState {
  int64_t userId;
  char name[32];
};

struct DiscordVoiceUserMultiplier {
  int64_t user_id;
  float multiplier;
};

template <typename A, size_t max>
struct Array {
  size_t size;
  A items[max];
};

constexpr size_t max_servers = 200;
typedef Array<DiscordServer, max_servers> DiscordServers;

typedef Array<DiscordChannel, 100>
    DiscordChannels;  // 500 is the limit of channels, pass them as chunks,
// otherwise it will get too big

typedef Array<DiscordVoiceState, 100> DiscordVoiceStates;

Login create_login(const std::string &email, const std::string &password);
DiscordServer create_discord_server(const std::string &name, int64_t id);
DiscordChannel create_discord_channel(const std::string &name, int64_t serverId,
                                      int64_t id,
                                      IPCStruct::DiscordChannelType type);
DiscordVoiceState create_discord_voice_state(int64_t userId,
                                             const std::string &name);

}  // namespace IPCStruct
