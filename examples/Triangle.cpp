#include <graphics/Graphics.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
template<class T> T require(gfx::Result<T> result) { if (!result) throw std::runtime_error(result.error().message); return std::move(*result); }
void require(gfx::Result<void> result) { if (!result) throw std::runtime_error(result.error().message); }
}
int main(int argc, char** argv) try {
    auto device = require(gfx::createDevice());
    constexpr std::string_view source = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    float2 p[3] = {float2(-.8,-.8),float2(0,.8),float2(.8,-.8)};
    return float4(p[id % 3],.5,1);
}
float4 PS() : SV_Target0 { return float4(1,.2,.05,1); }
)";
    auto vertex = require(gfx::compileHlsl(source, "VS", gfx::ShaderStage::Vertex));
    auto fragment = require(gfx::compileHlsl(source, "PS", gfx::ShaderStage::Fragment));
    auto pipeline = require(device->createGraphicsPipeline({vertex,fragment}));
    auto texture = require(device->createTexture({256,256,gfx::Format::RGBA8UNorm,true}));
    auto footprint = texture->footprint();
    auto readback = require(device->createBuffer({footprint.size,gfx::Memory::Readback}));
    auto cmd = require(device->createCommandList());
    require(cmd->begin()); require(cmd->writeTimestamp(0)); require(cmd->transition(texture,gfx::ResourceState::RenderTarget));
    require(cmd->beginRenderPass(texture,{.025f,.04f,.09f,1})); require(cmd->setPipeline(pipeline)); require(cmd->draw(3)); require(cmd->endRenderPass());
    require(cmd->writeTimestamp(1)); require(cmd->resolveTimestamps()); require(cmd->transition(texture,gfx::ResourceState::CopySource));
    require(cmd->copyTextureToBuffer(texture,readback)); require(cmd->end());
    auto fence = require(device->queue().submit(*cmd)); require(device->queue().wait(fence));
    auto rgba = require(readback->read(0,footprint.size));
    std::string output = argc > 1 ? argv[1] : "triangle.ppm";
    std::ofstream file(output, std::ios::binary); if (!file) throw std::runtime_error("Cannot create output image");
    file << "P6\n256 256\n255\n";
    for (unsigned y=0; y<256; ++y) for (unsigned x=0; x<256; ++x) file.write(reinterpret_cast<const char*>(rgba.data()+y*footprint.rowPitch+x*4),3);
    if (!file) throw std::runtime_error("Image write failed");
    std::cout << device->caps().adapterName << ": rendered " << output << ", GPU " << require(cmd->elapsedGpuMilliseconds()) << " ms\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
