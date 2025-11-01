// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <gpu.h>
#include <soc/gm20b/engines/maxwell/types.h>
#include "descriptor_allocator.h"

namespace skyline::gpu {
    DescriptorAllocator::DescriptorSetSlot::DescriptorSetSlot(vk::DescriptorSet descriptorSet) noexcept : descriptorSet{descriptorSet} {}

    DescriptorAllocator::DescriptorSetSlot::DescriptorSetSlot(DescriptorAllocator::DescriptorSetSlot &&other) noexcept : descriptorSet{other.descriptorSet} {
        other.descriptorSet = nullptr;
    }

    DescriptorAllocator::DescriptorPool::DescriptorPool(const vk::raii::Device &device, const vk::DescriptorPoolCreateInfo &createInfo) : vk::raii::DescriptorPool{device, createInfo}, freeSetCount{createInfo.maxSets} {}

    void DescriptorAllocator::AllocateDescriptorPool() {
        namespace maxwell3d = soc::gm20b::engine::maxwell3d::type; // We use Maxwell3D as reference for base descriptor counts
        using DescriptorSizes = std::array<vk::DescriptorPoolSize, 6>;

        constexpr DescriptorSizes BaseDescriptorSizes{
            vk::DescriptorPoolSize{ .descriptorCount = 512, .type = vk::DescriptorType::eUniformBuffer },
            vk::DescriptorPoolSize{ .descriptorCount = 64,  .type = vk::DescriptorType::eStorageBuffer },
            vk::DescriptorPoolSize{ .descriptorCount = 256, .type = vk::DescriptorType::eCombinedImageSampler },
            vk::DescriptorPoolSize{ .descriptorCount = 16,  .type = vk::DescriptorType::eStorageImage },
            vk::DescriptorPoolSize{ .descriptorCount = 4,   .type = vk::DescriptorType::eUniformTexelBuffer },
            vk::DescriptorPoolSize{ .descriptorCount = 4,   .type = vk::DescriptorType::eStorageTexelBuffer },
        };

        DescriptorSizes descriptorSizes{BaseDescriptorSizes};
        for (auto &descriptorSize : descriptorSizes)
            descriptorSize.descriptorCount *= descriptorMultiplier;

        pool = std::make_shared<DescriptorPool>(gpu.vkDevice, vk::DescriptorPoolCreateInfo{
            .maxSets = descriptorSetCount,
            .pPoolSizes = descriptorSizes.data(),
            .poolSizeCount = static_cast<uint32_t>(descriptorSizes.size()),
        });

        pool->freeSetCount = descriptorSetCount;
    }

    inline vk::Result DescriptorAllocator::AllocateVkDescriptorSetInternal(vk::DescriptorSetLayout layout, vk::DescriptorSet &outSet) {
        vk::DescriptorSetAllocateInfo allocateInfo{
            .descriptorPool = **pool,
            .pSetLayouts = &layout,
            .descriptorSetCount = 1,
        };
        return (*gpu.vkDevice).allocateDescriptorSets(&allocateInfo, &outSet, *gpu.vkDevice.getDispatcher());
    }

    vk::ResultValue<vk::DescriptorSet> DescriptorAllocator::AllocateVkDescriptorSet(vk::DescriptorSetLayout layout) {
        vk::DescriptorSet descriptorSet{};
        auto result = AllocateVkDescriptorSetInternal(layout, descriptorSet);

        if (pool->freeSetCount > 0)
            --pool->freeSetCount;

        if (result == vk::Result::eSuccess)
            return vk::ResultValue<vk::DescriptorSet>(vk::Result::eSuccess, descriptorSet);
        else
            return vk::ResultValue<vk::DescriptorSet>(result, vk::DescriptorSet{});
    }

    DescriptorAllocator::ActiveDescriptorSet::ActiveDescriptorSet(std::shared_ptr<DescriptorPool> pPool, DescriptorSetSlot *slot) noexcept : pool{std::move(pPool)}, slot{slot} {}

    DescriptorAllocator::ActiveDescriptorSet::ActiveDescriptorSet(DescriptorAllocator::ActiveDescriptorSet &&other) noexcept {
        pool = std::move(other.pool);
        slot = std::exchange(other.slot, nullptr);
    }

    DescriptorAllocator::ActiveDescriptorSet::~ActiveDescriptorSet() {
        if (slot) {
            slot->active.clear(std::memory_order_release);
        }
    }

    DescriptorAllocator::DescriptorAllocator(GPU &gpu) : gpu{gpu} {
        AllocateDescriptorPool();
    }

    DescriptorAllocator::ActiveDescriptorSet DescriptorAllocator::AllocateSet(vk::DescriptorSetLayout layout) {
        {
            std::shared_lock<std::shared_mutex> readLock{mutex};
            auto it = pool->layoutSlots.find(layout);
            if (it != pool->layoutSlots.end()) {
                auto &slots = it->second;
                for (auto slotIt = slots.begin(); slotIt != slots.end(); ++slotIt) {
                    if (!slotIt->active.test_and_set(std::memory_order_acq_rel)) {
                        if (slots.size() > 4) {
                            readLock.unlock();
                            std::unique_lock<std::shared_mutex> writeLock{mutex};
                            auto it2 = pool->layoutSlots.find(layout);
                            if (it2 != pool->layoutSlots.end()) {
                                auto &slots2 = it2->second;
                                for (auto sIt = slots2.begin(); sIt != slots2.end(); ++sIt) {
                                    if (&*sIt == &*slotIt) {
                                        slots2.splice(slots2.end(), slots2, sIt);
                                        break;
                                    }
                                }
                            }
                            return ActiveDescriptorSet{pool, &*slotIt};
                        }

                        return ActiveDescriptorSet{pool, &*slotIt};
                    }
                }
            }
        }

        std::unique_lock<std::shared_mutex> writeLock{mutex};

        auto it = pool->layoutSlots.find(layout);
        vk::Result lastResult{};
        if (it != pool->layoutSlots.end()) {
            auto &slots = it->second;
            for (auto slotIt = slots.begin(); slotIt != slots.end(); ++slotIt) {
                if (!slotIt->active.test_and_set(std::memory_order_acq_rel)) {
                    // move to end to reduce search time next time
                    slots.splice(slots.end(), slots, slotIt);
                    return ActiveDescriptorSet{pool, &*slotIt};
                }
            }

            // If we couldn't find an available slot, we need to allocate a new one
            vk::DescriptorSet set{};
            auto result = AllocateVkDescriptorSetInternal(layout, set);
            if (result == vk::Result::eSuccess) {
                auto &slot = slots.emplace_back(set);
                slot.active.test_and_set(std::memory_order_release);
                return ActiveDescriptorSet{pool, &slot};
            } else {
                lastResult = result;
            }
        } else {
            // If we couldn't find a layout, we need to allocate a new one
            vk::DescriptorSet set{};
            auto result = AllocateVkDescriptorSetInternal(layout, set);
            if (result == vk::Result::eSuccess) {
                auto &layoutSlots = pool->layoutSlots.try_emplace(layout).first->second;
                auto &slot = layoutSlots.emplace_back(set);
                slot.active.test_and_set(std::memory_order_release);
                return ActiveDescriptorSet{pool, &slot};
            } else {
                lastResult = result;
            }
        }

        for (;;) {
            // We attempt to modify the pool based on the last result
            if (lastResult == vk::Result::eErrorOutOfPoolMemory) {
                if (pool->freeSetCount == 0)
                    // The amount of maximum descriptor sets is insufficient
                    descriptorSetCount += DescriptorSetCountIncrement;
                else
                    // The amount of maximum descriptors is insufficient
                    descriptorMultiplier++;
                AllocateDescriptorPool();
            } else if (lastResult == vk::Result::eErrorFragmentedPool) {
                AllocateDescriptorPool(); // If the pool is fragmented, we reallocate without increasing the size
            }

            // Try to allocate a new layout
            vk::DescriptorSet set{};
            auto result = AllocateVkDescriptorSetInternal(layout, set);
            if (result == vk::Result::eSuccess) {
                auto &layoutSlots = pool->layoutSlots.try_emplace(layout).first->second;
                auto &slot = layoutSlots.emplace_back(set);
                slot.active.test_and_set(std::memory_order_release);
                return ActiveDescriptorSet{pool, &slot};
            } else {
                lastResult = result;
            }
        }
    }
}
