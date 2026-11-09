#pragma once

#include <common/loop_thread.hpp>
#include <cstdint>
#include <functional>
#include <nxcord/h264_encoder.hpp>
#include <string>
#include <vector>

class NXCordScreenShare {
 public:
  using SendFrameCallback = std::function<void(
      const NXCord::EncodedH264Frame &frame, uint32_t timestamp90k)>;
  using ErrorCallback = std::function<void(const std::string &message)>;

  NXCordScreenShare(SendFrameCallback sendCallback,
                    ErrorCallback errorCallback);
  ~NXCordScreenShare();

  bool start();
  void stop();
  bool isActive() const { return _capture_thread.isActive(); }

 private:
  friend void screen_share_capture_thread(NXCordScreenShare *self);

  bool captureFrameToYuv();
  bool decodeJpegToYuv(const uint8_t *jpegData, size_t jpegSize);
  bool captureRawStreamToYuv();

  SendFrameCallback _send_callback;
  ErrorCallback _error_callback;
  LoopThread<NXCordScreenShare *> _capture_thread;
  NXCord::H264Encoder _encoder;
  NXCord::EncodedH264Frame _encoded_frame;

  void *_tj = nullptr;
  bool _capssc_initialized = false;
  uint64_t _start_time_ms = 0;
  uint32_t _last_ts90k = 0;
  uint32_t _frames_sent = 0;
  int _consecutive_failures = 0;
  int _preferred_layer_stack = 0;

  std::vector<uint8_t> _jpeg_buf;
  std::vector<uint8_t> _y_plane;
  std::vector<uint8_t> _u_plane;
  std::vector<uint8_t> _v_plane;
  std::vector<uint8_t> _u_422;
  std::vector<uint8_t> _v_422;
  std::vector<uint8_t> _rgb_buf;
};
