// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "ISystemDisplayService.h"

namespace skyline::service::visrv {
    ISystemDisplayService::ISystemDisplayService(const DeviceState &state, ServiceManager &manager) : IDisplayService(state, manager) {}

    Result ISystemDisplayService::SetLayerZ(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }

    Result ISystemDisplayService::GetDisplayMode(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        u64 display_id = request.Pop<u64>();
        
        struct DisplayMode {
            u32 width;
            u32 height;
            f32 refresh_rate;
            u32 unknown;
        } mode{};
        
        mode.width = 1280;
        mode.height = 720;
        mode.refresh_rate = 60.0f;
        mode.unknown = 0;

        request.outputBuf.at(0).as<DisplayMode>() = mode;
        response.Push<u64>(1);
        return {};
    }
}
