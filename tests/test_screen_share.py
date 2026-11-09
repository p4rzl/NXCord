#!/usr/bin/env python3
"""Host regression tests for H.264 Constrained Baseline encoder and RFC 6184 RTP video packetization."""
from pathlib import Path
import shutil
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
#include <nxcord/h264_encoder.hpp>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace NXCord {
struct DaveSession {
  bool encryptVideo(uint32_t ssrc, const uint8_t* frame, size_t length, std::vector<uint8_t>& out) {
    assert(ssrc != 0);
    out.assign(frame, frame + length);
    // Simulate DAVE supplemental data trailer appended to the slice
    const uint8_t trailer[4] = {0xAA, 0xBB, 0xCC, 0xFB};
    out.insert(out.end(), trailer, trailer + 4);
    return true;
  }
};
}

struct SentRtp {
  std::vector<uint8_t> payload;
  uint32_t timestamp90k;
  bool marker;
};

struct VoiceConnection {
  enum State : uint8_t {
    NOT_CONNECTED = 0, CONNECTED = 1 << 0, OPEN = 1 << 1,
    AUDIO_ENABLED = 1 << 2, ABLE = 7
  };
  State state = ABLE;
  bool videoEnabled = true;
  uint32_t sSRC = 1000;
  uint32_t videoSSRC = 1001;
  std::unique_ptr<NXCord::DaveSession> dave = std::make_unique<NXCord::DaveSession>();
  std::vector<SentRtp> rtpPackets;

  void sendVideoRtpPacket(const uint8_t* payload, std::size_t payloadLen, uint32_t timestamp90k, bool marker) {
    assert(payload != nullptr && payloadLen > 0 && payloadLen <= 1150);
    rtpPackets.push_back({std::vector<uint8_t>(payload, payload + payloadLen), timestamp90k, marker});
  }
  void packetizeAndSendNal(const uint8_t* nal, std::size_t nalLen, uint32_t timestamp90k, bool isLastNal, std::size_t& packetsInBurst);
  void sendVideoFrame(const uint8_t* annexBFrame, std::size_t length, std::size_t spsLen, std::size_t ppsLen, uint32_t timestamp90k);
};
'''

checks = r'''
// Reassemble RFC 6184 RTP packets back into Annex-B frame (stripping simulated 4-byte DAVE trailer)
std::vector<uint8_t> reassembleRtpFrame(const std::vector<SentRtp>& pkts, uint32_t expectedTs) {
  assert(!pkts.empty());
  std::vector<uint8_t> annexB;
  std::vector<uint8_t> currentNal;
  for (size_t i = 0; i < pkts.size(); ++i) {
    assert(pkts[i].timestamp90k == expectedTs);
    if (i + 1 < pkts.size()) {
      assert(!pkts[i].marker);
    } else {
      assert(pkts[i].marker);
    }
    const auto& p = pkts[i].payload;
    const uint8_t nalType = p[0] & 0x1F;
    if (nalType >= 1 && nalType <= 23) {
      // Single NAL Unit packet
      annexB.insert(annexB.end(), {0x00, 0x00, 0x00, 0x01});
      annexB.insert(annexB.end(), p.begin(), p.end());
    } else if (nalType == 28) {
      // FU-A
      assert(p.size() > 2);
      const uint8_t fuIndicator = p[0];
      const uint8_t fuHeader = p[1];
      const bool startBit = (fuHeader & 0x80) != 0;
      const bool endBit = (fuHeader & 0x40) != 0;
      const uint8_t origType = fuHeader & 0x1F;
      if (startBit) {
        currentNal.clear();
        currentNal.push_back((fuIndicator & 0xE0) | origType);
      }
      currentNal.insert(currentNal.end(), p.begin() + 2, p.end());
      if (endBit) {
        annexB.insert(annexB.end(), {0x00, 0x00, 0x00, 0x01});
        annexB.insert(annexB.end(), currentNal.begin(), currentNal.end());
        currentNal.clear();
      }
    } else {
      assert(false && "Unexpected RTP NAL unit type");
    }
  }
  assert(annexB.size() > 4);
  // Strip simulated 4-byte DAVE trailer at end of slice
  assert(annexB[annexB.size() - 1] == 0xFB);
  annexB.resize(annexB.size() - 4);
  return annexB;
}

int main(int argc, char** argv) {
  NXCord::H264Encoder encoder;
  VoiceConnection conn;

  std::vector<uint8_t> y(640 * 360), u(320 * 180), v(320 * 180);
  FILE* outFile = (argc > 1) ? fopen(argv[1], "wb") : nullptr;
  const bool noisy = argc > 2;

  for (int f = 0; f < 14; ++f) {
    for (int py = 0; py < 360; ++py) {
      for (int px = 0; px < 640; ++px) {
        y[py * 640 + px] = static_cast<uint8_t>((px + py) & 0xFF);
      }
    }
    for (int py = 0; py < 180; ++py) {
      for (int px = 0; px < 320; ++px) {
        u[py * 320 + px] = static_cast<uint8_t>((px * 2) & 0xFF);
        v[py * 320 + px] = static_cast<uint8_t>((py * 2) & 0xFF);
      }
    }
    int bx0 = (f * 20) % 480;
    int by0 = (f * 10) % 240;
    for (int py = by0; py < by0 + 64; ++py) {
      for (int px = bx0; px < bx0 + 96; ++px) {
        y[py * 640 + px] = 220;
        u[(py / 2) * 320 + (px / 2)] = 100;
        v[(py / 2) * 320 + (px / 2)] = 190;
      }
    }

    if (noisy) {
      uint32_t seed = 12345u + f;
      for (auto& b : y) { seed = seed * 1664525u + 1013904223u; b = static_cast<uint8_t>((b + (seed >> 24)) >> 1); }
      for (auto& b : u) { seed = seed * 1664525u + 1013904223u; b = static_cast<uint8_t>(16 + (seed >> 24) % 220); }
      for (auto& b : v) { seed = seed * 1664525u + 1013904223u; b = static_cast<uint8_t>(16 + (seed >> 24) % 220); }
    }
    NXCord::EncodedH264Frame encoded;
    encoder.encodeFrame(y.data(), 640, u.data(), 320, v.data(), 320, false, encoded);
    if (f == 0 || f == 10) {
      assert(encoded.isKeyFrame);
      assert(encoded.spsLen > 0 && encoded.ppsLen > 0);
    } else {
      assert(!encoded.isKeyFrame);
      assert(encoded.spsLen == 0 && encoded.ppsLen == 0);
      if (!noisy) assert(encoded.annexB.size() < 6000);
    }

    conn.rtpPackets.clear();
    const uint32_t ts = static_cast<uint32_t>(f * 9000);
    conn.sendVideoFrame(encoded.annexB.data(), encoded.annexB.size(),
                        encoded.spsLen, encoded.ppsLen, ts);
    std::vector<uint8_t> reassembled = reassembleRtpFrame(conn.rtpPackets, ts);
    assert(reassembled == encoded.annexB);

    if (outFile) {
      fwrite(reassembled.data(), 1, reassembled.size(), outFile);
    }
  }

  if (outFile) fclose(outFile);
  puts("H.264 encoder and RFC 6184 RTP packetization tests passed");
  return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="nxcord-screen-share-") as temp:
    temp = Path(temp)
    cpp = temp / "screen_share_test.cpp"
    cpp.write_text(
        harness
        + method("void VoiceConnection::packetizeAndSendNal")
        + method("void VoiceConnection::sendVideoFrame")
        + checks
    )
    binary = temp / "screen_share_test"
    h264_out = temp / "stream.264"
    subprocess.run([
        "g++", "-std=c++17", "-O2", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer",
        "-I" + str(ROOT / "include"),
        str(cpp),
        str(ROOT / "nxcord/src/h264_encoder.cpp"),
        "-o", str(binary)
    ], check=True)
    subprocess.run([str(binary), str(h264_out)], check=True)
    noisy_out = temp / "noisy.264"
    subprocess.run([str(binary), str(noisy_out), "noisy"], check=True)

    if shutil.which("ffmpeg"):
        # ffmpeg exits 0 even on bitstream errors; any stderr output is a failure.
        proc = subprocess.run([
            "ffmpeg", "-v", "error", "-i", str(h264_out), "-f", "null", "-"
        ], capture_output=True, text=True)
        assert proc.returncode == 0 and not proc.stderr.strip(), proc.stderr[:2000]
        proc = subprocess.run([
            "ffmpeg", "-v", "error", "-i", str(noisy_out), "-f", "null", "-"
        ], capture_output=True, text=True)
        assert proc.returncode == 0 and not proc.stderr.strip(), proc.stderr[:2000]
