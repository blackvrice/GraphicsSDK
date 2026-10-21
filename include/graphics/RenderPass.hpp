#pragma once
#include "Resources.hpp"
#include <optional>
#include <vector>

namespace gfx {
enum class CompareOp { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
struct DepthState { bool testEnabled{}, writeEnabled{}; CompareOp compare{CompareOp::Less}; };
struct RenderTargetLayout { std::vector<Format> colors{Format::RGBA8UNorm}; std::optional<Format> depth; };
enum class LoadOp { Clear, Load };
struct ColorAttachment { TexturePtr texture; LoadOp load{LoadOp::Clear}; Color clear; };
struct DepthAttachment { TexturePtr texture; LoadOp load{LoadOp::Clear}; float clear{1}; bool readOnly{}; };
// All attachments are stored. Load requires previously initialized contents.
struct RenderPassDesc { std::vector<ColorAttachment> colors; DepthAttachment depth; };
[[nodiscard]] Result<void> validateTextureDescription(TextureDesc);
[[nodiscard]] Result<void> validateRenderTargets(const RenderTargetLayout&, DepthState = {});
[[nodiscard]] Result<void> validateRenderPass(const RenderPassDesc&);
}
