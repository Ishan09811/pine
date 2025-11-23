
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <queue>
#include <common/base.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

namespace FFmpeg {

class Packet;
class Frame;
class Decoder;
class HardwareContext;
class DecoderContext;
class DeinterlaceFilter;

// Wraps an AVPacket, a container for compressed bitstream data.
class Packet {
public:
    NON_COPYABLE(Packet);
    NON_MOVEABLE(Packet);

    explicit Packet(std::span<const uint8_t> data);
    ~Packet();

    AVPacket* GetPacket() const {
        return mPacket;
    }

private:
    AVPacket* mPacket{};
};

// Wraps an AVFrame, a container for audio and video stream data.
class Frame {
public:
    NON_COPYABLE(Frame);
    NON_MOVEABLE(Frame);

    explicit Frame();
    ~Frame();

    int GetWidth() const {
        return mFrame->width;
    }

    int GetHeight() const {
        return mFrame->height;
    }

    AVPixelFormat GetPixelFormat() const {
        return static_cast<AVPixelFormat>(mFrame->format);
    }

    int GetStride(int plane) const {
        return mFrame->linesize[plane];
    }

    int* GetStrides() const {
        return mFrame->linesize;
    }

    uint8_t* GetData(int plane) const {
        return mFrame->data[plane];
    }

    uint8_t* GetPlanes() const {
        return mFrame->data;
    }

    void SetFormat(int format) {
        mFrame->format = format;
    }

    AVFrame* GetFrame() const {
        return mFrame;
    }

private:
    AVFrame* mFrame{};
};

// Wraps an AVCodec, a type containing information about a codec.
class Decoder {
public:
    NON_COPYABLE(Decoder);
    NON_MOVEABLE(Decoder);

    explicit Decoder(skyline::soc::host1x::VideoCodec codec);
    ~Decoder() = default;

    bool SupportsDecodingOnDevice(AVPixelFormat* outPixFmt, AVHWDeviceType type) const;

    const AVCodec* GetCodec() const {
        return mCodec;
    }

private:
    const AVCodec* mCodec{};
};

// Wraps AVBufferRef for an accelerated decoder.
class HardwareContext {
public:
    NON_COPYABLE(HardwareContext);
    NON_MOVEABLE(HardwareContext);

    static std::vector<AVHWDeviceType> GetSupportedDeviceTypes();

    explicit HardwareContext() = default;
    ~HardwareContext();

    bool InitializeForDecoder(DecoderContext& decoderContext, const Decoder& decoder);

    AVBufferRef* GetBufferRef() const {
        return mGPUDecoder;
    }

private:
    bool InitializeWithType(AVHWDeviceType type);

    AVBufferRef* mGPUDecoder{};
};

// Wraps an AVCodecContext.
class DecoderContext {
public:
    NON_COPYABLE(DecoderContext);
    NON_MOVEABLE(DecoderContext);

    explicit DecoderContext(const Decoder& decoder);
    ~DecoderContext();

    void InitializeHardwareDecoder(const HardwareContext& context, AVPixelFormat hwPixFmt);
    bool OpenContext(const Decoder& decoder);
    bool SendPacket(const Packet& packet);
    std::unique_ptr<Frame> ReceiveFrame(bool* outIsInterlaced);

    AVCodecContext* GetCodecContext() const {
        return mCodecContext;
    }

private:
    AVCodecContext* mCodecContext{};
};

// Wraps an AVFilterGraph.
class DeinterlaceFilter {
public:
    NON_COPYABLE(DeinterlaceFilter);
    NON_MOVEABLE(DeinterlaceFilter);

    explicit DeinterlaceFilter(const Frame& frame);
    ~DeinterlaceFilter();

    bool AddSourceFrame(const Frame& frame);
    std::unique_ptr<Frame> DrainSinkFrame();

private:
    AVFilterGraph* mFilterGraph{};
    AVFilterContext* mSourceContext{};
    AVFilterContext* mSinkContext{};
    bool mInitialized{};
};

class DecodeApi {
public:
    NON_COPYABLE(DecodeApi);
    NON_MOVEABLE(DecodeApi);

    DecodeApi() = default;
    ~DecodeApi() = default;

    bool Initialize(skyline::soc::host1x::VideoCodec codec);
    void Reset();

    bool SendPacket(std::span<const uint8_t> packetData, size_t configurationSize);
    void ReceiveFrames(std::queue<std::unique_ptr<Frame>>& frameQueue);

private:
    std::optional<FFmpeg::Decoder> mDecoder;
    std::optional<FFmpeg::DecoderContext> mDecoderContext;
    std::optional<FFmpeg::HardwareContext> mHardwareContext;
    std::optional<FFmpeg::DeinterlaceFilter> mDeinterlaceFilter;
};

} // namespace FFmpeg
