// SPDX-License-Identifier: MPL-2.0
// Copyright © 2022 Ryujinx Team and Contributors (https://github.com/Ryujinx/)
// Copyright © 2022 yuzu Team and Contributors (https://github.com/yuzu-emu/)
// Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <range/v3/algorithm/for_each.hpp>
#include <nce.h>
#include <kernel/memory.h>
#include <soc/gm20b/channel.h>
#include <soc/gm20b/gmmu.h>
#include <gpu.h>
#include <vulkan/vulkan_enums.hpp>
#include "gpu/texture/common.h"
#include "gpu/texture/guest_texture.h"
#include "pipeline_state.h"

namespace skyline::gpu::interconnect::maxwell3d {
    std::pair<vk::SampleCountFlagBits, texture::Dimensions> GetMsaaState(engine::MsaaMode msaaMode, texture::Dimensions imageDimensions) {
        vk::SampleCountFlagBits sampleCount{};
        texture::Dimensions sampleDimensions{imageDimensions};
        switch (msaaMode) {
            case engine::MsaaMode::e1x1:
                sampleCount = vk::SampleCountFlagBits::e1;
                break;
            case engine::MsaaMode::e2x1:
            case engine::MsaaMode::e2x1D3D:
                sampleCount = vk::SampleCountFlagBits::e2;
                sampleDimensions.width *= 2;
            case engine::MsaaMode::e2x2:
            case engine::MsaaMode::e2x2Vc4:
            case engine::MsaaMode::e2x2Vc12:
                sampleCount = vk::SampleCountFlagBits::e4;
                sampleDimensions.width *= 2;
                sampleDimensions.height *= 2;
            case engine::MsaaMode::e4x2:
            case engine::MsaaMode::e4x2D3D:
            case engine::MsaaMode::e4x2Vc8:
            case engine::MsaaMode::e4x2Vc24:
                sampleCount = vk::SampleCountFlagBits::e8;
                sampleDimensions.width *= 4;
                sampleDimensions.height *= 2;
            case engine::MsaaMode::e4x4:
                sampleCount = vk::SampleCountFlagBits::e16;
                sampleDimensions.width *= 4;
                sampleDimensions.height *= 4;
            default:
                throw exception("Invalid MSAA mode: {}", static_cast<u32>(msaaMode));
        }
        return {sampleCount, sampleDimensions};
    }

    /* Colour Render Target */
    void ColorRenderTargetState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        manager.Bind(handle, colorTarget, surfaceClip);
    }

    ColorRenderTargetState::ColorRenderTargetState(dirty::Handle dirtyHandle, DirtyManager &manager, const EngineRegisters &engine, size_t index) : engine{manager, dirtyHandle, engine}, index{index} {}

    void ColorRenderTargetState::Flush(InterconnectContext &ctx, PackedPipelineState &packedState) {
        auto &target{engine->colorTarget};
        format = target.format;
        packedState.SetColorRenderTargetFormat(index, target.format);

        if (target.format == engine::ColorTarget::Format::Disabled) {
            view = {};
            return;
        }

        auto hostFormat{packedState.GetColorRenderTargetFormat(index)};

        bool thirdDimensionDefinesArraySize{target.memory.thirdDimensionControl == engine::TargetMemory::ThirdDimensionControl::ThirdDimensionDefinesArraySize};
        u32 depth{thirdDimensionDefinesArraySize ? 1U : target.thirdDimension};
        texture::Dimensions imageDimensions{};
        texture::TileConfig tileConfig{};
        if (target.memory.layout == engine::TargetMemory::Layout::Pitch) {
            imageDimensions = texture::Dimensions{target.width / hostFormat->bpb, target.height, depth};
            tileConfig = texture::TileConfig{
                .mode = gpu::texture::TileMode::Pitch,
                .pitch = target.width,
            };
        } else {
            imageDimensions = gpu::texture::Dimensions{target.width, target.height, depth};
            tileConfig = gpu::texture::TileConfig{
                .mode = gpu::texture::TileMode::Block,
                .blockHeight = target.memory.BlockHeight(),
                .blockDepth = target.memory.BlockDepth(),
            };
        }

        auto [sampleCount, sampleDimensions]{GetMsaaState(engine->msaaMode, imageDimensions)};
        u32 baseArrayLayer{target.layerOffset}, layerCount{static_cast<u32>(thirdDimensionDefinesArraySize ? target.thirdDimension : 1)};
        auto layerStride{(baseArrayLayer > 1 || layerCount > 1) ? target.ArrayPitch() : texture::CalculateLayerStride(sampleDimensions, hostFormat, tileConfig, 1, layerCount)};
        auto viewType{target.thirdDimension > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D};

        auto mappings{ctx.channelCtx.asCtx->gmmu.TranslateRange(target.offset, layerStride * layerCount)};
        if (ranges::all_of(mappings, [](const auto &mapping) { return mapping.valid(); })) {
            view = ctx.gpu.texture.FindOrCreate([=](auto &&executionCallback) {
                ctx.executor.AddOutsideRpCommand(std::forward<decltype(executionCallback)>(executionCallback));
            }, ctx.executor.tag, texture::Mappings{mappings}, sampleDimensions, imageDimensions, sampleCount, hostFormat, viewType, {}, tileConfig, 1, layerCount, layerStride);
        } else {
            format = engine::ColorTarget::Format::Disabled;
            packedState.SetColorRenderTargetFormat(index, engine::ColorTarget::Format::Disabled);
            view = {};
        }
    }

    /* Depth Render Target */
    void DepthRenderTargetState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        manager.Bind(handle, ztSize, ztOffset, ztFormat, ztBlockSize, ztArrayPitchLsr2, ztSelect, ztLayer, surfaceClip);
    }

    DepthRenderTargetState::DepthRenderTargetState(dirty::Handle dirtyHandle, DirtyManager &manager, const EngineRegisters &engine) : engine{manager, dirtyHandle, engine} {}

    void DepthRenderTargetState::Flush(InterconnectContext &ctx, PackedPipelineState &packedState) {
        packedState.SetDepthRenderTargetFormat(engine->ztFormat, engine->ztSelect.targetCount);

        if (!engine->ztSelect.targetCount) {
            view = {};
            return;
        }

        auto hostFormat{packedState.GetDepthRenderTargetFormat()};
        u32 baseArrayLayer{engine->ztLayer.offset}, layerCount{};
        vk::ImageViewType viewType{};

        bool thirdDimensionDefinesArraySize{engine->ztSize.control == engine::ZtSize::Control::ThirdDimensionDefinesArraySize};
        if (engine->ztSize.control == engine::ZtSize::Control::ThirdDimensionDefinesArraySize) {
            layerCount = engine->ztSize.thirdDimension;
            viewType = engine->ztSize.thirdDimension > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
        } else if (engine->ztSize.control == engine::ZtSize::Control::ArraySizeIsOne) {
            layerCount = 1;
            viewType = vk::ImageViewType::e2D;
        }

        gpu::texture::Dimensions imageDimensions{engine->ztSize.width, engine->ztSize.height, 1};
        auto [sampleCount, sampleDimensions]{GetMsaaState(engine->msaaMode, imageDimensions)};
        gpu::texture::TileConfig tileConfig{
            .mode = gpu::texture::TileMode::Block,
            .blockHeight = engine->ztBlockSize.BlockHeight(),
            .blockDepth = engine->ztBlockSize.BlockDepth(),
        };

        auto layerStride{(baseArrayLayer > 1 || layerCount > 1) ? engine->ZtArrayPitch() : texture::CalculateLayerStride(sampleDimensions, hostFormat, tileConfig, 1, layerCount)};
        auto mappings{ctx.channelCtx.asCtx->gmmu.TranslateRange(engine->ztOffset, layerStride * layerCount)};
        if (ranges::all_of(mappings, [](const auto &mapping) { return mapping.valid(); })) {
            view = ctx.gpu.texture.FindOrCreate([=](auto &&executionCallback) {
                ctx.executor.AddOutsideRpCommand(std::forward<decltype(executionCallback)>(executionCallback));
            }, ctx.executor.tag, texture::Mappings{mappings}, sampleDimensions, imageDimensions, sampleCount, hostFormat, viewType, {}, tileConfig, 1, layerCount, layerStride);
        } else {
            packedState.SetDepthRenderTargetFormat(engine->ztFormat, false);
            view = {};
        }
    }

    /* Pipeline Stages */
    void PipelineStageState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        manager.Bind(handle, pipeline, programRegion);
    }

    PipelineStageState::PipelineStageState(dirty::Handle dirtyHandle, DirtyManager &manager, const EngineRegisters &engine, u8 shaderType)
        : engine{manager, dirtyHandle, engine},
          shaderType{static_cast<engine::Pipeline::Shader::Type>(shaderType)} {}

    void PipelineStageState::Flush(InterconnectContext &ctx) {
        if (engine->pipeline.shader.type != shaderType)
            throw exception("Shader type mismatch: {} != {}!", engine->pipeline.shader.type, static_cast<u8>(shaderType));

        if (!engine->pipeline.shader.enable && shaderType != engine::Pipeline::Shader::Type::Vertex) {
            hash = 0;
            return;
        }

        std::tie(binary, hash) = cache.Lookup(ctx, engine->programRegion, engine->pipeline.programOffset);
    }

    bool PipelineStageState::Refresh(InterconnectContext &ctx) {
        return cache.Refresh(ctx, engine->programRegion, engine->pipeline.programOffset);
    }

    void PipelineStageState::PurgeCaches() {
        cache.PurgeCaches();
    }

    /* Vertex Input State */
    void VertexInputState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        ranges::for_each(vertexStreams, [&](const auto &regs) { manager.Bind(handle, regs.format, regs.frequency); });

        auto bindFull{[&](const auto &regs) { manager.Bind(handle, regs); }};
        ranges::for_each(vertexStreamInstance, bindFull);
        ranges::for_each(vertexAttributes, bindFull);
    }

    VertexInputState::VertexInputState(dirty::Handle dirtyHandle, DirtyManager &manager, const EngineRegisters &engine) : engine{manager, dirtyHandle, engine} {}

    void VertexInputState::Flush(PackedPipelineState &packedState) {
        for (u32 i{}; i < engine::VertexStreamCount; i++)
            packedState.SetVertexBinding(i, engine->vertexStreams[i], engine->vertexStreamInstance[i]);

        for (u32 i{}; i < engine::VertexAttributeCount; i++) {
            if (engine->vertexAttributes[i].source == engine::VertexAttribute::Source::Active)
                packedState.vertexAttributes[i] = engine->vertexAttributes[i];
            else
                packedState.vertexAttributes[i] = { .source = engine::VertexAttribute::Source::Inactive };
        }
    }

    /* Input Assembly State */
    void InputAssemblyState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        manager.Bind(handle, primitiveRestartEnable);
    }

    InputAssemblyState::InputAssemblyState(const EngineRegisters &engine) : engine{engine} {}

    void InputAssemblyState::Update(PackedPipelineState &packedState) {
        packedState.topology = currentEngineTopology;
        packedState.primitiveRestartEnabled = engine.primitiveRestartEnable & 1;
    }

    void InputAssemblyState::SetPrimitiveTopology(engine::DrawTopology topology) {
        currentEngineTopology = topology;
    }

    engine::DrawTopology InputAssemblyState::GetPrimitiveTopology() const {
        return currentEngineTopology;
    }

    bool InputAssemblyState::NeedsQuadConversion() const {
        return currentEngineTopology == engine::DrawTopology::Quads;
    }

    /* Tessellation State */
    void TessellationState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        manager.Bind(handle, patchSize, tessellationParameters);
    }

    TessellationState::TessellationState(const EngineRegisters &engine) : engine{engine} {}

    void TessellationState::Update(PackedPipelineState &packedState) {
        packedState.patchSize = engine.patchSize;
        packedState.SetTessellationParameters(engine.tessellationParameters);
    }

    /* Rasterizer State */
    void RasterizationState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        manager.Bind(handle, rasterEnable, frontPolygonMode, backPolygonMode, viewportClipControl, oglCullEnable, oglFrontFace, oglCullFace, windowOrigin, provokingVertex, polyOffset, pointSize, zClipRange);
    }

    RasterizationState::RasterizationState(dirty::Handle dirtyHandle, DirtyManager &manager, const EngineRegisters &engine) : engine{manager, dirtyHandle, engine} {}

    bool ConvertDepthBiasEnable(engine::PolyOffset polyOffset, engine::PolygonMode polygonMode) {
        switch (polygonMode) {
            case engine::PolygonMode::Point:
                return polyOffset.pointEnable;
            case engine::PolygonMode::Line:
                return polyOffset.lineEnable;
            case engine::PolygonMode::Fill:
                return polyOffset.fillEnable;
            default:
                throw exception("Invalid polygon mode: 0x{:X}", static_cast<u32>(polygonMode));
        }
    }

    void RasterizationState::Flush(PackedPipelineState &packedState) {
        packedState.rasterizerDiscardEnable = !engine->rasterEnable;
        packedState.SetPolygonMode(engine->frontPolygonMode);
        if (engine->backPolygonMode != engine->frontPolygonMode)
            LOGW("Non-matching polygon modes!");

        packedState.SetCullMode(engine->oglCullEnable, engine->oglCullFace);

        packedState.flipYEnable = engine->windowOrigin.flipY;

        bool origFrontFaceClockwise{engine->oglFrontFace == engine::FrontFace::CW};
        packedState.frontFaceClockwise = (packedState.flipYEnable != origFrontFaceClockwise);
        packedState.depthBiasEnable = ConvertDepthBiasEnable(engine->polyOffset, engine->frontPolygonMode);
        packedState.provokingVertex = engine->provokingVertex.value;
        packedState.pointSize = engine->pointSize;
        packedState.openGlNdc = engine->zClipRange == engine::ZClipRange::NegativeWToPositiveW;
        packedState.SetDepthClampEnable(engine->viewportClipControl.geometryClip);
    }

    /* Depth Stencil State */
    void DepthStencilState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        manager.Bind(handle, depthTestEnable, depthWriteEnable, depthFunc, depthBoundsTestEnable, stencilTestEnable, twoSidedStencilTestEnable, stencilOps, stencilBack, alphaTestEnable, alphaFunc, alphaRef);
    }

    DepthStencilState::DepthStencilState(dirty::Handle dirtyHandle, DirtyManager &manager, const EngineRegisters &engine) : engine{manager, dirtyHandle, engine} {}

    void DepthStencilState::Flush(PackedPipelineState &packedState) {
        packedState.depthTestEnable = engine->depthTestEnable;
        packedState.depthWriteEnable = engine->depthWriteEnable;
        packedState.SetDepthFunc(engine->depthTestEnable ? engine->depthFunc : engine::CompareFunc::OglAlways);
        packedState.depthBoundsTestEnable = engine->depthBoundsTestEnable;

        packedState.stencilTestEnable = engine->stencilTestEnable;
        if (packedState.stencilTestEnable) {
            auto stencilBack{engine->twoSidedStencilTestEnable ? engine->stencilBack : engine->stencilOps};
            packedState.SetStencilOps(engine->stencilOps, stencilBack);
        } else {
            packedState.SetStencilOps({ .func = engine::CompareFunc::OglAlways }, { .func = engine::CompareFunc::OglAlways });
        }

        packedState.alphaTestEnable = engine->alphaTestEnable;
        packedState.SetAlphaFunc(engine->alphaTestEnable ? engine->alphaFunc : engine::CompareFunc::OglAlways);
        packedState.alphaRef = engine->alphaTestEnable ? engine->alphaRef : 0;
    };

    /* Color Blend State */
    void ColorBlendState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        manager.Bind(handle, logicOp, singleCtWriteControl, ctWrites, blendStatePerTargetEnable, blendPerTargets, blend);
    }

    ColorBlendState::ColorBlendState(dirty::Handle dirtyHandle, DirtyManager &manager, const EngineRegisters &engine) : engine{manager, dirtyHandle, engine} {}

    void ColorBlendState::Flush(PackedPipelineState &packedState) {
        packedState.logicOpEnable = engine->logicOp.enable;
        packedState.SetLogicOp(engine->logicOp.func);
        writtenCtMask.reset();

        for (u32 i{}; i < engine::ColorTargetCount; i++) {
            auto ctWrite{[&]() {
                if (engine->singleCtWriteControl)
                    return engine->ctWrites[0];
                else
                    return engine->ctWrites[i];
            }()};

            bool enable{engine->blend.enable[i] != 0};

            if (engine->blendStatePerTargetEnable)
                packedState.SetAttachmentBlendState(i, enable, ctWrite, engine->blendPerTargets[i]);
            else
                packedState.SetAttachmentBlendState(i, enable, ctWrite, engine->blend);

            writtenCtMask.set(i, ctWrite.Any());
        }
    }

    /* Transform Feedback State */
    void TransformFeedbackState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        manager.Bind(handle, streamOutputEnable, streamOutControls, streamOutLayoutSelect);
    }

    TransformFeedbackState::TransformFeedbackState(dirty::Handle dirtyHandle, DirtyManager &manager, const EngineRegisters &engine) : engine{manager, dirtyHandle, engine} {}

    void TransformFeedbackState::Flush(PackedPipelineState &packedState) {
        packedState.transformFeedbackEnable = engine->streamOutputEnable;
        packedState.transformFeedbackVaryings = {};

        if (engine->streamOutputEnable)
            for (size_t i{}; i < engine::StreamOutBufferCount; i++)
                packedState.SetTransformFeedbackVaryings(engine->streamOutControls[i], engine->streamOutLayoutSelect[i], i);
    }

    /* Global Shader Config State */
    void GlobalShaderConfigState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        manager.Bind(handle, postVtgShaderAttributeSkipMask, bindlessTexture, apiMandatedEarlyZ, viewportScaleOffsetEnable);
    }

    GlobalShaderConfigState::GlobalShaderConfigState(const EngineRegisters &engine) : engine{engine} {}

    void GlobalShaderConfigState::Update(PackedPipelineState &packedState) {
        packedState.postVtgShaderAttributeSkipMask = engine.postVtgShaderAttributeSkipMask;
        packedState.bindlessTextureConstantBufferSlotSelect = engine.bindlessTexture.constantBufferSlotSelect;
        packedState.apiMandatedEarlyZ = engine.apiMandatedEarlyZ;
        packedState.viewportTransformEnable = engine.viewportScaleOffsetEnable;
    }

    /* Pipeline State */
    void PipelineState::EngineRegisters::DirtyBind(DirtyManager &manager, dirty::Handle handle) const {
        auto bindFunc{[&](auto &regs) { regs.DirtyBind(manager, handle); }};

        ranges::for_each(pipelineStageRegisters, bindFunc);
        ranges::for_each(colorRenderTargetsRegisters, bindFunc);
        bindFunc(depthRenderTargetRegisters);
        bindFunc(vertexInputRegisters);
        bindFunc(inputAssemblyRegisters);
        bindFunc(tessellationRegisters);
        bindFunc(rasterizationRegisters);
        bindFunc(depthStencilRegisters);
        bindFunc(colorBlendRegisters);
        bindFunc(globalShaderConfigRegisters);
        bindFunc(transformFeedbackRegisters);
        manager.Bind(handle, ctSelect);
    }

    PipelineState::PipelineState(dirty::Handle dirtyHandle, DirtyManager &manager, const EngineRegisters &engine)
        : engine{manager, dirtyHandle, engine},
          pipelineStages{util::MergeInto<dirty::ManualDirtyState<PipelineStageState>, engine::PipelineCount>(manager, engine.pipelineStageRegisters, util::IncrementingT<u8>{})},
          colorRenderTargets{util::MergeInto<dirty::ManualDirtyState<ColorRenderTargetState>, engine::ColorTargetCount>(manager, engine.colorRenderTargetsRegisters, util::IncrementingT<size_t>{})},
          depthRenderTarget{manager, engine.depthRenderTargetRegisters},
          vertexInput{manager, engine.vertexInputRegisters},
          tessellation{engine.tessellationRegisters},
          rasterization{manager, engine.rasterizationRegisters},
          depthStencil{manager, engine.depthStencilRegisters},
          colorBlend{manager, engine.colorBlendRegisters},
          transformFeedback{manager, engine.transformFeedbackRegisters},
          directState{engine.inputAssemblyRegisters},
          globalShaderConfig{engine.globalShaderConfigRegisters},
          ctSelect{engine.ctSelect} {}

    void PipelineState::Flush(InterconnectContext &ctx, Textures &textures, ConstantBufferSet &constantBuffers, StateUpdateBuilder &builder) {
        TRACE_EVENT("gpu", "PipelineState::Flush");

        packedState.dynamicStateActive = ctx.gpu.traits.supportsExtendedDynamicState;
        packedState.ctSelect = ctSelect;

        std::array<ShaderBinary, engine::PipelineCount> shaderBinaries;
        for (size_t i{}; i < engine::PipelineCount; i++) {
            const auto &stage{pipelineStages[i].UpdateGet(ctx)};
            packedState.shaderHashes[i] = stage.hash;
            shaderBinaries[i] = stage.binary;
        }

        colorBlend.Update(packedState);

        colorAttachments.clear();
        packedState.colorRenderTargetFormats = {};
        for (size_t i{}; i < engine::ColorTargetCount; i++) {
            if (i < ctSelect.count && colorBlend.Get().writtenCtMask.test(i)) {
                const auto &rt{colorRenderTargets[ctSelect[i]].UpdateGet(ctx, packedState)};
                const auto view{rt.view};
                packedState.SetColorRenderTargetFormat(ctSelect[i], rt.format);
                colorAttachments.push_back(view);

                if (view)
                    ctx.executor.AttachTexture(view);
            } else {
                colorAttachments.push_back({});
            }
        }

        depthAttachment = depthRenderTarget.UpdateGet(ctx, packedState).view;
        if (depthAttachment)
            ctx.executor.AttachTexture(depthAttachment);

        vertexInput.Update(packedState);
        directState.inputAssembly.Update(packedState);
        tessellation.Update(packedState);
        rasterization.Update(packedState);
        depthStencil.Update(packedState);
        transformFeedback.Update(packedState);
        globalShaderConfig.Update(packedState);

        if (pipeline) {
            if (auto newPipeline{pipeline->LookupNext(packedState)}) {
                pipeline = newPipeline;
                return;
            }
        }

        auto newPipeline{ctx.gpu.graphicsPipelineManager->FindOrCreate(ctx, textures, constantBuffers, packedState, shaderBinaries)};
        if (pipeline)
            pipeline->AddTransition(newPipeline);
        pipeline = newPipeline;
    }

    void PipelineState::PurgeCaches() {
        pipeline = nullptr;
        for (auto &stage : pipelineStages)
            stage.MarkDirty(true);
    }

    HostTextureView *PipelineState::GetColorRenderTargetForClear(InterconnectContext &ctx, size_t index) {
        return colorRenderTargets[index].UpdateGet(ctx, packedState).view;
    }

    HostTextureView *PipelineState::GetDepthRenderTargetForClear(InterconnectContext &ctx) {
        return depthRenderTarget.UpdateGet(ctx, packedState).view;
    }
}
