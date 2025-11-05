#pragma once

#include <vulkan/vulkan.hpp>

namespace skyline::gpu {

/**
 * @brief A wrapper for vk::PipelineStageFlags / vk::PipelineStageFlags2
 *        that allows code to work with both legacy and Synchronization2.
 */
struct StageMask {
    uint64_t mask{0};

    constexpr StageMask() = default;
    constexpr explicit StageMask(uint64_t value) : mask(value) {}

    constexpr StageMask(vk::PipelineStageFlags flags)
        : mask(static_cast<uint64_t>(static_cast<VkPipelineStageFlags>(flags))) {}

    constexpr StageMask(vk::PipelineStageFlags2 flags)
        : mask(static_cast<uint64_t>(static_cast<VkPipelineStageFlags2>(flags))) {}

    constexpr operator vk::PipelineStageFlags() const {
        return static_cast<vk::PipelineStageFlags>(
            static_cast<VkPipelineStageFlags>(mask));
    }

    constexpr operator vk::PipelineStageFlags2() const {
        return static_cast<vk::PipelineStageFlags2>(
            static_cast<VkPipelineStageFlags2>(mask));
    }

    constexpr StageMask& operator|=(StageMask rhs) {
        mask |= rhs.mask;
        return *this;
    }

    constexpr StageMask operator|(StageMask rhs) const {
        return StageMask(mask | rhs.mask);
    }

    constexpr StageMask operator&(StageMask rhs) const {
        return StageMask(mask & rhs.mask);
    }

    constexpr bool operator==(const StageMask& rhs) const { return mask == rhs.mask; }
    constexpr bool operator!=(const StageMask& rhs) const { return mask != rhs.mask; }
};

/**
 * @brief A wrapper for vk::AccessFlags / vk::AccessFlags2
 *        that allows code to work with both legacy and Synchronization2.
 */
struct AccessMask {
    uint64_t mask{0};

    constexpr AccessMask() = default;
    constexpr explicit AccessMask(uint64_t value) : mask(value) {}

    constexpr AccessMask(vk::AccessFlags flags)
        : mask(static_cast<uint64_t>(static_cast<VkAccessFlags>(flags))) {}

    constexpr AccessMask(vk::AccessFlags2 flags)
        : mask(static_cast<uint64_t>(static_cast<VkAccessFlags2>(flags))) {}

    constexpr operator vk::AccessFlags() const {
        return static_cast<vk::AccessFlags>(
            static_cast<VkAccessFlags>(mask));
    }

    constexpr operator vk::AccessFlags2() const {
        return static_cast<vk::AccessFlags2>(
            static_cast<VkAccessFlags2>(mask));
    }

    constexpr AccessMask& operator|=(AccessMask rhs) {
        mask |= rhs.mask;
        return *this;
    }

    constexpr AccessMask operator|(AccessMask rhs) const {
        return AccessMask(mask | rhs.mask);
    }

    constexpr AccessMask operator&(AccessMask rhs) const {
        return AccessMask(mask & rhs.mask);
    }

    constexpr bool operator==(const AccessMask& rhs) const { return mask == rhs.mask; }
    constexpr bool operator!=(const AccessMask& rhs) const { return mask != rhs.mask; }
};

} // namespace skyline::gpu
