// SPDX-License-Identifier: MPL-2.0
// Copyright © 2023 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <gpu.h>
#include <common/trace.h>
#include "host_texture.h"
#include "texture.h"
#include "bc_decoder.h"
#include "layout.h"

namespace skyline::gpu {
    HostTextureView::HostTextureView(HostTexture *hostTexture, vk::ImageViewType type, texture::Format format, vk::ComponentMapping components, vk::ImageSubresourceRange range, vk::raii::ImageView &&vkView) : hostTexture{hostTexture}, texture{&hostTexture->texture}, type{type}, format{format}, components{components}, range{range}, vkView{std::move(vkView)} {}

    void HostTextureView::lock() {
        std::lock_guard lock{mutex};
        if (texture)
            texture->lock();
    }

    bool HostTextureView::LockWithTag(ContextTag tag) {
        std::lock_guard lock{mutex};
        if (texture)
            return texture->LockWithTag(tag);
        else
            return false;
    }

    void HostTextureView::unlock() {
        if (texture)
            texture->unlock();
    }

    bool HostTextureView::try_lock() {
        return texture->try_lock();
    }

    std::shared_ptr<memory::StagingBuffer> HostTexture::SynchronizeHostImpl() {
        auto &guest{texture.guest};
        auto pointer{texture.mirror.data()};

        u8 *bufferData;
        auto stagingBuffer{[&]() -> std::shared_ptr<memory::StagingBuffer> {
            if (tiling == vk::ImageTiling::eOptimal) {
                // We need a staging buffer for all optimal copies (since we aren't aware of the host optimal layout) and linear textures which we cannot map on the CPU since we do not have access to their backing VkDeviceMemory
                auto stagingBuffer{texture.gpu.memory.AllocateStagingBuffer(guest.linearSize)};
                bufferData = stagingBuffer->data();
                return stagingBuffer;
            } else if (tiling == vk::ImageTiling::eLinear) {
                // We can optimize linear texture sync on a UMA by mapping the texture onto the CPU and copying directly into it rather than a staging buffer
                bufferData = backing.data();
                return nullptr;
            } else {
                throw exception("Guest -> Host synchronization of images tiled as '{}' isn't implemented", vk::to_string(tiling));
            }
        }()};

        std::vector<u8> deswizzleBuffer;
        u8 *deswizzleOutput;
        if (guest.format != format) {
            deswizzleBuffer.resize(guest.linearSize);
            deswizzleOutput = deswizzleBuffer.data();
        } else [[likely]] {
            deswizzleOutput = bufferData;
        }

        if (guest.levelCount == 1) {
            auto outputLayer{deswizzleOutput};
            for (size_t layer{}; layer < guest.layerCount; layer++) {
                if (guest.tileConfig.mode == texture::TileMode::Block)
                    texture::CopyBlockLinearToLinear(guest, pointer, outputLayer);
                else if (guest.tileConfig.mode == texture::TileMode::Pitch)
                    texture::CopyPitchLinearToLinear(guest, pointer, outputLayer);
                else if (guest.tileConfig.mode == texture::TileMode::Linear)
                    std::memcpy(outputLayer, pointer, guest.layerStride);
                pointer += guest.layerStride;
                outputLayer += guest.linearLayerStride;
            }
        } else if (guest.levelCount > 1 && guest.tileConfig.mode == texture::TileMode::Block) {
            // We need to generate a buffer that has all layers for a given mip level while Tegra X1 layout holds all mip levels for a given layer
            for (size_t layer{}; layer < guest.layerCount; layer++) {
                auto inputLevel{pointer}, outputLevel{deswizzleOutput};
                for (const auto &level : guest.mipLayouts) {
                    texture::CopyBlockLinearToLinear(
                        level.dimensions,
                        guest.format->blockWidth, guest.format->blockHeight, guest.format->bpb,
                        level.blockHeight, level.blockDepth,
                        inputLevel, outputLevel + (layer * level.linearSize) // Offset into the current layer relative to the start of the current mip level
                    );

                    inputLevel += level.blockLinearSize; // Skip over the current mip level as we've deswizzled it
                    outputLevel += guest.layerCount * level.linearSize; // We need to offset the output buffer by the size of the previous mip level
                }

                pointer += guest.layerStride; // We need to offset the input buffer by the size of the previous guest layer, this can differ from inputLevel's value due to layer end padding or guest RT layer stride
            }
        } else if (guest.levelCount != 0) {
            throw exception("Mipmapped textures with tiling mode '{}' aren't supported", static_cast<int>(tiling));
        }

        if (needsDecompression) {
            for (const auto &level : guest.mipLayouts) {
                size_t levelHeight{level.dimensions.height * guest.layerCount}; //!< The height of an image representing all layers in the entire level
                switch (guest.format->vkFormat) {
                    case vk::Format::eBc1RgbaUnormBlock:
                    case vk::Format::eBc1RgbaSrgbBlock:
                        bcn::DecodeBc1(deswizzleOutput, bufferData, level.dimensions.width, levelHeight, true);
                        break;

                    case vk::Format::eBc2UnormBlock:
                    case vk::Format::eBc2SrgbBlock:
                        bcn::DecodeBc2(deswizzleOutput, bufferData, level.dimensions.width, levelHeight);
                        break;

                    case vk::Format::eBc3UnormBlock:
                    case vk::Format::eBc3SrgbBlock:
                        bcn::DecodeBc3(deswizzleOutput, bufferData, level.dimensions.width, levelHeight);
                        break;

                    case vk::Format::eBc4UnormBlock:
                        bcn::DecodeBc4(deswizzleOutput, bufferData, level.dimensions.width, levelHeight, false);
                        break;
                    case vk::Format::eBc4SnormBlock:
                        bcn::DecodeBc4(deswizzleOutput, bufferData, level.dimensions.width, levelHeight, true);
                        break;

                    case vk::Format::eBc5UnormBlock:
                        bcn::DecodeBc5(deswizzleOutput, bufferData, level.dimensions.width, levelHeight, false);
                        break;
                    case vk::Format::eBc5SnormBlock:
                        bcn::DecodeBc5(deswizzleOutput, bufferData, level.dimensions.width, levelHeight, true);
                        break;

                    case vk::Format::eBc6HUfloatBlock:
                        bcn::DecodeBc6(deswizzleOutput, bufferData, level.dimensions.width, levelHeight, false);
                        break;
                    case vk::Format::eBc6HSfloatBlock:
                        bcn::DecodeBc6(deswizzleOutput, bufferData, level.dimensions.width, levelHeight, true);
                        break;

                    case vk::Format::eBc7UnormBlock:
                    case vk::Format::eBc7SrgbBlock:
                        bcn::DecodeBc7(deswizzleOutput, bufferData, level.dimensions.width, levelHeight);
                        break;

                    default:
                        throw exception("Unsupported guest format '{}'", vk::to_string(guest.format->vkFormat));
                }

                deswizzleOutput += level.linearSize * guest.layerCount;
                bufferData += format->GetSize(level.dimensions) * guest.layerCount;
            }
        }

        return stagingBuffer;
    }

    boost::container::small_vector<vk::BufferImageCopy, 10> HostTexture::GetBufferImageCopies() {
        boost::container::small_vector<vk::BufferImageCopy, 10> bufferImageCopies;

        auto &guest{texture.guest};
        auto pushBufferImageCopyWithAspect{[&](vk::ImageAspectFlagBits aspect) {
            vk::DeviceSize bufferOffset{};
            u32 mipLevel{};
            for (auto &level : guest.mipLayouts) {
                bufferImageCopies.emplace_back(
                    vk::BufferImageCopy{
                        .bufferOffset = bufferOffset,
                        .imageSubresource = {
                            .aspectMask = aspect,
                            .mipLevel = mipLevel++,
                            .layerCount = guest.layerCount,
                        },
                        .imageExtent = level.dimensions,
                    }
                );
                bufferOffset += (needsDecompression ? format->GetSize(level.dimensions) : guest.linearLayerStride) * guest.layerCount;
            }
        }};

        if (format->vkAspect & vk::ImageAspectFlagBits::eColor)
            pushBufferImageCopyWithAspect(vk::ImageAspectFlagBits::eColor);
        if (format->vkAspect & vk::ImageAspectFlagBits::eDepth)
            pushBufferImageCopyWithAspect(vk::ImageAspectFlagBits::eDepth);
        if (format->vkAspect & vk::ImageAspectFlagBits::eStencil)
            pushBufferImageCopyWithAspect(vk::ImageAspectFlagBits::eStencil);

        return bufferImageCopies;
    }

    void HostTexture::TransitionLayout(vk::ImageLayout pLayout) {
        texture.WaitOnFence();

        TRACE_EVENT("gpu", "HostTexture::TransitionLayout");

        if (layout != pLayout) {
            auto lCycle{texture.gpu.scheduler.Submit([&](vk::raii::CommandBuffer &commandBuffer) {
                commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe, {}, {}, {}, vk::ImageMemoryBarrier{
                    .image = backing.vkImage,
                    .srcAccessMask = vk::AccessFlagBits::eNoneKHR,
                    .dstAccessMask = vk::AccessFlagBits::eNoneKHR,
                    .oldLayout = std::exchange(layout, pLayout),
                    .newLayout = pLayout,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .subresourceRange = {
                        .aspectMask = format->vkAspect,
                        .levelCount = texture.guest.levelCount,
                        .layerCount = texture.guest.layerCount,
                    },
                });
            })};
            lCycle->AttachObject(texture.shared_from_this());
            texture.cycle = lCycle;
        }
    }

    void HostTexture::CopyFromStagingBuffer(const vk::raii::CommandBuffer &commandBuffer, const std::shared_ptr<memory::StagingBuffer> &stagingBuffer) {
        if (layout == vk::ImageLayout::eUndefined)
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, vk::ImageMemoryBarrier{
                .image = backing.vkImage,
                .srcAccessMask = vk::AccessFlagBits::eMemoryRead,
                .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
                .oldLayout = std::exchange(layout, vk::ImageLayout::eGeneral),
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .subresourceRange = {
                    .aspectMask = format->vkAspect,
                    .levelCount = texture.guest.levelCount,
                    .layerCount = texture.guest.layerCount,
                },
            });

        auto bufferImageCopies{GetBufferImageCopies()};
        commandBuffer.copyBufferToImage(stagingBuffer->vkBuffer, backing.vkImage, layout, vk::ArrayProxy(static_cast<u32>(bufferImageCopies.size()), bufferImageCopies.data()));
    }

    void HostTexture::CopyIntoStagingBuffer(const vk::raii::CommandBuffer &commandBuffer, const std::shared_ptr<memory::StagingBuffer> &stagingBuffer) {
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eBottomOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, vk::ImageMemoryBarrier{
            .image = backing.vkImage,
            .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
            .dstAccessMask = vk::AccessFlagBits::eTransferRead,
            .oldLayout = layout,
            .newLayout = layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .subresourceRange = {
                .aspectMask = format->vkAspect,
                .levelCount = texture.guest.levelCount,
                .layerCount = texture.guest.layerCount,
            },
        });

        auto bufferImageCopies{GetBufferImageCopies()};
        commandBuffer.copyImageToBuffer(backing.vkImage, layout, stagingBuffer->vkBuffer, vk::ArrayProxy(static_cast<u32>(bufferImageCopies.size()), bufferImageCopies.data()));

        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, {}, vk::BufferMemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eHostRead,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = stagingBuffer->vkBuffer,
            .offset = 0,
            .size = stagingBuffer->size(),
        }, {});
    }

    void HostTexture::CopyToGuest(u8 *hostBuffer) {
        auto &guest{texture.guest};
        auto guestOutput{texture.mirror.data()};
        if (guest.levelCount == 1) {
            for (size_t layer{}; layer < guest.layerCount; layer++) {
                if (guest.tileConfig.mode == texture::TileMode::Block)
                    texture::CopyLinearToBlockLinear(guest, hostBuffer, guestOutput);
                else if (guest.tileConfig.mode == texture::TileMode::Pitch)
                    texture::CopyLinearToPitchLinear(guest, hostBuffer, guestOutput);
                else if (guest.tileConfig.mode == texture::TileMode::Linear)
                    std::memcpy(hostBuffer, guestOutput, layerStride);
                guestOutput += guest.layerStride;
                hostBuffer += layerStride;
            }
        } else if (guest.levelCount > 1 && guest.tileConfig.mode == texture::TileMode::Block) {
            // We need to copy into the Tegra X1 layout holds all mip levels for a given layer while the input buffer has all layers for a given mip level
            // Note: See SynchronizeHostImpl for additional comments
            for (size_t layer{}; layer < guest.layerCount; layer++) {
                auto outputLevel{guestOutput}, inputLevel{hostBuffer};
                for (const auto &level : guest.mipLayouts) {
                    texture::CopyLinearToBlockLinear(
                        level.dimensions,
                        guest.format->blockWidth, guest.format->blockHeight, guest.format->bpb,
                        level.blockHeight, level.blockDepth,
                        inputLevel + (layer * level.linearSize), outputLevel
                    );

                    outputLevel += level.blockLinearSize;
                    inputLevel += guest.layerCount * level.linearSize;
                }

                guestOutput += guest.layerStride;
            }
        } else if (guest.levelCount != 0) {
            throw exception("Mipmapped textures with tiling mode '{}' aren't supported", static_cast<int>(tiling));
        }
    }

    texture::Format ConvertHostCompatibleFormat(texture::Format format, const TraitManager &traits) {
        auto bcnSupport{traits.bcnSupport};
        if (bcnSupport.all())
            return format;

        switch (format->vkFormat) {
            case vk::Format::eBc1RgbaUnormBlock:
                return bcnSupport[0] ? format : format::R8G8B8A8Unorm;
            case vk::Format::eBc1RgbaSrgbBlock:
                return bcnSupport[0] ? format : format::R8G8B8A8Srgb;

            case vk::Format::eBc2UnormBlock:
                return bcnSupport[1] ? format : format::R8G8B8A8Unorm;
            case vk::Format::eBc2SrgbBlock:
                return bcnSupport[1] ? format : format::R8G8B8A8Srgb;

            case vk::Format::eBc3UnormBlock:
                return bcnSupport[2] ? format : format::R8G8B8A8Unorm;
            case vk::Format::eBc3SrgbBlock:
                return bcnSupport[2] ? format : format::R8G8B8A8Srgb;

            case vk::Format::eBc4UnormBlock:
                return bcnSupport[3] ? format : format::R8Unorm;
            case vk::Format::eBc4SnormBlock:
                return bcnSupport[3] ? format : format::R8Snorm;

            case vk::Format::eBc5UnormBlock:
                return bcnSupport[4] ? format : format::R8G8Unorm;
            case vk::Format::eBc5SnormBlock:
                return bcnSupport[4] ? format : format::R8G8Snorm;

            case vk::Format::eBc6HUfloatBlock:
            case vk::Format::eBc6HSfloatBlock:
                return bcnSupport[5] ? format : format::R16G16B16A16Float; // This is a signed 16-bit FP format, we don't have an unsigned 16-bit FP format

            case vk::Format::eBc7UnormBlock:
                return bcnSupport[6] ? format : format::R8G8B8A8Unorm;
            case vk::Format::eBc7SrgbBlock:
                return bcnSupport[6] ? format : format::R8G8B8A8Srgb;

            default:
                return format;
        }
    }

    vk::ImageType HostTexture::ConvertViewType(vk::ImageViewType viewType, texture::Dimensions dimensions) {
        switch (viewType) {
            case vk::ImageViewType::e1D:
            case vk::ImageViewType::e1DArray:
                return vk::ImageType::e1D;
            case vk::ImageViewType::e2D:
            case vk::ImageViewType::e2DArray:
                // If depth is > 1 this is a 2D view into a 3D texture so the underlying image needs to be created as 3D
                if (dimensions.depth > 1)
                    return vk::ImageType::e3D;
                else
                    return vk::ImageType::e2D;
            case vk::ImageViewType::eCube:
            case vk::ImageViewType::eCubeArray:
                return vk::ImageType::e2D;
            case vk::ImageViewType::e3D:
                return vk::ImageType::e3D;
        }
    }

    size_t CalculateLinearLayerStride(const std::vector<texture::MipLevelLayout> &mipLayouts) {
        size_t layerStride{};
        for (const auto &level : mipLayouts)
            layerStride += level.linearSize;
        return layerStride;
    }

    HostTexture::HostTexture(Texture& texture, texture::Dimensions dimensions, vk::SampleCountFlagBits sampleCount, texture::Format pFormat, vk::ImageType imageType, bool mutableFormat)
        : texture{texture},
          dimensions{dimensions},
          sampleCount{sampleCount},
          format{ConvertHostCompatibleFormat(pFormat, texture.gpu.traits)},
          needsDecompression{format != texture.guest.format},
          layerStride{needsDecompression ? texture.guest.linearLayerStride : static_cast<u32>(format->GetSize(dimensions))},
          imageType{imageType},
          layout{vk::ImageLayout::eUndefined},
          tiling{vk::ImageTiling::eOptimal}, // Force Optimal due to not adhering to host subresource layout during Linear synchronization
          flags{mutableFormat ? vk::ImageCreateFlagBits::eMutableFormat : vk::ImageCreateFlags{}},
          usage{vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled} {
        if ((format->vkAspect & vk::ImageAspectFlagBits::eColor) && !format->IsCompressed())
            usage |= vk::ImageUsageFlagBits::eColorAttachment;
        if (format->vkAspect & (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil))
            usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;

        auto &guest{texture.guest};
        auto &gpu{texture.gpu};

        if (imageType == vk::ImageType::e2D && dimensions.width == dimensions.height && guest.layerCount >= 6)
            flags |= vk::ImageCreateFlagBits::eCubeCompatible;
        else if (imageType == vk::ImageType::e3D)
            flags |= vk::ImageCreateFlagBits::e2DArrayCompatible;

        vk::ImageCreateInfo imageCreateInfo{
            .flags = flags,
            .imageType = imageType,
            .format = *format,
            .extent = dimensions,
            .mipLevels = guest.levelCount,
            .arrayLayers = guest.layerCount,
            .samples = sampleCount,
            .tiling = tiling,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive,
            .queueFamilyIndexCount = 1,
            .pQueueFamilyIndices = &gpu.vkQueueFamilyIndex,
            .initialLayout = layout,
        };
        backing = tiling != vk::ImageTiling::eLinear ? gpu.memory.AllocateImage(imageCreateInfo) : gpu.memory.AllocateMappedImage(imageCreateInfo);
        TransitionLayout(vk::ImageLayout::eGeneral);
    }

    HostTexture::~HostTexture() {
        std::lock_guard lock{texture};
        for (auto &view : views) {
            std::lock_guard viewLock{view->mutex};
            view->texture = nullptr;
            view->hostTexture = nullptr;
            view->stale = true;
            view->vkView = nullptr;
        }
    }
}
