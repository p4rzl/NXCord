#!/usr/bin/env python3
"""Production UDP discovery parsing, client-thread-only protocol dispatch, and Voice Gateway JSON serialization."""
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "externals/sleepy-discord/sleepy_discord/voice_connection.cpp").read_text()


def method(signature):
    start = source.index(signature)
    pos = source.index("{", start)
    end, depth = pos + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


harness = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <rapidjson/document.h>

namespace NXCord {
struct DaveSession {
  static constexpr uint16_t maxVersion = 1;
  static constexpr size_t maxMembers = 32;
  std::vector<std::string> added;
  uint32_t videoSsrc = 0;
  bool addUser(const std::string& id) { added.push_back(id); return true; }
  void removeUser(const std::string&) {}
  bool configure(uint16_t, uint32_t) { return true; }
  bool configureVideo(uint32_t ssrc) { videoSsrc = ssrc; return true; }
  bool prepareEpoch(uint64_t, uint16_t) { return true; }
  bool prepareTransition(uint16_t, uint16_t) { return true; }
  bool executeTransition(uint16_t) { return true; }
  void stop() {}
  bool ready() const { return true; }
};
}

namespace json {
template<class T> int toInt(const T& v) { return v.GetInt(); }
template<class T> double toDouble(const T& v) { return v.GetDouble(); }
template<class T> uint32_t toUint(const T& v) { return v.GetUint(); }
template<class T> std::string toStdString(const T& v) { return std::string(v.GetString(), v.GetStringLength()); }
using Value = rapidjson::Value;
using Array = rapidjson::Value::ConstArray;
}

struct User {};
template<class T>
struct Snowflake {
  std::string raw;
  Snowflake() = default;
  Snowflake(const char* s) : raw(s) {}
  Snowflake(const std::string& s) : raw(s) {}
  Snowflake(const rapidjson::Value& v) : raw(v.GetString(), v.GetStringLength()) {}
  operator const std::string&() const { return raw; }
  const std::string& string() const { return raw; }
  int64_t number() const { return std::stoll(raw); }
};

struct Timer {
  bool isValid() const { return true; }
  void stop() {}
};

struct Origin {
  const std::thread::id clientThread = std::this_thread::get_id();
  int connection = 0;
  std::vector<std::string> sent;
  Snowflake<User> getID() const { return Snowflake<User>("1111222233334444"); }
  time_t getEpochTimeMillisecond() const { return 1700000000000LL; }
  void send(std::string message, int) {
    assert(std::this_thread::get_id() == clientThread);
    rapidjson::Document doc;
    doc.Parse(message.c_str(), message.size());
    assert(!doc.HasParseError() && doc.IsObject());
    sent.push_back(std::move(message));
  }
};

struct VoiceConnection {
  enum VoiceOPCode {
    IDENTIFY = 0, SELECT_PROTOCOL = 1, READY = 2, HEARTBEAT = 3,
    SESSION_DESCRIPTION = 4, SPEAKING = 5, HEARTBEAT_ACK = 6,
    RESUME = 7, HELLO = 8, RESUMED = 9, CLIENT_DISCONNECT = 13
  };
  enum State : uint8_t {
    NOT_CONNECTED = 0, CONNECTED = 1 << 0, OPEN = 1 << 1,
    AUDIO_ENABLED = 1 << 2, SENDING_AUDIO = 1 << 3, ABLE = 7
  };
  static constexpr size_t SECRET_KEY_SIZE = 32;
  uint32_t sSRC = 0x12345678;
  uint32_t videoSSRC = 0;
  uint32_t rtxSSRC = 0;
  uint16_t port = 0;
  time_t heartbeatInterval = 0;
  int lastVoiceSequence = 42;
  bool encryptionFailed = false;
  bool videoEnabled = false;
  bool wasPreviouslySpeaking = false;
  time_t lastTimeSentSpeakingState = 0;
  State state = CONNECTED;
  Timer heart;
  unsigned char secretKey[SECRET_KEY_SIZE]{};
  struct Context {
    std::string serverID = "9999888877776666";
    std::string channelID = "5555666677778888";
    std::string sessionID = "sess_abcdef";
    std::string token = "tok_123456";
    struct Handler {
      void onSpeaking(VoiceConnection&) {}
      void onHeartbeat(VoiceConnection&) {}
      void onHeartbeatAck(VoiceConnection&, time_t) {}
    } *eventHandler = nullptr;
  } context;
  struct Transport {
    int unset = 0;
    void unsetReceiveHandler() { ++unset; }
    template<class F> void setReceiveHandler(F&&) {}
    bool connect(const std::string&, uint16_t) { return true; }
    void send(const unsigned char*, size_t) {}
  } UDP;
  Origin *origin;
  int connection = 0;
  std::unique_ptr<NXCord::DaveSession> dave = std::make_unique<NXCord::DaveSession>();
  std::mutex protocolLock;
  std::mutex ssrcLock;
  std::mutex decodersLock;
  std::map<int64_t, uint32_t> userSSRCs;
  std::map<uint32_t, int> decoders;
  std::string pendingProtocol;
  void failEncryption() { encryptionFailed = true; }
  void heartbeat() {}
  void notifyCryptoState(unsigned int) {}
  void enableAudioIfReady() {}
  void initialize();
  void processMessage(const std::string&);
  void send_heartbeat(bool);
  void sendSpeaking(bool);
  void setVideoEnabled(bool enabled, int width = 640, int height = 360);
  void ipDiscovery(const std::vector<uint8_t>&);
  void flushPendingUDP();
};
'''
checks = r'''
int main() {
  Origin origin;
  VoiceConnection voice;
  voice.origin = &origin;

  // 1. RESUME (op 7) must be valid JSON with closing braces
  voice.initialize();
  assert(origin.sent.size() == 1);
  assert(origin.sent.back().find("\"op\":7") != std::string::npos);
  assert(origin.sent.back().find("\"seq_ack\":42") != std::string::npos);

  // 2. HELLO (op 8) -> IDENTIFY (op 0) must be valid JSON with video + max_dave_protocol_version
  voice.processMessage(R"({"op":8,"d":{"heartbeat_interval":15000}})");
  assert(!voice.encryptionFailed);
  assert(origin.sent.size() == 2);
  assert(origin.sent.back().find("\"op\": 0") != std::string::npos);
  assert(origin.sent.back().find("\"video\": true") != std::string::npos);
  assert(origin.sent.back().find("\"max_dave_protocol_version\":1") != std::string::npos);

  // 3. READY (op 2) parses ssrc, videoSSRC, rtxSSRC and sets State::OPEN
  voice.processMessage(R"({"op":2,"d":{"ssrc":305419896,"ip":"203.0.113.1","port":55555,"streams":[{"type":"video","ssrc":305419900,"rtx_ssrc":305419901}]}})");
  assert(voice.sSRC == 0x12345678);
  assert(voice.videoSSRC == 305419900);
  assert(voice.rtxSSRC == 305419901);

  // 4. HEARTBEAT (op 3), SPEAKING (op 5), and VIDEO (op 12 + Main op 4) outgoing JSON
  voice.send_heartbeat(true);
  assert(origin.sent.size() == 3);
  assert(origin.sent.back().find("\"op\": 3") != std::string::npos);
  voice.sendSpeaking(true);
  assert(origin.sent.size() == 4);
  assert(origin.sent.back().find("\"op\":5") != std::string::npos);
  voice.setVideoEnabled(true, 640, 360);
  assert(origin.sent.size() == 6);
  assert(origin.sent[4].find("\"self_video\":true") != std::string::npos);
  assert(origin.sent[5].find("\"op\":12") != std::string::npos);
  assert(origin.sent[5].find("\"video_ssrc\":305419900") != std::string::npos);
  assert(voice.dave->videoSsrc == 305419900);

  // 5. Incoming SPEAKING (op 5) and CLIENT_CONNECT (op 12) register user with DAVE and SSRC map
  voice.processMessage(R"({"op":5,"d":{"user_id":"2222333344445555","ssrc":777,"speaking":1}})");
  voice.processMessage(R"({"op":12,"d":{"user_id":"3333444455556666","audio_ssrc":888}})");
  assert(voice.dave->added.size() == 2);
  assert(voice.userSSRCs[2222333344445555LL] == 777);
  assert(voice.userSSRCs[3333444455556666LL] == 888);

  // 6. UDP IP discovery -> SELECT_PROTOCOL (op 1) with opus + H264 codecs
  origin.sent.clear();
  std::vector<uint8_t> response(74);
  response[1] = 2; response[3] = 70;
  response[4] = 0x12; response[5] = 0x34; response[6] = 0x56; response[7] = 0x78;
  std::memcpy(response.data() + 8, "203.0.113.1", 11);
  response[72] = 0xd9; response[73] = 0x03; // 55555, big endian
  std::thread udp([&] { voice.ipDiscovery(response); });
  udp.join();
  assert(origin.sent.empty() && voice.UDP.unset == 1);
  voice.flushPendingUDP();
  assert(origin.sent.size() == 1);
  assert(origin.sent[0].find("55555") != std::string::npos);
  assert(origin.sent[0].find("203.0.113.1") != std::string::npos);
  assert(origin.sent[0].find("aead_xchacha20_poly1305_rtpsize") != std::string::npos);
  assert(origin.sent[0].find("\"name\":\"H264\"") != std::string::npos);
  voice.flushPendingUDP();
  assert(origin.sent.size() == 1);
  auto reject = [&](std::vector<uint8_t> data) {
    voice.ipDiscovery(data); voice.flushPendingUDP();
    assert(origin.sent.size() == 1 && voice.UDP.unset == 1);
  };
  auto bad = response; bad.resize(73); reject(bad);
  bad = response; bad[1] = 1; reject(bad);
  bad = response; bad[3] = 69; reject(bad);
  bad = response; bad[7] ^= 1; reject(bad);
  bad = response; std::fill(bad.begin() + 8, bad.begin() + 72, 'a'); reject(bad);
  bad = response; bad[8] = '"'; reject(bad);
  bad = response; bad[8] = 0; reject(bad);
  bad = response; bad[72] = bad[73] = 0; reject(bad);
  puts("Voice UDP discovery/thread-dispatch and JSON payload regression tests passed");
}
'''
with tempfile.TemporaryDirectory(prefix="nxcord-voice-transport-") as temp:
    temp = Path(temp)
    cpp = temp / "transport.cpp"
    cpp.write_text(
        harness
        + method("void VoiceConnection::initialize")
        + method("void VoiceConnection::processMessage")
        + method("void VoiceConnection::send_heartbeat")
        + method("void VoiceConnection::sendSpeaking")
        + method("void VoiceConnection::setVideoEnabled")
        + method("void VoiceConnection::ipDiscovery")
        + method("void VoiceConnection::flushPendingUDP")
        + checks
    )
    binary = temp / "transport"
    subprocess.run([
        "g++", "-std=c++17", "-O1", "-pthread", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I" + str(ROOT / "externals/sleepy-discord/include/sleepy_discord"),
        str(cpp), "-o", str(binary)
    ], check=True)
    subprocess.run([str(binary)], check=True)

audio_capture = (ROOT / "nxcord/src/nxcord_audio_capture.cpp").read_text()
audio_player = (ROOT / "nxcord/src/nxcord_audio_player.cpp").read_text()
assert "audinGetReleasedAudioInBuffer" in audio_capture
assert "audin_released_buf == &audio_capture->_audin_buf" in audio_capture
assert "audoutGetReleasedAudioOutBuffer" in audio_player

