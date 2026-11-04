#include <graphics/Graphics.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
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
    check(size > 0 && size <= 4 * 1024 * 1024, "Unexpected MSL size");
    gfx::ShaderBinary result{stage, std::vector<std::byte>(size), gfx::ShaderFormat::MSL};
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(result.bytes.data()), static_cast<std::streamsize>(size));
    check(static_cast<bool>(file), "Could not read MSL");
    return result;
}
}

int main(int argc, char** argv) try {
    using gfx::ShaderStage;
    using State = gfx::ResourceState;
    check(argc == 2, "Expected directory with the generated MSL");
    const std::filesystem::path out(argv[1]);
    gfx::DeviceDesc selection;
    selection.backend = gfx::Backend::Metal;
    auto device = take(gfx::createDevice(selection));
    check(device->caps().backend == gfx::Backend::Metal && !device->caps().adapterName.empty(), "Expected a native Metal device");
    gfx::GraphicsPipelineDesc desc;
    desc.vertex = shader(out/"vert.metal", ShaderStage::Vertex);
    desc.fragment = shader(out/"frag.metal", ShaderStage::Fragment);
    auto invalid = desc;
    invalid.fragment = shader(out/"invalid.metal", ShaderStage::Fragment);
    const auto rejected = device->createGraphicsPipeline(invalid);
    check(!rejected && rejected.error().code == gfx::ErrorCode::BackendFailure &&
          rejected.error().message.find("missingSymbol") != std::string::npos, "Missing native MSL diagnostic");
    std::cout << rejected.error().message << '\n';

    auto pipeline = take(device->createGraphicsPipeline(desc));
    auto texture = take(device->createTexture({13, 7, gfx::Format::RGBA8UNorm, true}));
    const auto footprint = texture->footprint();
    check(footprint.height == 7 && footprint.rowBytes == 52 &&
          footprint.rowPitch == 256 && footprint.size == 1792, "Unexpected RGBA8 readback footprint");
    auto destination = take(device->createBuffer({footprint.size, gfx::Memory::Readback}));
    auto prefill = take(device->createBuffer({footprint.size, gfx::Memory::Upload}));
    take(prefill->write(0, std::vector<std::byte>(footprint.size, std::byte{0xB7})));
    auto commands = take(device->createCommandList());
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
    take(device->queue().wait(take(device->queue().submit(*commands))));
    const auto bytes = take(destination->read(0, footprint.size));
    std::ofstream raw(out/"metal.bin", std::ios::binary);
    raw.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    raw.close();
    check(static_cast<bool>(raw), "Could not save Metal readback");
    std::cout << "SDK_SHA256=" << gfx::buildFingerprint() << '\n';
    std::cout << "PASS Metal (" << device->caps().adapterName << "), 13x7\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
