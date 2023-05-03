// SPDX-License-Identifier: MPL-2.0
// Copyright © 2023 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <vulkan/vulkan.hpp>
#include <range/v3/algorithm.hpp>
#include <range/v3/view.hpp>
#include <common.h>
#include <vulkan/vulkan_enums.hpp>

namespace skyline::gpu::texture {
    struct Dimensions {
        u32 width;
        u32 height;
        u32 depth;

        constexpr Dimensions() : width(0), height(0), depth(0) {}

        constexpr Dimensions(u32 width) : width(width), height(1), depth(1) {}

        constexpr Dimensions(u32 width, u32 height) : width(width), height(height), depth(1) {}

        constexpr Dimensions(u32 width, u32 height, u32 depth) : width(width), height(height), depth(depth) {}

        constexpr Dimensions(vk::Extent2D extent) : Dimensions(extent.width, extent.height) {}

        constexpr Dimensions(vk::Extent3D extent) : Dimensions(extent.width, extent.height, extent.depth) {}

        auto operator<=>(const Dimensions &) const = default;

        constexpr operator vk::Extent2D() const {
            return vk::Extent2D{
                .width = width,
                .height = height,
            };
        }

        constexpr operator vk::Extent3D() const {
            return vk::Extent3D{
                .width = width,
                .height = height,
                .depth = depth,
            };
        }

        /**
         * @return If the dimensions are valid and don't equate to zero
         */
        constexpr operator bool() const {
            return width && height && depth;
        }
    };

    enum class MsaaConfig {
        e1x1 = VK_SAMPLE_COUNT_1_BIT,
        e2x1 = VK_SAMPLE_COUNT_2_BIT,
        e2x2 = VK_SAMPLE_COUNT_4_BIT,
        e4x2 = VK_SAMPLE_COUNT_8_BIT,
        e4x4 = VK_SAMPLE_COUNT_16_BIT,
    };

    constexpr Dimensions CalculateMsaaDimensions(Dimensions dimensions, MsaaConfig msaa) {
        u32 msaaHeight{[msaa]() -> u32 {
            switch (msaa) {
                case MsaaConfig::e1x1:
                    return 1;
                case MsaaConfig::e2x1:
                case MsaaConfig::e2x2:
                    return 2;
                case MsaaConfig::e4x2:
                case MsaaConfig::e4x4:
                    return 4;
            }
        }()}, msaaWidth{[msaa]() -> u32 {
            switch (msaa) {
                case MsaaConfig::e1x1:
                case MsaaConfig::e2x1:
                    return 1;
                case MsaaConfig::e2x2:
                case MsaaConfig::e4x2:
                    return 2;
                case MsaaConfig::e4x4:
                    return 4;
            }
        }()};

        dimensions.width *= msaaWidth;
        dimensions.height *= msaaHeight;

        return dimensions;
    }

    /**
     * @note Blocks refers to the atomic unit of a compressed format (IE: The minimum amount of data that can be decompressed)
     */
    struct FormatBase {
        u8 bpb{}; //!< Bytes Per Block, this is used instead of bytes per pixel as that might not be a whole number for compressed formats
        vk::Format vkFormat{vk::Format::eUndefined};
        vk::ImageAspectFlags vkAspect{vk::ImageAspectFlagBits::eColor};
        u16 blockHeight{1}; //!< The height of a block in pixels
        u16 blockWidth{1}; //!< The width of a block in pixels
        vk::ComponentMapping swizzleMapping{
            .r = vk::ComponentSwizzle::eR,
            .g = vk::ComponentSwizzle::eG,
            .b = vk::ComponentSwizzle::eB,
            .a = vk::ComponentSwizzle::eA
        };
        bool stencilFirst{}; //!< If the stencil channel is the first channel in the format

        constexpr bool IsCompressed() const {
            return (blockHeight != 1) || (blockWidth != 1);
        }

        /**
         * @param width The width of the texture in pixels
         * @param height The height of the texture in pixels
         * @param depth The depth of the texture in layers
         * @return The size of the texture in bytes
         */
        constexpr size_t GetSize(u32 width, u32 height, u32 depth = 1) const {
            return util::DivideCeil<size_t>(width, size_t{blockWidth}) * util::DivideCeil<size_t>(height, size_t{blockHeight}) * bpb * depth;
        }

        constexpr size_t GetSize(Dimensions dimensions) const {
            return GetSize(dimensions.width, dimensions.height, dimensions.depth);
        }

        constexpr bool operator==(const FormatBase &format) const {
            return vkFormat == format.vkFormat;
        }

        constexpr bool operator!=(const FormatBase &format) const {
            return vkFormat != format.vkFormat;
        }

        constexpr operator vk::Format() const {
            return vkFormat;
        }

        /**
         * @return If this format is actually valid or not
         */
        constexpr operator bool() const {
            return bpb;
        }

        /**
         * @return If the supplied format is texel-layout compatible with the current format
         */
        constexpr bool IsCompatible(const FormatBase &other) const {
            return vkFormat == other.vkFormat
                || (vkFormat == vk::Format::eD32Sfloat && other.vkFormat == vk::Format::eR32Sfloat)
                || (componentCount(vkFormat) == componentCount(other.vkFormat) &&
                    ranges::all_of(ranges::views::iota(u8{0}, componentCount(vkFormat)), [this, other](auto i) {
                        return componentBits(vkFormat, i) == componentBits(other.vkFormat, i);
                    }) && (vkAspect & other.vkAspect) != vk::ImageAspectFlags{});
        }

        /**
         * @brief Determines the image aspect to use based off of the format and the first swizzle component
         */
        constexpr vk::ImageAspectFlags Aspect(bool first) const {
            if (vkAspect & vk::ImageAspectFlagBits::eDepth && vkAspect & vk::ImageAspectFlagBits::eStencil) {
                if (first)
                    return stencilFirst ? vk::ImageAspectFlagBits::eStencil : vk::ImageAspectFlagBits::eDepth;
                else
                    return stencilFirst ? vk::ImageAspectFlagBits::eDepth : vk::ImageAspectFlagBits::eStencil;
            } else {
                return vkAspect;
            }
        }
    };

    /**
     * @brief A wrapper around a pointer to underlying format metadata to prevent redundant copies
     * @note The equality operators **do not** compare equality for pointers but for the underlying formats while considering nullability
     */
    class Format {
      private:
        const FormatBase *base;

      public:
        constexpr Format(const FormatBase &base) : base(&base) {}

        constexpr Format() : base(nullptr) {}

        constexpr const FormatBase *operator->() const {
            return base;
        }

        constexpr const FormatBase &operator*() const {
            return *base;
        }

        constexpr bool operator==(const Format &format) const {
            return base && format.base ? (*base) == (*format.base) : base == format.base;
        }

        constexpr bool operator!=(const Format &format) const {
            return base && format.base ? (*base) != (*format.base) : base != format.base;
        }

        constexpr operator bool() const {
            return base;
        }
    };
}
