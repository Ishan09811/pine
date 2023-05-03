// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <common/linear_allocator.h>
#include <vulkan/vulkan_structs.hpp>
#include "texture/texture.h"

namespace skyline::gpu {
    namespace interconnect {
        class CommandExecutor;
    }

    /**
     * @brief The Texture Manager is responsible for maintaining a global view of textures being mapped from the guest to the host, any lookups and creation of host texture from equivalent guest textures alongside reconciliation of any overlaps with existing textures
     */
    class TextureManager {
      public:
        using RecordFunction = std::function<void(std::function<void(vk::raii::CommandBuffer &, const std::shared_ptr<FenceCycle> &, GPU &)> &&)>;

      private:
        /**
         * @brief A single contiguous mapping of a texture in the CPU address space
         */
        struct TextureMapping : span<u8> {
            std::shared_ptr<Texture> texture;
            texture::Mappings::iterator iterator; //!< An iterator to the mapping in the texture's GuestTexture corresponding to this mapping

            template<typename... Args>
            TextureMapping(std::shared_ptr<Texture> texture, texture::Mappings::iterator iterator, Args &&... args)
                : span<u8>(std::forward<Args>(args)...),
                  texture(std::move(texture)),
                  iterator(iterator) {}
        };

        GPU &gpu;
        std::vector<TextureMapping> textures; //!< A sorted vector of all texture mappings

        std::optional<memory::Image> nullImage;
        vk::raii::ImageView nullImageView{nullptr}; //!< A cached null texture view to avoid unnecessary recreation

        using LookupResult = boost::container::small_vector<std::vector<TextureMapping>::iterator, 8>;
        LookupResult LookupRange(span<u8> range);

        std::shared_ptr<Texture> CreateTexture(const texture::Mappings& mappings, texture::Dimensions sampleDimensions, texture::Dimensions imageDimensions, vk::SampleCountFlagBits sampleCount, texture::Format format, vk::ImageViewType viewType, texture::TileConfig tileConfig, u32 levelCount, u32 layerCount, u32 layerStride = 0, bool mutableFormat = false);

        void DestroyTexture(const std::shared_ptr<Texture> &texture);

        /**
         * @brief Copies the specified subresource range from the source texture to the destination texture
         * @note Transitions the layout of the destination texture to general if it is undefined, this operation doesn't need to be performed redundantly
         */
        void CopyToTexture(RecordFunction recordCb, const std::shared_ptr<Texture> &source, const std::shared_ptr<Texture> &destination, u32 sourceLevel, u32 sourceLayer, u32 destinationLevel, u32 destinationLayer, u32 levelCount, u32 layerCount);

        HostTextureView *FindOrCreateView(RecordFunction recordCb, const std::shared_ptr<Texture> &texture, texture::Dimensions dimensions, texture::Format format, vk::ImageViewType viewType, vk::ImageSubresourceRange range, vk::ComponentMapping components, vk::SampleCountFlagBits sampleCount);

      public:
        SpinLock mutex; //!< The mutex used to lock the texture manager, this is used to prevent concurrent lookups and (re)creation of textures
        LinearAllocatorState<> viewAllocatorState; //!< Linear allocator used to allocate texture views

        TextureManager(GPU &gpu);

        /**
         * @param sampleDimensions The dimensions of the guest texture, this includes all samples from MSAA textures
         * @param imageDimensions An optional hint of the size of the image without MSAA, if this isn't specified then it'll be inferred along with the sample count based on any matches
         * @param sampleCount The sample count of the guest texture, this is ignored if imageDimensions isn't specified
         * @param viewMipBase The base mip level of the view, this is used to create a view of a subset of the texture
         * @param viewMipCount The number of mip levels in the view, if zero then levelCount - viewMipBase is used
         * @return A pre-existing or newly created HostTextureView which matches the specified criteria
         * @note The texture manager **must** be locked prior to calling this
         */
        HostTextureView *FindOrCreate(const RecordFunction& recordCb, ContextTag tag, texture::Mappings mappings, texture::Dimensions sampleDimensions, texture::Dimensions imageDimensions, vk::SampleCountFlagBits sampleCount, texture::Format format, vk::ImageViewType viewType, vk::ComponentMapping components, texture::TileConfig tileConfig, u32 levelCount, u32 layerCount, u32 layerStride, u32 viewMipBase = 0, u32 viewMipCount = 0);

        /**
         * @return A 2D 1x1 RGBA8888 null texture view with (0,0,0,1) component mappings
         */
        vk::ImageView GetNullView();
    };
}
