#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>

namespace bench {
struct UploadLayout {
    std::uint32_t rowBytes{}, rowPitch{}, height{};
    std::uint64_t size{}, tightSize{};
};
inline UploadLayout uploadLayout(std::uint32_t width, std::uint32_t height, bool half) {
    const auto row=static_cast<std::uint64_t>(width)*(half?8:4);
    const auto pitch=(row+255)&~std::uint64_t{255};
    // This benchmark's 2D copy path uses a 32-bit layer size; reject overflow before allocation.
    if (!width || !height || pitch>UINT32_MAX || pitch*height>UINT32_MAX)
        throw std::runtime_error("Unsupported benchmark upload extent/size");
    return {static_cast<std::uint32_t>(row),static_cast<std::uint32_t>(pitch),height,pitch*height,row*height};
}
inline void padUploadRows(std::span<std::byte> destination, std::span<const std::byte> source, const UploadLayout& layout) {
    if (destination.size()!=layout.size || source.size()!=layout.tightSize)
        throw std::runtime_error("Upload payload/padded size mismatch");
    std::memset(destination.data(),0,destination.size());
    for (std::uint32_t y=0;y<layout.height;++y)
        std::memcpy(destination.data()+static_cast<size_t>(y)*layout.rowPitch,source.data()+static_cast<size_t>(y)*layout.rowBytes,layout.rowBytes);
}
inline constexpr const char* llglUploadModel="llgl_writebuffer_default_buffer_then_copytexturefrombuffer_v1";
}
