// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "command_nodes.h"
#include "gpu/texture/texture.h"
#include <vulkan/vulkan_enums.hpp>

namespace skyline::gpu::interconnect::node {
    RenderPassNode::RenderPassNode(vk::Rect2D renderArea, span<HostTextureView *> pColorAttachments, HostTextureView *pDepthStencilAttachment) : renderArea{renderArea} {
        BindAttachments(pColorAttachments, pDepthStencilAttachment);
    }

    bool RenderPassNode::BindAttachments(span<HostTextureView *> pColorAttachments, HostTextureView *pDepthStencilAttachment) {
        size_t subsetAttachmentCount{std::min(colorAttachments.size(), pColorAttachments.size())};
        bool isColorSubset{std::equal(colorAttachments.begin(), colorAttachments.begin() + static_cast<ssize_t>(subsetAttachmentCount), pColorAttachments.begin(), pColorAttachments.begin() + static_cast<ssize_t>(subsetAttachmentCount), [](const auto &lhs, const auto &rhs) {
            return lhs && rhs && lhs->view == rhs;
        })};
        bool isDepthSubset{!depthStencilAttachment || !pDepthStencilAttachment || depthStencilAttachment->view == pDepthStencilAttachment};
        if (!isColorSubset || !isDepthSubset)
            // If the attachments aren't a subset of the existing attachments then we can't bind them
            return false;

        auto updateBarrierMask{[&](HostTextureView *view, bool isColor) {
            if (auto usage{view->texture->GetLastRenderPassUsage()}; usage != texture::RenderPassUsage::None) {
                vk::PipelineStageFlags attachmentDstStageMask{};
                if (isColor)
                    attachmentDstStageMask |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
                else
                    attachmentDstStageMask |= vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;

                dependencyDstStageMask |= attachmentDstStageMask;

                if (usage == texture::RenderPassUsage::RenderTarget)
                    dependencySrcStageMask |= attachmentDstStageMask;
                else if (usage == texture::RenderPassUsage::Sampled)
                    dependencySrcStageMask |= view->texture->GetReadStageMask();
            }
        }};

        if (colorAttachments.size() < pColorAttachments.size()) {
            // If the new attachments are larger than the existing attachments then we need to add them
            colorAttachments.resize(pColorAttachments.size());
            for (size_t i{subsetAttachmentCount}; i < pColorAttachments.size(); i++) {
                colorAttachments[i] = pColorAttachments[i];
                if (pColorAttachments[i])
                    updateBarrierMask(pColorAttachments[i], true);
            }

            if (!depthStencilAttachment && pDepthStencilAttachment) {
                depthStencilAttachment = pDepthStencilAttachment;
                if (pDepthStencilAttachment)
                    updateBarrierMask(pDepthStencilAttachment, false);
            }
        }

        // Note: No need to change the attachments if the new attachments are a subset of the existing attachments

        return true;
    }

    void RenderPassNode::UpdateDependency(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask) {
        dependencySrcStageMask |= srcStageMask;
        dependencyDstStageMask |= dstStageMask;
    }

    bool RenderPassNode::ClearColorAttachment(u32 attachmentIndex, const vk::ClearColorValue &value, GPU &gpu) {
        /*
        auto &attachment{colorAttachments.at(attachmentIndex)};

        if (attachment->hasClearValue && clearValues[attachmentIndex].color.uint32 == value.uint32) {
            return true;
        } else {
            clearValues.resize(attachmentIndex + 1);
            clearValues[attachmentIndex].color = value;
            attachment->hasClearValue = true;
            return true;
        }
        */

        return false;
    }

    bool RenderPassNode::ClearDepthStencilAttachment(const vk::ClearDepthStencilValue &value, GPU &gpu) {
        /*
        auto &attachment{depthStencilAttachment.value()};
        size_t attachmentIndex{colorAttachments.size()};

        if (attachment->hasClearValue && clearValues[attachmentIndex].color.uint32 == value.uint32) {
            return true;
        } else {
            clearValues.resize(attachmentIndex + 1);
            clearValues[attachmentIndex].color = value;
            attachment->hasClearValue = true;
            return true;
        }
        */

        return false;
    }

    vk::RenderPass RenderPassNode::operator()(vk::raii::CommandBuffer &commandBuffer, const std::shared_ptr<FenceCycle> &cycle, GPU &gpu) {
        // TODO: Replace all vector allocations here with a linear allocator
        std::vector<vk::ImageView> vkAttachments;
        std::vector<vk::AttachmentReference> attachmentReferences;
        std::vector<vk::AttachmentDescription> attachmentDescriptions;
        std::vector<vk::FramebufferAttachmentImageInfo> attachmentInfo;

        size_t attachmentCount{colorAttachments.size() + (depthStencilAttachment ? 1 : 0)};
        vkAttachments.reserve(attachmentCount);
        attachmentReferences.reserve(attachmentCount);
        attachmentDescriptions.reserve(attachmentCount);

        auto addAttachment{[&](const Attachment &attachment) {
            auto &view{attachment.view};
            auto &texture{view->hostTexture};
            vkAttachments.push_back(*view->vkView);
            if (gpu.traits.supportsImagelessFramebuffers)
                attachmentInfo.push_back(vk::FramebufferAttachmentImageInfo{
                    .flags = texture->flags,
                    .usage = texture->usage,
                    .width = texture->dimensions.width,
                    .height = texture->dimensions.height,
                    .layerCount = view->range.layerCount,
                    .viewFormatCount = 1,
                    .pViewFormats = &view->format->vkFormat,
                });
            attachmentReferences.push_back(vk::AttachmentReference{
                .attachment = static_cast<u32>(attachmentDescriptions.size()),
                .layout = texture->layout,
            });
            bool hasStencil{view->format->vkAspect & vk::ImageAspectFlagBits::eStencil};
            attachmentDescriptions.push_back(vk::AttachmentDescription{
                .format = view->format->vkFormat,
                .samples = texture->sampleCount,
                .loadOp = attachment.hasClearValue ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad,
                .storeOp = vk::AttachmentStoreOp::eStore,
                .stencilLoadOp = hasStencil ? (attachment.hasClearValue ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad) : vk::AttachmentLoadOp::eDontCare,
                .stencilStoreOp = hasStencil ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
                .initialLayout = texture->layout,
                .finalLayout = texture->layout,
            });
        }};

        for (const auto &attachment : colorAttachments) {
            if (attachment && attachment->view)
                addAttachment(*attachment);
            else
                attachmentReferences.push_back(vk::AttachmentReference{
                    .attachment = VK_ATTACHMENT_UNUSED,
                    .layout = vk::ImageLayout::eUndefined,
                });
        }

        if (depthStencilAttachment)
            addAttachment(*depthStencilAttachment);

        u32 colorAttachmentCount{static_cast<u32>(colorAttachments.size())};
        vk::SubpassDescription subpassDescription{
            .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
            .colorAttachmentCount = colorAttachmentCount,
            .pColorAttachments = reinterpret_cast<vk::AttachmentReference *>(attachmentReferences.data()),
            .pDepthStencilAttachment = reinterpret_cast<vk::AttachmentReference *>(depthStencilAttachment ? attachmentReferences.data() + colorAttachmentCount : nullptr),
        };

        if (dependencyDstStageMask && dependencySrcStageMask) {
            commandBuffer.pipelineBarrier(dependencySrcStageMask, dependencyDstStageMask, {}, {vk::MemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
                .dstAccessMask = vk::AccessFlagBits::eMemoryWrite | vk::AccessFlagBits::eMemoryRead,
            }}, {}, {});
        }

        auto renderPass{gpu.renderPassCache.GetRenderPass(vk::RenderPassCreateInfo{
            .attachmentCount = static_cast<u32>(attachmentDescriptions.size()),
            .pAttachments = attachmentDescriptions.data(),
            .subpassCount = 1,
            .pSubpasses = &subpassDescription,
            //.dependencyCount = 1,
            //.pDependencies = &subpassDependency,
        })};

        auto useImagelessFramebuffer{gpu.traits.supportsImagelessFramebuffers};
        cache::FramebufferCreateInfo framebufferCreateInfo{
            vk::FramebufferCreateInfo{
                .flags = useImagelessFramebuffer ? vk::FramebufferCreateFlagBits::eImageless : vk::FramebufferCreateFlags{},
                .renderPass = renderPass,
                .attachmentCount = static_cast<u32>(vkAttachments.size()),
                .pAttachments = vkAttachments.data(),
                .width = renderArea.extent.width + static_cast<u32>(renderArea.offset.x),
                .height = renderArea.extent.height + static_cast<u32>(renderArea.offset.y),
                .layers = 1,
            },
            vk::FramebufferAttachmentsCreateInfo{
                .attachmentImageInfoCount = static_cast<u32>(attachmentInfo.size()),
                .pAttachmentImageInfos = attachmentInfo.data(),
            }
        };

        if (!useImagelessFramebuffer)
            framebufferCreateInfo.unlink<vk::FramebufferAttachmentsCreateInfo>();

        auto framebuffer{gpu.framebufferCache.GetFramebuffer(framebufferCreateInfo)};

        vk::StructureChain<vk::RenderPassBeginInfo, vk::RenderPassAttachmentBeginInfo> renderPassBeginInfo{
            vk::RenderPassBeginInfo{
                .renderPass = renderPass,
                .framebuffer = framebuffer,
                .renderArea = renderArea,
                .clearValueCount = static_cast<u32>(clearValues.size()),
                .pClearValues = clearValues.data(),
            },
            vk::RenderPassAttachmentBeginInfo{
                .attachmentCount = static_cast<u32>(vkAttachments.size()),
                .pAttachments = vkAttachments.data(),
            }
        };

        if (!useImagelessFramebuffer)
            renderPassBeginInfo.unlink<vk::RenderPassAttachmentBeginInfo>();

        commandBuffer.beginRenderPass(renderPassBeginInfo.get<vk::RenderPassBeginInfo>(), vk::SubpassContents::eInline);

        return renderPass;
    }
}
