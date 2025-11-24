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
        BitField<0, 7, VideoPixelFormat> pixel_format;
        BitField<7, 2, u64_le> chroma_loc_horiz;
        BitField<9, 2, u64_le> chroma_loc_vert;
        BitField<11, 4, u64_le> block_linear_kind;
        BitField<15, 4, u64_le> block_linear_height_log2;
        BitField<32, 14, u64_le> surface_width_minus1;
        BitField<46, 14, u64_le> surface_height_minus1;
    };

    VicClass::VicClass(std::function<void()> opDoneCallback, const DeviceState &state)
        : opDoneCallback(std::move(opDoneCallback)), state(state) {}

    void VicClass::CallMethod(u32 method, u32 argument) {
        LOGW("Unknown VIC class method called: 0x{:X} argument: 0x{:X}", method, argument);
        const u64 arg = static_cast<u64>(argument) << 8;
        /*switch (method) {
            case Method::Execute:
                Execute();
                break;
            case Method::SetConfigStructOffset:
                config_struct_address = arg;
                break;
            case Method::SetOutputSurfaceLumaOffset:
                output_surface_luma_address = arg;
                break;
            case Method::SetOutputSurfaceChromaOffset:
                output_surface_chroma_address = arg;
                break;
            default:
                break;
        }*/
    }

    void Vic::Execute() {
        if (output_surface_luma_address == 0) {
            LOGE("VIC Luma address not set.");
            return;
        }
        const VicConfig config{state.soc->smmu.Read<u64>(config_struct_address + 0x20)};
        /*auto frame = nvdec_processor->GetFrame();
        if (!frame) {
            return;
        }
        const u64 surface_width = config.surface_width_minus1 + 1;
        const u64 surface_height = config.surface_height_minus1 + 1;
        if (static_cast<u64>(frame->GetWidth()) != surface_width ||
            static_cast<u64>(frame->GetHeight()) != surface_height) {
            // TODO: Properly support multiple video streams with differing frame dimensions
            LOGW("Frame dimensions {}x{} don't match surface dimensions {}x{}",
                        frame->GetWidth(), frame->GetHeight(), surface_width, surface_height);
        }
        switch (config.pixel_format) {
            case VideoPixelFormat::RGBA8:
            case VideoPixelFormat::BGRA8:
            case VideoPixelFormat::RGBX8:
                WriteRGBFrame(std::move(frame), config);
                break;
            case VideoPixelFormat::YUV420:
                WriteYUVFrame(std::move(frame), config);
                break;
        default:
            LOGW("Unknown video pixel format {:X}", config.pixel_format.Value());
            break;
        }*/
    }
}
