// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <common.h>

namespace skyline::soc::host1x {
    /**
     * @brief The NVDEC Host1x class implements hardware accelerated video decoding for the VP9/VP8/H264/VC1 codecs
     */
     enum class VideoCodec : u64 {
         None = 0x0,
         H264 = 0x3,
         VP8 = 0x5,
         H265 = 0x7,
         VP9 = 0x9,
     };

    class NvDecClass {
      private:
        std::function<void()> opDoneCallback;
        const DeviceState &state;

      public:
        NvDecClass(std::function<void()> opDoneCallback, const DeviceState &state);

        #pragma pack(push, 1)
        union NvdecRegisters {
            static constexpr size_t NUM_REGS = 0x178;

            std::array<u64, NUM_REGS> raw;

            template<size_t Offset, typename Type>
            using Register = util::OffsetMember<Offset, Type, u64>;

            Register<0x80, VideoCodec> setCodecId;    // 0x400 / 8
            Register<0xC0, u64> execute;              // 0x600 / 8

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

        void CallMethod(u32 method, u32 argument);
    };
}
