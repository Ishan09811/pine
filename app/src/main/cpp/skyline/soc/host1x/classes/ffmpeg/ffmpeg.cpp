// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright © 2025 Pine (https://github.com/Ishan09811/pine)
// SPDX-License-Identifier: GPL-3.0

#include <common/base.h>
#include "logger/logger.h"
#include "soc/host1x/classes/ffmpeg/ffmpeg.h"

namespace FFmpeg {

namespace {

constexpr AVPixelFormat PreferredGPUFormat = AV_PIX_FMT_MEDIACODEC;
constexpr AVPixelFormat PreferredCPUFormat = AV_PIX_FMT_YUV420P;
constexpr std::array PreferredGPUDecoders = {
    AV_HWDEVICE_TYPE_MEDIACODEC,
};

AVPixelFormat GetGPUFormat(AVCodecContext* codecContext, const AVPixelFormat* pixFmts) {
    for (const AVPixelFormat* p = pixFmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == codecContext->pix_fmt) {
            return codecContext->pix_fmt;
        }
    }

    LOGI("Could not find compatible GPU AV format, falling back to CPU");
    av_buffer_unref(&codecContext->hw_device_ctx);

    codecContext->pix_fmt = PreferredCPUFormat;
    return codecContext->pix_fmt;
}

std::string AVError(int errnum) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_make_error_string(errbuf, sizeof(errbuf) - 1, errnum);
    return errbuf;
}

} // namespace

Packet::Packet(std::span<const u8> data) {
    mPacket = av_packet_alloc();
    mPacket->data = const_cast<u8*>(data.data());
    mPacket->size = static_cast<s32>(data.size());
}

Packet::~Packet() {
    av_packet_free(&mPacket);
}

Frame::Frame() {
    mFrame = av_frame_alloc();
}

Frame::~Frame() {
    av_frame_free(&mFrame);
}

Decoder::Decoder(skyline::soc::host1x::VideoCodec codec) {
    const AVCodecID avCodec = [&] {
        switch (codec) {
        case skyline::soc::host1x::VideoCodec::H264:
            return AV_CODEC_ID_H264;
        default:
            LOGW("Unknown codec {}", codec);
            return AV_CODEC_ID_NONE;
        }
    }();

    mCodec = avcodec_find_decoder(avCodec);
}

bool Decoder::SupportsDecodingOnDevice(AVPixelFormat* outPixFmt, AVHWDeviceType type) const {
    for (int i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(mCodec, i);
        if (!config) {
            LOGD("{} decoder does not support device type {}", mCodec->name,
                      av_hwdevice_get_type_name(type));
            break;
        }
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0 &&
            config->device_type == type) {
            LOGI("Using {} GPU decoder", av_hwdevice_get_type_name(type));
            *outPixFmt = config->pix_fmt;
            return true;
        }
    }

    return false;
}

std::vector<AVHWDeviceType> HardwareContext::GetSupportedDeviceTypes() {
    std::vector<AVHWDeviceType> types;
    AVHWDeviceType currentDeviceType = AV_HWDEVICE_TYPE_NONE;

    while (true) {
        currentDeviceType = av_hwdevice_iterate_types(currentDeviceType);
        if (currentDeviceType == AV_HWDEVICE_TYPE_NONE) {
            return types;
        }

        types.push_back(currentDeviceType);
    }
}

HardwareContext::~HardwareContext() {
    av_buffer_unref(&mGPUDecoder);
}

bool HardwareContext::InitializeForDecoder(DecoderContext& decoderContext,
                                           const Decoder& decoder) {
    const auto supportedTypes = GetSupportedDeviceTypes();
    for (const auto type : PreferredGPUDecoders) {
        AVPixelFormat hwPixFmt;

        if (std::ranges::find(supportedTypes, type) == supportedTypes.end()) {
            LOGD("{} explicitly unsupported", av_hwdevice_get_type_name(type));
            continue;
        }

        if (!this->InitializeWithType(type)) {
            continue;
        }

        if (decoder.SupportsDecodingOnDevice(&hwPixFmt, type)) {
            decoderContext.InitializeHardwareDecoder(*this, hwPixFmt);
            return true;
        }
    }

    return false;
}

bool HardwareContext::InitializeWithType(AVHWDeviceType type) {
    av_buffer_unref(&mGPUDecoder);

    if (const int ret = av_hwdevice_ctx_create(&mGPUDecoder, type, nullptr, nullptr, 0);
        ret < 0) {
        LOGD("av_hwdevice_ctx_create({}) failed: {}", av_hwdevice_get_type_name(type),
                  AVError(ret));
        return false;
    }

    return true;
}

DecoderContext::DecoderContext(const Decoder& decoder) {
    mCodecContext = avcodec_alloc_context3(decoder.GetCodec());
    av_opt_set(mCodecContext->priv_data, "tune", "zerolatency", 0);
    mCodecContext->thread_count = 0;
    mCodecContext->thread_type &= ~FF_THREAD_FRAME;
}

DecoderContext::~DecoderContext() {
    av_buffer_unref(&mCodecContext->hw_device_ctx);
    avcodec_free_context(&mCodecContext);
}

void DecoderContext::InitializeHardwareDecoder(const HardwareContext& context,
                                               AVPixelFormat hwPixFmt) {
    mCodecContext->hw_device_ctx = av_buffer_ref(context.GetBufferRef());
    mCodecContext->get_format = GetGPUFormat;
    mCodecContext->pix_fmt = hwPixFmt;
}

bool DecoderContext::OpenContext(const Decoder& decoder) {
    if (const int ret = avcodec_open2(mCodecContext, decoder.GetCodec(), nullptr); ret < 0) {
        LOGE("avcodec_open2 error: {}", AVError(ret));
        return false;
    }

    if (!mCodecContext->hw_device_ctx) {
        LOGI("Using FFmpeg software decoding");
    }

    return true;
}

bool DecoderContext::SendPacket(const Packet& packet) {
    if (const int ret = avcodec_send_packet(mCodecContext, packet.GetPacket()); ret < 0) {
        LOGE("avcodec_send_packet error: {}", AVError(ret));
        return false;
    }

    return true;
}

std::unique_ptr<Frame> DecoderContext::ReceiveFrame(bool* outIsInterlaced) {
    auto dstFrame = std::make_unique<Frame>();

    const auto ReceiveImpl = [&](AVFrame* frame) {
        if (const int ret = avcodec_receive_frame(mCodecContext, frame); ret < 0) {
            LOGE("avcodec_receive_frame error: {}", AVError(ret));
            return false;
        }

        *outIsInterlaced =
#if defined(FF_API_INTERLACED_FRAME) || LIBAVUTIL_VERSION_MAJOR >= 59
            (frame->flags & AV_FRAME_FLAG_INTERLACED) != 0;
#else
            frame->interlaced_frame != 0;
#endif
        return true;
    };

    if (mCodecContext->hw_device_ctx) {
        // If we have a hardware context, make a separate frame here to receive the
        // hardware result before sending it to the output.
        Frame intermediateFrame;

        if (!ReceiveImpl(intermediateFrame.GetFrame())) {
            return {};
        }

        dstFrame->SetFormat(PreferredGPUFormat);
        if (const int ret =
                av_hwframe_transfer_data(dstFrame->GetFrame(), intermediateFrame.GetFrame(), 0);
            ret < 0) {
            LOGE("av_hwframe_transfer_data error: {}", AVError(ret));
            return {};
        }
    } else {
        // Otherwise, decode the frame as normal.
        if (!ReceiveImpl(dstFrame->GetFrame())) {
            return {};
        }
    }

    return dstFrame;
}

DeinterlaceFilter::DeinterlaceFilter(const Frame& frame) {
    const AVFilter* bufferSrc = avfilter_get_by_name("buffer");
    const AVFilter* bufferSink = avfilter_get_by_name("buffersink");
    auto inputs = std::unique_ptr<AVFilterInOut, decltype(&avfilter_inout_free)>(
        avfilter_inout_alloc(), &avfilter_inout_free
    );

    auto outputs = std::unique_ptr<AVFilterInOut, decltype(&avfilter_inout_free)>(
        avfilter_inout_alloc(), &avfilter_inout_free
    );


    // Don't know how to get the accurate time_base but it doesn't matter for yadif filter
    // so just use 1/1 to make buffer filter happy
    std::string args = fmt::format("video_size={}x{}:pix_fmt={}:time_base=1/1", frame.GetWidth(),
                                   frame.GetHeight(), static_cast<int>(frame.GetPixelFormat()));

    mFilterGraph = avfilter_graph_alloc();
    int ret = avfilter_graph_create_filter(&mSourceContext, bufferSrc, "in", args.c_str(),
                                           nullptr, mFilterGraph);
    if (ret < 0) {
        LOGE("avfilter_graph_create_filter source error: {}", AVError(ret));
        return;
    }

    ret = avfilter_graph_create_filter(&mSinkContext, bufferSink, "out", nullptr, nullptr,
                                       mFilterGraph);
    if (ret < 0) {
        LOGE("avfilter_graph_create_filter sink error: {}", AVError(ret));
        return;
    }

    inputs->name = av_strdup("out");
    inputs->filter_ctx = mSinkContext;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    outputs->name = av_strdup("in");
    outputs->filter_ctx = mSourceContext;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    const char* description = "yadif=1:-1:0";
    ret = avfilter_graph_parse_ptr(mFilterGraph, description, &inputs.get(), &outputs.get(), nullptr);
    if (ret < 0) {
        LOGE("avfilter_graph_parse_ptr error: {}", AVError(ret));
        return;
    }

    ret = avfilter_graph_config(mFilterGraph, nullptr);
    if (ret < 0) {
        LOGE("avfilter_graph_config error: {}", AVError(ret));
        return;
    }

    m_initialized = true;
}

bool DeinterlaceFilter::AddSourceFrame(const Frame& frame) {
    if (const int ret = av_buffersrc_add_frame_flags(mSourceContext, frame.GetFrame(),
                                                     AV_BUFFERSRC_FLAG_KEEP_REF);
        ret < 0) {
        LOGE("av_buffersrc_add_frame_flags error: {}", AVError(ret));
        return false;
    }

    return true;
}

std::unique_ptr<Frame> DeinterlaceFilter::DrainSinkFrame() {
    auto dstFrame = std::make_unique<Frame>();
    const int ret = av_buffersink_get_frame(mSinkContext, dstFrame->GetFrame());

    if (ret == AVERROR(EAGAIN) || ret == AVERROR(AVERROR_EOF)) {
        return {};
    }

    if (ret < 0) {
        LOGE("av_buffersink_get_frame error: {}", AVError(ret));
        return {};
    }

    return dstFrame;
}

DeinterlaceFilter::~DeinterlaceFilter() {
    avfilter_graph_free(&mFilterGraph);
}

void DecodeApi::Reset() {
    mDeinterlaceFilter.reset();
    mHardwareContext.reset();
    mDecoderContext.reset();
    mDecoder.reset();
}

bool DecodeApi::Initialize(Tegra::Host1x::NvdecCommon::VideoCodec codec) {
    this->Reset();
    mDecoder.emplace(codec);
    mDecoderContext.emplace(*mDecoder);

   // TODO: investigate gpu decoding
   // m_hardware_context.emplace();
   // m_hardware_context->InitializeForDecoder(*m_decoder_context, *m_decoder);

    // Open the decoder context.
    if (!mDecoderContext->OpenContext(*mDecoder)) {
        this->Reset();
        return false;
    }

    return true;
}

bool DecodeApi::SendPacket(std::span<const u8> packetData, size_t configurationSize) {
    FFmpeg::Packet packet(packetData);
    return mDecoderContext->SendPacket(packet);
}

void DecodeApi::ReceiveFrames(std::queue<std::unique_ptr<Frame>>& frameQueue) {
    // Receive raw frame from decoder.
    bool isInterlaced;
    auto frame = mDecoderContext->ReceiveFrame(&isInterlaced);
    if (!frame) {
        return;
    }

    if (!isInterlaced) {
        // If the frame is not interlaced, we can pend it now.
        frameQueue.push(std::move(frame));
    } else {
        // Create the deinterlacer if needed.
        if (!mDeinterlaceFilter) {
            mDeinterlaceFilter.emplace(*frame);
        }

        // Add the frame we just received.
        if (!mDeinterlaceFilter->AddSourceFrame(*frame)) {
            return;
        }

        // Pend output fields.
        while (true) {
            auto filterFrame = mDeinterlaceFilter->DrainSinkFrame();
            if (!filterFrame) {
                break;
            }

            frameQueue.push(std::move(filterFrame));
        }
    }
}

} // namespace FFmpeg
