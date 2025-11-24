
#include "soc/host1x/classes/codecs/codec.h"
#include "logger/logger.h"
#include "soc/host1x/classes/codecs/h264.h"

namespace skyline::soc::host1x {

Codec::Codec(const DeviceState &state, const NvdecRegisters& regs)
    : deviceState(state), state{regs}, h264Decoder(std::make_unique<Decoder::H264>(deviceState)) {}

Codec::~Codec() = default;

void Codec::Initialize() {
    initialized = decodeAPI.Initialize(currentCodec);
}

void Codec::SetTargetCodec(VideoCodec codec) {
    if (currentCodec != codec) {
        currentCodec = codec;
        LOGI("NVDEC video codec initialized to {}", GetCurrentCodecName());
    }
}

void Codec::Decode() {
    const bool isFirstFrame = !initialized;
    if (isFirstFrame) {
        Initialize();
    }

    if (!initialized) {
        return;
    }

    // Assemble bitstream.
    size_t configurationSize = 0;
    const auto packetData = [&]() {
        switch (currentCodec) {
        case VideoCodec::H264:
            return h264Decoder->ComposeFrame(state, &configurationSize, isFirstFrame);
        default:
            return std::span<const u8>{};
        }
    }();

    // Send assembled bitstream to decoder.
    if (!decodeAPI.SendPacket(packetData, configurationSize)) {
        LOGE("SendPacket: Failed")
        return;
    }

    // Receive output frames from decoder.
    decodeAPI.ReceiveFrames(frames);

    while (frames.size() > 10) {
        LOGD("ReceiveFrames overflow, dropped frame");
        frames.pop();
    }
}

std::unique_ptr<FFmpeg::Frame> Codec::GetCurrentFrame() {
    // Sometimes VIC will request more frames than have been decoded.
    // in this case, return a blank frame and don't overwrite previous data.
    if (frames.empty()) {
        return {};
    }

    auto frame = std::move(frames.front());
    frames.pop();
    return frame;
}

VideoCodec Codec::GetCurrentCodec() const {
    return currentCodec;
}

std::string_view Codec::GetCurrentCodecName() const {
    switch (currentCodec) {
    case VideoCodec::None:
        return "None";
    case VideoCodec::H264:
        return "H264";
    case VideoCodec::VP8:
        return "VP8";
    case VideoCodec::H265:
        return "H265";
    case VideoCodec::VP9:
        return "VP9";
    default:
        return "Unknown";
    }
}
} // namespace skyline
