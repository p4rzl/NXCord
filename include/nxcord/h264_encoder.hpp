#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace NXCord {

struct EncodedH264Frame {
  std::vector<uint8_t> annexB;
  size_t spsLen = 0;
  size_t ppsLen = 0;
  bool isKeyFrame = false;
};

class H264Encoder {
 public:
  static constexpr int kWidth = 640;
  static constexpr int kHeight = 360;
  static constexpr int kMbWidth = 40;
  static constexpr int kMbHeight = 23;  // 368 luma rows cropped to 360

  H264Encoder();
  void reset();

  void encodeFrame(const uint8_t *yPlane, int yStride,
                   const uint8_t *uPlane, int uStride,
                   const uint8_t *vPlane, int vStride,
                   bool forceKeyFrame,
                   EncodedH264Frame &outFrame);

 private:
  uint32_t frameNum_ = 0;
  uint16_t idrPicId_ = 0;
  bool hasReference_ = false;
  std::vector<uint8_t> refY_;
  std::vector<uint8_t> refU_;
  std::vector<uint8_t> refV_;
};

}  // namespace NXCord
