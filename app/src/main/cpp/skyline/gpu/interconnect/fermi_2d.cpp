// SPDX-License-Identifier: MPL-2.0
// Copyright © 2022 Ryujinx Team and Contributors (https://github.com/ryujinx/)
// Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <gpu/texture/formats.h>
#include <gpu/texture/texture.h>
#include <gpu/texture_manager.h>
#include <soc/gm20b/gmmu.h>
#include <soc/gm20b/channel.h>
#include <vulkan/vulkan_enums.hpp>
#include "fermi_2d.h"
#include "gpu/texture/guest_texture.h"

namespace skyline::gpu::interconnect {
    using IOVA = soc::gm20b::IOVA;
    using MemoryLayout = skyline::soc::gm20b::engine::fermi2d::type::MemoryLayout;

    Fermi2D::FermiTexture Fermi2D::GetFermiTexture(const Surface &surface) {
        auto determineFormat = [&](Surface::SurfaceFormat format) -> skyline::gpu::texture::Format {
            #define FORMAT_CASE(fermiFmt, skFmt, fmtType) \
                case Surface::SurfaceFormat::fermiFmt ## fmtType: \
                    return skyline::gpu::format::skFmt ## fmtType

            #define FORMAT_SAME_CASE(fmt, type) FORMAT_CASE(fmt, fmt, type)

            switch (format) {
                FORMAT_SAME_CASE(R8, Unorm);
                FORMAT_SAME_CASE(R8, Snorm);

                FORMAT_SAME_CASE(R16, Unorm);
                FORMAT_SAME_CASE(R16, Snorm);
                FORMAT_SAME_CASE(R16, Float);

                FORMAT_SAME_CASE(R8G8, Unorm);
                FORMAT_SAME_CASE(R8G8, Snorm);

                FORMAT_SAME_CASE(B5G6R5, Unorm);

                FORMAT_SAME_CASE(B5G5R5A1, Unorm);

                FORMAT_SAME_CASE(R32, Float);

                FORMAT_SAME_CASE(B10G11R11, Float);

                FORMAT_SAME_CASE(R16G16, Unorm);
                FORMAT_SAME_CASE(R16G16, Snorm);
                FORMAT_SAME_CASE(R16G16, Float);

                FORMAT_SAME_CASE(R8G8B8A8, Unorm);
                FORMAT_SAME_CASE(R8G8B8A8, Srgb);

                FORMAT_CASE(R8G8B8X8, R8G8B8A8, Unorm);
                FORMAT_CASE(R8G8B8X8, R8G8B8A8, Snorm);
                FORMAT_CASE(R8G8B8X8, R8G8B8A8, Srgb);

                FORMAT_SAME_CASE(B8G8R8A8, Unorm);
                FORMAT_SAME_CASE(B8G8R8A8, Srgb);

                FORMAT_SAME_CASE(A2B10G10R10, Unorm);

                FORMAT_SAME_CASE(R32G32, Float);

                FORMAT_SAME_CASE(R16G16B16A16, Float);

                FORMAT_CASE(R16G16B16X16, R16G16B16A16, Unorm);
                FORMAT_CASE(R16G16B16X16, R16G16B16A16, Snorm);
                FORMAT_CASE(R16G16B16X16, R16G16B16A16, Float);

                FORMAT_SAME_CASE(R32G32B32A32, Float);

                FORMAT_CASE(R32G32B32X32, R32G32B32A32, Float);

                default:
                    throw exception("Cannot translate the supplied surface format: 0x{:X}", static_cast<u32>(format));
            }

            #undef FORMAT_CASE
            #undef FORMAT_SAME_CASE
        };

        texture::Format format{determineFormat(surface.format)};
        texture::Dimensions dimensions{};
        texture::TileConfig tileConfig{};


        u64 addressOffset{};
        if (surface.memoryLayout == MemoryLayout::Pitch) {
            dimensions = gpu::texture::Dimensions{surface.stride / format->bpb, surface.height, 1};
            tileConfig = gpu::texture::TileConfig{
                .mode = gpu::texture::TileMode::Pitch,
                .pitch = surface.stride
            };
        } else {
            dimensions = gpu::texture::Dimensions{surface.width, surface.height, surface.depth};
            tileConfig = gpu::texture::TileConfig{
                .mode = gpu::texture::TileMode::Block,
                .blockHeight = surface.blockSize.Height(),
                .blockDepth = surface.blockSize.Depth(),
            };
        }

        u32 layerStride{texture::CalculateLayerStride(dimensions, format, tileConfig, 1, 1)};
        return FermiTexture{
            .format = format,
            .dimensions = dimensions,
            .tileConfig = tileConfig,
            .mappings = texture::Mappings{channelCtx.asCtx->gmmu.TranslateRange(surface.address, layerStride)},
            .layerStride = layerStride,
        };
    }

    Fermi2D::Fermi2D(GPU &gpu, soc::gm20b::ChannelContext &channelCtx)
        : gpu{gpu},
          channelCtx{channelCtx},
          executor{channelCtx.executor} {}

    void Fermi2D::Blit(const Surface &srcSurface, const Surface &dstSurface, float srcRectX, float srcRectY, u32 dstRectWidth, u32 dstRectHeight, u32 dstRectX, u32 dstRectY, float duDx, float dvDy, SampleModeOrigin sampleOrigin, bool resolve, SampleModeFilter filter) {
        // TODO: When we support MSAA perform a resolve operation rather than blit when the `resolve` flag is set.
        auto srcFermiTexture{GetFermiTexture(srcSurface)};
        auto dstFermiTexture{GetFermiTexture(dstSurface)};

        auto srcTextureView{gpu.texture.FindOrCreate([=](auto &&executionCallback) {
            executor.AddOutsideRpCommand(std::forward<decltype(executionCallback)>(executionCallback));
        }, executor.tag, srcFermiTexture.mappings, srcFermiTexture.dimensions, {}, {}, srcFermiTexture.format, vk::ImageViewType::e2D, {}, srcFermiTexture.tileConfig, 1, 1, srcFermiTexture.layerStride)};
        executor.AttachTexture(srcTextureView);

        auto dstTextureView{gpu.texture.FindOrCreate([=](auto &&executionCallback) {
            executor.AddOutsideRpCommand(std::forward<decltype(executionCallback)>(executionCallback));
        }, executor.tag, dstFermiTexture.mappings, dstFermiTexture.dimensions, {}, {}, dstFermiTexture.format, vk::ImageViewType::e2D, {}, dstFermiTexture.tileConfig, 1, 1, dstFermiTexture.layerStride)};
        executor.AttachTexture(dstTextureView);

        executor.AddCheckpoint("Before blit");
        gpu.helperShaders.blitHelperShader.Blit(
            gpu,
            {
                .width = duDx * dstRectWidth,
                .height = dvDy * dstRectHeight,
                .x = centredSrcRectX,
                .y = centredSrcRectY,
            },
            {
                .width = static_cast<float>(dstRectWidth),
                .height = static_cast<float>(dstRectHeight),
                .x = static_cast<float>(dstRectX),
                .y = static_cast<float>(dstRectY),
            },
            srcFermiTexture.dimensions, dstFermiTexture.dimensions,
            duDx, dvDy,
            filter == SampleModeFilter::Bilinear,
            srcTextureView, dstTextureView,
            [=](auto &&executionCallback) {
                auto dst{dstTextureView};
                std::array<HostTextureView *, 1> sampledImages{srcTextureView};
                executor.AddSubpass(std::forward<decltype(executionCallback)>(executionCallback), {{static_cast<i32>(dstRectX), static_cast<i32>(dstRectY)}, {dstRectWidth, dstRectHeight}},
                                    sampledImages, {dst}, {},
                                    vk::PipelineStageFlagBits::eAllGraphics, vk::PipelineStageFlagBits::eAllGraphics);
            }
        );
        executor.AddCheckpoint("After blit");


        executor.NotifyPipelineChange();
    }

}
