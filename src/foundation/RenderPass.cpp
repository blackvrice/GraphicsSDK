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
Result<TextureViewDesc> validateTextureView(TextureDesc textureDesc, TextureViewDesc viewDesc) {
    if (auto ok = validateTextureDescription(textureDesc); !ok) return std::unexpected(ok.error());
    const Format resolvedFormat = viewDesc.format.value_or(textureDesc.format);
    const bool knownFormat = colorFormat(resolvedFormat) || resolvedFormat == Format::D32Float;
    if (!knownFormat) return bad("Unknown texture view format");
    if (viewDesc.usage == TextureViewUsage::Storage) {
        return std::unexpected(Error{ErrorCode::Unsupported, "Storage/UAV texture views are not yet supported"});
    }
    if (viewDesc.usage != TextureViewUsage::ShaderRead &&
        viewDesc.usage != TextureViewUsage::RenderTarget &&
        viewDesc.usage != TextureViewUsage::DepthStencil) {
        return bad("Unknown texture view usage");
    }
    if (!viewDesc.mipLevelCount || viewDesc.baseMipLevel >= 1u || viewDesc.mipLevelCount > 1u - viewDesc.baseMipLevel) {
        return bad("Texture view mip range outside texture bounds");
    }
    if (!viewDesc.arrayLayerCount || viewDesc.baseArrayLayer >= 1u || viewDesc.arrayLayerCount > 1u - viewDesc.baseArrayLayer) {
        return bad("Texture view array layer range outside texture bounds");
    }
    if ((resolvedFormat == Format::D32Float) != (textureDesc.format == Format::D32Float)) {
        return bad("Cannot reinterpret between color and depth texture formats");
    }
    if (resolvedFormat != textureDesc.format) {
        return std::unexpected(Error{ErrorCode::Unsupported, "Cross-format texture view reinterpretation is unsupported"});
    }
    if (viewDesc.usage == TextureViewUsage::ShaderRead && textureDesc.depthStencil) {
        return std::unexpected(Error{ErrorCode::Unsupported, "Depth texture sampling view is unsupported"});
    }
    if (viewDesc.usage == TextureViewUsage::RenderTarget && (!textureDesc.renderTarget || textureDesc.depthStencil)) {
        return bad("RenderTarget view requires a color renderTarget texture");
    }
    if (viewDesc.usage == TextureViewUsage::DepthStencil && !textureDesc.depthStencil) {
        return bad("DepthStencil view requires a depthStencil texture");
    }
    viewDesc.format = resolvedFormat;
    return viewDesc;
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
    const bool hasDepth = pass.depth.texture || pass.depth.view;
    if(pass.colors.size()>8 || (pass.colors.empty() && !hasDepth))return bad("Render pass requires color or depth attachments");
    std::unordered_set<const Texture*> used;
    std::uint32_t width=0,height=0;
    auto attachment=[&](const TexturePtr& rawTexture, const TextureViewPtr& view, bool depth)->Result<void> {
        if(rawTexture && view)return bad("Specify either texture or view on attachment, not both");
        TexturePtr texture = rawTexture;
        if(view) {
            const auto viewDesc = view->description();
            const auto expectedUsage = depth ? TextureViewUsage::DepthStencil : TextureViewUsage::RenderTarget;
            if(viewDesc.usage != expectedUsage)return bad("Attachment texture view usage does not match attachment role");
            texture = view->texture();
            if(!texture)return bad("Texture view has null parent texture");
            auto validView = validateTextureView(texture->description(), viewDesc);
            if(!validView)return std::unexpected(validView.error());
        }
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
        auto valid=attachment(color.texture, color.view, false);if(!valid)return valid;
    }
    if(hasDepth) {
        const auto& depth=pass.depth;
        if(!loadOp(depth.load) || (depth.readOnly && depth.load!=LoadOp::Load) ||
           (depth.load==LoadOp::Clear && (!std::isfinite(depth.clear) || depth.clear<0 || depth.clear>1)))return bad("Invalid depth load/clear/read-only operation");
        auto valid=attachment(depth.texture, depth.view, true);if(!valid)return valid;
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
