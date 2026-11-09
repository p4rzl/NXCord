#include <switch.h>
#include <turbojpeg.h>

#include <algorithm>
#include <common/logger.hpp>
#include <common/utils.hpp>
#include <cstring>
#include <nxcord/nxcord_screen_share.hpp>

void screen_share_capture_thread(NXCordScreenShare *self) {
  const time_t t0 = Utils::current_time_millis();

  if (self->captureFrameToYuv()) {
    self->_consecutive_failures = 0;
    self->_encoder.encodeFrame(
        self->_y_plane.data(), NXCord::H264Encoder::kWidth,
        self->_u_plane.data(), NXCord::H264Encoder::kWidth / 2,
        self->_v_plane.data(), NXCord::H264Encoder::kWidth / 2, false,
        self->_encoded_frame);

    const uint64_t elapsed_ms =
        static_cast<uint64_t>(Utils::current_time_millis()) -
        self->_start_time_ms;
    const uint32_t ts90k = static_cast<uint32_t>(elapsed_ms * 90ULL);
    if (self->_send_callback) {
      self->_send_callback(self->_encoded_frame, ts90k);
    }
  } else {
    if (++self->_consecutive_failures == 20 && self->_error_callback) {
      self->_error_callback(
          "Impossibile catturare lo schermo (caps:sc). Verifica che nessun "
          "altro modulo stia bloccando la cattura.");
    }
  }

  const time_t frame_ms = Utils::current_time_millis() - t0;
  constexpr time_t kTargetFrameMs = 100;  // 10 FPS
  if (frame_ms < kTargetFrameMs) {
    svcSleepThread(static_cast<u64>(kTargetFrameMs - frame_ms) * 1000000ULL);
  } else {
    svcSleepThread(5000000ULL);  // 5 ms yield
  }
}

NXCordScreenShare::NXCordScreenShare(SendFrameCallback sendCallback,
                                     ErrorCallback errorCallback)
    : _send_callback(std::move(sendCallback)),
      _error_callback(std::move(errorCallback)),
      _capture_thread(this, screen_share_capture_thread, 0x20000, false) {}

NXCordScreenShare::~NXCordScreenShare() { stop(); }

bool NXCordScreenShare::start() {
  if (_capture_thread.isActive()) {
    return true;
  }

  Result rc = capsscInitialize();
  if (R_FAILED(rc)) {
    Logger::write("NXCordScreenShare: capsscInitialize failed: 0x%x\n", rc);
    if (_error_callback) {
      _error_callback(
          "Servizio cattura schermo (caps:sc) occupato o non disponibile.");
    }
    return false;
  }
  _capssc_initialized = true;

  _tj = tjInitDecompress();
  if (!_tj) {
    Logger::write("NXCordScreenShare: tjInitDecompress failed\n");
    capsscExit();
    _capssc_initialized = false;
    return false;
  }

  _jpeg_buf.resize(CAPSSC_JPEG_BUFFER_SIZE);
  _y_plane.assign(
      NXCord::H264Encoder::kWidth * (NXCord::H264Encoder::kMbHeight * 16), 16);
  _u_plane.assign((NXCord::H264Encoder::kWidth / 2) *
                      (NXCord::H264Encoder::kMbHeight * 8),
                  128);
  _v_plane.assign((NXCord::H264Encoder::kWidth / 2) *
                      (NXCord::H264Encoder::kMbHeight * 8),
                  128);

  _encoder.reset();
  _start_time_ms = static_cast<uint64_t>(Utils::current_time_millis());
  _consecutive_failures = 0;

  _capture_thread.start();
  Logger::write("NXCordScreenShare: started\n");
  return true;
}

void NXCordScreenShare::stop() {
  if (_capture_thread.isActive()) {
    _capture_thread.stop();
  }
  if (_tj) {
    tjDestroy(_tj);
    _tj = nullptr;
  }
  if (_capssc_initialized) {
    capsscExit();
    _capssc_initialized = false;
    Logger::write("NXCordScreenShare: stopped\n");
  }
  _jpeg_buf.clear();
  _jpeg_buf.shrink_to_fit();
  _y_plane.clear();
  _y_plane.shrink_to_fit();
  _u_plane.clear();
  _u_plane.shrink_to_fit();
  _v_plane.clear();
  _v_plane.shrink_to_fit();
  _u_422.clear();
  _u_422.shrink_to_fit();
  _v_422.clear();
  _v_422.shrink_to_fit();
  _rgb_buf.clear();
  _rgb_buf.shrink_to_fit();
  _encoded_frame.annexB.clear();
  _encoded_frame.annexB.shrink_to_fit();
}

bool NXCordScreenShare::captureFrameToYuv() {
  u64 jpeg_size = 0;
  constexpr s64 kTimeoutNs = 100000000LL;  // 100 ms
  Result rc = capsscCaptureJpegScreenShot(&jpeg_size, _jpeg_buf.data(),
                                          _jpeg_buf.size(), ViLayerStack_Default,
                                          kTimeoutNs);
  if (R_FAILED(rc)) {
    rc = capsscCaptureJpegScreenShot(&jpeg_size, _jpeg_buf.data(),
                                     _jpeg_buf.size(), ViLayerStack_Screenshot,
                                     kTimeoutNs);
  }
  if (R_FAILED(rc)) {
    rc = capsscCaptureJpegScreenShot(
        &jpeg_size, _jpeg_buf.data(), _jpeg_buf.size(),
        ViLayerStack_ApplicationForDebug, kTimeoutNs);
  }

  if (R_SUCCEEDED(rc) && jpeg_size > 0 && jpeg_size <= _jpeg_buf.size()) {
    if (decodeJpegToYuv(_jpeg_buf.data(), static_cast<size_t>(jpeg_size))) {
      return true;
    }
  }

  return captureRawStreamToYuv();
}

bool NXCordScreenShare::decodeJpegToYuv(const uint8_t *jpegData,
                                        size_t jpegSize) {
  if (!_tj || !jpegData || jpegSize == 0) return false;

  int width = 0, height = 0, subsamp = 0, colorspace = 0;
  if (tjDecompressHeader3(_tj, jpegData, static_cast<unsigned long>(jpegSize),
                          &width, &height, &subsamp, &colorspace) != 0) {
    return false;
  }

  constexpr int kDstW = NXCord::H264Encoder::kWidth;   // 640
  constexpr int kDstH = NXCord::H264Encoder::kHeight;  // 360

  if (subsamp == TJSAMP_420) {
    unsigned char *planes[3] = {_y_plane.data(), _u_plane.data(),
                                _v_plane.data()};
    int strides[3] = {kDstW, kDstW / 2, kDstW / 2};
    if (tjDecompressToYUVPlanes(_tj, jpegData,
                                static_cast<unsigned long>(jpegSize), planes,
                                kDstW, strides, kDstH, TJFLAG_FASTDCT) == 0) {
      return true;
    }
  } else if (subsamp == TJSAMP_422) {
    if (_u_422.size() < static_cast<size_t>((kDstW / 2) * kDstH)) {
      _u_422.resize((kDstW / 2) * kDstH);
      _v_422.resize((kDstW / 2) * kDstH);
    }
    unsigned char *planes[3] = {_y_plane.data(), _u_422.data(), _v_422.data()};
    int strides[3] = {kDstW, kDstW / 2, kDstW / 2};
    if (tjDecompressToYUVPlanes(_tj, jpegData,
                                static_cast<unsigned long>(jpegSize), planes,
                                kDstW, strides, kDstH, TJFLAG_FASTDCT) == 0) {
      for (int y = 0; y < kDstH / 2; ++y) {
        const uint8_t *u0 = _u_422.data() + (y * 2) * (kDstW / 2);
        const uint8_t *u1 = u0 + (kDstW / 2);
        const uint8_t *v0 = _v_422.data() + (y * 2) * (kDstW / 2);
        const uint8_t *v1 = v0 + (kDstW / 2);
        uint8_t *uDst = _u_plane.data() + y * (kDstW / 2);
        uint8_t *vDst = _v_plane.data() + y * (kDstW / 2);
        for (int x = 0; x < kDstW / 2; ++x) {
          uDst[x] = static_cast<uint8_t>((u0[x] + u1[x] + 1) >> 1);
          vDst[x] = static_cast<uint8_t>((v0[x] + v1[x] + 1) >> 1);
        }
      }
      return true;
    }
  }

  // Fallback for any other subsampling format via 640x360 RGB
  if (_rgb_buf.size() < static_cast<size_t>(kDstW * kDstH * 3)) {
    _rgb_buf.resize(kDstW * kDstH * 3);
  }
  if (tjDecompress2(_tj, jpegData, static_cast<unsigned long>(jpegSize),
                    _rgb_buf.data(), kDstW, kDstW * 3, kDstH, TJPF_RGB,
                    TJFLAG_FASTDCT) != 0) {
    return false;
  }

  for (int y = 0; y < kDstH; y += 2) {
    const uint8_t *row0 = _rgb_buf.data() + y * kDstW * 3;
    const uint8_t *row1 = row0 + kDstW * 3;
    uint8_t *yDst0 = _y_plane.data() + y * kDstW;
    uint8_t *yDst1 = yDst0 + kDstW;
    uint8_t *uDst = _u_plane.data() + (y / 2) * (kDstW / 2);
    uint8_t *vDst = _v_plane.data() + (y / 2) * (kDstW / 2);

    for (int x = 0; x < kDstW; x += 2) {
      const int r00 = row0[x * 3 + 0], g00 = row0[x * 3 + 1],
                b00 = row0[x * 3 + 2];
      const int r01 = row0[(x + 1) * 3 + 0], g01 = row0[(x + 1) * 3 + 1],
                b01 = row0[(x + 1) * 3 + 2];
      const int r10 = row1[x * 3 + 0], g10 = row1[x * 3 + 1],
                b10 = row1[x * 3 + 2];
      const int r11 = row1[(x + 1) * 3 + 0], g11 = row1[(x + 1) * 3 + 1],
                b11 = row1[(x + 1) * 3 + 2];

      yDst0[x] = static_cast<uint8_t>(
          ((66 * r00 + 129 * g00 + 25 * b00 + 128) >> 8) + 16);
      yDst0[x + 1] = static_cast<uint8_t>(
          ((66 * r01 + 129 * g01 + 25 * b01 + 128) >> 8) + 16);
      yDst1[x] = static_cast<uint8_t>(
          ((66 * r10 + 129 * g10 + 25 * b10 + 128) >> 8) + 16);
      yDst1[x + 1] = static_cast<uint8_t>(
          ((66 * r11 + 129 * g11 + 25 * b11 + 128) >> 8) + 16);

      const int rAvg = (r00 + r01 + r10 + r11 + 2) >> 2;
      const int gAvg = (g00 + g01 + g10 + g11 + 2) >> 2;
      const int bAvg = (b00 + b01 + b10 + b11 + 2) >> 2;

      uDst[x / 2] = static_cast<uint8_t>(
          ((-38 * rAvg - 74 * gAvg + 112 * bAvg + 128) >> 8) + 128);
      vDst[x / 2] = static_cast<uint8_t>(
          ((112 * rAvg - 94 * gAvg - 18 * bAvg + 128) >> 8) + 128);
    }
  }
  return true;
}

bool NXCordScreenShare::captureRawStreamToYuv() {
  u64 size = 0, width = 0, height = 0;
  constexpr s64 kTimeoutNs = 100000000LL;
  Result rc = capsscOpenRawScreenShotReadStream(
      &size, &width, &height, ViLayerStack_Default, kTimeoutNs);
  if (R_FAILED(rc)) {
    rc = capsscOpenRawScreenShotReadStream(&size, &width, &height,
                                           ViLayerStack_ApplicationForDebug,
                                           kTimeoutNs);
  }
  if (R_FAILED(rc)) {
    return false;
  }

  constexpr size_t kSrcW = 1280;
  constexpr size_t kSrcH = 720;
  constexpr size_t kStripRows = 16;
  constexpr size_t kStripBytes = kSrcW * kStripRows * 4;  // 81,920 bytes
  if (_jpeg_buf.size() < kStripBytes) {
    _jpeg_buf.resize(kStripBytes);
  }

  bool ok = (width == kSrcW && height == kSrcH);
  for (size_t srcY = 0; ok && srcY < kSrcH; srcY += kStripRows) {
    u64 bytes_read = 0;
    const u64 offset = srcY * kSrcW * 4;
    rc = capsscReadRawScreenShotReadStream(&bytes_read, _jpeg_buf.data(),
                                           kStripBytes, offset);
    if (R_FAILED(rc) || bytes_read < kStripBytes) {
      ok = false;
      break;
    }

    for (size_t dy = 0; dy < kStripRows; dy += 2) {
      const size_t dstY = (srcY + dy) / 2;
      const uint8_t *row0 = _jpeg_buf.data() + dy * kSrcW * 4;
      const uint8_t *row1 = row0 + kSrcW * 4;
      uint8_t *yDst = _y_plane.data() + dstY * 640;

      for (size_t dstX = 0; dstX < 640; ++dstX) {
        const size_t srcX = dstX * 2;
        const int r = (row0[srcX * 4 + 0] + row0[(srcX + 1) * 4 + 0] +
                       row1[srcX * 4 + 0] + row1[(srcX + 1) * 4 + 0] + 2) >>
                      2;
        const int g = (row0[srcX * 4 + 1] + row0[(srcX + 1) * 4 + 1] +
                       row1[srcX * 4 + 1] + row1[(srcX + 1) * 4 + 1] + 2) >>
                      2;
        const int b = (row0[srcX * 4 + 2] + row0[(srcX + 1) * 4 + 2] +
                       row1[srcX * 4 + 2] + row1[(srcX + 1) * 4 + 2] + 2) >>
                      2;
        yDst[dstX] = static_cast<uint8_t>(
            ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);

        if ((dstY & 1) == 0 && (dstX & 1) == 0) {
          _u_plane[(dstY / 2) * 320 + (dstX / 2)] = static_cast<uint8_t>(
              ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
          _v_plane[(dstY / 2) * 320 + (dstX / 2)] = static_cast<uint8_t>(
              ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
      }
    }
  }

  capsscCloseRawScreenShotReadStream();
  return ok;
}
