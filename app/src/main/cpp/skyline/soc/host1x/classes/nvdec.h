// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <common.h>
#include "common/bit_field.h"
#include "soc/host1x/classes/nvdec_common.h"
#include "soc/host1x/classes/codecs/codec.h"

namespace skyline::soc::host1x {
    /**
     * @brief The NVDEC Host1x class implements hardware accelerated video decoding for the VP9/VP8/H264/VC1 codecs
     */
    class NvDecClass {
      private:
        std::function<void()> opDoneCallback;
        std::unique_ptr<Codec> codec;
        NvdecRegisters registers{};
        void Execute();

      public:
        NvDecClass(std::function<void()> opDoneCallback, const DeviceState &state);
        void CallMethod(u32 method, u32 argument);
        [[nodiscard]] std::unique_ptr<FFmpeg::Frame> GetFrame();
    };
}
