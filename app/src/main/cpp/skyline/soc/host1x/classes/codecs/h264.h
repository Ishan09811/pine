// SPDX-FileCopyrightText: Ryujinx Team and Contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <span>
#include <vector>
#include "common/base.h"
#include "common/scratch_buffer.h"

namespace skyline::soc::host1x {

namespace Decoder {

class H264BitWriter {
public:
    H264BitWriter();
    ~H264BitWriter();

    /// The following Write methods are based on clause 9.1 in the H.264 specification.
    /// WriteSe and WriteUe write in the Exp-Golomb-coded syntax
    void WriteU(i32 value, i32 value_sz);
    void WriteSe(i32 value);
    void WriteUe(u32 value);

    /// Finalize the bitstream
    void End();

    /// append a bit to the stream, equivalent value to the state parameter
    void WriteBit(bool state);

    /// Based on section 7.3.2.1.1.1 and Table 7-4 in the H.264 specification
    /// Writes the scaling matrices of the sream
    void WriteScalingList(ScratchBuffer<u8>& scan, std::span<const u8> list, i32 start,
                          i32 count);

    /// Return the bitstream as a vector.
    [[nodiscard]] std::vector<u8>& GetByteArray();
    [[nodiscard]] const std::vector<u8>& GetByteArray() const;

private:
    void WriteBits(i32 value, i32 bit_count);
    void WriteExpGolombCodedInt(i32 value);
    void WriteExpGolombCodedUInt(u32 value);
    [[nodiscard]] i32 GetFreeBufferBits();
    void Flush();

    s32 buffer_size{8};

    s32 buffer{};
    s32 buffer_pos{};
    std::vector<u8> byte_array;
};

class H264 {
public:
    explicit H264(const DeviceState &state);
    ~H264();

    /// Compose the H264 frame for FFmpeg decoding
    [[nodiscard]] std::span<const u8> ComposeFrame(const NvdecRegisters& state,
                                                   size_t* outConfigurationSize,
                                                   bool isFirstFrame = false);

private:
    ScratchBuffer<u8> frame;
    ScratchBuffer<u8> scan;
    const DeviceState &deviceState;

    struct H264ParameterSet {
        i32 log2_max_pic_order_cnt_lsb_minus4; ///< 0x00
        i32 delta_pic_order_always_zero_flag;  ///< 0x04
        i32 frame_mbs_only_flag;               ///< 0x08
        u32 pic_width_in_mbs;                  ///< 0x0C
        u32 frame_height_in_map_units;         ///< 0x10

        u32 tileGobRaw;                        ///< 0x14

        u32 entropy_coding_mode_flag;               ///< 0x18
        i32 pic_order_present_flag;                 ///< 0x1C
        i32 num_refidx_l0_default_active;           ///< 0x20
        i32 num_refidx_l1_default_active;           ///< 0x24
        i32 deblocking_filter_control_present_flag; ///< 0x28
        i32 redundant_pic_cnt_present_flag;         ///< 0x2C
        u32 transform_8x8_mode_flag;                ///< 0x30
        u32 pitch_luma;                             ///< 0x34
        u32 pitch_chroma;                           ///< 0x38
        u32 luma_top_offset;                        ///< 0x3C
        u32 luma_bot_offset;                        ///< 0x40
        u32 luma_frame_offset;                      ///< 0x44
        u32 chroma_top_offset;                      ///< 0x48
        u32 chroma_bot_offset;                      ///< 0x4C
        u32 chroma_frame_offset;                    ///< 0x50
        u32 hist_buffer_size;                       ///< 0x54

        u64 psFlags;                                ///< 0x58

        u32 getTileFormat() const { return (tileGobRaw >> 0) & 0b11; }
        void setTileFormat(u32 v) {
            tileGobRaw = (tileGobRaw & ~(0b11u << 0)) | ((v & 0b11u) << 0);
        }

        u32 getGobHeight() const { return (tileGobRaw >> 2) & 0b111; }
        void setGobHeight(u32 v) {
            tileGobRaw = (tileGobRaw & ~(0b111u << 2)) | ((v & 0b111u) << 2);
        }

        bool getMbaffFrame() const { return (psFlags >> 0) & 1; }
        void setMbaffFrame(bool v) { psFlags = (psFlags & ~(1ULL << 0)) | (u64(v) << 0); }

        bool getDirect8x8Inference() const { return (psFlags >> 1) & 1; }
        void setDirect8x8Inference(bool v) { psFlags = (psFlags & ~(1ULL << 1)) | (uint64_t(v) << 1); }

        bool getWeightedPred() const { return (psFlags >> 2) & 1; }
        void setWeightedPred(bool v) { psFlags = (psFlags & ~(1ULL << 2)) | (u64(v) << 2); }

        bool getConstrainedIntraPred() const { return (psFlags >> 3) & 1; }
        void setConstrainedIntraPred(bool v) { psFlags = (psFlags & ~(1ULL << 3)) | (uint64_t(v) << 3); }

        bool getRefPic() const { return (psFlags >> 4) & 1; }
        void setRefPic(bool v) { psFlags = (psFlags & ~(1ULL << 4)) | (u64(v) << 4); }

        bool getFieldPic() const { return (psFlags >> 5) & 1; }
        void setFieldPic(bool v) { psFlags = (psFlags & ~(1ULL << 5)) | (u64(v) << 5); }

        bool getBottomField() const { return (psFlags >> 6) & 1; }
        void setBottomField(bool v) { psFlags = (psFlags & ~(1ULL << 6)) | (u64(v) << 6); }

        bool getSecondField() const { return (psFlags >> 7) & 1; }
        void setSecondField(bool v) { psFlags = (psFlags & ~(1ULL << 7)) | (u64(v) << 7); }

        u32 getLog2MaxFrameNumMinus4() const { return (psFlags >> 8) & 0xF; }
        void setLog2MaxFrameNumMinus4(u32 v) {
            psFlags = (psFlags & ~(0xFULL << 8)) | ((u64)(v & 0xF) << 8);
        }

        u32 getChromaFormatIdc() const { return (psFlags >> 12) & 0x3; }
        void setChromaFormatIdc(u32 v) {
            psFlags = (psFlags & ~(0x3ULL << 12)) | ((u64)(v & 0x3) << 12);
        }

        u32 getPicOrderCntType() const { return (psFlags >> 14) & 0x3; }
        void setPicOrderCntType(u32 v) {
            psFlags = (psFlags & ~(0x3ULL << 14)) | ((u64)(v & 0x3) << 14);
        }

        i32 getPicInitQpMinus26() const {
            return (i32)((psFlags >> 16) & 0x3F); // 6 bits signed
        }
        void setPicInitQpMinus26(i32) {
            psFlags = (psFlags & ~(0x3FULL << 16)) | ((u64)(v & 0x3F) << 16);
        }

        i32 getChromaQpIndexOffset() const {
            return (i32)((psFlags >> 22) & 0x1F);
        }
        void setChromaQpIndexOffset(i32 v) {
            psFlags = (psFlags & ~(0x1FULL << 22)) | ((u64)(v & 0x1F) << 22);
        }

        i32 getSecondChromaQpIndexOffset() const {
            return (i32)((psFlags >> 27) & 0x1F);
        }
        void setSecondChromaQpIndexOffset(i32 v) {
            psFlags = (psFlags & ~(0x1FULL << 27)) | ((u64)(v & 0x1F) << 27);
        }

        u32 getWeightedBipredIdc() const { return (psFlags >> 32) & 0x3; }
        void setWeightedBipredIdc(u32 v) {
            psFlags = (psFlags & ~(0x3ULL << 32)) | ((u64)(v & 0x3) << 32);
        }

        u32 getCurrPicIdx() const { return (psFlags >> 34) & 0x7F; }
        void setCurrPicIdx(u32 v) {
            psFlags = (psFlags & ~(0x7FULL << 34)) | ((u64)(v & 0x7F) << 34);
        }

        u32 getCurrColIdx() const { return (psFlags >> 41) & 0x1F; }
        void setCurrColIdx(u32 v) {
            psFlags = (psFlags & ~(0x1FULL << 41)) | ((u64)(v & 0x1F) << 41);
        }

        u32 getFrameNumber() const { return (psFlags >> 46) & 0xFFFF; }
        void setFrameNumber(u32 v) {
            psFlags = (psFlags & ~(0xFFFFULL << 46)) | ((u64)(v & 0xFFFF) << 46);
        }

        bool getFrameSurfaces() const { return (psFlags >> 62) & 1; }
        void setFrameSurfaces(bool v) {
            psFlags = (psFlags & ~(1ULL << 62)) | ((u64)v << 62);
        }

        bool getOutputMemoryLayout() const { return (psFlags >> 63) & 1; }
        void setOutputMemoryLayout(bool v) {
            psFlags = (psFlags & ~(1ULL << 63)) | ((u64)v << 63);
        }
    };
    static_assert(sizeof(H264ParameterSet) == 0x60, "H264ParameterSet is an invalid size");

    struct H264DecoderContext {
        u32 _pad0[18];                  // 0x0000
        u32 stream_len;                 // 0x0048
        u32 _pad1[3];                   // 0x004C
        H264ParameterSet h264_parameter_set; // 0x0058
        u32 _pad2[66];                  // 0x00B8
        std::array<u8, 0x60> weight_scale;       // 0x01C0
        std::array<u8, 0x80> weight_scale_8x8;   // 0x0220
    };
    static_assert(sizeof(H264DecoderContext) == 0x2A0);

#define ASSERT_POSITION(field_name, position)                                                      \
    static_assert(offsetof(H264ParameterSet, field_name) == position,                              \
                  "Field " #field_name " has invalid position")

    ASSERT_POSITION(log2_max_pic_order_cnt_lsb_minus4, 0x00);
    ASSERT_POSITION(delta_pic_order_always_zero_flag, 0x04);
    ASSERT_POSITION(frame_mbs_only_flag, 0x08);
    ASSERT_POSITION(pic_width_in_mbs, 0x0C);
    ASSERT_POSITION(frame_height_in_map_units, 0x10);
    ASSERT_POSITION(tile_format, 0x14);
    ASSERT_POSITION(entropy_coding_mode_flag, 0x18);
    ASSERT_POSITION(pic_order_present_flag, 0x1C);
    ASSERT_POSITION(num_refidx_l0_default_active, 0x20);
    ASSERT_POSITION(num_refidx_l1_default_active, 0x24);
    ASSERT_POSITION(deblocking_filter_control_present_flag, 0x28);
    ASSERT_POSITION(redundant_pic_cnt_present_flag, 0x2C);
    ASSERT_POSITION(transform_8x8_mode_flag, 0x30);
    ASSERT_POSITION(pitch_luma, 0x34);
    ASSERT_POSITION(pitch_chroma, 0x38);
    ASSERT_POSITION(luma_top_offset, 0x3C);
    ASSERT_POSITION(luma_bot_offset, 0x40);
    ASSERT_POSITION(luma_frame_offset, 0x44);
    ASSERT_POSITION(chroma_top_offset, 0x48);
    ASSERT_POSITION(chroma_bot_offset, 0x4C);
    ASSERT_POSITION(chroma_frame_offset, 0x50);
    ASSERT_POSITION(hist_buffer_size, 0x54);
    ASSERT_POSITION(psFlags, 0x58);
#undef ASSERT_POSITION

#define ASSERT_POSITION(field_name, position)                                                      \
    static_assert(offsetof(H264DecoderContext, field_name) == position,                            \
                  "Field " #field_name " has invalid position")

    ASSERT_POSITION(stream_len, 0x48);
    ASSERT_POSITION(h264_parameter_set, 0x58);
    ASSERT_POSITION(weight_scale, 0x1C0);
#undef ASSERT_POSITION
};

} // namespace Decoder
} // namespace skyline
