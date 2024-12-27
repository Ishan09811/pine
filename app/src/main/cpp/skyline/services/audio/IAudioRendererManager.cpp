// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
// Copyright © 2022 yuzu Emulator Project (https://github.com/yuzu-emu/)

#include <audio_core/common/audio_renderer_parameter.h>
#include <audio_core/audio_render_manager.h>
#include <common/utils.h>
#include <audio.h>
#include <fstream>
#include <string>
#include <sstream>
#include "IAudioRenderer.h"
#include "IAudioDevice.h"
#include "IAudioRendererManager.h"

namespace skyline::service::audio {
    IAudioRendererManager::IAudioRendererManager(const DeviceState &state, ServiceManager &manager)
        : BaseService(state, manager) {}

    Result IAudioRendererManager::OpenAudioRenderer(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        const auto &params{request.Pop<AudioCore::AudioRendererParameterInternal>()};
        u64 transferMemorySize{request.Pop<u64>()};
        u64 appletResourceUserId{request.Pop<u64>()};
        auto transferMemoryHandle{request.copyHandles.at(0)};
        auto processHandle{request.copyHandles.at(1)};

        // Log the transferMemorySize
        LOGI("TransferMemorySize: {}", transferMemorySize);

        constexpr u64 fallbackMaxAllowedMemorySize = 64 * 1024 * 1024; // 64 MB fallback
        u64 maxAllowedMemorySize = (GetTotalRAM() > 0) ? GetTotalRAM() / 2 : fallbackMaxAllowedMemorySize;

        if (transferMemorySize > maxAllowedMemorySize || transferMemorySize == 0) {
            LOGW("Invalid TransferMemorySize: {}. Using fallback size: {} bytes.", 
                 transferMemorySize, fallbackMaxAllowedMemorySize);
            transferMemorySize = fallbackMaxAllowedMemorySize; // Use fallback size
        }

        i32 sessionId{state.audio->audioRendererManager->GetSessionId()};
        if (sessionId == -1) {
            LOGW("Out of audio renderer sessions!");
            return Result{Service::Audio::ResultOutOfSessions};
        }

        try {
            auto renderer = std::make_shared<IAudioRenderer>(
                state, manager,
                *state.audio->audioRendererManager,
                params, transferMemorySize, processHandle, appletResourceUserId,
                state.audio->audioRendererManager->GetSessionId());

            manager.RegisterService(renderer, session, response);
        } catch (const std::bad_alloc &e) {
            LOGE("Memory allocation failed: {}", e.what());
            return Result{Service::Audio::ResultOperationFailed};
        }
        return {};
    }

    Result IAudioRendererManager::GetWorkBufferSize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        const auto &params{request.Pop<AudioCore::AudioRendererParameterInternal>()};

        u64 size{};
        auto err{state.audio->audioRendererManager->GetWorkBufferSize(params, size)};
        if (err.IsError())
            LOGW("Failed to calculate work buffer size");

        response.Push<u64>(size);

        return Result{err};
    }

    Result IAudioRendererManager::GetAudioDeviceService(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        u64 appletResourceUserId{request.Pop<u64>()};
        manager.RegisterService(std::make_shared<IAudioDevice>(state, manager, appletResourceUserId, util::MakeMagic<u32>("REV1")), session, response);
        return {};
    }

    Result IAudioRendererManager::GetAudioDeviceServiceWithRevisionInfo(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        u32 revision{request.Pop<u32>()};
        u64 appletResourceUserId{request.Pop<u64>()};
        manager.RegisterService(std::make_shared<IAudioDevice>(state, manager, appletResourceUserId, revision), session, response);
        return {};
    }

    u64 GetTotalRAM() {
        u64 totalRam = 0;
        std::ifstream meminfo("/proc/meminfo");
        std::string line;

        if (meminfo.is_open()) {
            while (std::getline(meminfo, line)) {
                if (line.find("MemTotal:") == 0) {
                    std::istringstream iss(line);
                    std::string label;
                    u64 value;
                    std::string unit;
                    iss >> label >> value >> unit;
                    // Convert kB to bytes
                    totalRam = value * 1024;
                    break;
                }
            }
        }

        return totalRam;
    }
}
