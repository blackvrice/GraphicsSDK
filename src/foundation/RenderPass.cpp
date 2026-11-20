#include <graphics/RenderPass.hpp>
#include <cmath>
#include <unordered_set>

namespace gfx {
namespace {
auto bad(const char* message){return std::unexpected(Error{ErrorCode::InvalidArgument,message});}
bool colorFormat(Format f){return f==Format::RGBA8UNorm || f==Format::RGBA16Float || f==Format::BGRA8UNorm;}
bool loadOp(LoadOp op){return op==LoadOp::Clear || op==LoadOp::Load;}
}
Result<void> validateTextureDescription(TextureDesc d) {
    if(!d.width || !d.height || d.width>16384 || d.height>16384 ||
       (!colorFormat(d.format) && d.format!=Format::D32Float) ||
       d.depthStencil!=(d.format==Format::D32Float) || (d.depthStencil && d.renderTarget))return bad("Invalid texture extent/format/attachment usage");
    return {};
}
Result<TextureFootprint> computeTextureRegionFootprint(
    Format format, std::uint32_t width, std::uint32_t height, std::uint32_t rowPitch) {
    if (!width || !height || width > 16384 || height > 16384) {
        return bad("Texture copy region extent must be 1..16384");
    }
    std::uint32_t texelBytes = 0;
    if (format == Format::RGBA8UNorm || format == Format::BGRA8UNorm || format == Format::D32Float) {
        texelBytes = 4;
    } else if (format == Format::RGBA16Float) {
        texelBytes = 8;
    } else {
        return bad("Unsupported texture format for region footprint");
    }
    const std::uint32_t rowBytes = width * texelBytes;
    const std::uint32_t minimumPitch = (rowBytes + 255u) & ~255u;
    const std::uint32_t resolvedPitch = rowPitch ? rowPitch : minimumPitch;
    if (resolvedPitch < rowBytes || (resolvedPitch % 256u) != 0) {
        return bad("Texture copy rowPitch must be a 256-byte multiple at least rowBytes");
    }
    const std::uint64_t totalBytes = static_cast<std::uint64_t>(resolvedPitch) * height;
    return TextureFootprint{totalBytes, resolvedPitch, rowBytes, height};
}
Result<void> validateRenderTargets(const RenderTargetLayout& layout,DepthState depth) {
    if(layout.colors.size()>8 || (layout.colors.empty() && !layout.depth))return bad("Render layout requires 1..8 color attachments or depth");
    for(auto f:layout.colors)if(!colorFormat(f))return bad("Color attachment requires a color format");
    if(layout.depth && *layout.depth!=Format::D32Float)return bad("Only D32Float depth is supported");
    if(depth.compare<CompareOp::Never || depth.compare>CompareOp::Always ||
       (depth.writeEnabled && !depth.testEnabled) || ((depth.testEnabled || depth.writeEnabled) && !layout.depth))return bad("Invalid depth state or missing depth format");
    return {};
}
Result<void> validateRenderPass(const RenderPassDesc& pass) {
    if(pass.colors.size()>8 || (pass.colors.empty() && !pass.depth.texture))return bad("Render pass requires color or depth attachments");
    std::unordered_set<const Texture*> used;
    std::uint32_t width=0,height=0;
    auto attachment=[&](const TexturePtr& texture,bool depth)->Result<void> {
        if(!texture || !used.insert(texture.get()).second)return bad("Null or duplicate render attachment");
        const auto d=texture->description();
        auto valid=validateTextureDescription(d);if(!valid)return valid;
        if(depth?!d.depthStencil:(!d.renderTarget || !colorFormat(d.format)))return bad("Incorrect render attachment usage");
        if(width && (width!=d.width || height!=d.height))return bad("Render attachment extents must match");
        width=d.width;height=d.height;return {};
    };
    for(const auto& color:pass.colors) {
        if(!loadOp(color.load))return bad("Invalid color load operation");
        if(color.load==LoadOp::Clear && (!std::isfinite(color.clear.r) || !std::isfinite(color.clear.g) || !std::isfinite(color.clear.b) || !std::isfinite(color.clear.a)))return bad("Clear color must be finite");
        auto valid=attachment(color.texture,false);if(!valid)return valid;
    }
    if(pass.depth.texture) {
        const auto& depth=pass.depth;
        if(!loadOp(depth.load) || (depth.readOnly && depth.load!=LoadOp::Load) ||
           (depth.load==LoadOp::Clear && (!std::isfinite(depth.clear) || depth.clear<0 || depth.clear>1)))return bad("Invalid depth load/clear/read-only operation");
        auto valid=attachment(depth.texture,true);if(!valid)return valid;
    }
    return {};
}
Result<void> validateViewport(const Viewport& viewport, std::uint32_t targetWidth, std::uint32_t targetHeight) {
    if (!targetWidth || !targetHeight) {
        return bad("Viewport requires positive target dimensions");
    }
    if (!std::isfinite(viewport.x) || !std::isfinite(viewport.y) ||
        !std::isfinite(viewport.width) || !std::isfinite(viewport.height) ||
        !std::isfinite(viewport.minDepth) || !std::isfinite(viewport.maxDepth)) {
        return bad("Viewport parameters must be finite");
    }
    if (viewport.width <= 0.0f || viewport.height <= 0.0f ||
        viewport.x < 0.0f || viewport.y < 0.0f ||
        static_cast<double>(viewport.x) + static_cast<double>(viewport.width) > static_cast<double>(targetWidth) ||
        static_cast<double>(viewport.y) + static_cast<double>(viewport.height) > static_cast<double>(targetHeight)) {
        return bad("Viewport outside target attachment bounds");
    }
    if (viewport.minDepth < 0.0f || viewport.maxDepth > 1.0f || viewport.minDepth > viewport.maxDepth) {
        return bad("Viewport depth bounds must satisfy 0 <= minDepth <= maxDepth <= 1");
    }
    return {};
}
}
