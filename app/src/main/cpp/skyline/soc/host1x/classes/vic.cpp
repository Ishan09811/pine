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

    VicClass::VicClass(std::function<void()> opDoneCallback, const DeviceState &state, NvDecClass nvDecClass)
        : opDoneCallback(std::move(opDoneCallback)), state(state), nvDecClass(nvDecClass) {}

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
        auto frame = nvDecClass.GetFrame();
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
        }
    }

    void Vic::WriteRGBFrame(std::unique_ptr<FFmpeg::Frame> frame, const VicConfig& config) {
        LOGD("Writing RGB Frame");

        const auto frameWidth = frame->GetWidth();
        const auto frameHeight = frame->GetHeight();
        const auto frameFormat = frame->GetPixelFormat();

        if (!scalerCtx || frameWidth != scalerWidth || frameHeight != scalerHeight) {
            const AVPixelFormat targetFormat = [pixelFormat = config.pixelFormat]() {
                switch (pixelFormat) {
                    case VideoPixelFormat::RGBA8:
                        return AV_PIX_FMT_RGBA;
                    case VideoPixelFormat::BGRA8:
                        return AV_PIX_FMT_BGRA;
                    case VideoPixelFormat::RGBX8:
                        return AV_PIX_FMT_RGB0;
                    default:
                        return AV_PIX_FMT_RGBA;
                }
            }();

            sws_freeContext(scalerCtx);
            // Frames are decoded into either YUV420 or NV12 formats. Convert to desired RGB format
            scalerCtx = sws_getContext(frameWidth, frameHeight, frameFormat, frameWidth,
                                    frameHeight, targetFormat, 0, nullptr, nullptr, nullptr);
            scalerWidth = frameWidth;
            scalerHeight = frameHeight;
            convertedFrameBuffer.reset();
        }
        if (!convertedFrameBuffer) {
            const size_t frameSize = frameWidth * frameHeight * 4;
            convertedFrameBuffer = AVMallocPtr{static_cast<u8*>(av_malloc(frameSize)), av_free};
        }
        const std::array<int, 4> convertedStride{frameWidth * 4, frameHeight * 4, 0, 0};
        u8* const convertedFrameBufAddr{convertedFrameBuffer.get()};
        sws_scale(scalerCtx, frame->GetPlanes(), frame->GetStrides(), 0, frameHeight,
                  &convertedFrameBufAddr, convertedStride.data());

        // Use the minimum of surface/frame dimensions to avoid buffer overflow.
        const u32 surfaceWidth = static_cast<u32>(config.surfaceWidthMinus1) + 1;
        const u32 surfaceHeight = static_cast<u32>(config.surfaceHeightMinus1) + 1;
        const u32 width = std::min(surfaceWidth, static_cast<u32>(frameWidth));
        const u32 height = std::min(surfaceHeight, static_cast<u32>(frameHeight));
        const u32 blkKind = static_cast<u32>(config.blockLinearKind);
        if (blkKind != 0) {
            // swizzle pitch linear to block linear
            const u32 blockHeight = static_cast<u32>(config.blockLinearHeightLog2);
            const auto size = Texture::CalculateSize(true, 4, width, height, 1, blockHeight, 0);
            lumaBuffer.resize_destructive(size);
            std::span<const u8> frameBuff(convertedFrameBufAddr, 4 * width * height);
            Texture::SwizzleSubrect(lumaBuffer, frameBuff, 4, width, height, 1, 0, 0, width, height,
                                    blockHeight, 0, width * 4);

            //state.soc->smmu.WriteBlock(outputSurfaceLumaAddress, lumaBuffer.data(), size);
        } else {
            // send pitch linear frame
            const size_t linearSize = width * height * 4;
            /*state.soc->smmu.WriteBlock(outputSurfaceLumaAddress, convertedFrameBufAddr,
                                     linearSize);*/
        }
    }
} // namespace skyline
