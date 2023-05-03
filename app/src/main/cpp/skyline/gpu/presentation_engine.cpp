// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <android/native_window_jni.h>
#include <android/choreographer.h>
#include <common/settings.h>
#include <common/signal.h>
#include <jvm.h>
#include <gpu.h>
#include <soc.h>
#include <loader/loader.h>
#include <kernel/types/KProcess.h>
#include <vulkan/vulkan_enums.hpp>
#include "presentation_engine.h"
#include "native_window.h"
#include "texture/formats.h"

extern jint Fps;
extern jfloat AverageFrametimeMs;
extern jfloat AverageFrametimeDeviationMs;

namespace skyline::gpu {
    using namespace service::hosbinder;

    std::atomic<bool> paused{false};

    PresentationEngine::PresentationEngine(const DeviceState &state, GPU &gpu)
        : state{state},
          gpu{gpu},
          images{util::MakeFilledArray<SwapchainImage, MaxSwapchainSlotCount>(gpu.vkDevice)},
          semaphorePool{util::MakeFilledArray<SemaphoreEntry, MaxSwapchainSlotCount>(gpu.vkDevice)},
          presentationTrack{static_cast<u64>(trace::TrackIds::Presentation), perfetto::ProcessTrack::Current()},
          vsyncEvent{std::make_shared<kernel::type::KEvent>(state, true)},
          choreographerThread{&PresentationEngine::ChoreographerThread, this},
          presentationThread{&PresentationEngine::PresentationThread, this} {
        auto desc{presentationTrack.Serialize()};
        desc.set_name("Presentation");
        perfetto::TrackEvent::SetTrackDescriptor(presentationTrack, desc);
    }

    PresentationEngine::~PresentationEngine() {
        auto env{state.jvm->GetEnv()};
        if (!env->IsSameObject(jSurface, nullptr))
            env->DeleteGlobalRef(jSurface);

        if (choreographerThread.joinable()) {
            if (choreographerLooper) {
                choreographerStop = true;
                ALooper_wake(choreographerLooper);
            }
            choreographerThread.join();
        }
    }

    void PresentationEngine::ChoreographerCallback(int64_t frameTimeNanos, PresentationEngine *engine) {
        // If the duration of this cycle deviates by ±0.5ms from the current refresh cycle duration then we reevaluate it
        i64 cycleLength{frameTimeNanos - engine->lastChoreographerTime};
        if (std::abs(cycleLength - engine->refreshCycleDuration) > (constant::NsInMillisecond / 2)) {
            if (engine->window)
                engine->window->perform(engine->window, NATIVE_WINDOW_GET_REFRESH_CYCLE_DURATION, &engine->refreshCycleDuration);
            else
                engine->refreshCycleDuration = cycleLength;
        }

        // Record the current cycle's timestamp and signal the V-Sync event to notify the game that a frame has been displayed
        engine->lastChoreographerTime = frameTimeNanos;
        if (!engine->skipSignal.exchange(false))
            engine->vsyncEvent->Signal();

        // Post the frame callback to be triggered on the next display refresh
        AChoreographer_postFrameCallback64(AChoreographer_getInstance(), reinterpret_cast<AChoreographer_frameCallback64>(&ChoreographerCallback), engine);
    }

    void PresentationEngine::ChoreographerThread() {
        if (int result{pthread_setname_np(pthread_self(), "Sky-Choreo")})
            LOGW("Failed to set the thread name: {}", strerror(result));
        AsyncLogger::UpdateTag();

        try {
            choreographerLooper = ALooper_prepare(0);
            AChoreographer_postFrameCallback64(AChoreographer_getInstance(), reinterpret_cast<AChoreographer_frameCallback64>(&ChoreographerCallback), this);


            while (ALooper_pollOnce(-1, nullptr, nullptr, nullptr) == ALOOPER_POLL_WAKE && !choreographerStop) {
                while (paused.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }

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

    std::shared_ptr<FenceCycle> PresentationEngine::CopyIntoSwapchain(HostTextureView *textureView, SwapchainImage &image, vk::Semaphore acquireSemaphore) {
        auto &texture{textureView->hostTexture};

        if (texture->layout != vk::ImageLayout::eGeneral && texture->layout != vk::ImageLayout::eTransferSrcOptimal)
            throw exception("Source texture layout is {} but must be either eGeneral or eTransferSrcOptimal", vk::to_string(texture->layout));
        if (texture->dimensions != swapchainExtent)
            throw exception("Texture dimensions ({}, {}) do not match swapchain dimensions ({}, {})", texture->dimensions.width, texture->dimensions.height, swapchainExtent.width, swapchainExtent.height);

        TRACE_EVENT("gpu", "PresentationEngine::CopyIntoSwapchain");

        vk::Semaphore presentSemaphore{*image.presentSemaphore};
        auto cycle{gpu.scheduler.Submit([&](vk::raii::CommandBuffer &commandBuffer) {
            vk::ImageSubresourceRange subresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
            vk::ImageMemoryBarrier imageBarrier{
                .srcAccessMask = vk::AccessFlagBits::eMemoryRead,
                .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
                .oldLayout = image.layout,
                .newLayout = vk::ImageLayout::eTransferDstOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = image.vkImage,
                .subresourceRange = subresourceRange,
            };
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, imageBarrier);

            auto &subresource{textureView->range};
            vk::ImageSubresourceLayers subresourceLayers{
                .aspectMask = subresource.aspectMask,
                .mipLevel = subresource.baseMipLevel,
                .baseArrayLayer = subresource.baseArrayLayer,
                .layerCount = subresource.layerCount,
            };

            auto &dimensions{texture->dimensions};
            if (textureView->format != swapchainFormat) {
                commandBuffer.blitImage(image.vkImage, texture->layout, texture->GetImage(), vk::ImageLayout::eTransferDstOptimal, vk::ImageBlit{
                    .srcSubresource = subresourceLayers,
                    .srcOffsets = std::array<vk::Offset3D, 2>{
                        vk::Offset3D{0, 0, 0},
                        vk::Offset3D{static_cast<i32>(dimensions.width),
                                     static_cast<i32>(dimensions.height),
                                     static_cast<i32>(subresourceLayers.layerCount)}
                    },
                    .dstSubresource = subresourceLayers,
                    .dstOffsets = std::array<vk::Offset3D, 2>{
                        vk::Offset3D{0, 0, 0},
                        vk::Offset3D{static_cast<i32>(dimensions.width),
                                     static_cast<i32>(dimensions.height),
                                     static_cast<i32>(subresourceLayers.layerCount)}
                    }
                }, vk::Filter::eLinear);
            } else {
                commandBuffer.copyImage(texture->GetImage(), texture->layout, image.vkImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageCopy{
                    .srcSubresource = subresourceLayers,
                    .dstSubresource = subresourceLayers,
                    .extent = dimensions,
                });
            }

            imageBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            imageBarrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
            imageBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            image.layout = imageBarrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
            commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands, {}, {}, {}, imageBarrier);
        }, span<vk::Semaphore>{acquireSemaphore}, span<vk::Semaphore>{presentSemaphore})};
        textureView->texture->AttachCycle(cycle);
        return cycle;
    }

    void PresentationEngine::PresentFrame(const PresentableFrame &frame) {
        std::unique_lock lock{mutex};
        surfaceCondition.wait(lock, [this]() { return vkSurface.has_value(); });

        frame.fence.Wait(state.soc->host1x);

        auto &textureView{frame.textureView};
        std::scoped_lock textureLock{*textureView};
        if (textureView->stale)
            return; // Simply skip this frame if the texture is stale, this isn't optimal and the texture should be looked up again but this is an edge case so this should be fine

        auto texture{textureView->texture}; // A shared_ptr to the texture is needed to keep it alive until the frame is presented
        if (textureView->format != swapchainFormat || textureView->hostTexture->dimensions != swapchainExtent)
            UpdateSwapchain(textureView->format, textureView->hostTexture->dimensions);

        int result;
        if (frame.crop && frame.crop != windowCrop) {
            if ((result = window->perform(window, NATIVE_WINDOW_SET_CROP, &frame.crop)))
                throw exception("Setting the layer crop to ({}-{})x({}-{}) failed with {}", frame.crop.left, frame.crop.right, frame.crop.top, frame.crop.bottom, result);
            windowCrop = frame.crop;
        }

        if (frame.scalingMode != NativeWindowScalingMode::Freeze && windowScalingMode != frame.scalingMode) {
            if ((result = window->perform(window, NATIVE_WINDOW_SET_SCALING_MODE, static_cast<i32>(frame.scalingMode))))
                throw exception("Setting the layer scaling mode to '{}' failed with {}", ToString(frame.scalingMode), result);
            windowScalingMode = frame.scalingMode;
        }

        if ((result = window->perform(window, NATIVE_WINDOW_SET_BUFFERS_TRANSFORM, static_cast<i32>(frame.transform))))
            throw exception("Setting the buffer transform to '{}' failed with {}", ToString(frame.transform), result);
        windowTransform = frame.transform;

        auto &currentSlot{semaphorePool.at(semaphoreIndex)};
        semaphoreIndex = (semaphoreIndex + 1) % semaphorePool.size(); // Increment the semaphore index and wrap around if it exceeds the pool size
        currentSlot.WaitTillAvailable();

        std::pair<vk::Result, u32> vkNextImage;
        while (vkNextImage = vkSwapchain->acquireNextImage(std::numeric_limits<u64>::max(), *currentSlot.semaphore, {}), vkNextImage.first != vk::Result::eSuccess) [[unlikely]] {
            if (vkNextImage.first == vk::Result::eSuboptimalKHR)
                surfaceCondition.wait(lock, [this]() { return vkSurface.has_value(); });
            else
                throw exception("vkAcquireNextImageKHR returned an unhandled result '{}'", vk::to_string(vkNextImage.first));
        }

        auto &swapchainImage{images.at(vkNextImage.second)};
        currentSlot.freeCycle = CopyIntoSwapchain(textureView, swapchainImage, *currentSlot.semaphore);

        auto getMonotonicNsNow{[]() -> i64 {
            timespec time{};
            if (clock_gettime(CLOCK_MONOTONIC, &time))
                throw exception("Failed to clock_gettime with '{}'", strerror(errno));
            return (time.tv_sec * constant::NsInSecond) + time.tv_nsec;
        }};

        i64 timestamp{frame.timestamp};
        if (timestamp) {
            // If the timestamp is specified, we need to convert it from the util::GetTimeNs base to the CLOCK_MONOTONIC one
            // We do so by getting an offset from the current time in nanoseconds and then adding it to the current time in CLOCK_MONOTONIC
            // Note: It's important we do this right before present as going past the timestamp could lead to fewer Binder IPC calls
            i64 current{util::GetTimeNs()};
            if (current < timestamp) {
                timestamp = getMonotonicNsNow() + (timestamp - current);
            } else {
                timestamp = 0;
            }
        }

        if (frame.swapInterval) {
            // If we have a swap interval, we have to adjust the timestamp to emulate the swap interval
            i64 lastFramePresentTime{util::AlignUpNpot(windowLastTimestamp, refreshCycleDuration)};
            if (lastFramePresentTime > lastChoreographerTime)
                // If the last frame was presented after the last choreographer callback, calculate the new frame's timestamp relative to it
                timestamp = std::max(timestamp, lastFramePresentTime + (refreshCycleDuration * frame.swapInterval));
            else
                // If there has been a choreographer callback since the last frame, calculate the new frame's timestamp relative to it
                timestamp = std::max(timestamp, lastChoreographerTime + (2 * refreshCycleDuration * frame.swapInterval));
        }

        i64 lastTimestamp{std::exchange(windowLastTimestamp, timestamp)};
        if (!timestamp && lastTimestamp)
            // We need to nullify the timestamp if it transitioned from being specified (non-zero) to unspecified (zero)
            timestamp = NativeWindowTimestampAuto;

        if (timestamp && (result = window->perform(window, NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP, timestamp)))
            throw exception("Setting the buffer timestamp to {} failed with {}", timestamp, result);

        u64 frameId{};
        if ((result = window->perform(window, NATIVE_WINDOW_GET_NEXT_FRAME_ID, &frameId)))
            throw exception("Retrieving the next frame's ID failed with {}", result);

        {
            std::scoped_lock queueLock{gpu.queueMutex};
            std::ignore = gpu.vkQueue.presentKHR(vk::PresentInfoKHR{
                .swapchainCount = 1,
                .pSwapchains = &**vkSwapchain,
                .pImageIndices = &vkNextImage.second,
                .waitSemaphoreCount = 1,
                .pWaitSemaphores = &*swapchainImage.presentSemaphore,
            }); // We don't care about suboptimal images as they are caused by not respecting the transform hint, we handle transformations externally
        }

        timestamp = (timestamp && (*state.settings->vsyncMode != 0 || *state.settings->vsyncMode != 1)) ? timestamp : getMonotonicNsNow(); // We tie FPS to the submission time rather than presentation timestamp, if we don't have the presentation timestamp available or if frame throttling is disabled as we want the maximum measured FPS to not be restricted to the refresh rate
        if (timestamp > getMonotonicNsNow() || *state.settings->vsyncMode == 0 || *state.settings->vsyncMode == 1) {
            timestamp = getMonotonicNsNow();
        }
        if (frameTimestamp) {
            i64 sampleWeight{Fps ? Fps : 1}; //!< The weight of each sample in calculating the average, we want to roughly average the past second

            auto weightedAverage{[](auto weight, auto previousAverage, auto current) {
                return (((weight - 1) * previousAverage) + current) / weight;
            }}; //!< Modified moving average (https://en.wikipedia.org/wiki/Moving_average#Modified_moving_average)

            i64 currentFrametime{timestamp - frameTimestamp};
            averageFrametimeNs = weightedAverage(sampleWeight, averageFrametimeNs, currentFrametime);
            AverageFrametimeMs = static_cast<jfloat>(averageFrametimeNs) / constant::NsInMillisecond;

            i64 currentFrametimeDeviation{std::abs(averageFrametimeNs - currentFrametime)};
            averageFrametimeDeviationNs = weightedAverage(sampleWeight, averageFrametimeDeviationNs, currentFrametimeDeviation);
            AverageFrametimeDeviationMs = static_cast<jfloat>(averageFrametimeDeviationNs) / constant::NsInMillisecond;

            Fps = static_cast<jint>(std::round(static_cast<float>(constant::NsInSecond) / static_cast<float>(averageFrametimeNs)));

            TRACE_EVENT_INSTANT("gpu", "Present", presentationTrack, "FrameTimeNs", timestamp - frameTimestamp, "Fps", Fps);

            frameTimestamp = timestamp;
        } else {
            frameTimestamp = timestamp;
        }
        if (*state.settings->enableSpeedLimit) LimitSpeed(constant::NsInSecond / 60);
    }

    void PresentationEngine::PresentationThread() {
        if (int result{pthread_setname_np(pthread_self(), "Sky-Present")})
            LOGW("Failed to set the thread name: {}", strerror(result));
        AsyncLogger::UpdateTag();

        try {
            presentQueue.Process([this](const PresentableFrame &frame) {
                while (paused.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                PresentFrame(frame);
                frame.presentCallback(); // We're calling the callback here as it's outside of all the locks in PresentFrame
                skipSignal = true;
                vsyncEvent->Signal();
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

    NativeWindowTransform GetAndroidTransform(vk::SurfaceTransformFlagBitsKHR transform) {
        using NativeWindowTransform = NativeWindowTransform;
        switch (transform) {
            case vk::SurfaceTransformFlagBitsKHR::eIdentity:
            case vk::SurfaceTransformFlagBitsKHR::eInherit:
                return NativeWindowTransform::Identity;
            case vk::SurfaceTransformFlagBitsKHR::eRotate90:
                return NativeWindowTransform::Rotate90;
            case vk::SurfaceTransformFlagBitsKHR::eRotate180:
                return NativeWindowTransform::Rotate180;
            case vk::SurfaceTransformFlagBitsKHR::eRotate270:
                return NativeWindowTransform::Rotate270;
            case vk::SurfaceTransformFlagBitsKHR::eHorizontalMirror:
                return NativeWindowTransform::MirrorHorizontal;
            case vk::SurfaceTransformFlagBitsKHR::eHorizontalMirrorRotate90:
                return NativeWindowTransform::MirrorHorizontalRotate90;
            case vk::SurfaceTransformFlagBitsKHR::eHorizontalMirrorRotate180:
                return NativeWindowTransform::MirrorVertical;
            case vk::SurfaceTransformFlagBitsKHR::eHorizontalMirrorRotate270:
                return NativeWindowTransform::MirrorVerticalRotate90;
        }
    }

    void PresentationEngine::UpdateSwapchain(texture::Format format, texture::Dimensions extent) {
        auto minImageCount{std::max(vkSurfaceCapabilities.minImageCount, *state.settings->forceTripleBuffering ? 3U : 2U)};
        if (minImageCount > MaxSwapchainSlotCount)
            throw exception("Requesting swapchain with higher image count ({}) than maximum slot count ({})", minImageCount, MaxSwapchainSlotCount);

        const auto &capabilities{vkSurfaceCapabilities};
        if (minImageCount < capabilities.minImageCount || (capabilities.maxImageCount && minImageCount > capabilities.maxImageCount))
            throw exception("Cannot update swapchain to accomodate image count: {} ({}-{})", minImageCount, capabilities.minImageCount, capabilities.maxImageCount);
        else if (capabilities.minImageExtent.height > extent.height || capabilities.minImageExtent.width > extent.width || capabilities.maxImageExtent.height < extent.height || capabilities.maxImageExtent.width < extent.width)
            throw exception("Cannot update swapchain to accomodate image extent: {}x{} ({}x{}-{}x{})", extent.width, extent.height, capabilities.minImageExtent.width, capabilities.minImageExtent.height, capabilities.maxImageExtent.width, capabilities.maxImageExtent.height);

        vk::Format vkFormat{*format};
        texture::Format underlyingFormat{format};
        if (swapchainFormat != format) {
            auto formats{gpu.vkPhysicalDevice.getSurfaceFormatsKHR(**vkSurface)};
            if (std::find(formats.begin(), formats.end(), vk::SurfaceFormatKHR{vkFormat, vk::ColorSpaceKHR::eSrgbNonlinear}) == formats.end()) {
                LOGD("Surface doesn't support requested image format '{}' with colorspace '{}'", vk::to_string(vkFormat), vk::to_string(vk::ColorSpaceKHR::eSrgbNonlinear));
                underlyingFormat = format::R8G8B8A8Unorm;
            }
        }

        constexpr vk::ImageUsageFlags presentUsage{vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst};
        if ((capabilities.supportedUsageFlags & presentUsage) != presentUsage)
            throw exception("Swapchain doesn't support image usage '{}': {}", vk::to_string(presentUsage), vk::to_string(capabilities.supportedUsageFlags));

        auto requestedMode{vk::PresentModeKHR::eFifo};
        switch (*state.settings->vsyncMode) {
            case 0: // Immediate
                requestedMode = vk::PresentModeKHR::eImmediate;
                break;

            case 1: // Mailbox
                requestedMode = vk::PresentModeKHR::eMailbox;
                break;

            case 2: // FIFO
                requestedMode = vk::PresentModeKHR::eFifo;
                break;

            case 3: // Relaxed FIFO
                requestedMode = vk::PresentModeKHR::eFifoRelaxed;
                break;
            
            default: // Default FIFO
                requestedMode = vk::PresentModeKHR::eFifo;
                break;
        }
        auto modes{gpu.vkPhysicalDevice.getSurfacePresentModesKHR(**vkSurface)};
        if (std::find(modes.begin(), modes.end(), requestedMode) == modes.end()) {
            LOGW("Swapchain doesn't support present mode: {} fallbacking to fifo mode", vk::to_string(requestedMode));
            requestedMode = vk::PresentModeKHR::eFifo;
        }

        vkSwapchain.emplace(gpu.vkDevice, vk::SwapchainCreateInfoKHR{
            .surface = **vkSurface,
            .minImageCount = minImageCount,
            .imageFormat = *underlyingFormat,
            .imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = presentUsage,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eInherit,
            .presentMode = requestedMode,
            .clipped = true,
        });

        auto vkImages{vkSwapchain->getImages()};
        if (vkImages.size() > MaxSwapchainSlotCount)
            throw exception("Swapchain has higher image count ({}) than maximum slot count ({})", minImageCount, MaxSwapchainSlotCount);

        for (size_t index{}; index < vkImages.size(); index++)
            images[index].SetImage(vkImages[index]);
/*
        // We need to clear all the slots which aren't filled, keeping around stale slots could lead to issues
        for (size_t index{vkImages.size()}; index < images.size(); index++)
            images[index].ClearImage();
*/
        swapchainFormat = format;
        swapchainExtent = extent;
    }

    void PresentationEngine::UpdateSurface(jobject newSurface) {
        std::scoped_lock guard{mutex};

        auto env{state.jvm->GetEnv()};
        if (!env->IsSameObject(jSurface, nullptr)) {
            env->DeleteGlobalRef(jSurface);
            jSurface = nullptr;
        }
        if (!env->IsSameObject(newSurface, nullptr))
            jSurface = env->NewGlobalRef(newSurface);

        vkSwapchain.reset();

        if (jSurface) {
            window = ANativeWindow_fromSurface(env, jSurface);
            vkSurface.emplace(gpu.vkInstance, vk::AndroidSurfaceCreateInfoKHR{
                .window = window,
            });
            if (!gpu.vkPhysicalDevice.getSurfaceSupportKHR(gpu.vkQueueFamilyIndex, **vkSurface))
                throw exception("Vulkan Queue doesn't support presentation with surface");
            vkSurfaceCapabilities = gpu.vkPhysicalDevice.getSurfaceCapabilitiesKHR(**vkSurface);

            if (swapchainExtent && swapchainFormat)
                UpdateSwapchain(swapchainFormat, swapchainExtent);

            if (window->common.magic != AndroidNativeWindowMagic)
                throw exception("ANativeWindow* has unexpected magic: {} instead of {}", span(&window->common.magic, 1).as_string(true), span<const u8>(reinterpret_cast<const u8 *>(&AndroidNativeWindowMagic), sizeof(u32)).as_string(true));
            if (window->common.version != sizeof(ANativeWindow))
                throw exception("ANativeWindow* has unexpected version: {} instead of {}", window->common.version, sizeof(ANativeWindow));

            int result;
            if (windowCrop && (result = window->perform(window, NATIVE_WINDOW_SET_CROP, &windowCrop)))
                throw exception("Setting the layer crop to ({}-{})x({}-{}) failed with {}", windowCrop.left, windowCrop.right, windowCrop.top, windowCrop.bottom, result);

            if (windowScalingMode != NativeWindowScalingMode::ScaleToWindow && (result = window->perform(window, NATIVE_WINDOW_SET_SCALING_MODE, static_cast<i32>(windowScalingMode))))
                throw exception("Setting the layer scaling mode to '{}' failed with {}", ToString(windowScalingMode), result);

            if (windowTransform != NativeWindowTransform::Identity && (result = window->perform(window, NATIVE_WINDOW_SET_BUFFERS_TRANSFORM, static_cast<i32>(windowTransform))))
                throw exception("Setting the buffer transform to '{}' failed with {}", ToString(windowTransform), result);

            if ((result = window->perform(window, NATIVE_WINDOW_ENABLE_FRAME_TIMESTAMPS, true)))
                throw exception("Enabling frame timestamps failed with {}", result);

            surfaceCondition.notify_all();
        } else {
            vkSurface.reset();
            window = nullptr;
        }
    }

    u64 PresentationEngine::Present(HostTextureView *texture, i64 timestamp, i64 swapInterval, AndroidRect crop, NativeWindowScalingMode scalingMode, NativeWindowTransform transform, skyline::service::hosbinder::AndroidFence fence, const std::function<void()> &presentCallback) {
        if (!vkSurface.has_value()) {
            // We want this function to generally (not necessarily always) block when a surface is not present to implicitly pause the game
            std::unique_lock lock{mutex};
            surfaceCondition.wait(lock, [this] { return vkSurface.has_value(); });
        }

        presentQueue.Push(PresentableFrame{
            texture,
            fence,
            timestamp,
            swapInterval,
            presentCallback,
            nextFrameId,
            crop,
            scalingMode,
            transform
        });

        return nextFrameId++;
    }

    void PresentationEngine::LimitSpeed(i64 targetFrameTimeNs) {
        static i64 lastFrameTime = 0;
        i64 currentTime = util::GetTimeNs();

        i64 adjustedFrameTimeNs = static_cast<i64>(targetFrameTimeNs / (*state.settings->speedLimit / 100.0f));

        if (lastFrameTime != 0) {
            i64 elapsedTime = currentTime - lastFrameTime;
            if (elapsedTime < adjustedFrameTimeNs) {
                // Sleep for the remaining time to meet the adjusted frame time
                std::this_thread::sleep_for(std::chrono::nanoseconds(adjustedFrameTimeNs - elapsedTime));
            }
        }

        lastFrameTime = util::GetTimeNs(); // Update last frame time
    }

    void PresentationEngine::Pause() {
        paused.store(true, std::memory_order_release);
        LOGI("PresentationEngine paused.");
    }

    void PresentationEngine::Resume() {
        paused.store(false, std::memory_order_release);
        LOGI("PresentationEngine resumed.");
    }

    NativeWindowTransform PresentationEngine::GetTransformHint() {
        if (!vkSurface.has_value()) {
            std::unique_lock lock{mutex};
            surfaceCondition.wait(lock, [this]() { return vkSurface.has_value(); });
        }

        return GetAndroidTransform(vkSurfaceCapabilities.currentTransform);
    }
}
