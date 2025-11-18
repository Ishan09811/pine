// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "nvdec.h"

namespace skyline::soc::host1x {
    #define NVDEC_INDEX(fieldName)                                                                \
        (offsetof(NvdecRegisters, fieldName) / sizeof(u64))

    NvDecClass::NvDecClass(std::function<void()> opDoneCallback)
        : opDoneCallback(std::move(opDoneCallback)) {}

    void NvDecClass::CallMethod(u32 method, u32 argument) {
        LOGW("Unknown NVDEC class method called: 0x{:X} argument: 0x{:X}", method, argument);
        registers.raw[method] = static_cast<u64>(argument) << 8;

        switch (method) {
            case NVDEC_INDEX(set_codec_id):
                //TODO: Implement decoder
                break;
            case NVDEC_INDEX(execute):
                //TODO: Execute
                break;
        }
    }
}
