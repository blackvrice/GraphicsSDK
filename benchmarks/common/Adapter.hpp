#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <string>
#include <string_view>

namespace bench {
struct Shaders { std::vector<std::byte> vertex, fragment; };
Shaders compileShaders();
struct DrawTimes { double record{}, submit{}, wait{}, frame{}; std::optional<double> gpu; };
struct PipelineTimes { double creation{}, release{}; };
using Clock = std::chrono::steady_clock;
inline double ms(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}
struct Adapter {
    virtual ~Adapter() = default;
    virtual std::string sourceFingerprint() const { return {}; }
    virtual std::string deviceName() const = 0;
    virtual void idle() = 0;
    virtual void* buffer(std::uint32_t bytes) = 0;
    virtual void releaseBuffer(void*) = 0;
    virtual void* texture(std::uint32_t width, std::uint32_t height, bool half) = 0;
    virtual void releaseTexture(void*) = 0;
    virtual void prepareDraw(const Shaders&) = 0;
    virtual DrawTimes draw(std::uint32_t count, bool instanced) = 0;
    virtual bool verifyDraw() = 0;
    virtual PipelineTimes recreatePipeline() = 0;
    virtual void prepareUpload(std::uint32_t width, std::uint32_t height, bool half) = 0;
    virtual void upload(std::span<const std::byte>) = 0;
    virtual bool verifyUpload(std::span<const std::byte>) = 0;
};
std::unique_ptr<Adapter> llgl(bool validation);
std::unique_ptr<Adapter> sdl(bool validation);
std::unique_ptr<Adapter> graphicsSDK(bool validation);
std::string quote(std::string_view);
std::string environment(std::string_view device);
}
