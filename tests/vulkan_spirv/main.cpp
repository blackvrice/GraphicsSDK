#include <graphics/Graphics.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "PipelineCacheSupport.hpp"

namespace {
constexpr unsigned width = 13;
constexpr unsigned height = 7;
constexpr std::byte padding{0xB7};

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class T>
T take(gfx::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message + " [" + std::to_string(result.error().nativeCode) + "]");
    return std::move(*result);
}

void take(gfx::Result<void> result) {
    if (!result) throw std::runtime_error(result.error().message);
}

gfx::ShaderBinary shader(const std::filesystem::path& path, gfx::ShaderStage stage) {
    const auto size = std::filesystem::file_size(path);
    check(size > 0 && size <= 4 * 1024 * 1024, "Unexpected shader file size");
    gfx::ShaderBinary result{stage, std::vector<std::byte>(size), gfx::ShaderFormat::SPIRV};
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(result.bytes.data()), static_cast<std::streamsize>(size));
    check(static_cast<bool>(file), "Could not read SPIR-V");
    return result;
}

void reject(gfx::Device& device, const gfx::GraphicsPipelineDesc& desc, gfx::ErrorCode code, std::string_view message) {
    const auto result = device.createGraphicsPipeline(desc);
    check(!result && result.error().code == code && result.error().message.find(message) != std::string::npos,
          "Unexpected SDK shader rejection");
    std::cout << result.error().message << '\n';
}

void draw(gfx::Device& device, const gfx::GraphicsPipelineDesc& desc, const std::filesystem::path& output,
          gfx::PipelinePtr pipeline = {}) {
    using State = gfx::ResourceState;
    if (!pipeline) pipeline = take(device.createGraphicsPipeline(desc));
    auto texture = take(device.createTexture({width, height, gfx::Format::RGBA8UNorm, true}));
    const auto footprint = texture->footprint();
    check(footprint.rowBytes == width * 4 && footprint.rowPitch > footprint.rowBytes &&
          footprint.height == height && footprint.size >= (height - 1) * footprint.rowPitch + footprint.rowBytes,
          "Expected padded RGBA readback");
    auto destination = take(device.createBuffer({footprint.size, gfx::Memory::Readback}));
    auto prefill = take(device.createBuffer({footprint.size, gfx::Memory::Upload}));
    take(prefill->write(0, std::vector<std::byte>(footprint.size, padding)));
    auto commands = take(device.createCommandList());
    take(commands->begin());
    take(commands->copyBuffer(prefill, destination, footprint.size));
    take(commands->transition(texture, State::RenderTarget));
    take(commands->beginRenderPass(texture, {0, 0, 1, 1}));
    take(commands->setPipeline(pipeline));
    take(commands->setScissor(2, 1, 8, 4));
    take(commands->draw(3));
    take(commands->endRenderPass());
    take(commands->transition(texture, State::CopySource));
    take(commands->copyTextureToBuffer(texture, destination));
    take(commands->end());
    take(device.queue().wait(take(device.queue().submit(*commands))));
    const auto bytes = take(destination->read(0, footprint.size));
    std::ofstream raw(output, std::ios::binary);
    for (unsigned y = 0; y < height; ++y) {
        const auto row = std::size_t(y) * footprint.rowPitch;
        raw.write(reinterpret_cast<const char*>(bytes.data() + row), footprint.rowBytes);
        for (auto i = row + footprint.rowBytes; i < row + footprint.rowPitch && i < bytes.size(); ++i)
            check(bytes[i] == padding, "Vulkan readback row padding changed");
    }
    raw.close();
    check(static_cast<bool>(raw), "Could not save Vulkan image");
}
}

int main(int argc, char** argv) try {
    using gfx::ErrorCode;
    using gfx::ShaderStage;
    check(argc == 2 || argc == 3, "Expected shader directory and optional cached fragment");
    const std::filesystem::path out(argv[1]);
    const bool measure = argc == 3 && std::string_view(argv[2]) == "--measure";
    gfx::DeviceDesc selection;
    selection.backend = gfx::Backend::Vulkan;
    selection.softwareAdapter = true;
    auto device = take(gfx::createDevice(selection));
    check(device->caps().backend == gfx::Backend::Vulkan && device->caps().softwareAdapter, "Expected CPU Vulkan device");
    gfx::GraphicsPipelineDesc desc;
    desc.vertex = shader(out/"vert.spv", ShaderStage::Vertex);
    desc.fragment = shader(out/"frag.spv", ShaderStage::Fragment);
    const auto vertex = take(gfx::reflectSpirvInterface(desc.vertex));
    const auto fragment = take(gfx::reflectSpirvInterface(desc.fragment));
    check(vertex.entryPoint == "main" && vertex.stage == ShaderStage::Vertex &&
          vertex.inputs.empty() && vertex.outputs.empty() && vertex.bindings.empty() &&
          fragment.entryPoint == "main" && fragment.stage == ShaderStage::Fragment &&
          fragment.inputs.empty() && fragment.outputs == std::vector<gfx::ShaderLocation>{{0, 4}} &&
          fragment.bindings.empty(), "Public interface of validated DXC modules differs");
    auto invalid = desc;
    invalid.fragment = shader(out/"named.spv", ShaderStage::Fragment);
    check(take(gfx::reflectSpirvInterface(invalid.fragment)).entryPoint == "pixelMain",
          "Reflection must preserve a non-default entry point");
    reject(*device, invalid, ErrorCode::Unsupported, "entry point must be main");
    invalid.fragment = desc.vertex;
    invalid.fragment.stage = ShaderStage::Fragment;
    reject(*device, invalid, ErrorCode::InvalidArgument, "execution model/stage mismatch");
    invalid = desc;
    invalid.vertex.bytes.resize(12);
    reject(*device, invalid, ErrorCode::InvalidArgument, "SPIR-V stage/byte framing");
    if (measure) {
        const auto timing = out/"timing";
        auto measured = desc;
        measured.vertex = shader(timing/"vert.spv", ShaderStage::Vertex);
        measured.fragment = shader(timing/"frag.spv", ShaderStage::Fragment);
        pipelineCacheTest::measure(*device, measured, [&](std::string name, gfx::PipelinePtr pipeline) {
            draw(*device, measured, timing/(name+".rgba"), std::move(pipeline));
        });
    } else if (argc == 3) {
        const std::filesystem::path cached(argv[2]);
        desc.fragment = shader(cached, ShaderStage::Fragment);
        const auto changed = shader(cached.parent_path()/"include-changed.spv", ShaderStage::Fragment);
        draw(*device, desc, out/"cached.rgba", pipelineCacheTest::exercise(*device, selection, desc, changed));
    } else {
        draw(*device, desc, out/"spirv.rgba");
    }
    std::cout << "SDK_SHA256=" << gfx::buildFingerprint() << '\n';
    std::cout << "PASS Vulkan software (" << device->caps().adapterName << "), 13x7\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
