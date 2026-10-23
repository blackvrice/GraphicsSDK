#include <graphics/Graphics.hpp>
#include "fixtures/SpirvFixtures.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using F = gfx::Format; using S = gfx::ResourceState; using Pixel = std::array<float, 4>;
unsigned assertions{}, draws{}, images{}, paddedImages{};
std::uint64_t verifiedBytes{};
void check(bool value, const char* message) { ++assertions; if (!value) throw std::runtime_error(message); }
template<class T> T take(gfx::Result<T> r) {
    if (!r) throw std::runtime_error(r.error().message + " native=" + std::to_string(r.error().nativeCode)); return std::move(*r);
}
void take(gfx::Result<void> r) { if (!r) throw std::runtime_error(r.error().message); }
template<class T> void reject(gfx::Result<T> r, gfx::ErrorCode c, const char* m) { check(!r && r.error().code == c, m); }
gfx::ShaderBinary binary(std::span<const std::uint32_t> words, gfx::ShaderStage stage) {
    const auto bytes = std::as_bytes(words); return {stage, {bytes.begin(), bytes.end()}, gfx::ShaderFormat::SPIRV};
}
gfx::GraphicsPipelineDesc description(std::span<const std::uint32_t> vertex, std::span<const std::uint32_t> fragment, std::vector<F> formats, bool blend = false) {
    gfx::GraphicsPipelineDesc p; p.vertex = binary(vertex, gfx::ShaderStage::Vertex); p.fragment = binary(fragment, gfx::ShaderStage::Fragment);
    p.renderTargets = gfx::RenderTargetLayout{std::move(formats), {}}; p.alphaBlend = blend; return p;
}
gfx::DeviceDesc deviceDesc(const gfx::AdapterInfo& a, bool validation) {
    gfx::DeviceDesc d; d.backend = gfx::Backend::Vulkan; d.adapter = a.id; d.softwareAdapter = a.softwareAdapter; d.validation = validation; return d;
}
void reclaimed(gfx::Device& d) {
    const auto s = take(d.resourceStatistics());
    check(!s.trackedObjects && !s.uniqueResources && !s.resourceAllocationBytes && !s.pendingSubmissions && !s.pendingObjectReferences, "All draw/attachment/pipeline references reclaimed");
}
void draw(gfx::CommandList& c, unsigned vertices = 3, unsigned instances = 1, unsigned firstVertex = 0, unsigned firstInstance = 0) {
    take(c.draw(vertices, instances, firstVertex, firstInstance)); ++draws;
}
struct Readback { gfx::BufferPtr buffer; gfx::TextureFootprint f; F format; };
Readback readback(gfx::Device& d, gfx::CommandList& c, const gfx::TexturePtr& t) {
    const auto f = t->footprint(); auto b = take(d.createBuffer({f.size, gfx::Memory::Readback})); auto sentinel = take(d.createBuffer({f.size, gfx::Memory::Upload}));
    take(sentinel->write(0, std::vector<std::byte>(static_cast<std::size_t>(f.size), std::byte{0xb9})));
    take(c.copyBuffer(sentinel, b, f.size)); take(c.transition(t, S::CopySource)); take(c.copyTextureToBuffer(t, b)); return {b, f, t->description().format};
}
void submit(gfx::Device& d, std::unique_ptr<gfx::CommandList>& c) {
    take(c->end()); const auto fence = take(d.queue().submit(*c)); c.reset(); take(d.queue().wait(fence));
}
float half(std::uint16_t h) {
    const auto e = (h >> 10) & 31, f = h & 1023; return std::ldexp(e ? 1.f + f / 1024.f : f / 1024.f, e ? int(e) - 15 : -14) * (h & 0x8000 ? -1.f : 1.f);
}
void verify(const Readback& r, const std::function<Pixel(unsigned, unsigned)>& expected) {
    const auto bytes = take(r.buffer->read(0, r.f.size)); const auto texel = r.format == F::RGBA8UNorm ? 4u : 8u;
    bool pixels = true, padding = true;
    for (unsigned y = 0; y < r.f.height; ++y) {
        const auto base = std::size_t(y) * r.f.rowPitch;
        for (unsigned x = 0; x < r.f.rowBytes / texel; ++x) {
            const auto reference = expected(x, y);
            for (unsigned i = 0; i < 4; ++i) {
                float value;
                if (texel == 4) value = std::to_integer<unsigned char>(bytes[base + x * 4 + i]) / 255.f;
                else { std::uint16_t h; std::memcpy(&h, bytes.data() + base + x * 8 + i * 2, 2); value = half(h); }
                pixels &= std::isfinite(value) && std::abs(value - reference[i]) <= (texel == 4 ? 1.01f / 255 : .001f);
            }
        }
        padding &= std::all_of(bytes.begin() + base + r.f.rowBytes, bytes.begin() + base + r.f.rowPitch, [](auto b) { return b == std::byte{0xb9}; });
    }
    check(pixels, "Every rasterized pixel matches independent CPU geometry/color reference"); check(padding, "Draw readback leaves row/final-row sentinel padding untouched");
    ++images; if (r.f.rowPitch != r.f.rowBytes) ++paddedImages; verifiedBytes += std::uint64_t(r.f.rowBytes) * r.f.height;
}
bool triangle(unsigned x, unsigned y, unsigned width, unsigned height, double shift = 0) {
    const double nx = (2.0 * x + 1) / width - 1 - shift, ny = 1 - (2.0 * y + 1) / height;
    // Odd dimensions and this 1:2 right triangle put no sample exactly on an edge.
    return nx >= -.75 && ny >= -.75 && (nx + .75) / .75 + (ny + .75) / 1.5 <= 1;
}
void scene(gfx::Device& d, F format, unsigned width, unsigned height, bool full) {
    auto t = take(d.createTexture({width, height, format, true}));
    auto p = take(d.createGraphicsPipeline(description(full ? std::span<const std::uint32_t>(fixtures::fullscreen) : std::span<const std::uint32_t>(fixtures::triangle), fixtures::red, {format})));
    auto c = take(d.createCommandList()); take(c->begin()); take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {0, 0, 1, 1}));
    take(c->setPipeline(p)); draw(*c); take(c->endRenderPass()); auto r = readback(d, *c, t);
    std::weak_ptr<gfx::Pipeline> wrapper = p; p.reset(); check(wrapper.expired(), "Recording retains native pipeline rather than public wrapper");
    t.reset(); submit(d, c); verify(r, [=](unsigned x, unsigned y) { return full || triangle(x, y, width, height) ? Pixel{1, 0, 0, 1} : Pixel{0, 0, 1, 1}; });
}
void scissorBlend(gfx::Device& d, F format) {
    auto t = take(d.createTexture({65, 37, format, true})); auto c = take(d.createCommandList());
    auto red = take(d.createGraphicsPipeline(description(fixtures::fullscreen, fixtures::red, {format})));
    auto green = take(d.createGraphicsPipeline(description(fixtures::fullscreen, fixtures::green, {format})));
    auto blend = take(d.createGraphicsPipeline(description(fixtures::fullscreen, fixtures::half_red, {format}, true)));
    take(c->begin()); take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {0, 0, 1, 1}));
    take(c->setPipeline(red)); take(c->setScissor(0, 0, 21, 37)); draw(*c);
    take(c->setPipeline(green)); take(c->setScissor(21, 0, 44, 37));
    reject(c->setScissor(UINT32_MAX, 0, 2, 1), gfx::ErrorCode::InvalidArgument, "Overflowing scissor preserves previous rectangle"); draw(*c);
    take(c->endRenderPass());
    take(c->beginRenderPass({{{t, gfx::LoadOp::Load}}, {}})); take(c->setPipeline(blend)); take(c->setScissor(16, 9, 33, 19)); draw(*c); take(c->endRenderPass());
    auto r = readback(d, *c, t); red.reset(); green.reset(); blend.reset(); t.reset(); submit(d, c);
    verify(r, [](unsigned x, unsigned y) { Pixel p = x < 21 ? Pixel{1, 0, 0, 1} : Pixel{0, 1, 0, 1}; if (x >= 16 && x < 49 && y >= 9 && y < 28) { p[0] = .5f + .5f * p[0]; p[1] *= .5f; p[3] = .5f; } return p; });
}
void instances(gfx::Device& d, F format, unsigned count, unsigned first) {
    auto t = take(d.createTexture({65, 37, format, true})); auto p = take(d.createGraphicsPipeline(description(fixtures::instanced, fixtures::red, {format})));
    auto c = take(d.createCommandList()); take(c->begin()); take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {0, 0, 1, 1})); take(c->setPipeline(p));
    draw(*c, 3, count, 6, first); take(c->endRenderPass()); auto r = readback(d, *c, t); p.reset(); t.reset(); submit(d, c);
    verify(r, [=](unsigned x, unsigned y) { bool red{}; for (unsigned n = 0; n < count; ++n) red |= triangle(x, y, 65, 37, (n + first) % 2); return red ? Pixel{1, 0, 0, 1} : Pixel{0, 0, 1, 1}; });
}
void mrt(gfx::Device& d, unsigned count) {
    std::vector<F> formats; gfx::RenderPassDesc pass;
    for (unsigned n = 0; n < count; ++n) { formats.push_back(n % 2 ? F::RGBA16Float : F::RGBA8UNorm); pass.colors.push_back({take(d.createTexture({33, 17, formats.back(), true}))}); }
    auto p = take(d.createGraphicsPipeline(description(fixtures::fullscreen, count == 2 ? std::span<const std::uint32_t>(fixtures::mrt2) : std::span<const std::uint32_t>(fixtures::mrt8), formats)));
    auto c = take(d.createCommandList()); take(c->begin()); for (auto& a : pass.colors) take(c->transition(a.texture, S::RenderTarget));
    take(c->beginRenderPass(pass)); take(c->setPipeline(p)); draw(*c); take(c->endRenderPass());
    std::vector<Readback> reads; for (auto& a : pass.colors) reads.push_back(readback(d, *c, a.texture));
    pass = {}; p.reset(); submit(d, c);
    for (unsigned n = 0; n < count; ++n) verify(reads[n], [=](unsigned, unsigned) { return count == 2 ? (n ? Pixel{0, 1, 0, 1} : Pixel{1, 0, 0, 1}) : Pixel{float(n & 1), float((n >> 1) & 1), float((n >> 2) & 1), 1}; });
}
void contracts(gfx::Device& d, const gfx::AdapterInfo& a, bool validation) {
    auto desc = description(fixtures::fullscreen, fixtures::red, {F::RGBA8UNorm});
    auto bad = desc; bad.vertex.bytes.pop_back(); reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::InvalidArgument, "Malformed byte framing rejected before driver");
    bad = desc; bad.fragment.stage = gfx::ShaderStage::Vertex; reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::InvalidArgument, "Shader stage mismatch");
    bad = desc; bad.vertex = binary(fixtures::unsupported_point, gfx::ShaderStage::Vertex); reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::Unsupported, "Validated but unsupported builtin rejected");
    bad = desc; bad.fragment = binary(fixtures::mrt2, gfx::ShaderStage::Fragment); reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::InvalidArgument, "Output count mismatch");
    bad = desc; bad.sampledTexture = true; bad.bindingLayout.entries.push_back({}); reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::InvalidArgument, "Legacy and explicit layouts cannot be combined");
    bad = desc; bad.vertex.format = gfx::ShaderFormat::DXBC; reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::Unsupported, "DXBC never sent to Vulkan");
    auto p = take(d.createGraphicsPipeline(desc)); auto wrongFormat = take(d.createGraphicsPipeline(description(fixtures::fullscreen, fixtures::green, {F::RGBA16Float})));
    auto other = take(gfx::createDevice(deviceDesc(a, validation))); auto foreign = take(other->createGraphicsPipeline(desc));
    auto t = take(d.createTexture({65, 37, F::RGBA8UNorm, true})); auto c = take(d.createCommandList());
    reject(c->setPipeline(p), gfx::ErrorCode::InvalidState, "Pipeline outside recording"); take(c->begin());
    reject(c->setPipeline(p), gfx::ErrorCode::InvalidState, "Pipeline requires pass"); reject(c->draw(3), gfx::ErrorCode::InvalidState, "Draw requires pass");
    take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {0, 0, 1, 1})); reject(c->draw(3), gfx::ErrorCode::InvalidState, "Draw requires pipeline"); take(c->setPipeline(p));
    reject(c->setPipeline({}), gfx::ErrorCode::InvalidArgument, "Null pipeline preserves binding");
    reject(c->setPipeline(foreign), gfx::ErrorCode::WrongDevice, "Foreign pipeline preserves binding");
    reject(c->setPipeline(wrongFormat), gfx::ErrorCode::InvalidArgument, "Format mismatch preserves binding");
    for (unsigned n = 0; n < 32; ++n) {
        reject(c->draw(0), gfx::ErrorCode::InvalidArgument, "Zero draw"); reject(c->draw(3, 0), gfx::ErrorCode::InvalidArgument, "Zero instances");
        reject(c->draw(3, 1, UINT32_MAX), gfx::ErrorCode::InvalidArgument, "Overflowing vertex range"); reject(c->draw(3, 2, 0, UINT32_MAX), gfx::ErrorCode::InvalidArgument, "Overflowing instance range");
        reject(c->setScissor(0, 0, 0, 1), gfx::ErrorCode::InvalidArgument, "Empty scissor"); reject(c->setScissor(64, 0, 2, 1), gfx::ErrorCode::InvalidArgument, "Out of bounds scissor");
    }
    bool threadRejected{}; std::thread worker([&] { auto r = d.createGraphicsPipeline(desc); threadRejected = !r && r.error().code == gfx::ErrorCode::InvalidState; }); worker.join(); check(threadRejected, "Pipeline creation requires creating thread");
    draw(*c); take(c->endRenderPass()); auto r = readback(d, *c, t); p.reset(); wrongFormat.reset(); foreign.reset(); t.reset(); submit(d, c); verify(r, [](unsigned, unsigned) { return Pixel{1, 0, 0, 1}; }); r.buffer.reset(); reclaimed(d);
    check(take(d.validationErrorCount()) == 0 && take(other->validationErrorCount()) == 0, "Rejections and drawing remain native-validation clean");
    auto discarded = take(d.createCommandList()); auto target = take(d.createTexture({3, 7, F::RGBA8UNorm, true})); auto pipeline = take(d.createGraphicsPipeline(desc));
    take(discarded->begin()); take(discarded->transition(target, S::RenderTarget)); take(discarded->beginRenderPass(target, {})); take(discarded->setPipeline(pipeline)); draw(*discarded); take(discarded->endRenderPass()); take(discarded->end());
    target.reset(); pipeline.reset(); discarded.reset(); reclaimed(d);
}
void retirement(const gfx::AdapterInfo& a, bool validation) {
    auto d = take(gfx::createDevice(deviceDesc(a, validation))); auto p = take(d->createGraphicsPipeline(description(fixtures::fullscreen, fixtures::red, {F::RGBA8UNorm})));
    auto t = take(d->createTexture({3, 7, F::RGBA8UNorm, true})); auto c = take(d->createCommandList()); take(c->begin()); take(c->transition(t, S::RenderTarget));
    take(c->beginRenderPass(t, {})); take(c->setPipeline(p)); draw(*c); take(c->endRenderPass()); take(c->end()); take(d->queue().submit(*c));
    auto unused = take(d->createGraphicsPipeline(description(fixtures::triangle, fixtures::green, {F::RGBA8UNorm}))); d.reset();
    reject(c->begin(), gfx::ErrorCode::DeviceLost, "Pipeline recording retires with device"); p.reset(); unused.reset();
    check(t->description().renderTarget, "Retired target retains description");
}
}
int main(int argc, char** argv) try {
    if (argc != 1 && !(argc == 2 && std::string_view(argv[1]) == "--validation")) throw std::runtime_error("Usage: gfx_vulkan_draw_tests [--validation]");
    const bool validation = argc == 2; std::cout << "source=" << gfx::buildFingerprint() << " validation=" << validation << std::endl;
    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan)); check(!adapters.empty(), "Real Vulkan adapter required");
    for (const auto& a : adapters) {
        std::cout << "adapter=" << a.name << " id=" << a.id.value << std::endl; auto d = take(gfx::createDevice(deviceDesc(a, validation))); contracts(*d, a, validation);
        for (auto format : {F::RGBA8UNorm, F::RGBA16Float}) {
            for (auto [width, height] : {std::pair{1u, 1u}, {3u, 7u}, {65u, 37u}, {1920u, 1080u}, {3840u, 2160u}}) { scene(*d, format, width, height, true); reclaimed(*d); }
            for (auto [width, height] : {std::pair{65u, 37u}, {129u, 73u}}) { scene(*d, format, width, height, false); reclaimed(*d); }
            scissorBlend(*d, format); reclaimed(*d);
            for (auto [count, first] : {std::pair{1u, 1u}, {2u, 0u}, {1u, 0u}}) { instances(*d, format, count, first); reclaimed(*d); }
        }
        mrt(*d, 2); reclaimed(*d); mrt(*d, 8); reclaimed(*d);
        check(take(d->validationErrorCount()) == 0, "No normal SDK draw validation errors"); retirement(a, validation);
    }
    std::cout << "PASS draw_assertions=" << assertions << " draws=" << draws << " images=" << images << " verified_texel_bytes=" << verifiedBytes << " padded_images=" << paddedImages << " adapters=" << adapters.size() << std::endl; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << std::endl; return 1; }
