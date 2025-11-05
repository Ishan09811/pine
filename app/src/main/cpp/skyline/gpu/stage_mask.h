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

    constexpr StageMask(vk::PipelineStageFlags flags)
        : mask(static_cast<uint64_t>(flags)) {}
    constexpr StageMask(vk::PipelineStageFlags2 flags)
        : mask(static_cast<uint64_t>(flags)) {}

    constexpr operator vk::PipelineStageFlags() const {
        return static_cast<vk::PipelineStageFlags>(mask);
    }
    constexpr operator vk::PipelineStageFlags2() const {
        return static_cast<vk::PipelineStageFlags2>(mask);
    }

    constexpr StageMask& operator|=(StageMask rhs) {
        mask |= rhs.mask;
        return *this;
    }
    constexpr StageMask operator|(StageMask rhs) const {
        return StageMask{mask | rhs.mask};
    }

    constexpr bool operator==(const StageMask& rhs) const {
        return mask == rhs.mask;
    }
    constexpr bool operator!=(const StageMask& rhs) const {
        return mask != rhs.mask;
    }

    constexpr StageMask operator&(StageMask rhs) const {
        return StageMask{mask & rhs.mask};
    }
};

/**
 * @brief A wrapper for vk::AccessFlags / vk::AccessFlags2
 *        that allows code to work with both legacy and Synchronization2.
 */
struct AccessMask {
    uint64_t mask{0};

    constexpr AccessMask() = default;

    constexpr AccessMask(vk::AccessFlags flags)
        : mask(static_cast<uint64_t>(flags)) {}
    constexpr AccessMask(vk::AccessFlags2 flags)
        : mask(static_cast<uint64_t>(flags)) {}

    constexpr operator vk::AccessFlags() const {
        return static_cast<vk::AccessFlags>(mask);
    }
    constexpr operator vk::AccessFlags2() const {
        return static_cast<vk::AccessFlags2>(mask);
    }

    constexpr AccessMask& operator|=(AccessMask rhs) {
        mask |= rhs.mask;
        return *this;
    }
    constexpr AccessMask operator|(AccessMask rhs) const {
        return AccessMask{mask | rhs.mask};
    }

    constexpr bool operator==(const AccessMask& rhs) const {
        return mask == rhs.mask;
    }
    constexpr bool operator!=(const AccessMask& rhs) const {
        return mask != rhs.mask;
    }

    constexpr AccessMask operator&(AccessMask rhs) const {
        return AccessMask{mask & rhs.mask};
    }
};

} // namespace skyline::gpu
