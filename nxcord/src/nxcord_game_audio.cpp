#include <malloc.h>

#include <algorithm>
#include <common/logger.hpp>
#include <cstring>
#include <nxcord/nxcord_game_audio.hpp>

void game_audio_thread(NXCordGameAudio *self) {
  if (!self->_thread.isActive()) return;

  // All blocking audrec IPC happens here, never on the IPC/main threads.
  if (!self->_setup_done) {
    self->_setup_done = true;
    self->_setup_ok = self->setup();
    if (!self->_setup_ok) {
      Logger::write("NXCordGameAudio: setup failed, game audio disabled\n");
    }
    return;
  }
  if (!self->_setup_ok) {
    svcSleepThread(100000000ULL);
    return;
  }

  // Wake at most every 40 ms so stop() is prompt.
  Result rc = eventWait(&self->_event, 40000000ULL);
  if (!self->_thread.isActive()) return;
  if (R_FAILED(rc)) return;

  u64 released_ptrs[NXCordGameAudio::kBufferCount] = {};
  u64 count = NXCordGameAudio::kBufferCount;
  u64 released = 0;
  rc = audrecRecorderGetReleasedFinalOutputRecorderBuffers(
      &self->_recorder, released_ptrs, &count, &released);
  if (R_FAILED(rc)) {
    svcSleepThread(10000000ULL);
    return;
  }
  for (u64 i = 0; i < NXCordGameAudio::kBufferCount; ++i) {
    FinalOutputRecorderBuffer *buf = nullptr;
    for (auto &candidate : self->_bufs) {
      if (released_ptrs[i] == reinterpret_cast<u64>(&candidate)) {
        buf = &candidate;
        break;
      }
    }
    if (!buf) continue;  // Ignore anything that is not one of our buffers.
    const size_t bytes = std::min<size_t>(buf->data_size,
                                          NXCordGameAudio::kBufferBytes);
    const auto *samples =
        reinterpret_cast<const int16_t *>(buf->sample_buffer_ptr);
    if (bytes >= 4 && samples) {
      std::scoped_lock lock(self->_mutex);
      self->_queue.insert(self->_queue.end(), samples,
                          samples + bytes / sizeof(int16_t));
      if (self->_queue.size() > NXCordGameAudio::kMaxQueuedSamples) {
        self->_queue.erase(
            self->_queue.begin(),
            self->_queue.end() - NXCordGameAudio::kMaxQueuedSamples);
      }
    }
    buf->data_size = NXCordGameAudio::kBufferBytes;
    buf->data_offset = 0;
    audrecRecorderAppendFinalOutputRecorderBuffer(
        &self->_recorder, reinterpret_cast<u64>(buf), buf);
  }
}

NXCordGameAudio::NXCordGameAudio()
    // Low priority (0x38) and small stack: only copies PCM into a queue.
    : _thread(this, game_audio_thread, 0x8000, true, 0x38, -2) {}

NXCordGameAudio::~NXCordGameAudio() { stop(); }

bool NXCordGameAudio::start() {
  if (_thread.isActive()) return true;
  _setup_done = false;
  _setup_ok = false;
  _queue.clear();
  _thread.start();
  return _thread.isActive();
}

bool NXCordGameAudio::setup() {
  Logger::write("NXCordGameAudio: audrecInitialize...\n");
  Result rc = audrecInitialize();
  if (R_FAILED(rc)) {
    Logger::write("NXCordGameAudio: audrecInitialize failed: 0x%x\n", rc);
    return false;
  }
  _audrec_initialized = true;

  Logger::write("NXCordGameAudio: opening recorder...\n");
  FinalOutputRecorderParameter param{48000, 2};
  FinalOutputRecorderParameterInternal out{};
  rc = audrecOpenFinalOutputRecorder(&_recorder, &param, 0, &out);
  if (R_FAILED(rc)) {
    Logger::write("NXCordGameAudio: open recorder failed: 0x%x\n", rc);
    return false;
  }
  _recorder_open = true;
  Logger::write("NXCordGameAudio: recorder %u Hz, %u ch, fmt %u\n",
                out.sample_rate, out.channel_count, out.sample_format);

  rc = audrecRecorderRegisterBufferEvent(&_recorder, &_event);
  if (R_FAILED(rc)) {
    Logger::write("NXCordGameAudio: register event failed: 0x%x\n", rc);
    return false;
  }
  _event_valid = true;

  for (size_t i = 0; i < kBufferCount; ++i) {
    _mem[i] = memalign(0x1000, kAllocBytes);
    if (!_mem[i]) return false;
    std::memset(_mem[i], 0, kAllocBytes);
    _bufs[i] = {};
    _bufs[i].sample_buffer_ptr = reinterpret_cast<u64>(_mem[i]);
    _bufs[i].sample_buffer_capacity = kAllocBytes;
    _bufs[i].data_size = kBufferBytes;
    _bufs[i].data_offset = 0;
    rc = audrecRecorderAppendFinalOutputRecorderBuffer(
        &_recorder, reinterpret_cast<u64>(&_bufs[i]), &_bufs[i]);
    if (R_FAILED(rc)) {
      Logger::write("NXCordGameAudio: append buffer failed: 0x%x\n", rc);
      return false;
    }
  }

  rc = audrecRecorderStart(&_recorder);
  if (R_FAILED(rc)) {
    Logger::write("NXCordGameAudio: start failed: 0x%x\n", rc);
    return false;
  }
  Logger::write("NXCordGameAudio: started\n");
  return true;
}

void NXCordGameAudio::stop() {
  _thread.stop();
  if (_recorder_open) {
    audrecRecorderStop(&_recorder);
    audrecRecorderClose(&_recorder);
    _recorder_open = false;
  }
  if (_event_valid) {
    // The event handle is owned by the recorder session in libnx's wrapper.
    _event_valid = false;
  }
  for (size_t i = 0; i < kBufferCount; ++i) {
    if (_mem[i]) {
      free(_mem[i]);
      _mem[i] = nullptr;
    }
  }
  if (_audrec_initialized) {
    audrecExit();
    _audrec_initialized = false;
  }
  std::scoped_lock lock(_mutex);
  _queue.clear();
  _queue.shrink_to_fit();
}

size_t NXCordGameAudio::read(int16_t *out, size_t count) {
  std::scoped_lock lock(_mutex);
  const size_t n = std::min(count, _queue.size());
  if (n) {
    std::memcpy(out, _queue.data(), n * sizeof(int16_t));
    _queue.erase(_queue.begin(), _queue.begin() + n);
  }
  if (n < count) std::memset(out + n, 0, (count - n) * sizeof(int16_t));
  return n;
}
