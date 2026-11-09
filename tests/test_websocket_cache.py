#!/usr/bin/env python3
"""Exercise production Gateway chunk callbacks with an SD-file/TLS test shim."""
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "nxcord/src/discord_websocket.cpp").read_text()
callbacks = source[source.index("void on_frame_start_callback"):
                   source.index("DiscordWebsocket::DiscordWebsocket")]
queue = source[source.index("int DiscordWebsocket::queue_message"):
               source.index("void DiscordWebsocket::disconnect")]

harness = r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>
#define WSLAY_VERSION "test"
#include <wslay/wslay.h>
#include <nxcord/websocket_zlib_stream.hpp>
namespace Logger { void write(const char *, ...) {} }
bool stopped_reading = false;
extern "C" void wslay_event_shutdown_read(wslay_event_context_ptr) { stopped_reading = true; }
uint8_t queued_opcode = 0;
std::string queued_data;
extern "C" int wslay_event_queue_msg(wslay_event_context_ptr, const wslay_event_msg *message) {
  queued_opcode = message->opcode;
  queued_data.assign(reinterpret_cast<const char *>(message->msg), message->msg_length);
  return 0;
}
struct Receiver {
  std::vector<uint8_t> binary;
  void processBinaryMessage(const uint8_t *bytes, size_t length) { binary.assign(bytes, bytes + length); }
  std::vector<size_t> sizes;
  std::vector<uLong> checksums;
  int closes = 0;
  void processCloseCode(int) { ++closes; }
  void processMessage(const std::string &text) {
    sizes.push_back(text.size());
    checksums.push_back(crc32(0, reinterpret_cast<const Bytef *>(text.data()), text.size()));
  }
  void processStream(SleepyDiscord::JsonInputStream &input) {
    uLong crc = 0;
    size_t count = 0, used = 0;
    unsigned char buf[4096];
    for (char c; (c = input.Take()) != '\0';) {
      buf[used++] = c;
      ++count;
      if (used == sizeof(buf)) { crc = crc32(crc, buf, used); used = 0; }
    }
    crc = crc32(crc, buf, used);
    sizes.push_back(count);
    checksums.push_back(crc);
  }
};
struct DiscordWebsocket {
  wslay_event_context_ptr _wslay_event_context = nullptr;
  int queue_message(const std::string &, bool binary = false);
  static constexpr char zlib_suffix[] = {0, 0, -1, -1};
  bool _zlib_compress = true, _frame_is_data = false;
  size_t _compressed_bytes = 0;
  uint8_t _compressed_tail[4]{};
  int _pending_close = 0;
  std::ofstream _zlib_buf;
  ZlibWrapper _zlib_wrapper;
  Receiver *_message_processor;
};
'''

checks = r'''
int main() {
  Receiver receiver;
  DiscordWebsocket websocket;
  websocket._message_processor = &receiver;
  z_stream deflater{};
  assert(deflateInit(&deflater, Z_DEFAULT_COMPRESSION) == Z_OK);
  auto compress = [&](const std::string &text) {
    std::string result;
    deflater.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(text.data()));
    deflater.avail_in = text.size();
    do {
      unsigned char buf[4096];
      deflater.next_out = buf;
      deflater.avail_out = sizeof(buf);
      assert(deflate(&deflater, Z_SYNC_FLUSH) == Z_OK);
      result.append(reinterpret_cast<char *>(buf), sizeof(buf) - deflater.avail_out);
    } while (deflater.avail_in || !deflater.avail_out);
    return result;
  };
  auto feed = [&](const uint8_t *bytes, size_t length, uint8_t opcode = WSLAY_BINARY_FRAME) {
    wslay_event_on_frame_recv_start_arg start{1, 0, opcode, length};
    on_frame_start_callback(nullptr, &start, &websocket);
    for (size_t offset = 0; offset < length;) {
      const size_t count = std::min<size_t>(1024, length - offset);
      wslay_event_on_frame_recv_chunk_arg chunk{bytes + offset, count};
      on_frame_chunk_callback(nullptr, &chunk, &websocket);
      offset += count;
    }
    wslay_event_on_msg_recv_arg message{0, opcode, nullptr, 0, 0};
    on_msg_recv_callback(nullptr, &message, &websocket);
  };
  // A multi-megabyte (poorly compressible) initial payload arrives in 1 KiB chunks.
  std::string packet = "{\"settings\":\"";
  uint32_t random = 1;
  for (size_t i = 0; i < 4 * 1024 * 1024; ++i) {
    random = random * 1664525 + 1013904223;
    packet += static_cast<char>('A' + ((random >> 24) % 26));
  }
  packet += "\"}";
  const auto compressed = compress(packet);
  assert(compressed.size() > 1024 * 1024);
  auto bytes = reinterpret_cast<const uint8_t *>(compressed.data());
  feed(bytes, compressed.size() - 3);
  assert(receiver.sizes.empty());
  const uint8_t ping[] = {1, 2, 3};
  feed(ping, sizeof(ping), WSLAY_PING); // control frames must not pollute zlib.
  feed(bytes + compressed.size() - 3, 1);
  assert(receiver.sizes.empty());
  feed(bytes + compressed.size() - 2, 2); // suffix split over messages/chunks.
  assert(receiver.sizes.size() == 1 && receiver.sizes[0] == packet.size());
  assert(receiver.checksums[0] == crc32(0, reinterpret_cast<const Bytef *>(packet.data()), packet.size()));
  auto second = compress("{\"op\":11,\"d\":null}");
  feed(reinterpret_cast<const uint8_t *>(second.data()), second.size());
  assert(receiver.sizes.size() == 2 && receiver.sizes[1] == 18);
  wslay_event_on_msg_recv_arg close{0, WSLAY_CONNECTION_CLOSE, nullptr, 0, 4004};
  on_msg_recv_callback(nullptr, &close, &websocket);
  assert(websocket._pending_close == 4004 && receiver.closes == 0);
  // Voice MLS packets are binary even when the sequence starts with '{'.
  // Embedded NUL bytes must not be truncated or sent through the JSON path.
  websocket._zlib_compress = false;
  const uint8_t mls[] = {'{', 0, 25, 0xff, 0, 0x12};
  wslay_event_on_msg_recv_arg binary{0, WSLAY_BINARY_FRAME, mls, sizeof(mls), 0};
  on_msg_recv_callback(nullptr, &binary, &websocket);
  assert(receiver.binary == std::vector<uint8_t>(mls, mls + sizeof(mls)));
  assert(receiver.sizes.size() == 2);
  websocket._wslay_event_context = reinterpret_cast<wslay_event_context_ptr>(1);
  const std::string outgoing(reinterpret_cast<const char *>(mls), sizeof(mls));
  assert(websocket.queue_message(outgoing, true) == 0);
  assert(queued_opcode == WSLAY_BINARY_FRAME && queued_data == outgoing);
  assert(websocket.queue_message("{}") == 0 && queued_opcode == WSLAY_TEXT_FRAME);
  websocket._zlib_compress = true;
  // File budget failure defers reconnect instead of freeing wslay in a callback.
  websocket._pending_close = 0;
  websocket._zlib_buf.open(LOG_PATH "/.websocket_cache", std::ios::binary | std::ios::trunc);
  websocket._compressed_bytes = 8 * 1024 * 1024;
  feed(ping, 1);
  assert(websocket._pending_close == 1006 && stopped_reading);
  deflateEnd(&deflater);
  puts("WebSocket chunk-cache regression tests passed");
}
'''

with tempfile.TemporaryDirectory(prefix="nxcord-websocket-") as temp:
    temp = Path(temp)
    cpp = temp / "websocket.cpp"
    cpp.write_text("#define LOG_PATH " + json.dumps(str(temp)) + "\n" + harness + callbacks + queue + checks)
    binary = temp / "websocket"
    subprocess.run([
        "g++", "-std=c++17", "-O1", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-Wno-deprecated-declarations",
        "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "externals/wslay/wslay/lib/includes"),
        "-I" + str(ROOT / "externals/sleepy-discord/include"),
        str(cpp), str(ROOT / "nxcord/src/zlib_wrapper.cpp"), "-lz", "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
