#pragma once

#include <switch.h>

#include <atomic>
#include <common/loop_thread.hpp>
#include <cstdint>
#include <mutex>
#include <vector>

// Captures the console's final audio mix (game audio) through audrec:u while
// screen sharing is active, so it can be mixed into the microphone stream.
// It is created/destroyed together with the screen share to cost nothing
// when idle.
class NXCordGameAudio {
 public:
  static constexpr size_t kBufferCount = 3;
  // 20 ms of 48 kHz stereo s16 per recorder buffer (matches one Opus frame).
  static constexpr size_t kBufferBytes = 960 * 2 * sizeof(int16_t);
  // Page aligned allocation size per buffer.
  static constexpr size_t kAllocBytes = 0x1000 * 2;
  // Max buffered audio: 100 ms (5 frames) to keep latency low.
  static constexpr size_t kMaxQueuedSamples = 960 * 2 * 5;

  NXCordGameAudio();
  ~NXCordGameAudio();

  bool start();
  void stop();
  bool isActive() const { return _thread.isActive(); }

  // Pops up to `count` interleaved stereo samples (zero-filled if fewer are
  // available). Returns the number of real samples copied.
  size_t read(int16_t *out, size_t count);

 private:
  friend void game_audio_thread(NXCordGameAudio *self);

  bool _audrec_initialized = false;
  bool _recorder_open = false;
  AudrecRecorder _recorder{};
  Event _event{};
  bool _event_valid = false;
  FinalOutputRecorderBuffer _bufs[kBufferCount]{};
  void *_mem[kBufferCount]{};

  std::mutex _mutex;
  std::vector<int16_t> _queue;
  LoopThread<NXCordGameAudio *> _thread;
};
