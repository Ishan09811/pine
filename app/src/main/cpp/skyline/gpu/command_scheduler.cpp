// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <gpu.h>
#include <loader/loader.h>
#include <vulkan/vulkan.hpp>
#include "command_scheduler.h"
#include "common/exception.h"

namespace skyline::gpu {
    void CommandScheduler::WaiterThread() {
        if (int result{pthread_setname_np(pthread_self(), "Sky-CycleWaiter")})
            LOGW("Failed to set the thread name: {}", strerror(result));
        AsyncLogger::UpdateTag();

        try {
            cycleQueue.Process([](const std::shared_ptr<FenceCycle> &cycle) {
                cycle->Wait(true);
            }, [] {});
        } catch (const signal::SignalException &e) {
            LOGE("{}\nStack Trace:{}", e.what(), state.loader->GetStackTrace(e.frames));
            if (state.process)
                state.process->Kill(false);
            else
                std::rethrow_exception(std::current_exception());
        } catch (const std::exception &e) {
            LOGE("{}", e.what());
            if (state.process)
                state.process->Kill(false);
            else
                std::rethrow_exception(std::current_exception());
        }
    }

    CommandScheduler::CommandBufferSlot::CommandBufferSlot(vk::raii::Device &device, vk::CommandBuffer commandBuffer, vk::raii::CommandPool &pool)
        : device{device},
          commandBuffer{device, static_cast<VkCommandBuffer>(commandBuffer), static_cast<VkCommandPool>(*pool)},
          fence{device, vk::FenceCreateInfo{}},
          semaphore{device, vk::SemaphoreCreateInfo{}},
          cycle{std::make_shared<FenceCycle>(device, *fence, *semaphore)} {}

    CommandScheduler::CommandScheduler(const DeviceState &state, GPU &pGpu)
        : state{state},
          gpu{pGpu},
          waiterThread{&CommandScheduler::WaiterThread, this},
          pool{std::ref(pGpu.vkDevice), vk::CommandPoolCreateInfo{
              .flags = vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
              .queueFamilyIndex = pGpu.vkQueueFamilyIndex,
          }} {}

    CommandScheduler::~CommandScheduler() {
        waiterThread.join();
    }

    CommandScheduler::ActiveCommandBuffer CommandScheduler::AllocateCommandBuffer() {
        for (auto &slot : pool->buffers) {
            if (!slot.active.test_and_set(std::memory_order_acq_rel)) {
                if (slot.cycle->Poll()) {
                    slot.commandBuffer.reset();
                    slot.cycle = std::make_shared<FenceCycle>(*slot.cycle);
                    return {slot};
                } else {
                    slot.active.clear(std::memory_order_release);
                }
            }
        }

        vk::CommandBuffer commandBuffer;
        vk::CommandBufferAllocateInfo commandBufferAllocateInfo{
            .commandPool = *pool->vkCommandPool,
            .level = vk::CommandBufferLevel::ePrimary,
            .commandBufferCount = 1,
        };

        auto result{(*gpu.vkDevice).allocateCommandBuffers(&commandBufferAllocateInfo, &commandBuffer, *gpu.vkDevice.getDispatcher())};
        if (result != vk::Result::eSuccess)
            vk::detail::throwResultException(result, __builtin_FUNCTION());
        return {pool->buffers.emplace_back(gpu.vkDevice, commandBuffer, pool->vkCommandPool)};
    }

    void CommandScheduler::SubmitCommandBuffer(
        const vk::raii::CommandBuffer &commandBuffer,
        std::shared_ptr<FenceCycle> cycle,
        span<vk::Semaphore> waitSemaphores,
        span<vk::Semaphore> signalSemaphores
    ) {
        if (gpu.traits.supportsSynchronization2) {
            boost::container::small_vector<vk::SemaphoreSubmitInfo, 4> waitInfos;
            waitInfos.reserve(waitSemaphores.size() + 1);

            for (auto &sem : waitSemaphores) {
                waitInfos.push_back(vk::SemaphoreSubmitInfo{
                    .semaphore = sem,
                    .value = 0,
                    .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
                    .deviceIndex = 0,
                });
            }

            if (cycle->semaphoreSubmitWait) {
                waitInfos.push_back(vk::SemaphoreSubmitInfo{
                    .semaphore = cycle->semaphore,
                    .value = 0,
                    .stageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                    .deviceIndex = 0,
                });
            }

            boost::container::small_vector<vk::SemaphoreSubmitInfo, 3> signalInfos;
            signalInfos.reserve(signalSemaphores.size() + 1);

            for (auto &sem : signalSemaphores) {
                signalInfos.push_back(vk::SemaphoreSubmitInfo{
                    .semaphore = sem,
                    .value = 0,
                    .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
                    .deviceIndex = 0,
                });
            }

            signalInfos.push_back(vk::SemaphoreSubmitInfo{
                .semaphore = cycle->semaphore,
                .value = 0,
                .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
                .deviceIndex = 0,
            });

            vk::CommandBufferSubmitInfo cmdBufferInfo{
                .commandBuffer = *commandBuffer,
                .deviceMask = 0,
            };

            vk::SubmitInfo2 submitInfo{
                .waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size()),
                .pWaitSemaphoreInfos = waitInfos.data(),
                .commandBufferInfoCount = 1,
                .pCommandBufferInfos = &cmdBufferInfo,
                .signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size()),
                .pSignalSemaphoreInfos = signalInfos.data(),
            };

            try {
                std::scoped_lock lock{gpu.queueMutex};
                gpu.vkQueue.submit2(submitInfo, cycle->fence);
            } catch (const vk::DeviceLostError &) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                throw exception("Vulkan device lost!");
            }
        } else {
            boost::container::small_vector<vk::Semaphore, 3> fullWaitSemaphores{waitSemaphores.begin(), waitSemaphores.end()};
            boost::container::small_vector<vk::PipelineStageFlags, 3> fullWaitStages{waitSemaphores.size(), vk::PipelineStageFlagBits::eAllCommands};

            if (cycle->semaphoreSubmitWait) {
                fullWaitSemaphores.push_back(cycle->semaphore);
                fullWaitStages.push_back(vk::PipelineStageFlagBits::eTopOfPipe);
            }

            boost::container::small_vector<vk::Semaphore, 2> fullSignalSemaphores{signalSemaphores.begin(), signalSemaphores.end()};
            fullSignalSemaphores.push_back(cycle->semaphore);

            vk::SubmitInfo submitInfo{
                .commandBufferCount = 1,
                .pCommandBuffers = &*commandBuffer,
                .waitSemaphoreCount = static_cast<uint32_t>(fullWaitSemaphores.size()),
                .pWaitSemaphores = fullWaitSemaphores.data(),
                .pWaitDstStageMask = fullWaitStages.data(),
                .signalSemaphoreCount = static_cast<uint32_t>(fullSignalSemaphores.size()),
                .pSignalSemaphores = fullSignalSemaphores.data(),
            };

            try {
                std::scoped_lock lock{gpu.queueMutex};
                gpu.vkQueue.submit(submitInfo, cycle->fence);
            } catch (const vk::DeviceLostError &) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                throw exception("Vulkan device lost!");
            }
        }

        cycle->NotifySubmitted();
        cycleQueue.Push(cycle);
    }
}
