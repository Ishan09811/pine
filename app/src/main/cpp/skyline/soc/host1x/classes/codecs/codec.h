// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <queue>
#include "common/base.h"
#include "soc/host1x/ffmpeg/ffmpeg.h"

namespace skyline::soc::host1x {

namespace Decoder {
class H264;
} // namespace Decoder

class Codec {
public:
    explicit Codec(const DeviceState &state, const NvdecRegisters& regs);
    ~Codec();

    /// Initialize the codec, returning success or failure
    void Initialize();

    /// Sets NVDEC video stream codec
    void SetTargetCodec(VideoCodec codec);

    /// Call decoders to construct headers, decode AVFrame with ffmpeg
    void Decode();

    /// Returns next decoded frame
    [[nodiscard]] std::unique_ptr<FFmpeg::Frame> GetCurrentFrame();

    /// Returns the value of current_codec
    [[nodiscard]] VideoCodec GetCurrentCodec() const;

    /// Return name of the current codec
    [[nodiscard]] std::string_view GetCurrentCodecName() const;

private:
    bool initialized{};
    VideoCodec current_codec{VideoCodec::None};
    FFmpeg::DecodeApi decodeAPI;

    const DeviceState &deviceState;
    const NvdecRegisters& state;
    std::unique_ptr<Decoder::H264> h264Decoder;

    std::queue<std::unique_ptr<FFmpeg::Frame>> frames{};
};

} // namespace skyline
