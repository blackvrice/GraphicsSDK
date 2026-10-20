#pragma once
#include "Types.hpp"
#include <memory>
#include <span>
#include <vector>

namespace gfx {
class Buffer {
public:
    virtual ~Buffer() = default;
    [[nodiscard]] virtual BufferDesc description() const = 0;
    [[nodiscard]] virtual Result<void> write(std::uint64_t offset, std::span<const std::byte> data) = 0;
    [[nodiscard]] virtual Result<std::vector<std::byte>> read(std::uint64_t offset, std::uint64_t bytes) = 0;
};
class Texture {
public:
    virtual ~Texture() = default;
    [[nodiscard]] virtual TextureDesc description() const = 0;
    [[nodiscard]] virtual TextureFootprint footprint() const = 0;
};
using BufferPtr = std::shared_ptr<Buffer>;
using TexturePtr = std::shared_ptr<Texture>;
}
