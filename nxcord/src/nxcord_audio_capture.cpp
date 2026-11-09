#include <malloc.h>

#include <algorithm>
#include <common/logger.hpp>
#include <common/utils.hpp>
#include <nxcord/nxcord_audio_capture.hpp>

namespace {
void drain_released_audin_buffers() {
  for (int i = 0; i < 32; ++i) {
    uint32_t released_count = 0;
    AudioInBuffer *released_buf = nullptr;
    Result rc = audinGetReleasedAudioInBuffer(&released_buf, &released_count);
    if (R_FAILED(rc) || (released_count == 0 && released_buf == nullptr)) {
      break;
    }
  }
}
}  // namespace

void capture_thread(NXCordAudioCapture *audio_capture) {
  if (!audio_capture->_capture_thread.isActive()) {
    return;
  }

  uint32_t released_in_count = 0;
  AudioInBuffer *audin_released_buf = nullptr;

  time_t current_time = Utils::current_time_millis();

  Result rc = audinWaitCaptureFinish(&audin_released_buf, &released_in_count,
                                     100000000ULL);
  if (!audio_capture->_capture_thread.isActive()) {
    return;
  }

  if (R_SUCCEEDED(rc) && released_in_count > 0 &&
      audin_released_buf == &audio_capture->_audin_buf &&
      audin_released_buf->buffer != nullptr) {
    const size_t max_bytes = std::min<size_t>(audin_released_buf->buffer_size,
                                              Utils::opus_framesize_bytes);
    const size_t data_bytes =
        std::min<size_t>(audin_released_buf->data_size, max_bytes);
    const size_t sample_count =
        data_bytes / sizeof(SleepyDiscord::AudioSample);

    if (sample_count > 0) {
      std::vector<SleepyDiscord::AudioSample> capture(sample_count);
      std::memcpy(capture.data(), audin_released_buf->buffer,
                  sample_count * sizeof(SleepyDiscord::AudioSample));

      AudioPacket packet(0, capture);
      std::scoped_lock lock(audio_capture->_queue_mutex);
      while (audio_capture->_queue.size() > 10) {
        audio_capture->_queue.pop();
      }
      audio_capture->_queue.push(std::move(packet));
    }

    if (audio_capture->_capture_thread.isActive()) {
      std::memset(audin_released_buf->buffer, 0,
                  audin_released_buf->buffer_size);
      audin_released_buf->next = nullptr;
      audin_released_buf->data_size = Utils::opus_framesize_bytes;
      audin_released_buf->data_offset = 0;
      audinAppendAudioInBuffer(audin_released_buf);
    }
  }

  if (!audio_capture->_capture_thread.isActive()) {
    return;
  }

  time_t time_passed = Utils::current_time_millis() - current_time;
  if (time_passed < 20) {
    svcSleepThread((20L - time_passed) * 1e+6L);
  }
}

NXCordAudioCapture::NXCordAudioCapture()
    : _capture_thread(this, capture_thread, 0x8000) {
  audinStopAudioIn();
  drain_released_audin_buffers();

  Result rc = audinStartAudioIn();
  Logger::write("audinStartAudioIn() returned 0x%x\n", rc);

  u32 buffer_size = (Utils::opus_framesize_bytes + 0xfff) & ~0xfff;
  void *in_buf_data = memalign(0x1000, buffer_size);
  if (in_buf_data) {
    std::memset(in_buf_data, 0, buffer_size);
  }

  _audin_buf.next = nullptr;
  _audin_buf.buffer = in_buf_data;
  _audin_buf.buffer_size = buffer_size;
  _audin_buf.data_size = Utils::opus_framesize_bytes;
  _audin_buf.data_offset = 0;

  if (in_buf_data) {
    rc = audinAppendAudioInBuffer(&_audin_buf);
    Logger::write("audinAppendAudioInBuffer() returned 0x%x\n", rc);
    _capture_thread.start();
  }
}

NXCordAudioCapture::~NXCordAudioCapture() {
  Logger::write("Releasing audio sender\n");
  _capture_thread.stop([]() {
    Result rc = audinStopAudioIn();
    Logger::write("audinStopAudioIn() returned 0x%x\n", rc);
  });
  drain_released_audin_buffers();

  if (_audin_buf.buffer) {
    free(_audin_buf.buffer);
    _audin_buf.buffer = nullptr;
  }
}

std::vector<SleepyDiscord::AudioSample> NXCordAudioCapture::poll() {
  time_t current_time = Utils::current_time_millis();
  std::scoped_lock lock(_queue_mutex);
  std::vector<SleepyDiscord::AudioSample> capture;

  if (!_queue.empty()) {
    AudioPacket *packet = &_queue.front();
    while (current_time - packet->time > 100) {
      _queue.pop();
      if (_queue.empty()) {
        return capture;
      }
      packet = &_queue.front();
    }
    capture = std::move(packet->data);
    _queue.pop();
  }
  return capture;
}
