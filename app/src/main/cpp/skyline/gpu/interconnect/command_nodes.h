// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <gpu.h>

namespace skyline::gpu::interconnect::node {
    /**
     * @brief A generic node for simply executing a function
     */
    template<typename FunctionSignature = void(vk::raii::CommandBuffer &, const std::shared_ptr<FenceCycle> &, GPU &)>
    struct FunctionNodeBase {
        std::function<FunctionSignature> function;

        FunctionNodeBase(std::function<FunctionSignature> &&function) : function(function) {}

        template<class... Args>
        void operator()(Args &&... args) {
            function(std::forward<Args>(args)...);
        }
    };

    using FunctionNode = FunctionNodeBase<>;

    /**
     * @brief Creates and begins a VkRenderPass alongside managing all resources bound to it and to the subpasses inside it
     */
    struct RenderPassNode {
      private:
        struct Attachment {
            HostTextureView *view{};
            bool hasClearValue{}; //!< If the attachment has a clear value and should use VK_ATTACHMENT_LOAD_OP_CLEAR

            Attachment(HostTextureView *view) : view{view} {}
        };
        std::vector<std::optional<Attachment>> colorAttachments; //!< The color attachments bound to the current subpass
        std::optional<Attachment> depthStencilAttachment; //!< The depth stencil attachment bound to the current subpass

      public:
        vk::PipelineStageFlags dependencySrcStageMask;
        vk::PipelineStageFlags dependencyDstStageMask;

        vk::Rect2D renderArea;
        std::vector<vk::ClearValue> clearValues;

        RenderPassNode(vk::Rect2D renderArea, span<HostTextureView *> colorAttachments, HostTextureView *depthStencilAttachment);

        /**
         * @brief Sets the attachments bound to the renderpass
         * @return If the attachments could be bound or not due to conflicts with existing attachments
         */
        bool BindAttachments(span<HostTextureView *> colorAttachments, HostTextureView *depthStencilAttachment);

        /**
         * @brief Updates the dependency barrier for the renderpass
         */
        void UpdateDependency(vk::PipelineStageFlags srcStageMask, vk::PipelineStageFlags dstStageMask);

        /**
         * @brief Clears a color attachment in the current subpass with VK_ATTACHMENT_LOAD_OP_CLEAR
         * @param colorAttachment The index of the attachment in the attachments bound to the current subpass
         * @return If the attachment could be cleared or not due to conflicts with other operations
         * @note We require a subpass to be attached during this as the clear will not take place unless it's referenced by a subpass
         */
        bool ClearColorAttachment(u32 colorAttachment, const vk::ClearColorValue &value, GPU& gpu);

        /**
         * @brief Clears the depth/stencil attachment in the current subpass with VK_ATTACHMENT_LOAD_OP_CLEAR
         * @return If the attachment could be cleared or not due to conflicts with other operations
         * @note We require a subpass to be attached during this as the clear will not take place unless it's referenced by a subpass
         */
        bool ClearDepthStencilAttachment(const vk::ClearDepthStencilValue &value, GPU& gpu);

        vk::RenderPass operator()(vk::raii::CommandBuffer &commandBuffer, const std::shared_ptr<FenceCycle> &cycle, GPU &gpu);
    };

    /**
     * @brief Ends a VkRenderPass that would be created prior with RenderPassNode
     */
    struct RenderPassEndNode {
        void operator()(vk::raii::CommandBuffer &commandBuffer, const std::shared_ptr<FenceCycle> &cycle, GPU &gpu) {
            commandBuffer.endRenderPass();
        }
    };

    using NodeVariant = std::variant<FunctionNode, RenderPassNode, RenderPassEndNode>; //!< A variant encompassing all command nodes types
}
