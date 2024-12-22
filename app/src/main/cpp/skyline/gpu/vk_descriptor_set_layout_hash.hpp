#ifndef VK_DESCRIPTOR_SET_LAYOUT_HASH_HPP
#define VK_DESCRIPTOR_SET_LAYOUT_HASH_HPP

#include <vulkan/vulkan.hpp>
#include <unordered_map>

// Specialization of std::hash for vk::DescriptorSetLayout
namespace std {
    template <>
    struct hash<vk::DescriptorSetLayout> {
        std::size_t operator()(const vk::DescriptorSetLayout &layout) const noexcept {
            // Use the raw Vulkan handle for hashing
            return std::hash<VkDescriptorSetLayout>()(static_cast<VkDescriptorSetLayout>(layout));
        }
    };
}

#endif // VK_DESCRIPTOR_SET_LAYOUT_HASH_HPP
