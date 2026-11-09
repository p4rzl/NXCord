#include <nxcord/h264_encoder.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace NXCord {
namespace {

class BitWriter {
 public:
  std::vector<uint8_t> data;
  uint8_t curByte = 0;
  int bitPos = 0;

  void reserve(size_t bytes) { data.reserve(bytes); }

  void writeBit(int bit) {
    curByte = static_cast<uint8_t>((curByte << 1) | (bit & 1));
    if (++bitPos == 8) {
      data.push_back(curByte);
      curByte = 0;
      bitPos = 0;
    }
  }

  void writeBits(uint32_t val, int n) {
    for (int i = n - 1; i >= 0; --i) {
      writeBit((val >> i) & 1);
    }
  }

  void writeUE(uint32_t val) {
    uint32_t v = val + 1;
    int len = 0;
    uint32_t tmp = v;
    while (tmp > 1) {
      tmp >>= 1;
      ++len;
    }
    for (int i = 0; i < len; ++i) writeBit(0);
    writeBits(v, len + 1);
  }

  void writeSE(int32_t val) {
    uint32_t codeNum =
        (val <= 0) ? (static_cast<uint32_t>(-val) * 2)
                   : (static_cast<uint32_t>(val) * 2 - 1);
    writeUE(codeNum);
  }

  void rbspTrailingBits() {
    writeBit(1);
    while (bitPos != 0) writeBit(0);
  }
};

void appendAnnexBNal(std::vector<uint8_t> &out, uint8_t nalHeader,
                     const std::vector<uint8_t> &rbsp, size_t *outNalSize = nullptr) {
  const size_t startOffset = out.size();
  out.push_back(0x00);
  out.push_back(0x00);
  out.push_back(0x00);
  out.push_back(0x01);
  out.push_back(nalHeader);
  int zeros = 0;
  for (uint8_t b : rbsp) {
    if (zeros == 2 && b <= 3) {
      out.push_back(0x03);
      zeros = 0;
    }
    out.push_back(b);
    if (b == 0)
      ++zeros;
    else
      zeros = 0;
  }
  if (outNalSize) {
    *outNalSize = out.size() - startOffset - 4;
  }
}

struct VLC {
  uint8_t len;
  uint16_t code;
};

constexpr VLC kCoeffToken0[4][17] = {
    {{1, 1},   {6, 5},   {8, 7},   {9, 7},   {10, 7},  {11, 7},
     {13, 15}, {13, 11}, {13, 8},  {14, 15}, {14, 11}, {15, 15},
     {15, 11}, {16, 15}, {16, 11}, {16, 7},  {16, 4}},
    {{0, 0},   {2, 1},   {6, 4},   {8, 6},   {9, 6},   {10, 6},
     {11, 6},  {13, 14}, {13, 10}, {14, 14}, {14, 10}, {15, 14},
     {15, 10}, {15, 1},  {16, 14}, {16, 10}, {16, 6}},
    {{0, 0},   {0, 0},   {3, 1},   {7, 5},   {8, 5},   {9, 5},
     {10, 5},  {11, 5},  {13, 13}, {13, 9},  {14, 13}, {14, 9},
     {15, 13}, {15, 9},  {16, 13}, {16, 9},  {16, 5}},
    {{0, 0},   {0, 0},   {0, 0},   {5, 3},   {6, 3},   {7, 4},
     {8, 4},   {9, 4},   {10, 4},  {11, 4},  {13, 12}, {14, 12},
     {14, 8},  {15, 12}, {15, 8},  {16, 12}, {16, 8}}};

constexpr VLC kCoeffToken2[4][17] = {
    {{2, 3},   {6, 11},  {6, 7},   {7, 7},   {8, 7},   {9, 7},
     {10, 7},  {11, 15}, {11, 11}, {12, 15}, {12, 11}, {12, 8},
     {13, 15}, {13, 11}, {13, 7},  {14, 9},  {14, 7}},
    {{0, 0},   {2, 2},   {5, 7},   {6, 6},   {6, 5},   {7, 6},
     {8, 6},   {9, 6},   {11, 14}, {11, 10}, {12, 14}, {12, 10},
     {13, 14}, {13, 10}, {14, 11}, {14, 8},  {14, 6}},
    {{0, 0},   {0, 0},   {3, 3},   {5, 6},   {6, 4},   {7, 5},
     {8, 5},   {9, 5},   {10, 5},  {11, 13}, {11, 9},  {12, 13},
     {12, 9},  {13, 13}, {13, 9},  {14, 10}, {14, 5}},
    {{0, 0},   {0, 0},   {0, 0},   {4, 5},   {4, 4},   {5, 5},
     {6, 3},   {7, 4},   {8, 4},   {9, 4},   {10, 4},  {11, 12},
     {11, 8},  {12, 12}, {13, 12}, {13, 8},  {14, 4}}};

constexpr VLC kCoeffToken4[4][17] = {
    {{4, 15},  {6, 15},  {6, 11},  {6, 8},   {7, 15},  {7, 11},
     {7, 9},   {7, 8},   {8, 15},  {8, 11},  {9, 15},  {9, 11},
     {9, 8},   {10, 13}, {10, 9},  {10, 5},  {10, 1}},
    {{0, 0},   {4, 14},  {5, 15},  {5, 12},  {6, 14},  {6, 10},
     {7, 14},  {7, 10},  {8, 14},  {8, 10},  {9, 14},  {9, 10},
     {10, 15}, {10, 12}, {10, 8},  {10, 4},  {10, 0}},
    {{0, 0},   {0, 0},   {4, 13},  {5, 14},  {5, 11},  {6, 13},
     {6, 9},   {7, 13},  {8, 13},  {8, 9},   {9, 13},  {9, 9},
     {10, 14}, {10, 11}, {10, 7},  {10, 3},  {0, 0}},
    {{0, 0},   {0, 0},   {0, 0},   {4, 12},  {5, 13},  {5, 10},
     {6, 12},  {7, 12},  {8, 12},  {8, 8},   {9, 12},  {10, 10},
     {10, 6},  {10, 2},  {0, 0},   {0, 0},   {0, 0}}};

constexpr VLC kCoeffTokenChromaDC[4][5] = {
    {{2, 1}, {6, 7}, {6, 4}, {6, 3}, {6, 2}},
    {{0, 0}, {1, 1}, {6, 6}, {7, 3}, {8, 3}},
    {{0, 0}, {0, 0}, {3, 1}, {7, 2}, {8, 2}},
    {{0, 0}, {0, 0}, {0, 0}, {6, 5}, {7, 0}}};

void writeCoeffToken(BitWriter &bw, int nC, int totalCoeffs, int trailingOnes) {
  if (nC == -1) {
    VLC v = kCoeffTokenChromaDC[trailingOnes][totalCoeffs];
    bw.writeBits(v.code, v.len);
  } else if (nC < 2) {
    VLC v = kCoeffToken0[trailingOnes][totalCoeffs];
    bw.writeBits(v.code, v.len);
  } else if (nC < 4) {
    VLC v = kCoeffToken2[trailingOnes][totalCoeffs];
    bw.writeBits(v.code, v.len);
  } else if (nC < 8) {
    VLC v = kCoeffToken4[trailingOnes][totalCoeffs];
    bw.writeBits(v.code, v.len);
  } else {
    if (totalCoeffs == 0) {
      bw.writeBits(3, 6);
    } else {
      uint32_t code =
          (static_cast<uint32_t>(totalCoeffs - 1) << 2) |
          static_cast<uint32_t>(trailingOnes);
      bw.writeBits(code, 6);
    }
  }
}

void writeCavlcSingleDC(BitWriter &bw, int nC, int32_t dcVal) {
  if (dcVal == 0) {
    writeCoeffToken(bw, nC, 0, 0);
    return;
  }
  const int32_t absVal = std::abs(dcVal);
  if (absVal == 1) {
    writeCoeffToken(bw, nC, 1, 1);
    bw.writeBit(dcVal < 0 ? 1 : 0);
  } else {
    writeCoeffToken(bw, nC, 1, 0);
    const uint32_t levelCode =
        static_cast<uint32_t>((absVal - 2) * 2 + (dcVal < 0 ? 1 : 0));
    if (levelCode < 14) {
      bw.writeBits(1, static_cast<int>(levelCode) + 1);
    } else if (levelCode < 30) {
      bw.writeBits(1, 15);
      bw.writeBits(levelCode - 14, 4);
    } else {
      bw.writeBits(1, 16);
      bw.writeBits(levelCode - 30, 12);
    }
  }
  bw.writeBit(1);  // total_zeros = 0 for TotalCoeff = 1
}

constexpr int kLuma4x4BlockX[16] = {0, 1, 0, 1, 2, 3, 2, 3,
                                    0, 1, 0, 1, 2, 3, 2, 3};
constexpr int kLuma4x4BlockY[16] = {0, 0, 1, 1, 0, 0, 1, 1,
                                    2, 2, 3, 3, 2, 2, 3, 3};

constexpr int kQp = 24;
constexpr int kQbits = 15 + (kQp / 6);  // 19
constexpr int kMf00 = 13107;            // QP%6 == 0
constexpr int kV00 = 10;                // QP%6 == 0, dequant = 160

void buildSPS(std::vector<uint8_t> &out, size_t *outLen) {
  BitWriter bw;
  bw.writeBits(66, 8);    // Baseline profile
  bw.writeBits(0xC0, 8);  // constraint_set0_flag=1, constraint_set1_flag=1
  bw.writeBits(30, 8);    // Level 3.0
  bw.writeUE(0);          // seq_parameter_set_id = 0
  bw.writeUE(0);          // log2_max_frame_num_minus4 = 0 (max 16)
  bw.writeUE(0);          // pic_order_cnt_type = 0
  bw.writeUE(2);          // log2_max_pic_order_cnt_lsb_minus4 = 2 (max 64)
  bw.writeUE(1);          // max_num_ref_frames = 1
  bw.writeBit(0);         // gaps_in_frame_num_value_allowed_flag = 0
  bw.writeUE(H264Encoder::kMbWidth - 1);   // 39 -> 640px
  bw.writeUE(H264Encoder::kMbHeight - 1);  // 22 -> 368px
  bw.writeBit(1);                          // frame_mbs_only_flag = 1
  bw.writeBit(1);                          // direct_8x8_inference_flag = 1
  bw.writeBit(1);                          // frame_cropping_flag = 1
  bw.writeUE(0);                           // crop_left = 0
  bw.writeUE(0);                           // crop_right = 0
  bw.writeUE(0);                           // crop_top = 0
  bw.writeUE(4);  // crop_bottom = 4 (8 luma lines -> 360px)
  bw.writeBit(0);  // vui_parameters_present_flag = 0
  bw.rbspTrailingBits();
  appendAnnexBNal(out, 0x67, bw.data, outLen);
}

void buildPPS(std::vector<uint8_t> &out, size_t *outLen) {
  BitWriter bw;
  bw.writeUE(0);   // pic_parameter_set_id = 0
  bw.writeUE(0);   // seq_parameter_set_id = 0
  bw.writeBit(0);  // entropy_coding_mode_flag = 0 (CAVLC)
  bw.writeBit(0);  // bottom_field_pic_order_in_frame_present_flag = 0
  bw.writeUE(0);   // num_slice_groups_minus1 = 0
  bw.writeUE(0);   // num_ref_idx_l0_default_active_minus1 = 0
  bw.writeUE(0);   // num_ref_idx_l1_default_active_minus1 = 0
  bw.writeBit(0);  // weighted_pred_flag = 0
  bw.writeBits(0, 2);  // weighted_bipred_idc = 0
  bw.writeSE(kQp - 26);  // pic_init_qp_minus26 = -2
  bw.writeSE(0);         // pic_init_qs_minus26 = 0
  bw.writeSE(0);         // chroma_qp_index_offset = 0
  bw.writeBit(1);        // deblocking_filter_control_present_flag = 1
  bw.writeBit(0);        // constrained_intra_pred_flag = 0
  bw.writeBit(0);        // redundant_pic_cnt_present_flag = 0
  bw.rbspTrailingBits();
  appendAnnexBNal(out, 0x68, bw.data, outLen);
}

inline uint8_t samplePlane(const uint8_t *plane, int stride, int x, int y,
                           int maxH) {
  const int cy = (y < maxH) ? y : (maxH - 1);
  return plane[cy * stride + x];
}

}  // namespace

H264Encoder::H264Encoder() { reset(); }

void H264Encoder::reset() {
  frameNum_ = 0;
  idrPicId_ = 0;
  hasReference_ = false;
  refY_.assign(kWidth * (kMbHeight * 16), 16);
  refU_.assign((kWidth / 2) * (kMbHeight * 8), 128);
  refV_.assign((kWidth / 2) * (kMbHeight * 8), 128);
}

void H264Encoder::encodeFrame(const uint8_t *yPlane, int yStride,
                              const uint8_t *uPlane, int uStride,
                              const uint8_t *vPlane, int vStride,
                              bool forceKeyFrame,
                              EncodedH264Frame &outFrame) {
  outFrame.annexB.clear();
  outFrame.spsLen = 0;
  outFrame.ppsLen = 0;

  const bool isKeyFrame =
      forceKeyFrame || !hasReference_ || (frameNum_ % 12 == 0);
  outFrame.isKeyFrame = isKeyFrame;

  if (isKeyFrame) {
    outFrame.annexB.reserve(40 * 1024);
    buildSPS(outFrame.annexB, &outFrame.spsLen);
    buildPPS(outFrame.annexB, &outFrame.ppsLen);
    frameNum_ = 0;
  } else {
    outFrame.annexB.reserve(16 * 1024);
  }

  BitWriter bw;
  bw.reserve(isKeyFrame ? 32 * 1024 : 12 * 1024);

  // Slice Header
  bw.writeUE(0);                      // first_mb_in_slice = 0
  bw.writeUE(isKeyFrame ? 7 : 5);     // slice_type: 7 (I) or 5 (P)
  bw.writeUE(0);                      // pic_parameter_set_id = 0
  bw.writeBits(frameNum_ & 0x0F, 4);  // frame_num (4 bits)
  if (isKeyFrame) {
    bw.writeUE(idrPicId_ & 0xFF);
    ++idrPicId_;
  }
  bw.writeBits((frameNum_ * 2) & 0x3F, 6);  // pic_order_cnt_lsb (6 bits)
  if (!isKeyFrame) {
    bw.writeBit(0);  // num_ref_idx_active_override_flag = 0
    bw.writeBit(0);  // ref_pic_list_modification_flag_l0 = 0
    bw.writeBit(0);  // adaptive_ref_pic_marking_mode_flag = 0
  } else {
    bw.writeBit(0);  // no_output_of_prior_pics_flag = 0
    bw.writeBit(0);  // long_term_reference_flag = 0
  }
  bw.writeSE(0);   // slice_qp_delta = 0
  bw.writeUE(1);   // disable_deblocking_filter_idc = 1

  uint8_t totalCoeffLuma[kMbWidth * kMbHeight][16] = {};
  uint8_t totalCoeffChroma[kMbWidth * kMbHeight][2][4] = {};

  auto getLumaNC = [&](int mbX, int mbY, int blkIdx) -> int {
    const int bx = kLuma4x4BlockX[blkIdx];
    const int by = kLuma4x4BlockY[blkIdx];
    int nA = -1, nB = -1;
    if (bx > 0) {
      for (int i = 0; i < 16; ++i) {
        if (kLuma4x4BlockX[i] == bx - 1 && kLuma4x4BlockY[i] == by) {
          nA = totalCoeffLuma[mbY * kMbWidth + mbX][i];
          break;
        }
      }
    } else if (mbX > 0) {
      for (int i = 0; i < 16; ++i) {
        if (kLuma4x4BlockX[i] == 3 && kLuma4x4BlockY[i] == by) {
          nA = totalCoeffLuma[mbY * kMbWidth + (mbX - 1)][i];
          break;
        }
      }
    }
    if (by > 0) {
      for (int i = 0; i < 16; ++i) {
        if (kLuma4x4BlockX[i] == bx && kLuma4x4BlockY[i] == by - 1) {
          nB = totalCoeffLuma[mbY * kMbWidth + mbX][i];
          break;
        }
      }
    } else if (mbY > 0) {
      for (int i = 0; i < 16; ++i) {
        if (kLuma4x4BlockX[i] == bx && kLuma4x4BlockY[i] == 3) {
          nB = totalCoeffLuma[(mbY - 1) * kMbWidth + mbX][i];
          break;
        }
      }
    }
    if (nA >= 0 && nB >= 0) return (nA + nB + 1) >> 1;
    if (nA >= 0) return nA;
    if (nB >= 0) return nB;
    return 0;
  };

  auto getChromaNC = [&](int mbX, int mbY, int iCbCr, int blkIdx) -> int {
    const int bx = blkIdx & 1;
    const int by = blkIdx >> 1;
    int nA = -1, nB = -1;
    if (bx > 0) {
      nA = totalCoeffChroma[mbY * kMbWidth + mbX][iCbCr][by * 2 + (bx - 1)];
    } else if (mbX > 0) {
      nA = totalCoeffChroma[mbY * kMbWidth + (mbX - 1)][iCbCr][by * 2 + 1];
    }
    if (by > 0) {
      nB = totalCoeffChroma[mbY * kMbWidth + mbX][iCbCr][(by - 1) * 2 + bx];
    } else if (mbY > 0) {
      nB = totalCoeffChroma[(mbY - 1) * kMbWidth + mbX][iCbCr][2 + bx];
    }
    if (nA >= 0 && nB >= 0) return (nA + nB + 1) >> 1;
    if (nA >= 0) return nA;
    if (nB >= 0) return nB;
    return 0;
  };

  uint32_t skipRun = 0;

  for (int mbY = 0; mbY < kMbHeight; ++mbY) {
    for (int mbX = 0; mbX < kMbWidth; ++mbX) {
      const int mbIdx = mbY * kMbWidth + mbX;

      if (!isKeyFrame) {
        int blockSadY = 0;
        int maxDiffY = 0;
        for (int by = 0; by < 4; ++by) {
          for (int bx = 0; bx < 4; ++bx) {
            int sum = 0;
            for (int dy = 0; dy < 4; ++dy) {
              const int py = mbY * 16 + by * 4 + dy;
              const int sy = (py < kHeight) ? py : (kHeight - 1);
              const uint8_t *srcRow = yPlane + sy * yStride + mbX * 16 + bx * 4;
              sum += srcRow[0] + srcRow[1] + srcRow[2] + srcRow[3];
            }
            const int curAvg = (sum + 8) >> 4;
            const int refVal =
                refY_[(mbY * 16 + by * 4) * kWidth + (mbX * 16 + bx * 4)];
            const int diff = std::abs(curAvg - refVal);
            blockSadY += diff;
            if (diff > maxDiffY) maxDiffY = diff;
          }
        }
        if (blockSadY <= 48 && maxDiffY <= 5) {
          int sumU = 0, sumV = 0, refSumU = 0, refSumV = 0;
          for (int dy = 0; dy < 8; ++dy) {
            const int py = mbY * 8 + dy;
            const int sy = (py < kHeight / 2) ? py : (kHeight / 2 - 1);
            const uint8_t *uSrc = uPlane + sy * uStride + mbX * 8;
            const uint8_t *vSrc = vPlane + sy * vStride + mbX * 8;
            const uint8_t *uRef = refU_.data() + py * (kWidth / 2) + mbX * 8;
            const uint8_t *vRef = refV_.data() + py * (kWidth / 2) + mbX * 8;
            for (int dx = 0; dx < 8; ++dx) {
              sumU += uSrc[dx];
              sumV += vSrc[dx];
              refSumU += uRef[dx];
              refSumV += vRef[dx];
            }
          }
          const int diffU = std::abs(((sumU + 32) >> 6) - ((refSumU + 32) >> 6));
          const int diffV = std::abs(((sumV + 32) >> 6) - ((refSumV + 32) >> 6));
          if (diffU <= 4 && diffV <= 4) {
            ++skipRun;
            continue;
          }
        }
        bw.writeUE(skipRun);
        skipRun = 0;
      }

      // 1. Luma 4x4 DC prediction and quantization (I_4x4 mode 2 = Intra_4x4_DC)
      int32_t lumaDC[16];
      bool hasNonZeroLuma = false;
      for (int idx = 0; idx < 16; ++idx) {
        const int bx = kLuma4x4BlockX[idx];
        const int by = kLuma4x4BlockY[idx];
        const int x0 = mbX * 16 + bx * 4;
        const int y0 = mbY * 16 + by * 4;

        int pred4x4 = 128;
        int sumTop = 0, sumLeft = 0;
        if (y0 > 0) {
          const uint8_t *topRow = refY_.data() + (y0 - 1) * kWidth + x0;
          sumTop = topRow[0] + topRow[1] + topRow[2] + topRow[3];
        }
        if (x0 > 0) {
          for (int dy = 0; dy < 4; ++dy) {
            sumLeft += refY_[(y0 + dy) * kWidth + (x0 - 1)];
          }
        }
        if (x0 > 0 && y0 > 0)
          pred4x4 = (sumTop + sumLeft + 4) >> 3;
        else if (y0 > 0)
          pred4x4 = (sumTop + 2) >> 2;
        else if (x0 > 0)
          pred4x4 = (sumLeft + 2) >> 2;

        int r = 0;
        for (int dy = 0; dy < 4; ++dy) {
          const int py = y0 + dy;
          for (int dx = 0; dx < 4; ++dx) {
            const int px = x0 + dx;
            const int orig = samplePlane(yPlane, yStride, px, py, kHeight);
            r += (orig - pred4x4);
          }
        }

        const int32_t absR = std::abs(r);
        int32_t q = (absR * kMf00 + ((1 << kQbits) / 3)) >> kQbits;
        if (r < 0) q = -q;
        lumaDC[idx] = q;
        if (q != 0) hasNonZeroLuma = true;

        const int32_t dq = q * (kV00 << 4);
        const int recon = std::clamp(pred4x4 + ((dq + 32) >> 6), 0, 255);
        for (int dy = 0; dy < 4; ++dy) {
          for (int dx = 0; dx < 4; ++dx) {
            refY_[(y0 + dy) * kWidth + (x0 + dx)] =
                static_cast<uint8_t>(recon);
          }
        }
      }

      // 2. Chroma DC prediction and quantization
      int32_t chromaDC[2][4];
      bool hasNonZeroChroma = false;
      for (int c = 0; c < 2; ++c) {
        const uint8_t *cPlane = (c == 0) ? uPlane : vPlane;
        const int cStride = (c == 0) ? uStride : vStride;
        std::vector<uint8_t> &refC = (c == 0) ? refU_ : refV_;

        int predC[4];
        int sT0 = 0, sT1 = 0, sL0 = 0, sL1 = 0;
        if (mbY > 0) {
          const int topY = mbY * 8 - 1;
          for (int x = 0; x < 4; ++x)
            sT0 += refC[topY * (kWidth / 2) + mbX * 8 + x];
          for (int x = 4; x < 8; ++x)
            sT1 += refC[topY * (kWidth / 2) + mbX * 8 + x];
        }
        if (mbX > 0) {
          const int leftX = mbX * 8 - 1;
          for (int y = 0; y < 4; ++y)
            sL0 += refC[(mbY * 8 + y) * (kWidth / 2) + leftX];
          for (int y = 4; y < 8; ++y)
            sL1 += refC[(mbY * 8 + y) * (kWidth / 2) + leftX];
        }
        if (mbX > 0 && mbY > 0) {
          predC[0] = (sT0 + sL0 + 4) >> 3;
          predC[1] = (sT1 + 2) >> 2;
          predC[2] = (sL1 + 2) >> 2;
          predC[3] = (sT1 + sL1 + 4) >> 3;
        } else if (mbY > 0) {
          predC[0] = predC[2] = (sT0 + 2) >> 2;
          predC[1] = predC[3] = (sT1 + 2) >> 2;
        } else if (mbX > 0) {
          predC[0] = predC[1] = (sL0 + 2) >> 2;
          predC[2] = predC[3] = (sL1 + 2) >> 2;
        } else {
          predC[0] = predC[1] = predC[2] = predC[3] = 128;
        }

        int32_t d[4];
        for (int b = 0; b < 4; ++b) {
          const int bx = b & 1;
          const int by = b >> 1;
          int s = 0;
          for (int dy = 0; dy < 4; ++dy) {
            const int py = mbY * 8 + by * 4 + dy;
            for (int dx = 0; dx < 4; ++dx) {
              const int px = mbX * 8 + bx * 4 + dx;
              const int orig =
                  samplePlane(cPlane, cStride, px, py, kHeight / 2);
              s += (orig - predC[b]);
            }
          }
          d[b] = s;
        }

        const int32_t h0 = d[0] + d[1] + d[2] + d[3];
        const int32_t absH = std::abs(h0);
        int32_t q0 =
            (absH * kMf00 + ((1 << (kQbits + 1)) / 3)) >> (kQbits + 1);
        if (h0 < 0) q0 = -q0;
        chromaDC[c][0] = q0;
        chromaDC[c][1] = 0;
        chromaDC[c][2] = 0;
        chromaDC[c][3] = 0;
        if (q0 != 0) hasNonZeroChroma = true;

        for (int b = 0; b < 4; ++b) {
          const int bx = b & 1;
          const int by = b >> 1;
          const int32_t dc = q0 * (kV00 << 3);
          const int recon = std::clamp(predC[b] + ((dc + 32) >> 6), 0, 255);
          for (int dy = 0; dy < 4; ++dy) {
            for (int dx = 0; dx < 4; ++dx) {
              refC[(mbY * 8 + by * 4 + dy) * (kWidth / 2) +
                   (mbX * 8 + bx * 4 + dx)] = static_cast<uint8_t>(recon);
            }
          }
        }
      }

      const int cbpLuma = hasNonZeroLuma ? 15 : 0;
      const int cbpChroma = hasNonZeroChroma ? 1 : 0;
      const uint32_t mbType = isKeyFrame ? 0 : 5;  // I_4x4

      bw.writeUE(mbType);
      // 16 blocks: if block (0,0) has neither left nor top, pred is already 2 (DC).
      // Wait: if x0 > 0 && y0 == 0 (first row, mbX > 0) or x0 == 0 && y0 > 0 (first col, mbY > 0),
      // H.264 spec 8.3.1.1 says dcPredModePredictedFlag = 1 when EITHER left OR top is unavailable!
      // And when both are available, both have mode 2, so pred is 2!
      // Thus predIntra4x4PredMode is ALWAYS 2 for every block in the frame!
      bw.writeBits(0xFFFF, 16);  // prev_intra4x4_pred_mode_flag = 1 x 16
      bw.writeUE(0);             // intra_chroma_pred_mode = 0 (DC)

      // coded_block_pattern me(v) Table 9-4 for Intra_4x4:
      // cbp=31 (Luma=15, Chroma=1) -> codeNum=1
      // cbp=15 (Luma=15, Chroma=0) -> codeNum=2
      // cbp=0  (Luma=0,  Chroma=0) -> codeNum=3
      // cbp=16 (Luma=0,  Chroma=1) -> codeNum=16
      uint32_t cbpCodeNum = 3;
      if (cbpLuma == 15 && cbpChroma == 1)
        cbpCodeNum = 1;
      else if (cbpLuma == 15 && cbpChroma == 0)
        cbpCodeNum = 2;
      else if (cbpLuma == 0 && cbpChroma == 1)
        cbpCodeNum = 16;
      bw.writeUE(cbpCodeNum);

      if (cbpLuma > 0 || cbpChroma > 0) {
        bw.writeSE(0);  // mb_qp_delta = 0
      }

      // Luma4x4Level (16coeffs per 4x4 block; index 0 is the 4x4 DC!)
      if (cbpLuma > 0) {
        for (int idx = 0; idx < 16; ++idx) {
          const int nC = getLumaNC(mbX, mbY, idx);
          const int32_t val = lumaDC[idx];
          writeCavlcSingleDC(bw, nC, val);
          totalCoeffLuma[mbIdx][idx] = (val != 0) ? 1 : 0;
        }
      }

      if (cbpChroma > 0) {
        for (int iCbCr = 0; iCbCr < 2; ++iCbCr) {
          writeCavlcSingleDC(bw, -1, chromaDC[iCbCr][0]);
        }
      }
      (void)getChromaNC;
    }
  }

  if (!isKeyFrame && skipRun > 0) {
    bw.writeUE(skipRun);
  }

  bw.rbspTrailingBits();
  appendAnnexBNal(outFrame.annexB, isKeyFrame ? 0x65 : 0x41, bw.data);

  hasReference_ = true;
  ++frameNum_;
}

}  // namespace NXCord
