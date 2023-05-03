// SPDX-License-Identifier: MPL-2.0
// Copyright © 2023 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "guest_texture.h"

#include <utility>
#include "layout.h"

namespace skyline::gpu {
    namespace texture {
        size_t CalculateLinearLayerStride(const std::vector<texture::MipLevelLayout> &mipLayouts) {
            size_t layerStride{};
            for (const auto &level : mipLayouts)
                layerStride += level.linearSize;
            return layerStride;
        }

        u32 CalculateLayerStride(texture::Dimensions dimensions, texture::Format format, texture::TileConfig tileConfig, u32 levelCount, u32 layerCount) {
            switch (tileConfig.mode) {
                case texture::TileMode::Linear:
                    return static_cast<u32>(format->GetSize(dimensions));

                case texture::TileMode::Pitch:
                    return dimensions.height * tileConfig.pitch;

                case texture::TileMode::Block:
                    return static_cast<u32>(texture::GetBlockLinearLayerSize(dimensions, format->blockHeight, format->blockWidth, format->bpb, tileConfig.blockHeight, tileConfig.blockDepth, levelCount, layerCount > 1));
            }
        }
    }

    GuestTexture::GuestTexture(texture::Mappings mappings, texture::Dimensions sampleDimensions, texture::Dimensions imageDimensions, vk::SampleCountFlagBits sampleCount, texture::Format format, texture::TileConfig tileConfig, u32 levelCount, u32 layerCount, u32 layerStride)
        : mappings{std::move(mappings)},
          dimensions{sampleDimensions},
          imageDimensions{imageDimensions},
          sampleCount{sampleCount},
          format{format},
          tileConfig{tileConfig},
          levelCount{levelCount},
          layerCount{layerCount},
          layerStride{layerStride},
          size{layerStride * layerCount},
          mipLayouts{
              texture::CalculateMipLayout(
                  dimensions,
                  format->blockHeight, format->blockWidth, format->bpb,
                  tileConfig.blockHeight, tileConfig.blockDepth,
                  levelCount
              )
          },
          linearLayerStride{CalculateLinearLayerStride(mipLayouts) * layerCount},
          linearSize{linearLayerStride * layerCount} {}

    std::optional<vk::ImageSubresourceRange> GuestTexture::CalculateSubresource(texture::TileConfig pTileConfig, u32 offset, u32 pLevelCount, u32 pLayerCount, u32 pLayerStride, vk::ImageAspectFlags aspectMask) {
        if (offset >= size)
            return {};

        if (pTileConfig != tileConfig)
            return {}; // The tiling mode is not compatible, this is a hard requirement

        if (pLayerCount > 1 && pLayerStride != layerStride)
            return {}; // The layer stride is not compatible, if the stride doesn't match then layers won't be aligned

        u32 layer{offset / layerStride}, layerOffset{layer * layerStride}, level{}, levelOffset{};
        for (; level < levelCount; level++) {
            if (levelOffset >= offset)
                break;
            levelOffset += mipLayouts[level].blockLinearSize;
        }

        if (offset - layerOffset != levelOffset)
            return {}; // The offset is not aligned to the start of a level

        if (layer + pLayerCount > layerCount || level + pLevelCount > levelCount)
            return {}; // The layer/level count is out of bounds

        return vk::ImageSubresourceRange{
            .aspectMask = aspectMask,
            .baseMipLevel = level,
            .levelCount = pLevelCount,
            .baseArrayLayer = layer,
            .layerCount = pLayerCount,
        };
    }
}
