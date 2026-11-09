#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace NXCord {
// All MLS and frame operations are serialized internally, including the UDP
// receive thread. No key material is exposed to IPC, persisted, or logged.
class DaveSession {
 public:
  using Send = std::function<void(bool binary, const std::string &message)>;
  static constexpr uint16_t maxVersion = 1;
  static constexpr size_t maxMembers = 32;
  DaveSession(uint64_t channel, std::string self, Send send);
  static std::unique_ptr<DaveSession> create(uint64_t channel, std::string self, Send send) noexcept;
  ~DaveSession();
  DaveSession(const DaveSession &) = delete;
  DaveSession &operator=(const DaveSession &) = delete;
  bool configure(uint16_t version, uint32_t ssrc) noexcept;
  bool configureVideo(uint32_t videoSsrc) noexcept;
  bool addUser(const std::string &id) noexcept;
  void removeUser(const std::string &id) noexcept;
  bool prepareEpoch(uint64_t epoch, uint16_t version) noexcept;
  bool prepareTransition(uint16_t id, uint16_t version) noexcept;
  bool executeTransition(uint16_t id) noexcept;
  bool processBinary(const uint8_t *message, size_t length) noexcept;
  bool ready() const noexcept;
  void stop() noexcept;
  bool encrypt(const uint8_t *frame, size_t length, std::vector<uint8_t> &out) noexcept;
  bool encryptVideo(uint32_t videoSsrc, const uint8_t *frame, size_t length,
                    std::vector<uint8_t> &out) noexcept;
  bool decrypt(const std::string &user, const uint8_t *frame, size_t length,
               std::vector<uint8_t> &out) noexcept;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
}  // namespace NXCord
