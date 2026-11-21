#include <graphics/RenderPass.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace gfx {
namespace {
auto bad(const char* message){return std::unexpected(Error{ErrorCode::InvalidArgument,message});}
auto unsupported(const char* message){return std::unexpected(Error{ErrorCode::Unsupported,message});}
bool uncompressedColorFormat(Format f){return f==Format::RGBA8UNorm || f==Format::RGBA16Float || f==Format::BGRA8UNorm || f==Format::RGBA8SRGB || f==Format::BGRA8SRGB || f==Format::RGB10A2UNorm;}
bool compressedFormat(Format f) {
    return f >= Format::BC1UNorm && f <= Format::ASTC8x8UNorm;
}
bool knownTextureFormat(Format f) {
    return uncompressedColorFormat(f) || f == Format::D32Float || compressedFormat(f);
}
bool loadOp(LoadOp op){return op==LoadOp::Clear || op==LoadOp::Load;}
bool validSampleCount(std::uint32_t n){return n==1u || n==2u || n==4u || n==8u;}
bool validDimension(TextureDimension d) {
    return d == TextureDimension::Texture2D || d == TextureDimension::Texture1D ||
           d == TextureDimension::Texture3D || d == TextureDimension::Texture2DArray ||
           d == TextureDimension::TextureCube;
}
std::uint32_t maxMipCount(const TextureDesc& d) {
    std::uint32_t maxDim = d.width;
    if (d.dimension == TextureDimension::Texture3D) {
        maxDim = std::max({d.width, d.height, d.depth});
    } else if (d.dimension != TextureDimension::Texture1D) {
        maxDim = std::max(d.width, d.height);
    }
    std::uint32_t mips = 1u;
    while (maxDim > 1u) {
        maxDim >>= 1u;
        ++mips;
    }
    return mips;
}
}
Result<FormatBlockInfo> queryFormatBlockInfo(Format format) {
    switch (format) {
    case Format::RGBA8UNorm:
    case Format::BGRA8UNorm:
    case Format::RGBA8SRGB:
    case Format::BGRA8SRGB:
    case Format::RGB10A2UNorm:
    case Format::D32Float:
        return FormatBlockInfo{1u, 1u, 4u, false};
    case Format::RGBA16Float:
        return FormatBlockInfo{1u, 1u, 8u, false};
    case Format::BC1UNorm:
    case Format::BC4UNorm:
    case Format::ETC2RGB8UNorm:
    case Format::EACR11UNorm:
        return FormatBlockInfo{4u, 4u, 8u, true};
    case Format::BC2UNorm:
    case Format::BC3UNorm:
    case Format::BC5UNorm:
    case Format::BC6HUFloat:
    case Format::BC7UNorm:
    case Format::ETC2RGBA8UNorm:
    case Format::EACRG11UNorm:
    case Format::ASTC4x4UNorm:
        return FormatBlockInfo{4u, 4u, 16u, true};
    case Format::ASTC6x6UNorm:
        return FormatBlockInfo{6u, 6u, 16u, true};
    case Format::ASTC8x8UNorm:
        return FormatBlockInfo{8u, 8u, 16u, true};
    }
    return bad("Unknown texture format");
}
Result<void> validateTextureDescription(TextureDesc d) {
    if (!validDimension(d.dimension)) return bad("Unknown texture dimension");
    const auto blockInfo = queryFormatBlockInfo(d.format);
    if (!blockInfo) return std::unexpected(blockInfo.error());
    if (d.depthStencil != (d.format == Format::D32Float) ||
        (d.depthStencil && (d.renderTarget || d.storage)) ||
        !validSampleCount(d.sampleCount)) {
        return bad("Invalid texture format, attachment usage, or sample count");
    }
    if (blockInfo->compressed) {
        if (d.renderTarget || d.storage) {
            return unsupported("Block-compressed textures support transfer and ShaderRead sampling only");
        }
        if (d.sampleCount > 1u) {
            return bad("Block-compressed textures cannot be multisampled");
        }
        if (d.dimension == TextureDimension::Texture1D || d.dimension == TextureDimension::Texture3D) {
            return unsupported("Block-compressed textures require Texture2D, Texture2DArray, or TextureCube dimension");
        }
    }
    if (d.sampleCount > 1u) {
        if (d.dimension != TextureDimension::Texture2D || d.mipLevels != 1u || d.arrayLayers != 1u || d.depth != 1u ||
            (!d.renderTarget && !d.depthStencil) || d.storage) {
            return bad("Multisampled textures require single-mip, single-layer Texture2D renderTarget or depthStencil without storage");
        }
    }
    switch (d.dimension) {
    case TextureDimension::Texture2D:
        if (!d.width || !d.height || d.width > 16384u || d.height > 16384u || d.depth != 1u || d.arrayLayers != 1u)
            return bad("Invalid Texture2D extent, depth, or arrayLayers");
        break;
    case TextureDimension::Texture1D:
        if (!d.width || d.width > 16384u || d.height != 1u || d.depth != 1u || d.arrayLayers != 1u)
            return bad("Texture1D requires width 1..16384 and height=depth=arrayLayers=1");
        if (d.renderTarget || d.depthStencil || d.storage)
            return unsupported("Texture1D supports transfer and ShaderRead sampling only");
        break;
    case TextureDimension::Texture3D:
        if (!d.width || !d.height || !d.depth || d.width > 2048u || d.height > 2048u || d.depth > 2048u || d.arrayLayers != 1u)
            return bad("Texture3D requires width/height/depth 1..2048 and arrayLayers=1");
        if (d.renderTarget || d.depthStencil || d.storage)
            return unsupported("Texture3D supports transfer and ShaderRead sampling only");
        break;
    case TextureDimension::Texture2DArray:
        if (!d.width || !d.height || d.width > 16384u || d.height > 16384u || d.depth != 1u ||
            !d.arrayLayers || d.arrayLayers > 2048u)
            return bad("Texture2DArray requires width/height 1..16384, depth=1, and arrayLayers 1..2048");
        if (d.storage)
            return unsupported("Texture2DArray storage textures are unsupported");
        break;
    case TextureDimension::TextureCube:
        if (!d.width || !d.height || d.width > 16384u || d.width != d.height || d.depth != 1u || d.arrayLayers != 6u)
            return bad("TextureCube requires square width==height 1..16384, depth=1, and arrayLayers=6");
        if (d.depthStencil || d.storage)
            return unsupported("TextureCube depthStencil and storage usages are unsupported");
        break;
    }
    if (blockInfo->compressed &&
        ((d.width % blockInfo->blockWidth) != 0u || (d.height % blockInfo->blockHeight) != 0u)) {
        return bad("Block-compressed texture mip 0 extent must be a multiple of the format block dimensions");
    }
    if (!d.mipLevels || d.mipLevels > maxMipCount(d)) {
        return bad("Texture mipLevels outside valid mip chain range");
    }
    return {};
}
Result<TextureViewDesc> validateTextureView(TextureDesc textureDesc, TextureViewDesc viewDesc) {
    if (auto ok = validateTextureDescription(textureDesc); !ok) return std::unexpected(ok.error());
    const Format resolvedFormat = viewDesc.format.value_or(textureDesc.format);
    if (!knownTextureFormat(resolvedFormat)) return bad("Unknown texture view format");
    if (viewDesc.usage != TextureViewUsage::ShaderRead &&
        viewDesc.usage != TextureViewUsage::RenderTarget &&
        viewDesc.usage != TextureViewUsage::DepthStencil &&
        viewDesc.usage != TextureViewUsage::Storage) {
        return bad("Unknown texture view usage");
    }
    if (!viewDesc.mipLevelCount || viewDesc.baseMipLevel >= textureDesc.mipLevels ||
        viewDesc.mipLevelCount > textureDesc.mipLevels - viewDesc.baseMipLevel) {
        return bad("Texture view mip range outside texture bounds");
    }
    if (!viewDesc.arrayLayerCount || viewDesc.baseArrayLayer >= textureDesc.arrayLayers ||
        viewDesc.arrayLayerCount > textureDesc.arrayLayers - viewDesc.baseArrayLayer) {
        return bad("Texture view array layer range outside texture bounds");
    }
    if ((resolvedFormat == Format::D32Float) != (textureDesc.format == Format::D32Float)) {
        return bad("Cannot reinterpret between color and depth texture formats");
    }
    if (resolvedFormat != textureDesc.format) {
        return unsupported("Cross-format texture view reinterpretation is unsupported");
    }
    TextureDimension resolvedDim = textureDesc.dimension;
    if (viewDesc.dimension.has_value()) {
        if (!validDimension(*viewDesc.dimension)) return bad("Unknown texture view dimension");
        resolvedDim = *viewDesc.dimension;
    } else if ((viewDesc.usage == TextureViewUsage::RenderTarget || viewDesc.usage == TextureViewUsage::DepthStencil) &&
               (textureDesc.dimension == TextureDimension::Texture2DArray || textureDesc.dimension == TextureDimension::TextureCube) &&
               viewDesc.arrayLayerCount == 1u) {
        resolvedDim = TextureDimension::Texture2D;
    }
    if (viewDesc.usage == TextureViewUsage::ShaderRead) {
        if (textureDesc.depthStencil) return unsupported("Depth texture sampling view is unsupported");
        if (textureDesc.sampleCount > 1u) return unsupported("Multisampled texture sampling view is unsupported");
        switch (textureDesc.dimension) {
        case TextureDimension::Texture1D:
            if (resolvedDim != TextureDimension::Texture1D) return bad("Texture1D ShaderRead view requires Texture1D dimension");
            break;
        case TextureDimension::Texture2D:
            if (resolvedDim != TextureDimension::Texture2D) return bad("Texture2D ShaderRead view requires Texture2D dimension");
            break;
        case TextureDimension::Texture3D:
            if (resolvedDim != TextureDimension::Texture3D) return bad("Texture3D ShaderRead view requires Texture3D dimension");
            break;
        case TextureDimension::Texture2DArray:
            if (resolvedDim == TextureDimension::Texture2D)
                return unsupported("Use Texture2DArray ShaderRead views for arrayed textures");
            if (resolvedDim != TextureDimension::Texture2DArray)
                return bad("Texture2DArray ShaderRead view requires Texture2DArray dimension");
            break;
        case TextureDimension::TextureCube:
            if (resolvedDim == TextureDimension::Texture2D)
                return unsupported("Use TextureCube or Texture2DArray ShaderRead views for cubemap textures");
            if (resolvedDim == TextureDimension::TextureCube) {
                if (viewDesc.baseArrayLayer != 0u || viewDesc.arrayLayerCount != 6u)
                    return bad("TextureCube ShaderRead view requires baseArrayLayer=0 and arrayLayerCount=6");
            } else if (resolvedDim != TextureDimension::Texture2DArray) {
                return bad("TextureCube ShaderRead view requires TextureCube or Texture2DArray dimension");
            }
            break;
        }
    } else if (viewDesc.usage == TextureViewUsage::RenderTarget) {
        if (!textureDesc.renderTarget || textureDesc.depthStencil)
            return bad("RenderTarget view requires a color renderTarget texture");
        if (resolvedDim != TextureDimension::Texture2D || viewDesc.mipLevelCount != 1u || viewDesc.arrayLayerCount != 1u)
            return bad("RenderTarget view requires single-mip, single-layer Texture2D view");
    } else if (viewDesc.usage == TextureViewUsage::DepthStencil) {
        if (!textureDesc.depthStencil)
            return bad("DepthStencil view requires a depthStencil texture");
        if (resolvedDim != TextureDimension::Texture2D || viewDesc.mipLevelCount != 1u || viewDesc.arrayLayerCount != 1u)
            return bad("DepthStencil view requires single-mip, single-layer Texture2D view");
    } else if (viewDesc.usage == TextureViewUsage::Storage) {
        if (!textureDesc.storage || textureDesc.depthStencil)
            return bad("Storage view requires a color storage texture");
        if (textureDesc.dimension != TextureDimension::Texture2D || resolvedDim != TextureDimension::Texture2D ||
            viewDesc.mipLevelCount != 1u || viewDesc.arrayLayerCount != 1u)
            return bad("Storage view requires single-mip, single-layer Texture2D view");
    }
    viewDesc.format = resolvedFormat;
    viewDesc.dimension = resolvedDim;
    return viewDesc;
}
Result<TextureFootprint> computeTextureRegionFootprint(
    Format format, std::uint32_t width, std::uint32_t height, std::uint32_t rowPitch) {
    if (!width || !height || width > 16384 || height > 16384) {
        return bad("Texture copy region extent must be 1..16384");
    }
    const auto info = queryFormatBlockInfo(format);
    if (!info) {
        return bad("Unsupported texture format for region footprint");
    }
    const std::uint32_t blocksX = (width + info->blockWidth - 1u) / info->blockWidth;
    const std::uint32_t blocksY = (height + info->blockHeight - 1u) / info->blockHeight;
    const std::uint32_t rowBytes = blocksX * info->bytesPerBlock;
    const std::uint32_t minimumPitch = (rowBytes + 255u) & ~255u;
    const std::uint32_t resolvedPitch = rowPitch ? rowPitch : minimumPitch;
    if (resolvedPitch < rowBytes || (resolvedPitch % 256u) != 0) {
        return bad("Texture copy rowPitch must be a 256-byte multiple at least rowBytes");
    }
    const std::uint64_t totalBytes = static_cast<std::uint64_t>(resolvedPitch) * blocksY;
    return TextureFootprint{totalBytes, resolvedPitch, rowBytes, blocksY};
}
Result<void> validateRenderTargets(const RenderTargetLayout& layout,DepthState depth) {
    if(layout.colors.size()>8 || (layout.colors.empty() && !layout.depth))return bad("Render layout requires 1..8 color attachments or depth");
    if(!validSampleCount(layout.sampleCount))return bad("Render layout sampleCount must be 1, 2, 4, or 8");
    for(auto f:layout.colors)if(!uncompressedColorFormat(f))return bad("Color attachment requires an uncompressed color format");
    if(layout.depth && *layout.depth!=Format::D32Float)return bad("Only D32Float depth is supported");
    if(depth.compare<CompareOp::Never || depth.compare>CompareOp::Always ||
       (depth.writeEnabled && !depth.testEnabled) || ((depth.testEnabled || depth.writeEnabled) && !layout.depth))return bad("Invalid depth state or missing depth format");
    return {};
}
Result<void> validateRenderPass(const RenderPassDesc& pass) {
    const bool hasDepth = pass.depth.texture || pass.depth.view;
    if(pass.colors.size()>8 || (pass.colors.empty() && !hasDepth))return bad("Render pass requires color or depth attachments");
    std::unordered_set<const Texture*> used;
    std::uint32_t width=0,height=0,samples=0;
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
        if(!view && d.dimension!=TextureDimension::Texture2D)return bad("Non-Texture2D render attachments require an explicit single-slice TextureView");
        if(depth?!d.depthStencil:(!d.renderTarget || !uncompressedColorFormat(d.format)))return bad("Incorrect render attachment usage");
        const std::uint32_t mip = view ? view->description().baseMipLevel : 0u;
        const std::uint32_t attW = std::max(1u, d.width >> mip);
        const std::uint32_t attH = std::max(1u, d.height >> mip);
        if(width && (width!=attW || height!=attH))return bad("Render attachment extents must match");
        if(samples && samples!=d.sampleCount)return bad("Render attachment sample counts must match");
        width=attW;height=attH;samples=d.sampleCount;return {};
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
