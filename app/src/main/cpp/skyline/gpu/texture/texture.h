// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once
#include <vulkan/vulkan_format_traits.hpp>
#include "common/trap_manager.h"
#include <gpu/tag_allocator.h>
#include <gpu/memory_manager.h>
#include "guest_texture.h"
#include "host_texture.h"

namespace skyline::gpu {
    class TextureManager;

    class Texture : public std::enable_shared_from_this<Texture> {
      private:
        GPU &gpu;

        span<u8> mirror{}; //!< A contiguous mirror of all the guest mappings to allow linear access on the CPU
        span<u8> alignedMirror{}; //!< The mirror mapping aligned to page size to reflect the full mapping
        std::optional<TrapHandle> trapHandle{}; //!< The handle of the traps for the guest mappings
        enum class DirtyState {
            Clean, //!< The CPU mappings are in sync with the GPU texture
            CpuDirty, //!< The CPU mappings have been modified but the GPU texture is not up to date
            GpuDirty, //!< The GPU texture has been modified but the CPU mappings have not been updated
        } dirtyState{DirtyState::CpuDirty}; //!< The state of the CPU mappings with respect to the GPU texture
        bool memoryFreed{}; //!< If the guest backing memory has been freed
        bool everUsedAsRt{}; //!< If this texture has ever been used as a rendertarget
        std::recursive_mutex stateMutex; //!< Synchronizes access to the dirty state

        std::atomic<ContextTag> tag{}; //!< The tag associated with the last lock call on this texture
        RecursiveSpinLock mutex; //!< Synchronizes any mutations to the texture or any of its host textures

        GuestTexture guest;
        std::shared_ptr<memory::StagingBuffer> downloadStagingBuffer{}; //!< A staging buffer used to download the texture from the GPU
        bool mutableFormat{}; //!< If the format of all the host textures is mutable for views

        std::list<HostTexture> hosts;
        HostTexture *activeHost{}; //!< A non-null pointer to the host texture that is currently active, all operations on the texture are performed on this host texture

        static constexpr size_t SkipReadbackHackWaitCountThreshold{6}; //!< Threshold for the number of times a texture can be waited on before it should be considered for the readback hack
        static constexpr std::chrono::nanoseconds SkipReadbackHackWaitTimeThreshold{constant::NsInSecond / 4}; //!< Threshold for the amount of time a texture can be waited on before it should be considered for the readback hack, `SkipReadbackHackWaitCountThreshold` needs to be hit before this
        size_t accumulatedGuestWaitCounter{}; //!< Total number of times the texture has been waited on
        std::chrono::nanoseconds accumulatedGuestWaitTime{}; //!< Amount of time the texture has been waited on for since the `SkipReadbackHackWaitCountThreshold`th wait on it by the guest

        u32 lastRenderPassIndex{}; //!< The index of the last render pass that used this texture
        texture::RenderPassUsage lastRenderPassUsage{texture::RenderPassUsage::None}; //!< The type of usage in the last render pass
        vk::PipelineStageFlags pendingStageMask{}; //!< List of pipeline stages that are yet to be flushed for reads since the last time this texture was used an an RT
        vk::PipelineStageFlags readStageMask{}; //!< Set of pipeline stages that this texture has been read in since it was last used as an RT

        friend class HostTexture;
        friend class TextureManager;

        /**
         * @brief Sets up mirror mappings for the guest mappings, this must be called after construction for the mirror to be valid
         */
        void SetupGuestMappings();

      public:
        std::shared_ptr<FenceCycle> cycle; //!< A fence cycle for when any host operation mutating the texture has completed, it must be waited on prior to any host texture changes

        Texture(GPU &gpu, texture::Mappings mappings, texture::Dimensions sampleDimensions, texture::Dimensions imageDimensions, vk::SampleCountFlagBits sampleCount, texture::Format format, texture::TileConfig tileConfig, u32 levelCount, u32 layerCount, u32 layerStride, bool mutableFormat = false);

        void Initialize(vk::ImageViewType viewType);

        ~Texture();

        /**
         * @brief Acquires an exclusive lock on the texture for the calling thread
         * @note Naming is in accordance to the BasicLockable named requirement
         */
        void lock();

        /**
         * @brief Acquires an exclusive lock on the texture for the calling thread
         * @param tag A tag to associate with the lock, future invocations with the same tag prior to the unlock will acquire the lock without waiting (A default initialised tag will disable this behaviour)
         * @return If the lock was acquired by this call as opposed to the texture already being locked with the same tag
         * @note All locks using the same tag **must** be from the same thread as it'll only have one corresponding unlock() call
         */
        bool LockWithTag(ContextTag tag);

        /**
         * @brief Relinquishes an existing lock on the texture by the calling thread
         * @note Naming is in accordance to the BasicLockable named requirement
         */
        void unlock();

        /**
         * @brief Attempts to acquire an exclusive lock but returns immediately if it's captured by another thread
         * @note Naming is in accordance to the Lockable named requirement
         */
        bool try_lock();

        /**
         * @brief Attempts to find or create a host texture view for the given parameters, this may result in the creation of a new host texture
         * @return A pointer to the host texture view, this may be null if a host texture is compatible but needs VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT
         */
        HostTextureView *FindOrCreateView(texture::Dimensions dimensions, texture::Format format, vk::ImageViewType viewType, vk::ImageSubresourceRange viewRange, vk::ComponentMapping components = {}, vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1);

        /**
         * @brief Waits on a fence cycle if it exists till it's signalled and resets it after
         * @note The texture **must** be locked prior to calling this
         */
        void WaitOnFence();

        /**
         * @brief Attaches a fence cycle to the texture, chaining it to the existing fence cycle if one exists
         */
        void AttachCycle(const std::shared_ptr<FenceCycle>& cycle);

        /**
         * @brief Synchronizes the active host texture with the guest after it has been modified
         * @param gpuDirty If true, the texture will be transitioned to being GpuDirty by this call
         * @note This function is not blocking and the synchronization will not be complete until the associated fence is signalled, it can be waited on with WaitOnFence()
         * @note The texture **must** be locked prior to calling this
         */
        void SynchronizeHost(bool gpuDirty = false);

        /**
         * @brief Same as SynchronizeHost but this records any commands into the supplied command buffer rather than creating one as necessary
         * @param gpuDirty If true, the texture will be transitioned to being GpuDirty by this call
         * @note It is more efficient to call SynchronizeHost than allocating a command buffer purely for this function as it may conditionally not record any commands
         * @note The texture **must** be locked prior to calling this
         */
        void SynchronizeHostInline(const vk::raii::CommandBuffer &commandBuffer, const std::shared_ptr<FenceCycle> &cycle, bool gpuDirty = false);

        /**
         * @brief Synchronizes the guest texture with the host texture after it has been modified
         * @param cpuDirty If true, the texture will be transitioned to being CpuDirty by this call
         * @param skipTrap If true, trapping/untrapping the guest mappings will be skipped and has to be handled by the caller
         * @note This function is blocking and waiting on the fence is not required
         * @note The texture **must** be locked prior to calling this
         */
        void SynchronizeGuest(bool cpuDirty = false, bool skipTrap = false);

        /**
         * @brief Checks if the previous usage in the renderpass is compatible with the current one
         * @return If the new usage is compatible with the previous usage
         */
        bool ValidateRenderPassUsage(u32 renderPassIndex, texture::RenderPassUsage renderPassUsage);

        /**
         * @brief Updates renderpass usage tracking information
         */
        void UpdateRenderPassUsage(u32 renderPassIndex, texture::RenderPassUsage renderPassUsage);

        /**
         * @return The last usage of the texture
         */
        texture::RenderPassUsage GetLastRenderPassUsage();

        /**
         * @return The set of stages this texture has been read in since it was last used as an RT
         */
        vk::PipelineStageFlags GetReadStageMask();

        /**
         * @brief Populates the input src and dst stage masks with appropriate read barrier parameters for the current texture state
         */
        void PopulateReadBarrier(vk::PipelineStageFlagBits dstStage, vk::PipelineStageFlags &srcStageMask, vk::PipelineStageFlags &dstStageMask);
    };
}
