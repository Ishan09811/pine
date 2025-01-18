// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <gpu.h>
#include <kernel/memory.h>
#include <kernel/types/KProcess.h>
#include <common/settings.h>
#include "host_texture.h"
#include "texture.h"
#include "formats.h"
#include "host_compatibility.h"

namespace skyline::gpu {
    void Texture::SetupGuestMappings() {
        auto &mappings{guest.mappings};
        if (mappings.size() == 1) {
            auto mapping{mappings.front()};
            u8 *alignedData{util::AlignDown(mapping.data(), constant::PageSize)};
            size_t alignedSize{static_cast<size_t>(util::AlignUp(mapping.data() + mapping.size(), constant::PageSize) - alignedData)};

            alignedMirror = gpu.state.process->memory.CreateMirror(span<u8>{alignedData, alignedSize});
            mirror = alignedMirror.subspan(static_cast<size_t>(mapping.data() - alignedData), mapping.size());
        } else {
            std::vector<span<u8>> alignedMappings;

            const auto &frontMapping{mappings.front()};
            u8 *alignedData{util::AlignDown(frontMapping.data(), constant::PageSize)};
            alignedMappings.emplace_back(alignedData, (frontMapping.data() + frontMapping.size()) - alignedData);

            size_t totalSize{frontMapping.size()};
            for (auto it{std::next(mappings.begin())}; it != std::prev(mappings.end()); ++it) {
                auto mappingSize{it->size()};
                alignedMappings.emplace_back(it->data(), mappingSize);
                totalSize += mappingSize;
            }

            const auto &backMapping{mappings.back()};
            totalSize += backMapping.size();
            alignedMappings.emplace_back(backMapping.data(), util::AlignUp(backMapping.size(), constant::PageSize));

            alignedMirror = gpu.state.process->memory.CreateMirrors(alignedMappings);
            mirror = alignedMirror.subspan(static_cast<size_t>(frontMapping.data() - alignedData), totalSize);
        }

        // We can't just capture `this` in the lambda since the lambda could exceed the lifetime of the buffer
        std::weak_ptr<Texture> weakThis{weak_from_this()};
        trapHandle = gpu.state.process->trap.CreateTrap(mappings, [weakThis] {
            auto texture{weakThis.lock()};
            if (!texture)
                return;

            std::unique_lock stateLock{texture->stateMutex};
            if (texture->dirtyState == DirtyState::GpuDirty) {
                stateLock.unlock(); // If the lock isn't unlocked, a deadlock from threads waiting on the other lock can occur

                // If this mutex would cause other callbacks to be blocked then we should block on this mutex in advance
                std::shared_ptr<FenceCycle> waitCycle{};
                do {
                    // We need to do a loop here since we can't wait with the texture locked but not doing so means that the texture could have it's cycle changed which we wouldn't wait on, loop until we are sure the cycle hasn't changed to avoid that
                    if (waitCycle) {
                        i64 startNs{texture->accumulatedGuestWaitCounter > SkipReadbackHackWaitCountThreshold ? util::GetTimeNs() : 0};
                        waitCycle->Wait();
                        if (startNs)
                            texture->accumulatedGuestWaitTime += std::chrono::nanoseconds(util::GetTimeNs() - startNs);

                        texture->accumulatedGuestWaitCounter++;
                    }

                    std::scoped_lock lock{texture->mutex};
                    if (waitCycle && texture->cycle == waitCycle) {
                        texture->cycle = {};
                        waitCycle = {};
                    } else {
                        waitCycle = texture->cycle;
                    }
                } while (waitCycle);
            }
        }, [weakThis] {
            TRACE_EVENT("gpu", "Texture::ReadTrap");

            auto texture{weakThis.lock()};
            if (!texture)
                return true;

            std::unique_lock stateLock{texture->stateMutex, std::try_to_lock};
            if (!stateLock)
                return false;

            if (texture->dirtyState != DirtyState::GpuDirty)
                return true; // If state is already CPU dirty/Clean we don't need to do anything

            std::unique_lock lock{texture->mutex, std::try_to_lock};
            if (!lock)
                return false;

            if (texture->cycle)
                return false;

            texture->SynchronizeGuest(false, true); // We can skip trapping since the caller will do it
            return true;
        }, [weakThis] {
            TRACE_EVENT("gpu", "Texture::WriteTrap");

            auto texture{weakThis.lock()};
            if (!texture)
                return true;

            std::unique_lock stateLock{texture->stateMutex, std::try_to_lock};
            if (!stateLock)
                return false;

            if (texture->dirtyState != DirtyState::GpuDirty) {
                texture->dirtyState = DirtyState::CpuDirty;
                return true; // If the texture is already CPU dirty or we can transition it to being CPU dirty then we don't need to do anything
            }

            if (texture->accumulatedGuestWaitTime > SkipReadbackHackWaitTimeThreshold && *texture->gpu.state.settings->enableFastGpuReadbackHack && !texture->memoryFreed) {
                texture->dirtyState = DirtyState::Clean;
                return true;
            }

            std::unique_lock lock{texture->mutex, std::try_to_lock};
            if (!lock)
                return false;

            if (texture->cycle)
                return false;

            texture->SynchronizeGuest(true, true); // We need to assume the texture is dirty since we don't know what the guest is writing
            return true;
        });
    }

    Texture::Texture(GPU &gpu, texture::Mappings mappings, texture::Dimensions sampleDimensions, texture::Dimensions imageDimensions, vk::SampleCountFlagBits sampleCount, texture::Format format, texture::TileConfig tileConfig, u32 levelCount, u32 layerCount, u32 layerStride, bool mutableFormat) : gpu{gpu}, guest{std::move(mappings), sampleDimensions, imageDimensions, sampleCount, format, tileConfig, levelCount, layerCount, layerStride}, dirtyState{DirtyState::CpuDirty}, mutableFormat{mutableFormat || !gpu.traits.quirks.vkImageMutableFormatCostly} {}

    void Texture::Initialize(vk::ImageViewType viewType) {
        SetupGuestMappings();

        activeHost = &hosts.emplace_back(*this, guest.imageDimensions, guest.sampleCount, guest.format, HostTexture::ConvertViewType(viewType, guest.imageDimensions), mutableFormat);
    }

    Texture::~Texture() {
        std::scoped_lock lock{mutex};
        SynchronizeGuest(true);
        if (trapHandle)
            gpu.state.process->trap.DeleteTrap(*trapHandle);
        if (alignedMirror.valid())
            munmap(alignedMirror.data(), alignedMirror.size());
    }

    void Texture::lock() {
        mutex.lock();
    }

    bool Texture::LockWithTag(ContextTag pTag) {
        if (pTag && pTag == tag)
            return false;

        mutex.lock();
        tag = pTag;
        return true;
    }

    void Texture::unlock() {
        tag = ContextTag{};
        mutex.unlock();
    }

    bool Texture::try_lock() {
        if (mutex.try_lock())
            return true;

        return false;
    }

    HostTextureView *Texture::FindOrCreateView(texture::Dimensions dimensions, texture::Format format, vk::ImageViewType viewType, vk::ImageSubresourceRange range, vk::ComponentMapping components, vk::SampleCountFlagBits sampleCount) {
        std::scoped_lock lock{mutex};

        auto createView{[this](HostTexture &host, texture::Format viewFormat, vk::ImageViewType viewType, vk::ImageSubresourceRange range, vk::ComponentMapping components) {
            vk::ImageViewCreateInfo createInfo{
                .image = host.backing.vkImage,
                .viewType = viewType,
                .format = viewFormat->vkFormat,
                .components = components,
                .subresourceRange = range,
            };

            auto view{gpu.texture.viewAllocatorState.EmplaceUntracked<HostTextureView>(&host, viewType, viewFormat, components, range, vk::raii::ImageView{gpu.vkDevice, createInfo})};
            host.views.emplace_back(view);
            return view;
        }};

        vk::ImageType imageType{HostTexture::ConvertViewType(viewType, dimensions)};
        for (auto &host : hosts) {
            if (host.dimensions == dimensions && host.imageType == imageType && host.sampleCount == sampleCount) {
                auto viewFormat{format == guest.format ? host.format : format}; // We want to use the texture's format if it isn't supplied or if the requested format matches the guest format then we want to use the host format just in case it's compressed

                if ((viewFormat->vkAspect & format->vkAspect) == vk::ImageAspectFlagBits{}) {
                    viewFormat = format; // If the requested format doesn't share any aspects then fallback to the texture's format in the hope it's more likely to function
                    range.aspectMask = format->Aspect(components.r == vk::ComponentSwizzle::eR);
                }

                // Workaround to avoid aliasing when sampling from a BGRA texture with a RGBA view and a mapping to counteract that
                // TODO: drop this after new texture manager
                if (viewFormat == format::R8G8B8A8Unorm && host.format == format::B8G8R8A8Unorm && components == vk::ComponentMapping{vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eA}) {
                    viewFormat = host.format;
                    components = vk::ComponentMapping{};
                }

                auto view{ranges::find_if(host.views, [&](HostTextureView *view) { return view->format == viewFormat && view->type == viewType && view->range == range && view->components == components; })};
                if (view != host.views.end())
                    return *view;

                if (host.needsDecompression)
                    // If the host texture needs decompression then we can't create a view with a different format, we depend on variants to handle that
                    continue;

                bool isViewFormatCompatible{texture::IsVulkanFormatCompatible(static_cast<VkFormat>(viewFormat->vkFormat), static_cast<VkFormat>(host.format->vkFormat))};
                if (!isViewFormatCompatible)
                    continue; // If the view format isn't compatible then we can't create a view

                if (host.format == viewFormat || host.flags & vk::ImageCreateFlagBits::eMutableFormat)
                    return createView(host, viewFormat, viewType, range, components);
                else
                    return nullptr; // We need to create a whole new texture if the host texture doesn't support mutable formats
            }
        }

        auto &host{hosts.emplace_back(*this, dimensions, sampleCount, format, HostTexture::ConvertViewType(viewType, dimensions), mutableFormat)};
        return createView(host, format, viewType, range, components);
    }

    void Texture::WaitOnFence() {
        TRACE_EVENT("gpu", "Texture::WaitOnFence");

        if (cycle) {
            cycle->Wait();
            cycle = nullptr;
        }
    }

    void Texture::AttachCycle(const std::shared_ptr<FenceCycle>& lCycle) {
        lCycle->AttachObject(shared_from_this());
        lCycle->ChainCycle(cycle);
        cycle = lCycle;
    }

    bool Texture::ValidateRenderPassUsage(u32 renderPassIndex, texture::RenderPassUsage renderPassUsage) {
        return lastRenderPassUsage == renderPassUsage || lastRenderPassIndex != renderPassIndex || lastRenderPassUsage == texture::RenderPassUsage::None;
    }

    void Texture::UpdateRenderPassUsage(u32 renderPassIndex, texture::RenderPassUsage renderPassUsage) {
        lastRenderPassUsage = renderPassUsage;
        lastRenderPassIndex = renderPassIndex;

        if (renderPassUsage == texture::RenderPassUsage::RenderTarget) {
            everUsedAsRt = true;
            pendingStageMask = vk::PipelineStageFlagBits::eVertexShader |
                vk::PipelineStageFlagBits::eTessellationControlShader |
                vk::PipelineStageFlagBits::eTessellationEvaluationShader |
                vk::PipelineStageFlagBits::eGeometryShader |
                vk::PipelineStageFlagBits::eFragmentShader |
                vk::PipelineStageFlagBits::eComputeShader;
            readStageMask = {};
        } else if (renderPassUsage == texture::RenderPassUsage::None) {
            pendingStageMask = {};
            readStageMask = {};
        }
    }

    texture::RenderPassUsage Texture::GetLastRenderPassUsage() {
        return lastRenderPassUsage;
    }

    vk::PipelineStageFlags Texture::GetReadStageMask() {
        return readStageMask;
    }

    void Texture::PopulateReadBarrier(vk::PipelineStageFlagBits dstStage, vk::PipelineStageFlags &srcStageMask, vk::PipelineStageFlags &dstStageMask) {
        if (!guest)
            return;

        readStageMask |= dstStage;

        if (!(pendingStageMask & dstStage))
            return;

        if (activeHost->format->vkAspect & (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil))
            srcStageMask |= vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
        else if (activeHost->format->vkAspect & vk::ImageAspectFlagBits::eColor)
            srcStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;

        pendingStageMask &= ~dstStage;
        dstStageMask |= dstStage;
    }

    void Texture::SynchronizeHost(bool gpuDirty) {
        TRACE_EVENT("gpu", "Texture::SynchronizeHost");
        {
            std::scoped_lock lock{stateMutex};
            if (gpuDirty && dirtyState == DirtyState::Clean) {
                // If a texture is Clean then we can just transition it to being GPU dirty and retrap it
                dirtyState = DirtyState::GpuDirty;
                gpu.state.nce->TrapRegions(*trapHandle, false);
                gpu.state.process->memory.FreeMemory(mirror);
                return;
            } else if (dirtyState != DirtyState::CpuDirty) {
                return; // If the texture has not been modified on the CPU, there is no need to synchronize it
            }

            dirtyState = gpuDirty ? DirtyState::GpuDirty : DirtyState::Clean;
            gpu.state.nce->TrapRegions(*trapHandle, !gpuDirty); // Trap any future CPU reads (optionally) + writes to this texture
        }

        // From this point on Clean -> CPU dirty state transitions can occur, GPU dirty -> * transitions will always require the full lock to be held and thus won't occur

        auto stagingBuffer{activeHost->SynchronizeHostImpl()};
        if (stagingBuffer) {
            if (cycle)
                cycle->WaitSubmit();
            auto lCycle{gpu.scheduler.Submit([&](vk::raii::CommandBuffer &commandBuffer) {
                activeHost->CopyFromStagingBuffer(commandBuffer, stagingBuffer);
            })};
            lCycle->AttachObjects(stagingBuffer, shared_from_this());
            lCycle->ChainCycle(cycle);
            cycle = lCycle;
        }

        {
            std::scoped_lock lock{stateMutex};

            if (dirtyState != DirtyState::CpuDirty && gpuDirty)
                gpu.state.process->memory.FreeMemory(mirror); // All data can be paged out from the guest as the guest mirror won't be used
        }
    }

    void Texture::SynchronizeHostInline(const vk::raii::CommandBuffer &commandBuffer, const std::shared_ptr<FenceCycle> &pCycle, bool gpuDirty) {
        TRACE_EVENT("gpu", "Texture::SynchronizeHostInline");

        {
            std::scoped_lock lock{stateMutex};
            if (gpuDirty && dirtyState == DirtyState::Clean) {
                dirtyState = DirtyState::GpuDirty;
                gpu.state.nce->TrapRegions(*trapHandle, false);
                gpu.state.process->memory.FreeMemory(mirror);
                return;
            } else if (dirtyState != DirtyState::CpuDirty) {
                return;
            }

            dirtyState = gpuDirty ? DirtyState::GpuDirty : DirtyState::Clean;
            gpu.state.nce->TrapRegions(*trapHandle, !gpuDirty); // Trap any future CPU reads (optionally) + writes to this texture
        }

        auto stagingBuffer{activeHost->SynchronizeHostImpl()};
        if (stagingBuffer) {
            activeHost->CopyFromStagingBuffer(commandBuffer, stagingBuffer);
            pCycle->AttachObjects(stagingBuffer, shared_from_this());
            pCycle->ChainCycle(cycle);
            cycle = pCycle;
        }

        {
            std::scoped_lock lock{stateMutex};

            if (dirtyState != DirtyState::CpuDirty && gpuDirty)
                gpu.state.process->memory.FreeMemory(mirror); // All data can be paged out from the guest as the guest mirror won't be used
        }
    }

    void Texture::SynchronizeGuest(bool cpuDirty, bool skipTrap) {
        TRACE_EVENT("gpu", "Texture::SynchronizeGuest");

        {
            std::scoped_lock lock{stateMutex};
            if (cpuDirty && dirtyState == DirtyState::Clean) {
                dirtyState = DirtyState::CpuDirty;
                if (!skipTrap)
                    gpu.state.nce->RemoveTrap(*trapHandle);
                return;
            } else if (dirtyState != DirtyState::GpuDirty) {
                return;
            }

            dirtyState = cpuDirty ? DirtyState::CpuDirty : DirtyState::Clean;
        }

        if (activeHost->layout == vk::ImageLayout::eUndefined || activeHost->needsDecompression)
            // We cannot sync the contents of an undefined texture and we don't support recompression of a decompressed texture
            return;

        if (activeHost->tiling == vk::ImageTiling::eOptimal) {
            if (!downloadStagingBuffer)
                downloadStagingBuffer = gpu.memory.AllocateStagingBuffer(guest.size);

            WaitOnFence();
            auto lCycle{gpu.scheduler.Submit([&](vk::raii::CommandBuffer &commandBuffer) {
                activeHost->CopyIntoStagingBuffer(commandBuffer, downloadStagingBuffer);
            })};
            lCycle->Wait(); // We block till the copy is complete

            activeHost->CopyToGuest(downloadStagingBuffer->data());
        } else if (activeHost->tiling == vk::ImageTiling::eLinear) {
            // We can optimize linear texture sync on a UMA by mapping the texture onto the CPU and copying directly from it rather than using a staging buffer
            WaitOnFence();
            activeHost->CopyToGuest(activeHost->backing.data());
        } else {
            throw exception("Host -> Guest synchronization of images tiled as '{}' isn't implemented", vk::to_string(activeHost->tiling));
        }

        if (!skipTrap)
            if (cpuDirty)
                gpu.state.nce->RemoveTrap(*trapHandle);
            else
                gpu.state.nce->TrapRegions(*trapHandle, true); // Trap any future CPU writes to this texture
    }
}
