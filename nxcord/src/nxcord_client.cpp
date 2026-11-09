#include <algorithm>
#include <common/logger.hpp>
#include <common/utils.hpp>
#include <nxcord/discord_websocket.hpp>
#include <nxcord/nxcord_audio_capture.hpp>
#include <nxcord/nxcord_audio_player.hpp>
#include <nxcord/nxcord_client.hpp>
#include <nxcord/nxcord_screen_share.hpp>
#include <nxcord/discord_list_parser.hpp>

class AudioReceiver : public SleepyDiscord::BaseAudioOutput {
 private:
  NXCordClient &_client;
  NXCordAudioPlayer _player;

  friend NXCordClient;

 public:
  explicit AudioReceiver(NXCordClient &client) : _client(client) {}

  void write(std::vector<SleepyDiscord::AudioSample> &audio,
             SleepyDiscord::AudioTransmissionDetails &details) override {
    std::string global_audio_volume_str =
        _client.getSettings().getvoiceglobal_audio_volume();
    float global_audio_volume = std::stof(global_audio_volume_str);
    for (auto &sample : audio) {
      float scaled = static_cast<float>(sample) * global_audio_volume;
      if (scaled > static_cast<float>(Utils::audio_sample_max)) {
        sample = Utils::audio_sample_max;
      } else if (scaled < static_cast<float>(Utils::audio_sample_min)) {
        sample = Utils::audio_sample_min;
      } else {
        sample = static_cast<SleepyDiscord::AudioSample>(scaled);
      }
    }
    _player.queue(audio, details);
  }
};

class AudioSender : public SleepyDiscord::AudioVectorSource {
 private:
  NXCordClient &_client;
  NXCordAudioCapture _audio_capture;
  std::vector<SleepyDiscord::AudioSample> _capture;

 public:
  explicit AudioSender(NXCordClient &client) : _client(client) {}

  ~AudioSender() override { _client._previous_mic_volume = 0; }

  bool frameAvailable() override {
    _capture = _audio_capture.poll();
    float average_volume = 0;
    std::string multiplier_str = _client.getSettings().getvoicemic_multiplier();
    float multiplier = std::stof(multiplier_str);
    if (!_capture.empty()) {
      for (auto &sample : _capture) {
        float scaled = static_cast<float>(sample) * multiplier;
        if (scaled > static_cast<float>(Utils::audio_sample_max)) {
          sample = Utils::audio_sample_max;
        } else if (scaled < static_cast<float>(Utils::audio_sample_min)) {
          sample = Utils::audio_sample_min;
        } else {
          sample = static_cast<SleepyDiscord::AudioSample>(scaled);
        }
        average_volume += static_cast<float>(std::abs(sample)) /
                          static_cast<float>(Utils::audio_sample_max);
      }
      average_volume /= static_cast<float>(_capture.size());
    }
    _client._previous_mic_volume = average_volume;

    std::string threshold_str = _client.getSettings().getvoicemic_threshold();
    float threshold = std::stof(threshold_str);
    bool send = average_volume > std::max(threshold, 0.00001f);

    // Game audio (only while screen sharing) is mixed into the mic stream and
    // always bypasses the voice gate so the viewers keep hearing the game.
    if (_client._game_audio && _client._game_audio->isActive()) {
      constexpr size_t kFrameSamples = Utils::opus_framesize_bytes / sizeof(int16_t);
      int16_t game[kFrameSamples];
      const size_t got = _client._game_audio->read(game, kFrameSamples);
      if (got > 0) {
        if (_capture.size() != kFrameSamples) {
          _capture.assign(kFrameSamples, 0);
        }
        // Game audio is attenuated to ~60% so the voice stays intelligible.
        for (size_t i = 0; i < kFrameSamples; ++i) {
          const int mixed = static_cast<int>(_capture[i]) +
                            (static_cast<int>(game[i]) * 6) / 10;
          _capture[i] = static_cast<SleepyDiscord::AudioSample>(
              std::clamp(mixed, static_cast<int>(Utils::audio_sample_min),
                         static_cast<int>(Utils::audio_sample_max)));
        }
        send = true;
      }
    }
    return send;
  }

  void read(SleepyDiscord::AudioTransmissionDetails &details,
            SleepyDiscord::AudioVectorSource::Container &target) override {
    if (!_capture.empty()) {
      std::memcpy(target.data(), _capture.data(),
                  _capture.size() * sizeof(SleepyDiscord::AudioSample));
    }
  }
};

class VoiceEventHandler : public SleepyDiscord::BaseVoiceEventHandler {
 private:
  NXCordClient &_client;

 public:
  explicit VoiceEventHandler(NXCordClient &client) : _client(client) {}

  void onReady(SleepyDiscord::VoiceConnection &connection) override {
    Logger::write("NXCordClient: VoiceEventHandler: onReady\n");

    _client._current_voice_connection = &connection;
    _client._current_voice_context =
        NXCordClient::LocalVoiceContext(connection.getContext());
    _client._voice_status.state = IPCStruct::VoiceConnectionState::Connected;
    std::strncpy(_client._voice_status.detail, "Sessione vocale pronta. Puoi tornare al gioco.",
                  sizeof(_client._voice_status.detail) - 1);

    SleepyDiscord::BaseAudioOutput *audio_receiver = new AudioReceiver(_client);
    connection.setAudioOutput(audio_receiver);
    connection.startSpeaking<AudioSender>(_client);
    connection.startListening();
  }

  void onClosed(SleepyDiscord::VoiceConnection &, int code) override {
    _client.stopScreenShare();
    _client._current_voice_connection = nullptr;
    _client._current_voice_context = NXCordClient::LocalVoiceContext();
    _client._voice_status.state = IPCStruct::VoiceConnectionState::Failed;
    _client._voice_status.close_code = code;
    _client._voice_status.screen_sharing = false;
    const char *reason = code == 4017 ? "Il server richiede una versione DAVE/E2EE non supportata." :
                         code == -2 ? "Negoziazione DAVE/voce fallita o limite di 32 partecipanti superato." :
                         code == 4002 ? "Payload vocale non valido (4002)." :
                         code == 4003 ? "Sessione vocale non autenticata (4003)." :
                         code == 4006 ? "Sessione vocale non piu valida. Attendi e riprova." :
                         code == 4009 ? "Timeout della sessione vocale (4009). Riprova." :
                         code == 4015 ? "Il server vocale si e riavviato (4015). Riprova." :
                         code == 1006 ? "Connessione di rete vocale interrotta senza codice Discord." :
                         code == 4014 ? "Discord ha disconnesso la sessione vocale." :
                         code == 4016 ? "Modalita di cifratura rifiutata da Discord." :
                         code == 4004 ? "Sessione vocale rifiutata. Riprova l'ingresso." :
                                        "Connessione vocale chiusa. Controlla rete e permessi.";
    std::strncpy(_client._voice_status.detail, reason, sizeof(_client._voice_status.detail) - 1);
    Logger::write("Voice connection closed: %d\n", code);
  }

  void onCryptoState(SleepyDiscord::VoiceConnection &, unsigned int opcode, bool ready) override {
    Logger::write("Voice crypto: opcode=%u ready=%d\n", opcode, ready);
    if (!ready && _client._voice_status.state == IPCStruct::VoiceConnectionState::Connected)
      _client._voice_started = Utils::current_time_millis();
    _client._voice_status.state = ready ? IPCStruct::VoiceConnectionState::Connected :
                                       IPCStruct::VoiceConnectionState::Connecting;
    std::strncpy(_client._voice_status.detail,
                 !ready ? "Negoziazione della cifratura vocale in corso..." :
                 _client.isScreenSharing() ? "Condivisione schermo attiva (640x360 H.264)." :
                                             "Sessione vocale pronta. Puoi tornare al gioco.",
                 sizeof(_client._voice_status.detail) - 1);
  }

  inline void onHeartbeat(SleepyDiscord::VoiceConnection &) override {
    Logger::write("Voice heartbeat sent\n");
  }

  inline void onHeartbeatAck(SleepyDiscord::VoiceConnection &,
                             const time_t ping) override {
    Logger::write("Voice heartbeat acknowledged ping %lums\n", ping);
  }
};

NXCordClient::~NXCordClient() { stopScreenShare(); }

void NXCordClient::onReady() {
  Logger::write("NXCordClient: onReady\n");
  stopScreenShare();
  _ready = true;
  _servers.clear();
  _servers.shrink_to_fit();
  _channels.clear();
  _current_voice_connection = nullptr;
  _voice_states.clear();
  _voice_status = {};

  auto response = getServers();
  if (response.statusCode != SleepyDiscord::OK ||
      !DiscordLists::parse(response.text, _servers, IPCStruct::max_servers)) {
    Logger::write("Unable to load server list (HTTP %d)\n", response.statusCode);
  }
}

void NXCordClient::onResumed() {
  Logger::write("NXCordClient: onResume\n");
  stopScreenShare();
  _ready = true;
  _channels.clear();

  _current_voice_connection = nullptr;
  if (_current_voice_context.serverID.number() && _current_voice_context.channelID.number()) {
    Logger::write("Rejoining voice channel %ld %ld\n",
                  _current_voice_context.serverID.number(),
                  _current_voice_context.channelID.number());
    const auto server = _current_voice_context.serverID.number();
    const auto channel = _current_voice_context.channelID.number();
    // A new voice socket needs fresh Gateway voice events. Reusing the cached
    // token/endpoint/session can race the updates and produce voice 4006.
    disconnectVoiceChannel();
    joinVoiceChannel(server, channel);
  }
}

void NXCordClient::onServer(SleepyDiscord::Server server) {
  Logger::write("NXCordClient: onServer %s %ld\n", server.name.c_str(),
                server.ID.number());
  addServer(server);

  for (const auto &voice_state : server.voiceStates) {
    if (!voice_state.channelID.string().empty()) {
      std::string name;
      auto it = server.findMember(voice_state.userID);
      if (it != server.members.end()) {
        name = it->nick;
        if (name.empty()) {
          name = it->user.username;
        }
      }
      if (name.empty()) {
        name = voice_state.serverMember.nick;
        if (name.empty()) {
          name = voice_state.serverMember.user.username;
        }
      }
      if (name.empty()) {
        name = "User " + voice_state.userID.string();
      }
      auto state = voice_state;
      state.serverMember.nick = name;
      onEditVoiceState(state);
    }
  }
}

void NXCordClient::onEditServer(SleepyDiscord::Server editServer) {
  Logger::write("NXCordClient: onEditServer %s %ld\n", editServer.name.c_str(),
                editServer.ID.number());

  auto it = std::find_if(_servers.begin(), _servers.end(),
                         [&editServer](const IPCStruct::DiscordServer &server) {
                           return editServer.ID.number() == server.id;
                         });
  if (it != _servers.end() && !editServer.name.empty()) {
    IPCStruct::DiscordServer discord_server = IPCStruct::create_discord_server(
        editServer.name, editServer.ID.number());
    *it = discord_server;
  }
}

void NXCordClient::onDeleteServer(
    SleepyDiscord::UnavailableServer unavailableServer) {
  Logger::write("NXCordClient: onDeleteServer %ld\n",
                unavailableServer.ID.number());

  _servers.erase(
      std::remove_if(
          _servers.begin(), _servers.end(),
          [&unavailableServer](const IPCStruct::DiscordServer &server) {
            return unavailableServer.ID.number() == server.id;
          }),
      _servers.end());
}

void NXCordClient::addServer(const SleepyDiscord::Server &serverToAdd) {
  if (serverToAdd.ID.number() == 0) return;
  auto it =
      std::find_if(_servers.begin(), _servers.end(),
                   [&serverToAdd](const IPCStruct::DiscordServer &server) {
                     return serverToAdd.ID.number() == server.id;
                   });
  if (it != _servers.end()) {
    return;
  }

  if (_servers.size() < IPCStruct::max_servers) {
    _servers.push_back(IPCStruct::create_discord_server(
        serverToAdd.name, serverToAdd.ID.number()));
  }
}

inline IPCStruct::DiscordChannel create_ipc_channel(
    const SleepyDiscord::Channel &channel) {
  return IPCStruct::create_discord_channel(
      channel.name, channel.serverID.number(), channel.ID.number(),
      static_cast<IPCStruct::DiscordChannelType>(channel.type));
}

void NXCordClient::onChannel(SleepyDiscord::Channel channel) {
  Logger::write("NXCordClient: onChannel %s %ld\n", channel.name.c_str(),
                channel.ID.number());

  auto channels = getChannelsPtr(channel);
  if (channels && channel.type == SleepyDiscord::Channel::SERVER_VOICE &&
      channels->size() < NXCordLimits::channels_per_guild) {
    auto it = std::find_if(channels->begin(), channels->end(),
                           [&channel](const auto &item) { return item.id == channel.ID.number(); });
    if (it == channels->end()) channels->push_back(create_ipc_channel(channel));
  }
}

void NXCordClient::onEditChannel(SleepyDiscord::Channel channel) {
  Logger::write("NXCordClient: onEditChannel %s %ld\n", channel.name.c_str(),
                channel.ID.number());

  auto channels = getChannelsPtr(channel);
  if (channels) {
    auto it = std::find_if(
        channels->begin(), channels->end(),
        [&channel](const IPCStruct::DiscordChannel &cached_channel) {
          return channel.ID.number() == cached_channel.id;
        });

    if (channel.type != SleepyDiscord::Channel::SERVER_VOICE) {
      if (it != channels->end()) channels->erase(it);
    } else if (it != channels->end()) {
      *it = create_ipc_channel(channel);
    } else if (channels->size() < NXCordLimits::channels_per_guild) {
      channels->push_back(create_ipc_channel(channel));
    }
  }
}

void NXCordClient::onDeleteChannel(SleepyDiscord::Channel channel) {
  Logger::write("NXCordClient: onDeleteChannel %s %ld\n", channel.name.c_str(),
                channel.ID.number());

  auto channels = getChannelsPtr(channel);
  if (channels) {
    channels->erase(
        std::remove_if(
            channels->begin(), channels->end(),
            [&channel](const IPCStruct::DiscordChannel &cached_channel) {
              return channel.ID.number() == cached_channel.id;
            }),
        channels->end());
  }
}

bool NXCordClient::fillChannels(
    const SleepyDiscord::Snowflake<SleepyDiscord::Server> &channel) {
  std::vector<IPCStruct::DiscordChannel> &cached_channels =
      _channels[channel.number()];
  auto response = getServerChannels(channel);
  if (response.statusCode != SleepyDiscord::OK ||
      !DiscordLists::parse(response.text, cached_channels,
                           NXCordLimits::channels_per_guild, channel.number())) {
    Logger::write("Unable to load voice channels (HTTP %d)\n", response.statusCode);
    _channels.erase(channel.number());
    return false;
  }
  return true;
}

const std::vector<IPCStruct::DiscordChannel> &NXCordClient::getCachedChannels(
    int64_t serverId) {
  SleepyDiscord::Snowflake<SleepyDiscord::Server> serverSnowflake(serverId);
  auto channels = getChannelsPtr(serverSnowflake);
  if (channels) {
    return *channels;
  } else {
    static const std::vector<IPCStruct::DiscordChannel> empty;
    if (serverId <= 0 || !NXCordLimits::make_room(
            _channels, serverId, _current_voice_context.serverID.number(),
            NXCordLimits::cached_guilds)) return empty;
    if (!fillChannels(serverSnowflake)) return empty;
    return _channels[serverId];
  }
}

void NXCordClient::onEditVoiceState(const SleepyDiscord::VoiceState &state) {
  if (state.userID.number() == 0) return;
  for (auto &pair : _voice_states) {
    std::vector<IPCStruct::DiscordVoiceState> &states = pair.second;
    auto it = std::find_if(
        states.begin(), states.end(),
        [&state](const IPCStruct::DiscordVoiceState &discordVoiceState) {
          return state.userID.number() == discordVoiceState.userId;
        });
    if (it != states.end()) {
      states.erase(it);
      if (states.empty()) {
        _voice_states.erase(pair.first);
      }
      break;
    }
  }

  if (!state.channelID.string().empty()) {
    const int64_t channel_id = state.channelID.number();
    if (!NXCordLimits::make_room(_voice_states, channel_id,
                                  _current_voice_context.channelID.number(),
                                  NXCordLimits::voice_channels)) return;
    std::vector<IPCStruct::DiscordVoiceState> &states =
        _voice_states[channel_id];
    if (states.size() >= NXCordLimits::users_per_channel) return;
    std::string name = state.serverMember.nick;
    if (name.empty()) {
      name = state.serverMember.user.username;
    }
    if (name.empty()) {
      name = "User " + state.userID.string();
    }

    states.emplace_back(
        IPCStruct::create_discord_voice_state(state.userID.number(), name));
  }
}

void NXCordClient::joinVoiceChannel(int64_t serverId, int64_t channelId) {
  Logger::write("Joining voice channel %ld %ld\n", serverId, channelId);

  if (!isConnected() || serverId <= 0 || channelId <= 0) {
    _voice_status.state = IPCStruct::VoiceConnectionState::Failed;
    std::strncpy(_voice_status.detail, "Connettiti prima a Discord e scegli un canale valido.",
                  sizeof(_voice_status.detail) - 1);
    return;
  }
  if ((_voice_status.state == IPCStruct::VoiceConnectionState::Connecting ||
       isConnectedVoiceChannel()) && _voice_status.channel.id == channelId) return;
  disconnectVoiceChannel();
  _voice_status.state = IPCStruct::VoiceConnectionState::Connecting;
  _voice_status.channel = IPCStruct::create_discord_channel(
      "Canale vocale", serverId, channelId, IPCStruct::DiscordChannelType::SERVER_VOICE);
  auto cached = _channels.find(serverId);
  if (cached != _channels.end()) {
    for (const auto &item : cached->second) {
      if (item.id == channelId) { _voice_status.channel = item; break; }
    }
  }
  std::strncpy(_voice_status.detail, "Richiesta inviata. Attendo il server vocale...",
                sizeof(_voice_status.detail) - 1);
  _voice_started = Utils::current_time_millis();

  SleepyDiscord::Snowflake<SleepyDiscord::Server> server(serverId);
  SleepyDiscord::Snowflake<SleepyDiscord::Channel> channel(channelId);

  SleepyDiscord::VoiceContext &context = createVoiceContext(server, channel);
  context.startVoiceHandler<VoiceEventHandler>(*this);
  connectToVoiceChannel(context);
}

void NXCordClient::disconnectVoiceChannel() {
  stopScreenShare();
  disconnectAllVoiceChannels();
  _current_voice_connection = nullptr;
  _current_voice_context = LocalVoiceContext();
  _voice_status = {};
  _voice_started = 0;
}

void NXCordClient::tick() {
  DiscordClient::tick();
  if (_voice_status.state == IPCStruct::VoiceConnectionState::Failed && !voiceConnections.empty()) {
    const auto failed = _voice_status;
    disconnectVoiceChannel();
    _voice_status = failed;
  }
  if (_voice_status.state == IPCStruct::VoiceConnectionState::Connecting &&
      Utils::current_time_millis() - _voice_started >= 30000) {
    auto failed = _voice_status;
    disconnectVoiceChannel();
    _voice_status = failed;
    _voice_status.state = IPCStruct::VoiceConnectionState::Failed;
    std::strncpy(_voice_status.detail, "Nessuna risposta vocale. Verifica rete, accesso e permessi.",
                  sizeof(_voice_status.detail) - 1);
    Logger::write("Voice join timed out\n");
  }
}

IPCStruct::DiscordServer NXCordClient::getServer(int64_t serverId) const {
  auto it = std::find_if(_servers.begin(), _servers.end(),
                         [&serverId](const IPCStruct::DiscordServer &server) {
                           return serverId == server.id;
                         });
  if (it != _servers.end()) {
    return *it;
  }
  return IPCStruct::DiscordServer();
}

IPCStruct::DiscordChannel NXCordClient::getConnectedVoiceChannel() const {
  if (isConnectedVoiceChannel()) {
    for (const auto &pair : _channels) {
      const std::vector<IPCStruct::DiscordChannel> &channel = pair.second;
      auto it = std::find_if(
          channel.begin(), channel.end(),
          [this](const IPCStruct::DiscordChannel &channel) {
            return _current_voice_context.channelID.number() == channel.id;
          });
      if (it != channel.end()) {
        return *it;
      }
    }
  }
  return IPCStruct::DiscordChannel();
}

void NXCordClient::loadSettings(std::unique_ptr<NXCordSettings> &settings) {
  if (settings->valid()) {
    _settings = std::move(settings);
    _token = _settings->gettoken();
  }
}

void NXCordClient::startConnection() {
  if (_settings) {
    _settings->settoken(_token);
  }
  DiscordClient::startConnection();
}

void NXCordClient::setVoiceUserMultiplier(int64_t user_id, float multiplier) {
  if (isConnectedVoiceChannel()) {
    const std::map<int64_t, uint32_t> &ssrcs =
        _current_voice_connection->getUserSSRCs();
    auto it = ssrcs.find(user_id);
    if (it != ssrcs.end()) {
      auto &audio_receiver = dynamic_cast<AudioReceiver &>(
          _current_voice_connection->getAudioOutput());
      audio_receiver._player.setSSRCMultiplier(it->second, multiplier);
    }
  }
}

float NXCordClient::getVoiceUserMultiplier(int64_t user_id) const {
  if (isConnectedVoiceChannel()) {
    const std::map<int64_t, uint32_t> &ssrcs =
        _current_voice_connection->getUserSSRCs();
    auto it = ssrcs.find(user_id);
    if (it != ssrcs.end()) {
      auto &audio_receiver = dynamic_cast<AudioReceiver &>(
          _current_voice_connection->getAudioOutput());
      return audio_receiver._player.getSSRCMultiplier(it->second);
    }
  }
  return 1;
}

void NXCordClient::stopScreenShare() {
  if (_game_audio) {
    _game_audio->stop();
    _game_audio.reset();
  }
  if (_screen_share) {
    _screen_share->stop();
    _screen_share.reset();
  }
  if (_current_voice_connection && _current_voice_connection->isVideoEnabled()) {
    _current_voice_connection->setVideoEnabled(false);
  }
  _voice_status.screen_sharing = false;
}

bool NXCordClient::isScreenSharing() const {
  return _screen_share && _screen_share->isActive();
}

void NXCordClient::setScreenSharing(bool enabled) {
  if (!enabled) {
    stopScreenShare();
    if (_voice_status.state == IPCStruct::VoiceConnectionState::Connected) {
      std::strncpy(_voice_status.detail,
                   "Sessione vocale pronta. Puoi tornare al gioco.",
                   sizeof(_voice_status.detail) - 1);
    }
    return;
  }

  if (!isConnectedVoiceChannel() || !_current_voice_connection) {
    _voice_status.screen_sharing = false;
    return;
  }
  if (isScreenSharing()) {
    return;
  }

  _current_voice_connection->setVideoEnabled(
      true, NXCord::H264Encoder::kWidth, NXCord::H264Encoder::kHeight);

  SleepyDiscord::VoiceConnection *conn = _current_voice_connection;
  _screen_share = std::make_unique<NXCordScreenShare>(
      [conn](const NXCord::EncodedH264Frame &frame, uint32_t ts90k) {
        conn->sendVideoFrame(frame.annexB.data(), frame.annexB.size(),
                             frame.spsLen, frame.ppsLen, ts90k);
      },
      [this](const std::string &err) {
        std::strncpy(_voice_status.detail, err.c_str(),
                     sizeof(_voice_status.detail) - 1);
      });

  if (_screen_share->start()) {
    _game_audio = std::make_unique<NXCordGameAudio>();
    if (!_game_audio->start()) {
      Logger::write("Game audio capture unavailable; sharing video only\n");
      _game_audio.reset();
    }
    _voice_status.screen_sharing = true;
    std::strncpy(_voice_status.detail,
                 "Condivisione schermo attiva (640x360 H.264).",
                 sizeof(_voice_status.detail) - 1);
  } else {
    _screen_share.reset();
    _current_voice_connection->setVideoEnabled(false);
    _voice_status.screen_sharing = false;
  }
}

#include "h264_encoder.cpp"
#include "nxcord_screen_share.cpp"
#include "nxcord_game_audio.cpp"
