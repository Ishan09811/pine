// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "vic.h"
#include "soc.h"
#include "common/bit_field.h"
extern "C" {
#include <libswscale/swscale.h>
}

namespace skyline::soc::host1x {

    namespace {
        enum class VideoPixelFormat : u64_le {
            RGBA8 = 0x1f,
            BGRA8 = 0x20,
            RGBX8 = 0x23,
            YUV420 = 0x44,
        };
    } // Anonymous namespace

    union VicConfig {
        u64_le raw{};
        BitField<0, 7, VideoPixelFormat> pixelFormat;
        BitField<7, 2, u64_le> chromaLocHoriz;
        BitField<9, 2, u64_le> chromaLocVert;
        BitField<11, 4, u64_le> blockLinearKind;
        BitField<15, 4, u64_le> blockLinearHeightLog2;
        BitField<32, 14, u64_le> surfaceWidthMinus1;
        BitField<46, 14, u64_le> surfaceHeightMinus1;
    };

    VicClass::VicClass(std::function<void()> opDoneCallback, const DeviceState &state)
        : opDoneCallback(std::move(opDoneCallback)), state(state) {}

    void VicClass::CallMethod(Method method, u32 argument) {
        LOGW("VIC class method called: 0x{:X} argument: 0x{:X}", method, argument);
        const u64 arg = static_cast<u64>(argument) << 8;
        switch (method) {
            case Method::Execute:
                Execute();
                break;
            case Method::SetConfigStructOffset:
                configStructAddress = arg;
                break;
            case Method::SetOutputSurfaceLumaOffset:
                outputSurfaceLumaAddress = arg;
                break;
            case Method::SetOutputSurfaceChromaOffset:
                outputSurfaceChromaAddress = arg;
                break;
            default:
                break;
        }
    }

    void Vic::Execute() {
        if (outputSurfaceLumaAddress == 0) {
            LOGE("VIC Luma address not set.");
            return;
        }
        const VicConfig config{state.soc->smmu.Read<u64>(configStructAddress + 0x20)};
        /*auto frame = nvdec_processor->GetFrame();
        if (!frame) {
            return;
        }
        const u64 surfaceWidth = config.surfaceWidthMinus1 + 1;
        const u64 surfaceHeight = config.surfaceHeightMinus1 + 1;
        if (static_cast<u64>(frame->GetWidth()) != surfaceWidth ||
            static_cast<u64>(frame->GetHeight()) != surfaceHeight) {
            // TODO: Properly support multiple video streams with differing frame dimensions
            LOGW("Frame dimensions {}x{} don't match surface dimensions {}x{}",
                        frame->GetWidth(), frame->GetHeight(), surfaceWidth, surfaceHeight);
        }
        switch (config.pixelFormat) {
            case VideoPixelFormat::RGBA8:
            case VideoPixelFormat::BGRA8:
            case VideoPixelFormat::RGBX8:
                WriteRGBFrame(std::move(frame), config);
                break;
            case VideoPixelFormat::YUV420:
                WriteYUVFrame(std::move(frame), config);
                break;
        default:
            LOGW("Unknown video pixel format {:X}", config.pixelFormat.Value());
            break;
        }*/
    }
}
