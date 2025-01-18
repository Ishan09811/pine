// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <common/trace.h>
#include <gpu.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vulkan/vulkan_structs.hpp>
#include "texture/guest_texture.h"
#include "texture_manager.h"
#include "texture/layout.h"

namespace skyline::gpu {
    TextureManager::LookupResult TextureManager::LookupRange(span<u8> range) {
        /*auto mappingEnd{std::upper_bound(textures.begin(), textures.end(), range, [](const auto &value, const auto &element) {
            return value.end() < element.end();
        })}, mappingBegin{std::lower_bound(mappingEnd, textures.end(), range, [](const auto &value, const auto &element) {
            return value.begin() < element.end();
        })};*/
        LookupResult result;
        for (auto it{textures.begin()}; it != textures.end(); ++it) {
            if (it->iterator->begin() <= range.end() && range.end() <= it->iterator->end())
                result.push_back(it);
        }

        return result;
    }

    std::shared_ptr<Texture> TextureManager::CreateTexture(const texture::Mappings& mappings, texture::Dimensions sampleDimensions, texture::Dimensions imageDimensions, vk::SampleCountFlagBits sampleCount, texture::Format format, vk::ImageViewType viewType, texture::TileConfig tileConfig, u32 levelCount, u32 layerCount, u32 layerStride, bool mutableFormat) {
        //LOGE("0x{:X} - 0x{:X}, {}x{}x{} samples, {}x{}x{} pixels, {} sample count, {}, {} type, {} tile mode, {} levels, {} layers, {} layer stride, mutableFormat: {}", mappings.front().begin().base(), mappings.front().end().base(), sampleDimensions.width, sampleDimensions.height, sampleDimensions.depth, imageDimensions.width, imageDimensions.height, imageDimensions.depth, vk::to_string(sampleCount), vk::to_string(format->vkFormat), vk::to_string(viewType), tileConfig.mode, levelCount, layerCount, layerStride, mutableFormat);

        auto texture{std::make_shared<Texture>(gpu, mappings, sampleDimensions, imageDimensions, sampleCount, format, tileConfig, levelCount, layerCount, layerStride, mutableFormat)};
        std::scoped_lock lock{texture->mutex};
        texture->Initialize(viewType);
        for (auto mappingIt{texture->guest.mappings.begin()}; mappingIt != texture->guest.mappings.end(); ++mappingIt) {
            auto &mapping{*mappingIt};
            if (!mapping.valid())
                continue;

            /*
            auto it{std::upper_bound(textures.begin(), textures.end(), mapping, [](const auto &value, const auto &element) {
                return value.end() < element.end();
            })};
            */

            textures.emplace_back(texture, mappingIt, mapping);
        }

        return texture;
    }

    void TextureManager::DestroyTexture(const std::shared_ptr<Texture> &texture) {
        for (const auto &host : texture->hosts) {
            for (auto view : host.views) {
                std::lock_guard lock{view->mutex};
                view->stale = true;
            }
        }

        for (const auto &mapping : texture->guest.mappings) {
            if (!mapping.valid())
                continue;

            std::erase_if(textures, [texture](const auto &element) {
                return element.texture == texture;
            });
        }
    }

    void TextureManager::CopyToTexture(RecordFunction recordCb, const std::shared_ptr<Texture> &source, const std::shared_ptr<Texture> &destination, u32 sourceLevel, u32 sourceLayer, u32 destinationLevel, u32 destinationLayer, u32 levelCount, u32 layerCount) {
        auto sourceHost{source->activeHost}, destinationHost{destination->activeHost};
        auto aspect{sourceHost->format->vkAspect & destinationHost->format->vkAspect};
        if (aspect == vk::ImageAspectFlags{}) {
            LOGW("Source and destination textures have no common aspect: {} -> {}", vk::to_string(sourceHost->format->vkFormat), vk::to_string(destinationHost->format->vkFormat));
            return;
        } else if (sourceHost->dimensions != destinationHost->dimensions) {
            LOGW("Source and destination textures have different dimensions: {}x{}x{} -> {}x{}x{}", sourceHost->dimensions.width, sourceHost->dimensions.height, sourceHost->dimensions.depth, destinationHost->dimensions.width, destinationHost->dimensions.height, destinationHost->dimensions.depth);
            return;
        } else if (sourceHost->sampleCount != destinationHost->sampleCount) {
            LOGW("Source and destination textures have different sample counts: {} -> {}", vk::to_string(sourceHost->sampleCount), vk::to_string(destinationHost->sampleCount));
            return;
        } else if (sourceHost->layout == vk::ImageLayout::eUndefined) {
            LOGW("Source texture has undefined layout");
            return;
        }

        recordCb([source, destination, sourceLevel, sourceLayer, destinationLevel, destinationLayer, levelCount, layerCount, aspect](vk::raii::CommandBuffer &commandBuffer, const std::shared_ptr<FenceCycle> &cycle, GPU &) {
            auto sourceHost{source->activeHost}, destinationHost{destination->activeHost};
            auto sourceImage{sourceHost->backing.vkImage}, destinationImage{destinationHost->backing.vkImage};
            auto sourceLayout{sourceHost->layout}, destinationLayout{destinationHost->layout};
            if (destinationLayout == vk::ImageLayout::eUndefined)
                destinationHost->layout = vk::ImageLayout::eGeneral;

            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, {
                vk::ImageMemoryBarrier{
                    .srcAccessMask = {},
                    .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                    .oldLayout = sourceLayout,
                    .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = sourceImage,
                    .subresourceRange = {
                        .aspectMask = sourceHost->format->vkAspect,
                        .baseMipLevel = sourceLevel,
                        .levelCount = levelCount,
                        .baseArrayLayer = sourceLayer,
                        .layerCount = layerCount,
                    },
                },
                vk::ImageMemoryBarrier{
                    .srcAccessMask = {},
                    .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
                    .oldLayout = destinationLayout,
                    .newLayout = vk::ImageLayout::eTransferDstOptimal,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = destinationImage,
                    .subresourceRange = {
                        .aspectMask = destinationHost->format->vkAspect,
                        .baseMipLevel = destinationLevel,
                        .levelCount = levelCount,
                        .baseArrayLayer = destinationLayer,
                        .layerCount = layerCount,
                    },
                },
            });

            auto dimensions{sourceHost->dimensions};
            for (u32 level{}; level < levelCount; level++) {
                vk::ImageCopy copy{
                    .srcSubresource = {
                        .aspectMask = aspect,
                        .mipLevel = sourceLevel + level,
                        .baseArrayLayer = sourceLayer,
                        .layerCount = layerCount,
                    },
                    .dstSubresource = {
                        .aspectMask = aspect,
                        .mipLevel = destinationLevel + level,
                        .baseArrayLayer = destinationLayer,
                        .layerCount = layerCount,
                    },
                    .extent = {
                        .width = std::max(1U, dimensions.width >> (sourceLevel + level)),
                        .height = std::max(1U, dimensions.height >> (sourceLevel + level)),
                        .depth = std::max(1U, dimensions.depth >> (sourceLevel + level)),
                    },
                };

                commandBuffer.copyImage(sourceImage, vk::ImageLayout::eTransferSrcOptimal, destinationImage, vk::ImageLayout::eTransferDstOptimal, copy);
            }

            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTopOfPipe, {}, {}, {}, {
                vk::ImageMemoryBarrier{
                    .srcAccessMask = vk::AccessFlagBits::eTransferRead,
                    .dstAccessMask = {},
                    .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
                    .newLayout = sourceLayout,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = sourceImage,
                    .subresourceRange = {
                        .aspectMask = sourceHost->format->vkAspect,
                        .baseMipLevel = sourceLevel,
                        .levelCount = levelCount,
                        .baseArrayLayer = sourceLayer,
                        .layerCount = layerCount,
                    },
                },
                vk::ImageMemoryBarrier{
                    .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                    .dstAccessMask = {},
                    .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                    .newLayout = destinationLayout == vk::ImageLayout::eUndefined ? vk::ImageLayout::eGeneral : destinationLayout,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = destinationImage,
                    .subresourceRange = {
                        .aspectMask = destinationHost->format->vkAspect,
                        .baseMipLevel = destinationLevel,
                        .levelCount = levelCount,
                        .baseArrayLayer = destinationLayer,
                        .layerCount = layerCount,
                    },
                },
            });

            source->AttachCycle(cycle);
            destination->AttachCycle(cycle);
        });
    }

    HostTextureView *TextureManager::FindOrCreateView(RecordFunction recordCb, const std::shared_ptr<Texture> &texture, texture::Dimensions dimensions, texture::Format format, vk::ImageViewType viewType, vk::ImageSubresourceRange range, vk::ComponentMapping components, vk::SampleCountFlagBits sampleCount) {
        auto view{texture->FindOrCreateView(dimensions, format, viewType, range, components, sampleCount)};
        if (view)
            return view;

        // We need to create a successor texture with host mutability to allow for the view to be created
        DestroyTexture(texture);
        auto &guest{texture->guest};
        auto successor{CreateTexture(guest.mappings, guest.dimensions, guest.imageDimensions, guest.sampleCount, guest.format, viewType, guest.tileConfig, guest.levelCount, guest.layerCount, guest.layerStride, true)};
        CopyToTexture(std::move(recordCb), texture, successor, 0, 0, 0, 0, guest.levelCount, guest.layerCount);
        return successor->FindOrCreateView(dimensions, format, viewType, range, components, sampleCount);
    }

    TextureManager::TextureManager(GPU &gpu) : gpu(gpu) {}

    HostTextureView *TextureManager::FindOrCreate(const RecordFunction &recordCb, ContextTag tag, texture::Mappings mappings, texture::Dimensions sampleDimensions, texture::Dimensions imageDimensions, vk::SampleCountFlagBits sampleCount, texture::Format format, vk::ImageViewType viewType, vk::ComponentMapping components, texture::TileConfig tileConfig, u32 levelCount, u32 layerCount, u32 layerStride, u32 viewMipBase, u32 viewMipCount) {
        if (viewMipCount == 0)
            viewMipCount = levelCount - viewMipBase;

        /*
         * Iterate over all textures that overlap with the first mapping of the guest texture and compare the mappings:
         * 1) All mappings match up perfectly, we check that the rest of the supplied mappings correspond to mappings in the texture
         * 1.1) If they match as well, we check for format/dimensions/tiling config matching the texture and return or move onto (3)
         * 2) Only a contiguous range of mappings match, we check for if the overlap is meaningful and compatible with layout math, it can go two ways:
         * 2.1) If there is a meaningful overlap, we return a view to the texture
         * 2.2) If there isn't, we move onto (3)
         * 3) If there's another overlap we go back to (1) else we go to (4)
         * 4) We check all the overlapping texture for if they're in the texture pool:
         * 4.1) If they are, we do nothing to them
         * 4.2) If they aren't, we delete them from the map
         * 5) Create a new texture and insert it in the map then return it
         */

        auto checkMappingCompatible{[&mappings](texture::Mappings::iterator mappingIt, const TextureMapping &firstTargetOverlap) {
            auto &targetMappings{firstTargetOverlap.texture->guest.mappings};
            if (mappingIt != mappings.begin() && firstTargetOverlap.iterator != targetMappings.begin())
                return false; // The target texture is only allowed to have mappings before the first mapping of the source texture
            for (auto it{firstTargetOverlap.iterator}; it != targetMappings.end() && mappingIt != mappings.end(); ++it, ++mappingIt)
                if ((it->begin() != mappingIt->begin() && it != targetMappings.begin()) || // Begin should match exactly if it's not the first mapping of the target texture
                    (it->end() != mappingIt->end() && it != std::prev(targetMappings.end()) && mappingIt != std::prev(mappings.end()))) // End should match exactly if it's not the last mapping of the target texture or the source texture
                    return false;
            return true;
        }}; //!< Checks if an overlapping texture has a compatible with the supplied mappings

        auto getOffsetFromTarget{[](const texture::Mappings::iterator& mappingIt, const TextureMapping &firstTargetOverlap) {
            auto &targetMappings{firstTargetOverlap.texture->guest.mappings};
            return std::accumulate(targetMappings.begin(), firstTargetOverlap.iterator, static_cast<u32>(mappingIt->begin() - firstTargetOverlap.iterator->begin()), [](u32 offset, const auto &mapping) {
                return offset + mapping.size();
            });
        }}; //!< Gets the offset of the first mapping in the source texture in the target texture

        auto getOffsetFromSource{[&mappings](const texture::Mappings::iterator& mappingIt, const TextureMapping &firstTargetOverlap) {
            return std::accumulate(mappings.begin(), mappingIt, static_cast<u32>(firstTargetOverlap.iterator->begin() - mappingIt->begin()), [](u32 offset, const auto &mapping) {
                return offset + mapping.size();
            });
        }}; //!< Gets the offset of the first mapping in the target texture in the source texture

        auto firstMapping{mappings.front()};
        auto firstOverlaps{LookupRange(firstMapping)};
        for (const auto &target : firstOverlaps) {
            auto &matchTargetMapping{target->iterator};
            if (!matchTargetMapping->contains(firstMapping))
                continue;

            if (!checkMappingCompatible(mappings.begin(), *target))
                continue;

            auto &targetGuest{target->texture->guest};
            u32 offset{getOffsetFromTarget(mappings.begin(), *target)}; //!< Offset of the first mapping in the source texture in the target texture
            auto subresource{targetGuest.CalculateSubresource(tileConfig, offset, levelCount, layerCount, layerStride, format->vkAspect)};
            if (!subresource)
                continue;

            subresource->baseMipLevel += viewMipBase;
            subresource->levelCount = viewMipCount;

            if (!imageDimensions) {
                imageDimensions = targetGuest.imageDimensions;
                sampleCount = targetGuest.sampleCount;
            }

            auto targetTexture{target->texture};
            ContextLock lock{tag, *targetTexture};
            return FindOrCreateView(recordCb, targetTexture, imageDimensions, format, viewType, *subresource, components, sampleCount);
        }

        if (!imageDimensions) {
            // If there's no texture to match, we assume the texture has no MSAA
            imageDimensions = sampleDimensions;
            sampleCount = vk::SampleCountFlagBits::e1;
        }

        auto texture{CreateTexture(mappings, sampleDimensions, imageDimensions, sampleCount, format, viewType, tileConfig, levelCount, layerCount, layerStride, false)};
        ContextLock lock{tag, *texture};

        LookupResult uniqueOverlaps;
        for (auto it{firstOverlaps.begin()}; it != firstOverlaps.end(); ++it) {
            if (std::find_if(uniqueOverlaps.begin(), uniqueOverlaps.end(), [&it](const auto &overlap) {
                return overlap->texture == (*it)->texture;
            }) == uniqueOverlaps.end() && (*it)->texture != texture)
                uniqueOverlaps.emplace_back(*it);
        }
        for (auto it{mappings.begin()}; it != mappings.end(); ++it) {
            auto overlaps{LookupRange(*it)};
            for (auto overlapIt{overlaps.begin()}; overlapIt != overlaps.end(); ++overlapIt) {
                if (std::find_if(uniqueOverlaps.begin(), uniqueOverlaps.end(), [&overlapIt](const auto &overlap) {
                    return overlap->texture == (*overlapIt)->texture;
                }) == uniqueOverlaps.end() && (*overlapIt)->texture != texture)
                    uniqueOverlaps.emplace_back(*overlapIt);
            }
        }

        for (const auto &target : uniqueOverlaps) {
            if (!checkMappingCompatible(mappings.begin(), *target))
                continue;

            auto &targetGuest{target->texture->guest};
            u32 offset{getOffsetFromSource(mappings.begin(), *target)}; //!< Offset of the first mapping in the target texture in the source texture
            auto subresource{targetGuest.CalculateSubresource(tileConfig, offset, levelCount, layerCount, layerStride, format->vkAspect)};
            if (!subresource)
                continue;

            ContextLock targetLock{tag, *target->texture};
            CopyToTexture(recordCb, texture, target->texture, subresource->baseArrayLayer, subresource->baseMipLevel, 0, 0, levelCount, layerCount);
            DestroyTexture(target->texture);
        }

        return texture->FindOrCreateView(imageDimensions, format, viewType, vk::ImageSubresourceRange{format->vkAspect, viewMipBase, viewMipCount, 0, layerCount}, components, sampleCount);
    }

    vk::ImageView TextureManager::GetNullView() {
        if (*nullImageView)
            return *nullImageView;

        std::scoped_lock lock{mutex};
        if (*nullImageView)
            // Check again in case another thread created the null texture
            return *nullImageView;

        constexpr texture::Format NullImageFormat{format::R8G8B8A8Unorm};
        constexpr texture::Dimensions NullImageDimensions{1, 1, 1};
        constexpr vk::ImageLayout NullImageInitialLayout{vk::ImageLayout::eUndefined};
        constexpr vk::ImageTiling NullImageTiling{vk::ImageTiling::eOptimal};
        constexpr vk::ImageCreateFlags NullImageFlags{};
        constexpr vk::ImageUsageFlags NullImageUsage{vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled};

        nullImage = gpu.memory.AllocateImage(
            {
                .flags = NullImageFlags,
                .imageType = vk::ImageType::e2D,
                .format = NullImageFormat->vkFormat,
                .extent = NullImageDimensions,
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = vk::SampleCountFlagBits::e1,
                .tiling = NullImageTiling,
                .usage = NullImageUsage,
                .sharingMode = vk::SharingMode::eExclusive,
                .queueFamilyIndexCount = 1,
                .pQueueFamilyIndices = &gpu.vkQueueFamilyIndex,
                .initialLayout = NullImageInitialLayout
            }
        );

        gpu.scheduler.Submit([&](vk::raii::CommandBuffer &commandBuffer) {
            commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eTopOfPipe,
                {},
                {},
                {},
                vk::ImageMemoryBarrier{
                    .srcAccessMask = vk::AccessFlags{},
                    .dstAccessMask = vk::AccessFlags{},
                    .oldLayout = NullImageInitialLayout,
                    .newLayout = vk::ImageLayout::eGeneral,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = nullImage->vkImage,
                    .subresourceRange = vk::ImageSubresourceRange{
                        .aspectMask = vk::ImageAspectFlagBits::eColor,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    }
                });
        })->Wait();

        nullImageView = vk::raii::ImageView(
            gpu.vkDevice,
            vk::ImageViewCreateInfo{
                .image = nullImage->vkImage,
                .viewType = vk::ImageViewType::e2D,
                .format = NullImageFormat->vkFormat,
                .components = vk::ComponentMapping{
                    .r = vk::ComponentSwizzle::eZero,
                    .g = vk::ComponentSwizzle::eZero,
                    .b = vk::ComponentSwizzle::eZero,
                    .a = vk::ComponentSwizzle::eOne,
                },
                .subresourceRange = vk::ImageSubresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                }
            }
        );

        return *nullImageView;
    }
}
