#pragma once
#include "Resources.hpp"
#include <optional>
#include <vector>

namespace gfx {
enum class CompareOp { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
struct DepthState {
    bool testEnabled{}, writeEnabled{};
    CompareOp compare{CompareOp::Less};
    bool operator==(const DepthState&) const = default;
};
struct RenderTargetLayout {
    std::vector<Format> colors{Format::RGBA8UNorm};
    std::optional<Format> depth;
    std::uint32_t sampleCount{1};
    bool operator==(const RenderTargetLayout&) const = default;
};
enum class LoadOp { Clear, Load };
// Specify either texture or view (with RenderTarget usage), never both.
struct ColorAttachment { TexturePtr texture; LoadOp load{LoadOp::Clear}; Color clear; TextureViewPtr view; };
// Specify either texture or view (with DepthStencil usage), never both.
struct DepthAttachment { TexturePtr texture; LoadOp load{LoadOp::Clear}; float clear{1}; bool readOnly{}; TextureViewPtr view; };
// All attachments are stored. Load requires previously initialized contents.
struct RenderPassDesc { std::vector<ColorAttachment> colors; DepthAttachment depth; };
[[nodiscard]] Result<FormatBlockInfo> queryFormatBlockInfo(Format format);
[[nodiscard]] Result<void> validateTextureDescription(TextureDesc);
[[nodiscard]] Result<TextureViewDesc> validateTextureView(TextureDesc, TextureViewDesc);
[[nodiscard]] Result<TextureFootprint> computeTextureRegionFootprint(
    Format format, std::uint32_t width, std::uint32_t height, std::uint32_t rowPitch = 0);
[[nodiscard]] Result<void> validateRenderTargets(const RenderTargetLayout&, DepthState = {});
[[nodiscard]] Result<void> validateRenderPass(const RenderPassDesc&);
[[nodiscard]] Result<void> validateViewport(const Viewport&, std::uint32_t targetWidth, std::uint32_t targetHeight);
}
