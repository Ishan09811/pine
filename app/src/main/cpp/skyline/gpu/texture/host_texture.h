// SPDX-License-Identifier: MPL-2.0
// Copyright © 2023 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <vulkan/vulkan_raii.hpp>
#include <common/base.h>
#include <common/spin_lock.h>
#include <gpu/tag_allocator.h>
#include <gpu/memory_manager.h>
#include "common.h"

namespace skyline::gpu {
    namespace texture {
        enum class RenderPassUsage : u8 {
            None,
            Sampled,
            RenderTarget
        };
    }

    class Texture;
    class HostTexture;
    class TextureManager;

    /**
     * @brief A view into a specific subresource of a Texture
     * @note The object **must** be locked prior to accessing any members as values will be mutated
     * @note This class conforms to the Lockable and BasicLockable C++ named requirements
     */
    struct HostTextureView {
        std::mutex mutex; //!< Synchronizes access to the backing and texture pointer, these are written to externally
        Texture *texture; //!< The backing texture for this view, this is set to null when the host texture is destroyed
        HostTexture *hostTexture; //!< The backing host texture for this view, this is set to null when the host texture is destroyed
        bool stale{false}; //!< If the view is stale and should no longer be used in any future operations, this doesn't imply that the backing is destroyed
        vk::ImageViewType type;
        texture::Format format;
        vk::ComponentMapping components;
        vk::ImageSubresourceRange range;
        vk::raii::ImageView vkView; //!< The backing Vulkan image view for this view, this is destroyed with the texture

        HostTextureView(HostTexture *hostTexture, vk::ImageViewType type, texture::Format format, vk::ComponentMapping components, vk::ImageSubresourceRange range, vk::raii::ImageView &&vkView);

        /**
         * @brief Acquires an exclusive lock on the backing texture for the calling thread
         * @note Naming is in accordance to the BasicLockable named requirement
         */
        void lock();

        /**
         * @brief Acquires an exclusive lock on the texture for the calling thread
         * @param tag A tag to associate with the lock, future invocations with the same tag prior to the unlock will acquire the lock without waiting (0 is not a valid tag value and will disable tag behavior)
         * @return If the lock was acquired by this call rather than having the same tag as the holder
         * @note All locks using the same tag **must** be from the same thread as it'll only have one corresponding unlock() call
         */
        bool LockWithTag(ContextTag tag);

        /**
         * @brief Relinquishes an existing lock on the backing texture by the calling thread
         * @note Naming is in accordance to the BasicLockable named requirement
         */
        void unlock();

        /**
         * @brief Attempts to acquire an exclusive lock on the backing texture but returns immediately if it's captured by another thread
         * @note Naming is in accordance to the Lockable named requirement
         */
        bool try_lock();
    };

    class Texture;

    /**
     * @brief A texture which is backed by host constructs while being synchronized with the underlying guest texture
     * @note This class conforms to the Lockable and BasicLockable C++ named requirements
     */
    class HostTexture {
      private:
        Texture& texture;
        memory::Image backing; //!< The Vulkan image that backs this texture, it is nullable

        std::vector<HostTextureView *> views;

        friend TextureManager;
        friend Texture;
        friend HostTextureView;

        /**
         * @brief An implementation function for guest -> host texture synchronization, it allocates and copies data into a staging buffer or directly into a linear host texture
         * @return If a staging buffer was required for the texture sync, it's returned filled with guest texture data and must be copied to the host texture by the callee
         */
        std::shared_ptr<memory::StagingBuffer> SynchronizeHostImpl();

        /**
         * @brief Records commands for copying data from a staging buffer to the texture's backing into the supplied command buffer
         */
        void CopyFromStagingBuffer(const vk::raii::CommandBuffer &commandBuffer, const std::shared_ptr<memory::StagingBuffer> &stagingBuffer);

        /**
         * @brief Records commands for copying data from the texture's backing to a staging buffer into the supplied command buffer
         * @note Any caller **must** ensure that the layout is not `eUndefined`
         */
        void CopyIntoStagingBuffer(const vk::raii::CommandBuffer &commandBuffer, const std::shared_ptr<memory::StagingBuffer> &stagingBuffer);

        /**
         * @brief Copies data from the supplied host buffer into the guest texture
         * @note The host buffer must be contain the entire image
         */
        void CopyToGuest(u8 *hostBuffer);

        /**
         * @return A vector of all the buffer image copies that need to be done for every aspect of every level of every layer of the texture
         */
        boost::container::small_vector<vk::BufferImageCopy, 10> GetBufferImageCopies();

        void TransitionLayout(vk::ImageLayout layout);

      public:
        texture::Dimensions dimensions;
        vk::SampleCountFlagBits sampleCount;
        texture::Format format;
        bool needsDecompression; //!< If the guest format is compressed and needs to be decompressed before being used on the host
        size_t layerStride; //!< The stride between each layer of the texture with the host format
        vk::ImageType imageType;
        vk::ImageLayout layout;
        vk::ImageTiling tiling;
        vk::ImageCreateFlags flags;
        vk::ImageUsageFlags usage;
        bool replaced{};

        static vk::ImageType ConvertViewType(vk::ImageViewType viewType, texture::Dimensions dimensions);

        /**
         * @brief Creates a texture object wrapping the supplied backing with the supplied attributes
         */
        HostTexture(Texture& texture, texture::Dimensions dimensions, vk::SampleCountFlagBits sampleCount, texture::Format format, vk::ImageType imageType = vk::ImageType::e2D, bool mutableFormat = false);

        ~HostTexture();

        vk::Image GetImage() {
            return backing.vkImage;
        }
    };
}
