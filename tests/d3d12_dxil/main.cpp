#include <graphics/Graphics.hpp>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include "PipelineCacheSupport.hpp"

namespace {
constexpr unsigned width = 13;
constexpr unsigned height = 7;

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

template<class T>
void reject(const gfx::Result<T>& result, gfx::ErrorCode code) {
    check(!result && result.error().code == code, "Expected a specific failure");
}

void write(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(file), "Could not write shader or image evidence");
}

gfx::ShaderBinary shader(const std::filesystem::path& path, gfx::ShaderStage stage) {
    std::ifstream file(path, std::ios::binary);
    check(static_cast<bool>(file), "Could not open offline DXIL");
    const std::string object{std::istreambuf_iterator<char>(file), {}};
    check(!object.empty(), "Offline DXIL was empty");
    gfx::ShaderBinary result{stage, std::vector<std::byte>(object.size()), gfx::ShaderFormat::DXIL};
    std::memcpy(result.bytes.data(), object.data(), object.size());
    return result;
}

std::vector<std::byte> render(gfx::Device& device, const gfx::GraphicsPipelineDesc& desc, gfx::PipelinePtr pipeline = {}) {
    using State = gfx::ResourceState;
    if (!pipeline) pipeline = take(device.createGraphicsPipeline(desc));
    auto texture = take(device.createTexture({width, height, gfx::Format::RGBA8UNorm, true}));
    const auto footprint = texture->footprint();
    auto destination = take(device.createBuffer({footprint.size, gfx::Memory::Readback}));
    auto padding = take(device.createBuffer({footprint.size, gfx::Memory::Upload}));
    take(padding->write(0, std::vector<std::byte>(footprint.size, std::byte{0xB7})));
    auto commands = take(device.createCommandList());
    take(commands->begin());
    take(commands->copyBuffer(padding, destination, footprint.size));
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

    auto bytes = take(destination->read(0, footprint.size));
    std::vector<std::byte> pixels;
    for (unsigned y = 0; y < height; ++y) {
        const auto row = std::size_t(y) * footprint.rowPitch;
        for (unsigned x = 0; x < width; ++x) {
            const bool covered = 2 <= x && x < 10 && 1 <= y && y < 5;
            const std::array<unsigned char, 4> expected = covered
                ? std::array<unsigned char, 4>{255, 64, 0, 255} : std::array<unsigned char, 4>{0, 0, 255, 255};
            for (unsigned channel = 0; channel < 4; ++channel) {
                const auto value = bytes.at(row + x * 4 + channel);
                check(value == std::byte{expected[channel]}, "RGBA reference differs");
                pixels.push_back(value);
            }
        }
        for (auto i = row + width * 4; i < row + footprint.rowPitch && i < bytes.size(); ++i)
            check(bytes[i] == std::byte{0xB7}, "Readback row padding changed");
    }
    return pixels;
}
}

int main(int argc, char** argv) try {
    using gfx::ErrorCode;
    using gfx::ShaderFormat;
    using gfx::ShaderStage;
    if (argc == 2 && std::string_view(argv[1]) == "--missing-runtime") {
        constexpr std::string_view pixel = "float4 main() : SV_Target0 { return 1; }";
        const auto dxil = gfx::compileHlslDxil(pixel, "main", ShaderStage::Fragment);
        reject(dxil, ErrorCode::Unsupported);
        check(dxil.error().message.find("application-local") != std::string::npos, "Unexpected missing-runtime diagnostic");
        check(take(gfx::compileHlsl(pixel, "main", ShaderStage::Fragment)).format == ShaderFormat::DXBC,
              "DXBC must work with the DXC runtime absent");
        std::cout << "PASS missing app-local DXC and working DXBC bootstrap\n";
        return 0;
    }
    const bool measure = argc == 5 && std::string_view(argv[4]) == "--measure";
    check(argc == 3 || argc == 4 || measure, "Expected HLSL, output, optional cached fragment and --measure");
    std::ifstream input(argv[1], std::ios::binary);
    check(static_cast<bool>(input), "Could not open HLSL fixture");
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    const std::filesystem::path output(argv[2]);
    std::filesystem::create_directories(output);
    gfx::GraphicsPipelineDesc dxil;
    dxil.vertex = take(gfx::compileHlslDxil(source, "vertexMain", ShaderStage::Vertex));
    dxil.fragment = take(gfx::compileHlslDxil(source, "pixelMain", ShaderStage::Fragment));
    check(dxil.vertex.format == ShaderFormat::DXIL && dxil.fragment.format == ShaderFormat::DXIL, "Explicit DXIL format");
    check(take(gfx::reflectShader(dxil.vertex)).empty(), "Vertex shader must have no bindings");
    auto malformed = dxil.vertex;
    malformed.bytes.resize(12);
    check(!gfx::reflectShader(malformed), "Malformed DXIL must fail reflection");
    malformed = dxil.vertex;
    malformed.stage = ShaderStage::Fragment;
    reject(gfx::reflectShader(malformed), ErrorCode::InvalidArgument);
    const auto broken = gfx::compileHlslDxil("float4 broken() : SV_Target0 { return missingSymbol; }",
                                             "broken", ShaderStage::Fragment);
    reject(broken, ErrorCode::BackendFailure);
    check(broken.error().nativeCode != 0 && broken.error().message.find("missingSymbol") != std::string::npos,
          "Compiler diagnostic must retain the offending identifier and status");
    std::ofstream(output/"invalid-hlsl.log") << broken.error().message;
    reject(gfx::compileHlslDxil("#include \"no_include.hlsl\"\nfloat4 main() : SV_Target0 { return 1; }",
                              "main", ShaderStage::Fragment), ErrorCode::BackendFailure);

    gfx::DeviceDesc deviceDesc;
    deviceDesc.softwareAdapter = true;
    auto device = take(gfx::createDevice(deviceDesc));
    check(device->caps().backend == gfx::Backend::D3D12 && device->caps().softwareAdapter, "Explicit D3D12 WARP");
    auto wrongOutput = dxil;
    wrongOutput.fragment = take(gfx::compileHlslDxil("float4 main() : SV_Target1 { return 1; }", "main", ShaderStage::Fragment));
    reject(device->createGraphicsPipeline(wrongOutput), ErrorCode::InvalidArgument);
    auto binding = dxil;
    binding.fragment = take(gfx::compileHlslDxil(
        "cbuffer Color : register(b2, space3) { float4 color; }; float4 main() : SV_Target0 { return color; }",
        "main", ShaderStage::Fragment));
    const auto resources = take(gfx::reflectShader(binding.fragment));
    check(resources.size() == 1 && resources[0].type == gfx::ShaderResourceType::ConstantBuffer &&
          resources[0].slot == 2 && resources[0].space == 3 && resources[0].count == 1 &&
          resources[0].minimumBytes == 16, "DXIL cbuffer reflection");
    reject(device->createGraphicsPipeline(binding), ErrorCode::InvalidArgument);

    gfx::GraphicsPipelineDesc dxbc;
    dxbc.vertex = take(gfx::compileHlsl(source, "vertexMain", ShaderStage::Vertex));
    dxbc.fragment = take(gfx::compileHlsl(source, "pixelMain", ShaderStage::Fragment));
    check(dxbc.vertex.format == ShaderFormat::DXBC && dxbc.fragment.format == ShaderFormat::DXBC, "Default DXBC format");
    const auto dxilImage = render(*device, dxil);
    const auto dxbcImage = render(*device, dxbc);
    check(dxilImage == dxbcImage, "DXBC and DXIL must produce identical reference images");
    if (argc >= 4) {
        auto warm = dxil;
        warm.fragment = shader(argv[3], ShaderStage::Fragment);
        const auto different = take(gfx::compileHlslDxil("float4 main() : SV_Target0 { return float4(0,1,0,1); }",
                                                         "main", ShaderStage::Fragment));
        const auto pipeline = pipelineCacheTest::exercise(*device, deviceDesc, warm, different);
        const auto cachedImage = render(*device, warm, pipeline);
        check(cachedImage == dxilImage, "Cached DXIL differs from the uncached image");
        write(output/"cached.rgba", cachedImage);
    }
    if (measure) {
        const auto timing = output/"timing";
        auto desc = dxil;
        desc.vertex = shader(timing/"vert.dxil", ShaderStage::Vertex);
        desc.fragment = shader(timing/"frag.dxil", ShaderStage::Fragment);
        pipelineCacheTest::measure(*device, desc, [&](std::string name, gfx::PipelinePtr pipeline) {
            write(timing/(name+".rgba"), render(*device, desc, std::move(pipeline)));
        });
    }
    write(output/"vert.dxil", dxil.vertex.bytes);
    write(output/"frag.dxil", dxil.fragment.bytes);
    write(output/"dxil.rgba", dxilImage);
    write(output/"dxbc.rgba", dxbcImage);
    std::cout << "PASS D3D12 WARP (" << device->caps().adapterName << "), 2 images of " << width << 'x' << height << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
