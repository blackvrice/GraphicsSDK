#pragma once
// Independent GraphicsUI foundation batch renderer built solely on GraphicsSDK public APIs.
// Demonstrates:
//  - 2D alpha blending (Porter-Duff srcAlpha / 1 - srcAlpha)
//  - Dynamic per-frame VB/IB upload via UploadRing + copyBufferRegion
//  - Scissor clipping + dynamic viewport + LoadOp::Load preserve
//  - Partial texture/font atlas updates via copyBufferToTextureRegion
//  - Offscreen target rendering and low-latency multi-surface presentation

#include <graphics/Graphics.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace gfx_ui_example {

struct UiVertex {
    float x{}, y{};
    std::uint8_t r{}, g{}, b{}, a{};
    std::uint32_t pad{};
};
static_assert(sizeof(UiVertex) == 16);

struct UiScissor {
    std::uint32_t x{}, y{}, w{}, h{};
};

struct UiQuadCmd {
    std::uint32_t firstIndex{};
    std::uint32_t indexCount{};
    UiScissor scissor{};
};

struct UiAtlasOverlay {
    std::uint32_t x{}, y{}, w{}, h{};
};

struct UiShaderSet {
    gfx::ShaderBinary quadVs;
    gfx::ShaderBinary quadPs;
    gfx::ShaderBinary atlasVs;
    gfx::ShaderBinary atlasPs;
};

class UiBatchRenderer {
public:
    static constexpr std::uint32_t kAtlasSize = 64;
    static constexpr std::uint32_t kMaxQuads = 256;

    static gfx::Result<std::unique_ptr<UiBatchRenderer>> create(gfx::Device& device, const UiShaderSet& shaders) {
        auto self = std::unique_ptr<UiBatchRenderer>(new UiBatchRenderer(device));

        // 1. 2D alpha-blended colored quad pipeline (slot 2: Float2 POSITION @ 0, UNorm8x4 COLOR @ 8, stride 16).
        gfx::GraphicsPipelineDesc quadDesc{shaders.quadVs, shaders.quadPs};
        quadDesc.colorFormat = gfx::Format::RGBA8UNorm;
        quadDesc.alphaBlend = true;
        quadDesc.vertexLayout.buffers.push_back({2u, sizeof(UiVertex)});
        quadDesc.vertexLayout.attributes = {
            {"POSITION", 0u, gfx::VertexFormat::Float2, 2u, 0u},
            {"COLOR", 0u, gfx::VertexFormat::UNorm8x4, 2u, 8u},
        };
        auto qp = device.createGraphicsPipeline(quadDesc);
        if (!qp) return std::unexpected(qp.error());
        self->quadPipeline_ = std::move(*qp);

        // 2. 2D alpha-blended sampled atlas pipeline (legacy single texture slot).
        gfx::GraphicsPipelineDesc atlasDesc{shaders.atlasVs, shaders.atlasPs};
        atlasDesc.colorFormat = gfx::Format::RGBA8UNorm;
        atlasDesc.alphaBlend = true;
        atlasDesc.sampledTexture = true;
        auto ap = device.createGraphicsPipeline(atlasDesc);
        if (!ap) return std::unexpected(ap.error());
        self->atlasPipeline_ = std::move(*ap);

        // 3. Dynamic VB/IB device-local buffers + UploadRing for zero-allocation per-frame staging.
        auto vb = device.createBuffer({sizeof(UiVertex) * kMaxQuads * 4u, gfx::Memory::Device});
        if (!vb) return std::unexpected(vb.error());
        self->vb_ = std::move(*vb);

        auto ib = device.createBuffer({sizeof(std::uint16_t) * kMaxQuads * 6u, gfx::Memory::Device});
        if (!ib) return std::unexpected(ib.error());
        self->ib_ = std::move(*ib);

        gfx::UploadRingDesc ringDesc{};
        ringDesc.capacity = 1024u * 1024u;
        ringDesc.segmentCount = 4u;
        auto ring = device.createUploadRing(ringDesc);
        if (!ring) return std::unexpected(ring.error());
        self->ring_ = std::move(*ring);

        // 4. Font / icon atlas texture (64x64 RGBA8UNorm, initialized to transparent zero).
        auto atlas = device.createTexture({kAtlasSize, kAtlasSize, gfx::Format::RGBA8UNorm});
        if (!atlas) return std::unexpected(atlas.error());
        self->atlas_ = std::move(*atlas);

        const auto fp = self->atlas_->footprint();
        std::vector<std::byte> zeros(static_cast<std::size_t>(fp.size), std::byte{0});
        auto stage = self->ring_->allocate(fp.size, 512u);
        if (!stage) return std::unexpected(stage.error());
        if (auto ok = stage->buffer->write(stage->offset, zeros); !ok) return std::unexpected(ok.error());

        auto initCmd = device.createCommandList();
        if (!initCmd) return std::unexpected(initCmd.error());
        if (auto ok = (*initCmd)->begin(); !ok) return std::unexpected(ok.error());
        if (auto ok = (*initCmd)->transition(self->atlas_, gfx::ResourceState::CopyDestination); !ok) return std::unexpected(ok.error());
        gfx::TextureCopyRegion full{};
        full.bufferOffset = stage->offset;
        full.bufferRowPitch = fp.rowPitch;
        full.x = 0; full.y = 0; full.width = kAtlasSize; full.height = kAtlasSize;
        if (auto ok = (*initCmd)->copyBufferToTextureRegion(stage->buffer, self->atlas_, full); !ok) return std::unexpected(ok.error());
        if (auto ok = (*initCmd)->transition(self->atlas_, gfx::ResourceState::ShaderRead); !ok) return std::unexpected(ok.error());
        if (auto ok = (*initCmd)->end(); !ok) return std::unexpected(ok.error());
        auto fence = device.queue().submit(**initCmd);
        if (!fence) return std::unexpected(fence.error());
        if (auto ok = self->ring_->commit(*fence); !ok) return std::unexpected(ok.error());
        if (auto ok = device.queue().wait(*fence); !ok) return std::unexpected(ok.error());
        if (auto ok = self->ring_->reclaim(); !ok) return std::unexpected(ok.error());

        return self;
    }

    // Partially updates a sub-rectangle of the 64x64 font/icon atlas via copyBufferToTextureRegion.
    gfx::Result<void> updateAtlasRegion(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                                        std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
        auto fp = gfx::computeTextureRegionFootprint(gfx::Format::RGBA8UNorm, w, h);
        if (!fp) return std::unexpected(fp.error());
        auto stage = ring_->allocate(fp->size, 512u);
        if (!stage) return std::unexpected(stage.error());
        std::vector<std::byte> bytes(static_cast<std::size_t>(fp->size), std::byte{0});
        for (std::uint32_t row = 0; row < h; ++row) {
            auto* dst = reinterpret_cast<std::uint8_t*>(bytes.data() + static_cast<std::size_t>(row) * fp->rowPitch);
            for (std::uint32_t col = 0; col < w; ++col) {
                dst[col * 4 + 0] = r;
                dst[col * 4 + 1] = g;
                dst[col * 4 + 2] = b;
                dst[col * 4 + 3] = a;
            }
        }
        if (auto ok = stage->buffer->write(stage->offset, bytes); !ok) return ok;

        auto cmd = device_.createCommandList();
        if (!cmd) return std::unexpected(cmd.error());
        if (auto ok = (*cmd)->begin(); !ok) return ok;
        if (auto ok = (*cmd)->transition(atlas_, gfx::ResourceState::CopyDestination); !ok) return ok;
        gfx::TextureCopyRegion reg{};
        reg.bufferOffset = stage->offset;
        reg.bufferRowPitch = fp->rowPitch;
        reg.x = x; reg.y = y; reg.width = w; reg.height = h;
        if (auto ok = (*cmd)->copyBufferToTextureRegion(stage->buffer, atlas_, reg); !ok) return ok;
        if (auto ok = (*cmd)->transition(atlas_, gfx::ResourceState::ShaderRead); !ok) return ok;
        if (auto ok = (*cmd)->end(); !ok) return ok;
        auto fence = device_.queue().submit(**cmd);
        if (!fence) return std::unexpected(fence.error());
        if (auto ok = ring_->commit(*fence); !ok) return ok;
        if (auto ok = device_.queue().wait(*fence); !ok) return ok;
        if (auto ok = ring_->reclaim(); !ok) return std::unexpected(ok.error());
        return {};
    }

    void beginBatch(std::uint32_t targetWidth, std::uint32_t targetHeight) {
        targetW_ = targetWidth;
        targetH_ = targetHeight;
        vertices_.clear();
        indices_.clear();
        quads_.clear();
        overlays_.clear();
    }

    void addRect(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                 std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a,
                 UiScissor scissor) {
        const float x0 = (2.0f * static_cast<float>(x) / static_cast<float>(targetW_)) - 1.0f;
        const float x1 = (2.0f * static_cast<float>(x + w) / static_cast<float>(targetW_)) - 1.0f;
        const float y0 = 1.0f - (2.0f * static_cast<float>(y) / static_cast<float>(targetH_));
        const float y1 = 1.0f - (2.0f * static_cast<float>(y + h) / static_cast<float>(targetH_));
        const auto base = static_cast<std::uint16_t>(vertices_.size());
        vertices_.push_back({x0, y0, r, g, b, a, 0u});
        vertices_.push_back({x1, y0, r, g, b, a, 0u});
        vertices_.push_back({x0, y1, r, g, b, a, 0u});
        vertices_.push_back({x1, y1, r, g, b, a, 0u});
        const auto firstIdx = static_cast<std::uint32_t>(indices_.size());
        indices_.push_back(base + 0);
        indices_.push_back(base + 1);
        indices_.push_back(base + 2);
        indices_.push_back(base + 2);
        indices_.push_back(base + 1);
        indices_.push_back(base + 3);
        quads_.push_back({firstIdx, 6u, scissor});
    }

    void addAtlasOverlay(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
        overlays_.push_back({x, y, w, h});
    }

    struct StagedBatch {
        gfx::UploadAllocation vbStage;
        gfx::UploadAllocation ibStage;
        std::uint64_t vbBytes{};
        std::uint64_t ibBytes{};
        std::uint32_t targetW{};
        std::uint32_t targetH{};
        std::vector<UiQuadCmd> quads;
        std::vector<UiAtlasOverlay> overlays;
    };

    // Stages dynamic VB/IB bytes into UploadRing BEFORE recording commands (stage-before-record).
    gfx::Result<StagedBatch> stageBatch() {
        StagedBatch out{};
        out.targetW = targetW_;
        out.targetH = targetH_;
        out.quads = quads_;
        out.overlays = overlays_;
        out.vbBytes = vertices_.size() * sizeof(UiVertex);
        out.ibBytes = indices_.size() * sizeof(std::uint16_t);
        if (out.vbBytes > 0u && out.ibBytes > 0u) {
            auto vbStage = ring_->allocate(out.vbBytes, 16u);
            if (!vbStage) return std::unexpected(vbStage.error());
            if (auto ok = vbStage->buffer->write(vbStage->offset, std::as_bytes(std::span{vertices_})); !ok) {
                return std::unexpected(ok.error());
            }
            auto ibStage = ring_->allocate(out.ibBytes, 16u);
            if (!ibStage) return std::unexpected(ibStage.error());
            if (auto ok = ibStage->buffer->write(ibStage->offset, std::as_bytes(std::span{indices_})); !ok) {
                return std::unexpected(ok.error());
            }
            out.vbStage = std::move(*vbStage);
            out.ibStage = std::move(*ibStage);
        }
        return out;
    }

    // Records GPU copy + render pass using already-staged VB/IB bytes (zero CPU writes during recording).
    gfx::Result<void> recordStagedPass(gfx::CommandList& cmd, const StagedBatch& batch,
                                       const gfx::TexturePtr& target,
                                       gfx::LoadOp loadOp, gfx::Color clearColor,
                                       gfx::ResourceState finalState) {
        if (batch.vbBytes > 0u && batch.ibBytes > 0u) {
            if (auto ok = cmd.transition(vb_, gfx::ResourceState::CopyDestination); !ok) return ok;
            if (auto ok = cmd.transition(ib_, gfx::ResourceState::CopyDestination); !ok) return ok;
            if (auto ok = cmd.copyBufferRegion(batch.vbStage.buffer, batch.vbStage.offset, vb_, 0u, batch.vbBytes); !ok) return ok;
            if (auto ok = cmd.copyBufferRegion(batch.ibStage.buffer, batch.ibStage.offset, ib_, 0u, batch.ibBytes); !ok) return ok;
            if (auto ok = cmd.transition(vb_, gfx::ResourceState::VertexBuffer); !ok) return ok;
            if (auto ok = cmd.transition(ib_, gfx::ResourceState::IndexBuffer); !ok) return ok;
        }

        if (auto ok = cmd.transition(target, gfx::ResourceState::RenderTarget); !ok) return ok;
        gfx::RenderPassDesc rp{};
        rp.colors = {gfx::ColorAttachment{target, loadOp, clearColor}};
        if (auto ok = cmd.beginRenderPass(rp); !ok) return ok;

        if (!batch.quads.empty()) {
            if (auto ok = cmd.setPipeline(quadPipeline_); !ok) return ok;
            if (auto ok = cmd.bindVertexBuffer(2u, vb_, sizeof(UiVertex), 0u, batch.vbBytes); !ok) return ok;
            if (auto ok = cmd.bindIndexBuffer(ib_, gfx::IndexFormat::UInt16, 0u, batch.ibBytes); !ok) return ok;
            if (auto ok = cmd.setViewport({0.0f, 0.0f, static_cast<float>(batch.targetW), static_cast<float>(batch.targetH), 0.0f, 1.0f}); !ok) return ok;
            for (const auto& q : batch.quads) {
                if (auto ok = cmd.setScissor(q.scissor.x, q.scissor.y, q.scissor.w, q.scissor.h); !ok) return ok;
                if (auto ok = cmd.drawIndexed(q.indexCount, 1u, q.firstIndex, 0, 0u); !ok) return ok;
            }
        }

        if (!batch.overlays.empty()) {
            if (auto ok = cmd.setPipeline(atlasPipeline_); !ok) return ok;
            if (auto ok = cmd.bindTexture(atlas_); !ok) return ok;
            for (const auto& ov : batch.overlays) {
                if (auto ok = cmd.setViewport({static_cast<float>(ov.x), static_cast<float>(ov.y),
                                               static_cast<float>(ov.w), static_cast<float>(ov.h), 0.0f, 1.0f}); !ok) return ok;
                if (auto ok = cmd.setScissor(ov.x, ov.y, ov.w, ov.h); !ok) return ok;
                if (auto ok = cmd.draw(3u, 1u); !ok) return ok;
            }
        }

        if (auto ok = cmd.endRenderPass(); !ok) return ok;
        if (auto ok = cmd.transition(target, finalState); !ok) return ok;
        return {};
    }

    gfx::Result<void> commitAndReclaim(gfx::Fence fence) {
        if (auto ok = ring_->commit(fence); !ok) return ok;
        if (auto ok = ring_->reclaim(); !ok) return std::unexpected(ok.error());
        return {};
    }

private:
    explicit UiBatchRenderer(gfx::Device& device) : device_(device) {}

    gfx::Device& device_;
    gfx::PipelinePtr quadPipeline_;
    gfx::PipelinePtr atlasPipeline_;
    gfx::BufferPtr vb_;
    gfx::BufferPtr ib_;
    gfx::TexturePtr atlas_;
    std::unique_ptr<gfx::UploadRing> ring_;
    std::uint32_t targetW_{};
    std::uint32_t targetH_{};
    std::vector<UiVertex> vertices_;
    std::vector<std::uint16_t> indices_;
    std::vector<UiQuadCmd> quads_;
    std::vector<UiAtlasOverlay> overlays_;
};

} // namespace gfx_ui_example
