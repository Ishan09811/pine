// SPDX-FileCopyrightText: Copyright 2025 Pine Emulator Project
// SPDX-License-Identifier: GPL-3.0

#pragma once

#include "common/utils.h"
#include "common/bit_field.h"
#include "common/base.h"

namespace skyline::soc::host1x {
    enum class VideoCodec : u64 {
         None = 0x0,
         H264 = 0x3,
         VP8 = 0x5,
         H265 = 0x7,
         VP9 = 0x9,
    };

    #pragma pack(push, 1)
    union NvdecRegisters {
        static constexpr size_t NUM_REGS = 0x178;

        std::array<u64, NUM_REGS> raw;

        template<size_t Offset, typename Type>
        using Register = util::OffsetMember<Offset, Type, u64>;

        Register<0x80, VideoCodec> setCodecId;    // 0x400 / 8
        Register<0xC0, u64> execute;              // 0x600 / 8

        struct ControlParams {                          //< 0x0800
            union {
                BitField<0, 3, VideoCodec> codec;
                BitField<4, 1, u64> gpTimerOn;
                BitField<13, 1, u64> mbTimerOn;
                BitField<14, 1, u64> intraFramePslc;
                BitField<17, 1, u64> allIntraFrame;
            };
        };
        static_assert(sizeof(ControlParams) == 8);

        Register<0x100, ControlParams> controlParams;     // 0x800 / 8
        Register<0x101, u64> pictureInfoOffset;           // 0x808
        Register<0x102, u64> frameBitstreamOffset;        // 0x810
        Register<0x103, u64> frameNumber;                 // 0x818
        Register<0x104, u64> h264SliceDataOffsets;        // 0x820
        Register<0x105, u64> h264MvDumpOffset;            // 0x828

        Register<0x109, u64> frameStatsOffset;            // 0x848
        Register<0x10A, u64> h264LastSurfaceLumaOffset;   // 0x850
        Register<0x10B, u64> h264LastSurfaceChromaOffset; // 0x858

        Register<0x10C, std::array<u64,17>> surfaceLumaOffset;   // 0x860
        Register<0x11D, std::array<u64,17>> surfaceChromaOffset; // 0x8E8

        // VP8
        Register<0x150, u64> vp8ProbDataOffset;                // 0xA80
        Register<0x151, u64> vp8HeaderPartitionOffset;         // 0xA88

        // VP9
        Register<0x160, u64> vp9EntropyProbsOffset;            // 0xB80
        Register<0x161, u64> vp9BackwardUpdatesOffset;         // 0xB88
        Register<0x162, u64> vp9LastFrameSegmapOffset;         // 0xB90
        Register<0x163, u64> vp9CurrFrameSegmapOffset;         // 0xB98
        Register<0x165, u64> vp9LastFrameMvsOffset;            // 0xBA8
        Register<0x166, u64> vp9CurrFrameMvsOffset;            // 0xBB0

    } registers{};
    static_assert(sizeof(NvdecRegisters) == 0xBC0);
    #pragma pack(pop)
} // namespace skyline
