// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "nvdec.h"

namespace skyline::soc::host1x {
    #define NVDEC_INDEX(fieldName) (decltype(registers.fieldName)::kOffset / sizeof(u64))

    NvDecClass::NvDecClass(std::function<void()> opDoneCallback, const DeviceState &state)
        : opDoneCallback(std::move(opDoneCallback)), codec(std::make_unique<Codec>(state, registers)) {}

    void NvDecClass::CallMethod(u32 method, u32 argument) {
        LOGW("NVDEC class method called: 0x{:X} argument: 0x{:X}", method, argument);
        registers.raw[method] = static_cast<u64>(argument) << 8;

        switch (method) {
            case NVDEC_INDEX(setCodecId):
                codec->SetTargetCodec(static_cast<VideoCodec>(argument));
                break;
            case NVDEC_INDEX(execute):
                Execute();
                break;
        }
    }

    std::unique_ptr<FFmpeg::Frame> NvDecClass::GetFrame() {
        return codec->GetCurrentFrame();
    }

    void NvDecClass::Execute() {
        switch (codec->GetCurrentCodec()) {
            case VideoCodec::H264:
                codec->Decode();
                break;
            default:
                LOGW("Codec {}", codec->GetCurrentCodecName());
                break;
        }
    }
}
