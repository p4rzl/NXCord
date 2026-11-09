#include <nxcord/dave_session.hpp>
#include <dave/dave.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <set>
#include <utility>

bool nxcord_init_dave_crypto();

namespace NXCord {
namespace {
template <typename T, void (*Destroy)(T *)>
using Handle = std::unique_ptr<T, decltype(Destroy)>;
using Session = Handle<DAVESessionHandle_s, daveSessionDestroy>;
using Encryptor = Handle<DAVEEncryptorHandle_s, daveEncryptorDestroy>;
using Decryptor = Handle<DAVEDecryptorHandle_s, daveDecryptorDestroy>;
using Ratchet = Handle<DAVEKeyRatchetHandle_s, daveKeyRatchetDestroy>;
using Bytes = std::unique_ptr<uint8_t, decltype(&daveFree)>;
bool userId(const std::string &id) {
  if (id.empty() || id.size() > 20 || id[0] == '0') return false;
  uint64_t value = 0;
  for (unsigned char c : id) {
    if (c < '0' || c > '9' || value > (UINT64_MAX - (c - '0')) / 10) return false;
    value = value * 10 + c - '0';
  }
  return true;
}
}

struct DaveSession::Impl {
  mutable std::mutex mutex;
  uint64_t channel;
  std::string self;
  Send send;
  bool valid = false, failure = false, configured = false, audioReady = false;
  bool external = false, group = false;
  uint16_t version = 0, pendingVersion = 0;
  uint32_t ssrc = 0, videoSsrc = 0;
  unsigned recoveries = 0;
  Session session{nullptr, daveSessionDestroy};
  Encryptor encryptor{nullptr, daveEncryptorDestroy};
  std::set<std::string> recognized, roster;
  std::map<std::string, Decryptor> decryptors;
  struct Transition {
    uint16_t version = 0;
    bool replaceKey = false;
    Ratchet key{nullptr, daveKeyRatchetDestroy};
  };
  std::map<uint16_t, Transition> transitions;

  Impl(uint64_t channel, std::string self, Send send)
      : channel(channel), self(std::move(self)), send(std::move(send)) {
    // Upstream verbose logs include serialized MLS messages. Use only our
    // fixed, metadata-only diagnostics, even in release/debug builds.
    daveSetLogSinkCallback([](DAVELoggingSeverity, const char *, int, const char *) {});
    if (!channel || !userId(this->self) || !nxcord_init_dave_crypto()) return;
    session.reset(daveSessionCreate(nullptr, nullptr,
        [](const char *, const char *, void *ctx) { static_cast<Impl *>(ctx)->failure = true; }, this));
    encryptor.reset(daveEncryptorCreate());
    recognized.insert(this->self);
    valid = session && encryptor && daveMaxSupportedProtocolVersion() >= maxVersion;
  }
  void text(uint8_t op, uint16_t id) {
    send(false, "{\"op\":" + std::to_string(op) + ",\"d\":{\"transition_id\":" + std::to_string(id) + "}}");
  }
  bool binary(uint8_t op, uint8_t *data, size_t length) {
    Bytes bytes(data, daveFree);
    if (!data || !length || length > 256 * 1024 - 1) return false;
    std::string message(1, static_cast<char>(op));
    message.append(reinterpret_cast<const char *>(data), length);
    send(true, message);
    return true;
  }
  bool keyPackage() {
    uint8_t *bytes = nullptr;
    size_t length = 0;
    daveSessionGetMarshalledKeyPackage(session.get(), &bytes, &length);
    return binary(26, bytes, length) && !failure;
  }
  bool init(uint16_t nextVersion) {
    if (nextVersion == 0 || nextVersion > maxVersion) return false;
    pendingVersion = nextVersion;
    group = false;
    // libdave returns roster *changes* and retains its roster across Reset().
    // Retain our matching roster too, including after a sole-member reset.
    daveSessionInit(session.get(), nextVersion, channel, self.c_str());
    return !failure && keyPackage();
  }
  std::vector<const char *> users() const {
    std::vector<const char *> result;
    for (const auto &id : recognized) result.push_back(id.c_str());
    return result;
  }
  bool stage(uint16_t id, bool replaceKey) {
    if (transitions.count(id) || transitions.size() >= 8) return false;
    Transition transition;
    transition.version = pendingVersion;
    transition.replaceKey = replaceKey;
    if (replaceKey && group) {
      transition.key.reset(daveSessionGetKeyRatchet(session.get(), self.c_str()));
      if (!transition.key) return false;
    }
    transitions.emplace(id, std::move(transition));
    if (id == 0) return execute(id);
    text(23, id);
    return true;
  }
  bool execute(uint16_t id) {
    auto it = transitions.find(id);
    if (it == transitions.end()) return false;
    const auto &transition = it->second;
    if (transition.replaceKey) {
      // nullptr after a sole-member reset stops sending until the group is
      // established. Never retain the previous group's outbound media key.
      daveEncryptorSetKeyRatchet(encryptor.get(), transition.key.get());
    }
    version = transition.version;
    daveEncryptorSetPassthroughMode(encryptor.get(), version == 0);
    // End a former downgrade's receive passthrough too, including a solo
    // upgrade with no commit/roster update. libdave bounds its grace to 10 s.
    if (version) {
      for (auto &item : decryptors)
        daveDecryptorTransitionToPassthroughMode(item.second.get(), false);
    }
    audioReady = true;
    transitions.erase(it);
    return true;
  }
  template <typename Result, typename Ids, typename Signature>
  bool updateRoster(Result result, Ids idsFn, Signature sigFn) {
    uint64_t *ids = nullptr;
    size_t length = 0;
    idsFn(result, &ids, &length);
    std::unique_ptr<uint64_t, decltype(&daveFree)> owned(ids, daveFree);
    if (length > maxMembers * 2 || (length && !ids)) return false;
    for (size_t i = 0; i < length; ++i) {
      const auto id = std::to_string(ids[i]);
      uint8_t *signature = nullptr;
      size_t size = 0;
      sigFn(result, ids[i], &signature, &size);
      Bytes bytes(signature, daveFree);
      if (!size) roster.erase(id);
      else {
        if (!recognized.count(id)) return false;
        roster.insert(id);
      }
    }
    if (roster.size() > maxMembers || !roster.count(self)) return false;
    // Set receiving ratchets before acknowledging readiness, retaining old
    // epochs in libdave for in-flight media. Sending changes only on execute.
    for (const auto &id : roster) {
      if (id == self) continue;
      auto it = decryptors.find(id);
      if (it == decryptors.end()) {
        if (decryptors.size() >= maxMembers) return false;
        it = decryptors.emplace(id, Decryptor(daveDecryptorCreate(), daveDecryptorDestroy)).first;
      }
      Ratchet key(daveSessionGetKeyRatchet(session.get(), id.c_str()), daveKeyRatchetDestroy);
      if (!key || !it->second) return false;
      daveDecryptorTransitionToKeyRatchet(it->second.get(), key.get());
      daveDecryptorTransitionToPassthroughMode(it->second.get(), false);
    }
    for (auto it = decryptors.begin(); it != decryptors.end();) {
      if (!roster.count(it->first)) it = decryptors.erase(it);
      else ++it;
    }
    group = true;
    return !failure;
  }
  bool recover(uint16_t id) {
    if (++recoveries > 3) return false;
    text(31, id);
    transitions.clear();
    audioReady = false;
    daveEncryptorSetKeyRatchet(encryptor.get(), nullptr);
    failure = false;
    return init(pendingVersion);
  }
};

DaveSession::DaveSession(uint64_t channel, std::string self, Send send)
    : impl(std::make_unique<Impl>(channel, std::move(self), std::move(send))) {}
DaveSession::~DaveSession() = default;
std::unique_ptr<DaveSession> DaveSession::create(uint64_t channel, std::string self, Send send) noexcept try {
  auto result = std::make_unique<DaveSession>(channel, std::move(self), std::move(send));
  return result->impl->valid ? std::move(result) : nullptr;
} catch (...) { return nullptr; }

bool DaveSession::configure(uint16_t version, uint32_t ssrc) noexcept try {
  std::lock_guard<std::mutex> lock(impl->mutex);
  if (!impl->valid || version > maxVersion) return false;
  if (impl->configured) {
    impl->ssrc = ssrc;
    daveEncryptorAssignSsrcToCodec(impl->encryptor.get(), ssrc, DAVE_CODEC_OPUS);
    if (impl->videoSsrc)
      daveEncryptorAssignSsrcToCodec(impl->encryptor.get(), impl->videoSsrc, DAVE_CODEC_H264);
    return true;
  }
  impl->configured = true;
  impl->ssrc = ssrc;
  impl->version = impl->pendingVersion = version;
  daveEncryptorAssignSsrcToCodec(impl->encryptor.get(), ssrc, DAVE_CODEC_OPUS);
  if (impl->videoSsrc)
    daveEncryptorAssignSsrcToCodec(impl->encryptor.get(), impl->videoSsrc, DAVE_CODEC_H264);
  daveEncryptorSetPassthroughMode(impl->encryptor.get(), version == 0);
  impl->audioReady = version == 0;
  return version == 0 || impl->init(version);
} catch (...) { return false; }

bool DaveSession::configureVideo(uint32_t videoSsrc) noexcept try {
  std::lock_guard<std::mutex> lock(impl->mutex);
  if (!impl->valid || !videoSsrc) return false;
  impl->videoSsrc = videoSsrc;
  daveEncryptorAssignSsrcToCodec(impl->encryptor.get(), videoSsrc, DAVE_CODEC_H264);
  return true;
} catch (...) { return false; }

bool DaveSession::addUser(const std::string &id) noexcept try {
  std::lock_guard<std::mutex> lock(impl->mutex);
  if (!userId(id) || (!impl->recognized.count(id) && impl->recognized.size() >= maxMembers)) return false;
  impl->recognized.insert(id);
  return true;
} catch (...) { return false; }

void DaveSession::removeUser(const std::string &id) noexcept {
  std::lock_guard<std::mutex> lock(impl->mutex);
  if (id != impl->self) {
    impl->recognized.erase(id);
    impl->decryptors.erase(id);
  }
}

bool DaveSession::prepareEpoch(uint64_t epoch, uint16_t version) noexcept try {
  std::lock_guard<std::mutex> lock(impl->mutex);
  if (!impl->valid || !impl->configured || !epoch || !version || version > maxVersion) return false;
  impl->failure = false;
  if (epoch == 1) return impl->init(version);
  impl->pendingVersion = version;
  daveSessionSetProtocolVersion(impl->session.get(), version);
  return true;
} catch (...) { return false; }

bool DaveSession::prepareTransition(uint16_t id, uint16_t version) noexcept try {
  std::lock_guard<std::mutex> lock(impl->mutex);
  if (!impl->valid || !impl->configured || version > maxVersion) return false;
  if (version && !impl->external) return false;
  impl->pendingVersion = version;
  if (!version) for (auto &item : impl->decryptors)
    daveDecryptorTransitionToPassthroughMode(item.second.get(), true);
  return impl->stage(id, version && !impl->group);
} catch (...) { return false; }

bool DaveSession::executeTransition(uint16_t id) noexcept try {
  std::lock_guard<std::mutex> lock(impl->mutex);
  return impl->valid && impl->execute(id);
} catch (...) { return false; }

bool DaveSession::processBinary(const uint8_t *message, size_t length) noexcept try {
  std::lock_guard<std::mutex> lock(impl->mutex);
  if (!impl->valid || !message || length < 4 || length > 256 * 1024) return false;
  // Incoming: sequence (u16 BE), opcode (u8), then opcode-specific data.
  const uint8_t op = message[2];
  const uint8_t *data = message + 3;
  size_t size = length - 3;
  impl->failure = false;
  if (op == 25) {
    daveSessionSetExternalSender(impl->session.get(), data, size);
    impl->external = !impl->failure;
    return impl->external;
  }
  if (!impl->configured || !impl->pendingVersion) return false;
  if (op == 27) {
    if (!impl->external) return false;
    auto users = impl->users();
    uint8_t *bytes = nullptr;
    size_t outLength = 0;
    daveSessionProcessProposals(impl->session.get(), data, size, users.data(), users.size(), &bytes, &outLength);
    if (!bytes) return !impl->failure;
    return impl->binary(28, bytes, outLength) && !impl->failure;
  }
  if ((op != 29 && op != 30) || size <= 2) return false;
  const uint16_t id = (uint16_t(data[0]) << 8) | data[1];
  data += 2;
  size -= 2;
  bool success = false;
  if (op == 29) {
    Handle<DAVECommitResultHandle_s, daveCommitResultDestroy> result(
        daveSessionProcessCommit(impl->session.get(), data, size), daveCommitResultDestroy);
    if (result && daveCommitResultIsIgnored(result.get())) return true;
    if (result && !daveCommitResultIsFailed(result.get()))
      success = impl->updateRoster(result.get(), daveCommitResultGetRosterMemberIds, daveCommitResultGetRosterMemberSignature);
  } else {
    auto users = impl->users();
    Handle<DAVEWelcomeResultHandle_s, daveWelcomeResultDestroy> result(
        daveSessionProcessWelcome(impl->session.get(), data, size, users.data(), users.size()), daveWelcomeResultDestroy);
    if (result) success = impl->updateRoster(result.get(), daveWelcomeResultGetRosterMemberIds, daveWelcomeResultGetRosterMemberSignature);
  }
  return success ? impl->stage(id, true) : impl->recover(id);
} catch (...) { return false; }

bool DaveSession::ready() const noexcept {
  std::lock_guard<std::mutex> lock(impl->mutex);
  return impl->valid && impl->configured && impl->audioReady;
}

void DaveSession::stop() noexcept {
  std::lock_guard<std::mutex> lock(impl->mutex);
  impl->valid = impl->audioReady = false;
}

bool DaveSession::encrypt(const uint8_t *frame, size_t length, std::vector<uint8_t> &out) noexcept try {
  std::lock_guard<std::mutex> lock(impl->mutex);
  out.clear();
  if (!impl->audioReady || !frame || !length || length > 8192) return false;
  if (impl->version && !daveEncryptorHasKeyRatchet(impl->encryptor.get())) return false;
  out.resize(daveEncryptorGetMaxCiphertextByteSize(impl->encryptor.get(), DAVE_MEDIA_TYPE_AUDIO, length));
  size_t written = 0;
  if (daveEncryptorEncrypt(impl->encryptor.get(), DAVE_MEDIA_TYPE_AUDIO, impl->ssrc,
      frame, length, out.data(), out.size(), &written) != DAVE_ENCRYPTOR_RESULT_CODE_SUCCESS) {
    out.clear(); return false;
  }
  out.resize(written);
  return true;
} catch (...) { out.clear(); return false; }

bool DaveSession::encryptVideo(uint32_t videoSsrc, const uint8_t *frame, size_t length,
                               std::vector<uint8_t> &out) noexcept try {
  std::lock_guard<std::mutex> lock(impl->mutex);
  out.clear();
  if (!impl->audioReady || !frame || !length || length > 256 * 1024) return false;
  if (impl->version && !daveEncryptorHasKeyRatchet(impl->encryptor.get())) return false;
  const uint32_t ssrc = videoSsrc ? videoSsrc : impl->videoSsrc;
  if (!ssrc) return false;
  if (ssrc != impl->videoSsrc) {
    impl->videoSsrc = ssrc;
    daveEncryptorAssignSsrcToCodec(impl->encryptor.get(), ssrc, DAVE_CODEC_H264);
  }
  out.resize(daveEncryptorGetMaxCiphertextByteSize(impl->encryptor.get(), DAVE_MEDIA_TYPE_VIDEO, length));
  size_t written = 0;
  if (daveEncryptorEncrypt(impl->encryptor.get(), DAVE_MEDIA_TYPE_VIDEO, ssrc,
      frame, length, out.data(), out.size(), &written) != DAVE_ENCRYPTOR_RESULT_CODE_SUCCESS) {
    out.clear(); return false;
  }
  out.resize(written);
  return true;
} catch (...) { out.clear(); return false; }

bool DaveSession::decrypt(const std::string &user, const uint8_t *frame, size_t length,
                          std::vector<uint8_t> &out) noexcept try {
  std::lock_guard<std::mutex> lock(impl->mutex);
  out.clear();
  if (!impl->valid || !impl->configured || !frame || !length || length > 8192 || !impl->recognized.count(user)) return false;
  auto it = impl->decryptors.find(user);
  if (it == impl->decryptors.end()) {
    if (impl->version != 0) return false;
    it = impl->decryptors.emplace(user, Decryptor(daveDecryptorCreate(), daveDecryptorDestroy)).first;
    daveDecryptorTransitionToPassthroughMode(it->second.get(), true);
  }
  if (!it->second) return false;
  out.resize(length);
  size_t written = 0;
  if (daveDecryptorDecrypt(it->second.get(), DAVE_MEDIA_TYPE_AUDIO, frame, length,
      out.data(), out.size(), &written) != DAVE_DECRYPTOR_RESULT_CODE_SUCCESS) {
    out.clear(); return false;
  }
  out.resize(written);
  return true;
} catch (...) { out.clear(); return false; }
}  // namespace NXCord
