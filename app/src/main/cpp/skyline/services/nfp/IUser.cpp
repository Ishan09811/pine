// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <kernel/types/KProcess.h>
#include "IUserManager.h"
#include "IUser.h"

namespace skyline::service::nfp {
    IUser::IUser(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager), attachAvailabilityChangeEvent(std::make_shared<type::KEvent>(state, false)), mountedDevice(std::nullopt) {}

    Result IUser::Initialize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        nfpState = State::Initialized;
        return {};
    }

    Result IUser::ListDevices(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u32>(0);
        return {};
    }

    Result IUser::GetState(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push(nfpState);
        return {};
    }

    Result IUser::GetApplicationAreaSize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u32>(0xD8); // 216 bytes
        return {};
    }

    Result IUser::AttachAvailabilityChangeEvent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto handle{state.process->InsertItem(attachAvailabilityChangeEvent)};
        LOGD("Attach Availability Change Event Handle: 0x{:X}", handle);
        response.copyHandles.push_back(handle);

        return {};
    }

    Result IUser::Mount(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        u32 device_handle = request.Pop<u32>();
        u32 model_type = request.Pop<u32>();
        u32 mount_target = request.Pop<u32>();

        LOGD("IUser::Mount called with device_handle=0x{:X}, model_type={}, mount_target={}", device_handle, model_type, mount_target);

        if (mountedDevice.has_value()) {
            LOGE("Error: Another device is already mounted.");
            return {}; // Error: Already mounted
        }
        mountedDevice = {device_handle, model_type, mount_target};
        nfpState = State::Mounted;
        attachAvailabilityChangeEvent->Signal();
        return {};
    }

    Result IUser::Unmount(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        u32 device_handle = request.Pop<u32>();

        LOGD("IUser::Unmount called with device_handle=0x{:X}", device_handle);

        if (!mountedDevice.has_value() || mountedDevice->handle != device_handle) {
            LOGE("Error: No such device mounted.");
            return {};
        }

        mountedDevice.reset();
        nfpState = State::Initialized;
        attachAvailabilityChangeEvent->Signal();
        return {};
    }
}
