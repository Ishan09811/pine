// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <common.h>
#include "common/scratch_buffer.h"
#include "nvdec.h"

struct SwsContext;

namespace skyline::soc::host1x {
    union VicConfig;
    /**
     * @brief The VIC Host1x class implements hardware accelerated image operations
     */
    class VicClass {
      private:
        NvDecClass nvDecClass;
        std::function<void()> opDoneCallback;
        const DeviceState &state;
        void Execute();
        void WriteRGBFrame(std::unique_ptr<FFmpeg::Frame> frame, const VicConfig& config);
        void WriteYUVFrame(std::unique_ptr<FFmpeg::Frame> frame, const VicConfig& config);
        using AVMallocPtr = std::unique_ptr<u8, decltype(&av_free)>;
        AVMallocPtr convertedFrameBuffer;
        ScratchBuffer<u8> lumaBuffer;
        ScratchBuffer<u8> chromaBuffer;

        u64 configStructAddress{};
        u64 outputSurfaceLumaAddress{};
        u64 outputSurfaceChromaAddress{};

        SwsContext* scalerCtx{};
        i32 scalerWidth{};
        i32 scalerHeight{};

      public:
        enum class Method : u32 {
            Execute = 0xc0,
            SetControlParams = 0x1c1,
            SetConfigStructOffset = 0x1c2,
            SetOutputSurfaceLumaOffset = 0x1c8,
            SetOutputSurfaceChromaOffset = 0x1c9,
            SetOutputSurfaceChromaUnusedOffset = 0x1ca
        };

        VicClass(std::function<void()> opDoneCallback, const DeviceState &state, NvDecClass nvDecClass);

        void CallMethod(Method method, u32 argument);
    };
}
