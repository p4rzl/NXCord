#include <nxcord/dave_session.hpp>
#include "external_sender.h"
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <thread>

using NXCord::DaveSession;
using discord::dave::test::ExternalSender;
using Bytes = std::vector<uint8_t>;
constexpr uint64_t channel = 1234567890;
const std::string alice = "1234123412341234", bob = "5678567856785678", carol = "9012901290129012";

struct Peer {
  std::vector<std::pair<bool, std::string>> sent;
  std::unique_ptr<DaveSession> session;
  explicit Peer(const std::string &id) {
    session = DaveSession::create(channel, id, [this](bool binary, const std::string &msg) {
      sent.emplace_back(binary, msg);
    });
    assert(session);
  }
  Bytes take(uint8_t op) {
    for (auto it = sent.begin(); it != sent.end(); ++it) {
      if (it->first && static_cast<uint8_t>(it->second[0]) == op) {
        Bytes data(it->second.begin() + 1, it->second.end());
        sent.erase(it);
        return data;
      }
    }
    assert(false && "Missing binary opcode"); return {};
  }
  bool receive(uint8_t op, Bytes data, int transition = -1) {
    Bytes message{0x12, 0x34, op};
    if (transition >= 0) { message.push_back(transition >> 8); message.push_back(transition & 255); }
    message.insert(message.end(), data.begin(), data.end());
    return session->processBinary(message.data(), message.size());
  }
  void announce(const std::string &id) { assert(session->addUser(id)); }
};

void roundtrip(Peer &sender, Peer &receiver, const std::string &id) {
  const Bytes opus{0xf8, 0x8a, 0x07, 0x42, 0x12, 0x55, 0xa3};
  Bytes encrypted, plain;
  assert(sender.session->encrypt(opus.data(), opus.size(), encrypted));
  assert(encrypted != opus && encrypted.size() > opus.size());
  assert(encrypted[encrypted.size() - 1] == 0xfa && encrypted[encrypted.size() - 2] == 0xfa);
  auto tampered = encrypted;
  tampered[0] ^= 1;
  assert(!receiver.session->decrypt(id, tampered.data(), tampered.size(), plain));
  assert(plain.empty());
  assert(receiver.session->decrypt(id, encrypted.data(), encrypted.size(), plain));
  assert(plain == opus);
  assert(!receiver.session->decrypt(id, encrypted.data(), encrypted.size(), plain)); // replay
  assert(!receiver.session->decrypt("999", encrypted.data(), encrypted.size(), plain));
}

int main() {
  ExternalSender external(1, channel);
  const auto package = external.GetMarshalledExternalSender();
  Peer a(alice), b(bob);
  // Both permitted orders for external sender and Session Description.
  assert(a.receive(25, package));
  assert(a.session->configure(1, 111));
  assert(b.session->configure(1, 222));
  assert(b.receive(25, package));
  a.announce(bob); b.announce(alice);
  Bytes encrypted;
  const Bytes opus{0xf8, 0x77, 0x66};
  assert(!a.session->encrypt(opus.data(), opus.size(), encrypted));
  assert(!a.session->ready());
  auto proposal = external.ProposeAdd(0, b.take(26));
  assert(a.receive(27, proposal));
  auto [commit, welcome] = external.SplitCommitWelcome(a.take(28));
  assert(a.receive(29, commit, 513));
  assert(b.receive(30, welcome, 513));
  assert(!a.session->ready() && !b.session->ready());
  assert(a.session->executeTransition(513));
  assert(b.session->executeTransition(513));
  assert(a.session->ready() && b.session->ready());
  roundtrip(a, b, alice); roundtrip(b, a, bob);
  assert(!a.session->executeTransition(513)); // never reset nonce by executing twice

  // New epoch: receivers accept the new keys before senders transition, while
  // still accepting old in-flight packets. Test roster *diffs*, not full lists.
  Peer c(carol);
  assert(c.session->configure(1, 333)); assert(c.receive(25, package));
  for (auto *p : {&a, &b}) p->announce(carol);
  c.announce(alice); c.announce(bob);
  proposal = external.ProposeAdd(1, c.take(26));
  assert(a.receive(27, proposal)); assert(b.receive(27, proposal));
  auto next = external.SplitCommitWelcome(a.take(28));
  assert(a.receive(29, next.first, 514)); assert(b.receive(29, next.first, 514));
  assert(c.receive(30, next.second, 514));
  roundtrip(a, b, alice); // old sender epoch / new receive epoch
  assert(a.session->executeTransition(514));
  assert(b.session->executeTransition(514));
  assert(c.session->executeTransition(514));
  roundtrip(a, b, alice); roundtrip(c, a, carol); roundtrip(b, c, bob);

  // Removed users are not accepted, even while libdave retains old epochs.
  b.session->removeUser(alice);
  assert(a.session->encrypt(opus.data(), opus.size(), encrypted));
  Bytes plain;
  assert(!b.session->decrypt(alice, encrypted.data(), encrypted.size(), plain));

  // Solo reset must NOT continue sending under the key of the previous group.
  assert(a.session->prepareEpoch(1, 1));
  assert(a.session->prepareTransition(0, 1));
  assert(a.session->ready());
  assert(!a.session->encrypt(opus.data(), opus.size(), encrypted));

  // Re-form the group after the solo reset. libdave retains its previous
  // roster, so the resulting diff includes removal entries as well as adds.
  Peer b2(bob);
  assert(b2.session->configure(1, 444)); assert(b2.receive(25, package));
  b2.announce(alice);
  proposal = external.ProposeAdd(0, b2.take(26));
  assert(a.receive(27, proposal));
  auto rebuilt = external.SplitCommitWelcome(a.take(28));
  assert(a.receive(29, rebuilt.first, 0));
  assert(b2.receive(30, rebuilt.second, 0));
  assert(a.session->ready() && b2.session->ready());
  roundtrip(a, b2, alice); roundtrip(b2, a, bob);
  // Plaintext is refused until an explicit downgrade is actually executed.
  assert(a.session->prepareTransition(999, 0));
  assert(a.session->encrypt(opus.data(), opus.size(), encrypted));
  assert(encrypted != opus);
  assert(a.session->executeTransition(999));
  assert(a.session->encrypt(opus.data(), opus.size(), encrypted) && encrypted == opus);
  assert(a.session->prepareEpoch(1, 1));
  assert(a.session->prepareTransition(0, 1));
  assert(!a.session->encrypt(opus.data(), opus.size(), encrypted));

  // Explicit server-negotiated version 0 is the only plaintext fallback.
  // libdave retains up to 10 seconds of receive passthrough for in-flight
  // version-0 frames; it must not remain enabled forever after the upgrade.
  std::this_thread::sleep_for(std::chrono::milliseconds(10100));
  assert(!a.session->decrypt(bob, opus.data(), opus.size(), plain));
  Peer legacy(alice);
  assert(legacy.session->configure(0, 123)); legacy.announce(bob);
  assert(legacy.session->encrypt(opus.data(), opus.size(), encrypted));
  assert(encrypted == opus);
  assert(!b2.session->decrypt(alice, opus.data(), opus.size(), plain));

  Peer invalid(alice);
  assert(!invalid.session->configure(2, 42));
  assert(!invalid.session->addUser("18446744073709551616"));
  assert(!invalid.session->addUser("not-a-snowflake"));
  assert(!invalid.session->processBinary(nullptr, 0));
  assert(!invalid.receive(25, Bytes{0xff}));
  Peer recovering(alice);
  assert(recovering.receive(25, package)); assert(recovering.session->configure(1, 5));
  assert(recovering.receive(30, Bytes{0xff}, 90));
  assert(!recovering.session->ready());
  recovering.take(26);
  assert(std::any_of(recovering.sent.begin(), recovering.sent.end(), [](const auto &msg) {
    return !msg.first && msg.second.find("\"op\":31") != std::string::npos;
  }));
  for (size_t i = 0; i < DaveSession::maxMembers - 1; ++i)
    assert(invalid.session->addUser(std::to_string(100000 + i)));
  assert(!invalid.session->addUser("9999999"));
  legacy.session->stop();
  assert(!legacy.session->ready());
  assert(!legacy.session->encrypt(opus.data(), opus.size(), encrypted));
  assert(!legacy.session->decrypt(bob, opus.data(), opus.size(), plain));
  puts("DAVE real MLS / Opus encryption / epoch / replay / recovery tests passed");
}
