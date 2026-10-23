#include "VulkanDepthSupport.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using F = gfx::Format; using S = gfx::ResourceState; using L = gfx::LoadOp; using C = gfx::CompareOp; using Pixel = std::array<float, 4>;
unsigned assertions{}, passes{}, draws{}, images{}, depthImages{}, paddedImages{};
std::uint64_t verifiedBytes{};
void check(bool value, const char* m) { ++assertions; if (!value) throw std::runtime_error(m); }
template<class T> T take(gfx::Result<T> r) { if (!r) throw std::runtime_error(r.error().message + " native=" + std::to_string(r.error().nativeCode)); return std::move(*r); }
void take(gfx::Result<void> r) { if (!r) throw std::runtime_error(r.error().message); }
template<class T> void reject(gfx::Result<T> r, gfx::ErrorCode code, const char* m) { check(!r && r.error().code == code, m); }
gfx::DeviceDesc desc(const gfx::AdapterInfo& a, bool validation) { gfx::DeviceDesc d; d.backend = gfx::Backend::Vulkan; d.adapter = a.id; d.softwareAdapter = a.softwareAdapter; d.validation = validation; return d; }
void reclaimed(gfx::Device& d) { const auto s = take(d.resourceStatistics()); check(!s.trackedObjects && !s.uniqueResources && !s.resourceAllocationBytes && !s.pendingSubmissions && !s.pendingObjectReferences, "All depth/attachment/pipeline/recording references reclaimed"); }
gfx::TexturePtr depth(gfx::Device& d, unsigned w = 65, unsigned h = 37) { return take(d.createTexture({w, h, F::D32Float, false, true})); }
struct Readback { gfx::BufferPtr buffer; gfx::TextureFootprint f; F format; };
Readback readback(gfx::Device& d, gfx::CommandList& c, const gfx::TexturePtr& t) {
    const auto f = t->footprint(); auto b = take(d.createBuffer({f.size, gfx::Memory::Readback}));
    auto sentinel = take(d.createBuffer({f.size, gfx::Memory::Upload})); take(sentinel->write(0, std::vector<std::byte>(static_cast<std::size_t>(f.size), std::byte{0xb3})));
    take(c.copyBuffer(sentinel, b, f.size)); take(c.transition(t, S::CopySource)); take(c.copyTextureToBuffer(t, b)); return {b, f, t->description().format};
}
void submit(gfx::Device& d, std::unique_ptr<gfx::CommandList>& c) { take(c->end()); const auto fence = take(d.queue().submit(*c)); c.reset(); take(d.queue().wait(fence)); }
float half(std::uint16_t h) { const auto e = (h >> 10) & 31, f = h & 1023; return std::ldexp(e ? 1.f + f / 1024.f : f / 1024.f, e ? int(e) - 15 : -14) * (h & 0x8000 ? -1.f : 1.f); }
void verify(const Readback& r, const std::function<Pixel(unsigned, unsigned)>& expected) {
    const auto bytes = take(r.buffer->read(0, r.f.size)); const auto texel = r.format == F::RGBA8UNorm ? 4u : 8u; bool pixels = true, padding = true;
    for (unsigned y = 0; y < r.f.height; ++y) {
        const auto at = std::size_t(y) * r.f.rowPitch;
        for (unsigned x = 0; x < r.f.rowBytes / texel; ++x) {
            const auto p = expected(x, y);
            for (unsigned c = 0; c < 4; ++c) {
                float value; if (texel == 4) value = std::to_integer<unsigned char>(bytes[at + x * 4 + c]) / 255.f;
                else { std::uint16_t h; std::memcpy(&h, bytes.data() + at + x * 8 + c * 2, 2); value = half(h); }
                pixels &= std::isfinite(value) && std::abs(value - p[c]) <= (texel == 4 ? 2.1f / 255 : .004f);
            }
        }
        padding &= std::all_of(bytes.begin() + at + r.f.rowBytes, bytes.begin() + at + r.f.rowPitch, [](auto b) { return b == std::byte{0xb3}; });
    }
    check(pixels, "Every color/depth-tested pixel agrees with independent CPU reference"); check(padding, "Resource draw preserves readback row padding");
    ++images; if (r.f.rowPitch != r.f.rowBytes) ++paddedImages; verifiedBytes += std::uint64_t(r.f.rowBytes) * r.f.height;
}


void verifyDepth(const Readback& r, const std::function<float(unsigned, unsigned)>& expected) {
    auto data = take(r.buffer->read(0, r.f.size)); bool pixels = true, padding = true;
    for (unsigned y = 0; y < r.f.height; ++y) {
        auto at = std::size_t(y) * r.f.rowPitch;
        for (unsigned x = 0; x < r.f.rowBytes / 4; ++x) { float value; std::memcpy(&value, data.data() + at + x * 4, 4); pixels &= std::isfinite(value) && std::abs(value - expected(x, y)) < 0.000001f; }
        padding &= std::all_of(data.begin() + at + r.f.rowBytes, data.begin() + at + r.f.rowPitch, [](auto b) { return b == std::byte{0xb3}; });
    }
    check(pixels, "Every D32 texel matches independent CPU reference"); check(padding, "Depth copy preserves all row padding"); ++images; ++depthImages;
    if (r.f.rowPitch != r.f.rowBytes) ++paddedImages; verifiedBytes += std::uint64_t(r.f.rowBytes) * r.f.height;
}
gfx::BufferPtr constants(gfx::Device& d, gfx::CommandList& c) {
    auto b = take(d.createBuffer({4096, gfx::Memory::Upload})); std::vector<std::byte> data(4096);
    const std::array<Pixel, 4> colors{{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {0, 0, 1, 1}}};
    for (unsigned i = 0; i < 4; ++i) { const float z = .25f * (i % 3 + 1); std::memcpy(data.data() + i * 256, colors[i].data(), 16); std::memcpy(data.data() + i * 256 + 16, &z, 4); }
    take(b->write(0, data)); take(c.transition(b, S::ConstantBuffer)); return b;
}
void draw(gfx::CommandList& c, const gfx::BufferPtr& b, unsigned slot) { take(c.bindResources({{}, {{0, 0, b, slot * 256ull, 256}}})); take(c.draw(3)); ++draws; }
void begin(gfx::CommandList& c, const gfx::RenderPassDesc& pass) { take(c.beginRenderPass(pass)); ++passes; }
bool compare(C op, float a, float b) {
    switch (op) { case C::Never: return false; case C::Less: return a < b; case C::Equal: return a == b; case C::LessEqual: return a <= b; case C::Greater: return a > b; case C::NotEqual: return a != b; case C::GreaterEqual: return a >= b; case C::Always: return true; } return false;
}
void transfer(gfx::Device& d, unsigned w, unsigned h, bool device) {
    auto z = depth(d, w, h); const auto f = z->footprint(); check(f.rowBytes == w * 4 && f.rowPitch % 256 == 0 && f.size == std::uint64_t(f.rowPitch) * h, "D32 footprint is float32 with full final padding");
    auto c = take(d.createCommandList()); take(c->begin()); std::vector<std::byte> data(static_cast<std::size_t>(f.size), std::byte{0x6a});
    auto reference = [](unsigned x, unsigned y) { return float((x * 3 + y * 7) % 17) / 16; };
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) { float value = reference(x, y); std::memcpy(data.data() + y * f.rowPitch + x * 4, &value, 4); }
    auto upload = take(d.createBuffer({f.size, gfx::Memory::Upload})); take(upload->write(0, data)); auto source = upload;
    if (device) { source = take(d.createBuffer({f.size})); take(c->transition(source, S::CopyDestination)); take(c->copyBuffer(upload, source, f.size)); take(c->transition(source, S::CopySource)); }
    take(c->transition(z, S::CopyDestination)); take(c->copyBufferToTexture(source, z)); take(c->transition(z, S::DepthRead)); begin(*c, {{}, {z, L::Load, 1, true}}); take(c->endRenderPass());
    // A second image copy through a Device buffer checks both transfer directions.
    if (device) { take(c->transition(z, S::CopySource)); auto bridge = take(d.createBuffer({f.size})); take(c->transition(bridge, S::CopyDestination)); take(c->copyTextureToBuffer(z, bridge)); take(c->transition(bridge, S::CopySource)); take(c->transition(z, S::CopyDestination)); take(c->copyBufferToTexture(bridge, z)); }
    auto r = readback(d, *c, z); z.reset(); source.reset(); upload.reset(); submit(d, c); verifyDepth(r, reference);
}
void comparisons(gfx::Device& d, F format, C op, bool writes, bool enabled = true) {
    auto p = take(d.createGraphicsPipeline(depthTest::pipeline({{format}, F::D32Float}, {enabled, writes, op})));
    auto t = take(d.createTexture({65, 37, format, true})), z = depth(d); auto c = take(d.createCommandList()); take(c->begin()); auto b = constants(d, *c);
    take(c->transition(t, S::RenderTarget)); take(c->transition(z, S::DepthWrite)); begin(*c, {{{t, L::Clear, {0, 0, 0, 1}}}, {z, L::Clear, .5f}}); take(c->setPipeline(p));
    for (unsigned i = 0; i < 3; ++i) { take(c->setScissor(i * 21, 0, i == 2 ? 23 : 21, 37)); draw(*c, b, i); } take(c->endRenderPass());
    auto color = readback(d, *c, t), dep = readback(d, *c, z); b.reset(); p.reset(); t.reset(); z.reset(); submit(d, c);
    verify(color, [=](unsigned x, unsigned) { auto i = std::min(x / 21, 2u); bool pass = !enabled || compare(op, .25f * (i + 1), .5f); return pass ? Pixel{float(i == 0), float(i == 1), float(i == 2), 1} : Pixel{0, 0, 0, 1}; });
    verifyDepth(dep, [=](unsigned x, unsigned) { float v = .25f * (std::min(x / 21, 2u) + 1); return enabled && writes && compare(op, v, .5f) ? v : .5f; });
}
void mrt(gfx::Device& d, unsigned count, bool reverse) {
    std::vector<F> formats; std::vector<gfx::TexturePtr> targets; gfx::RenderPassDesc pass;
    for (unsigned i = 0; i < count; ++i) { auto f = i % 2 ? F::RGBA16Float : F::RGBA8UNorm; formats.push_back(f); targets.push_back(take(d.createTexture({65, 37, f, true}))); pass.colors.push_back({targets.back(), L::Clear, {0, 0, 0, 1}}); }
    auto z = depth(d); pass.depth = {z}; auto p = take(d.createGraphicsPipeline(depthTest::pipeline({formats, F::D32Float}, {true, true, C::Less})));
    auto ro = take(d.createGraphicsPipeline(depthTest::pipeline({formats, F::D32Float}, {true, false, C::LessEqual}))); auto c = take(d.createCommandList()); take(c->begin()); auto b = constants(d, *c);
    for (auto& t : targets) take(c->transition(t, S::RenderTarget)); take(c->transition(z, S::DepthWrite)); begin(*c, pass); take(c->setPipeline(p));
    if (!reverse) draw(*c, b, 2); take(c->setScissor(0, 0, 32, 37)); draw(*c, b, 0); take(c->setScissor(32, 0, 33, 37)); draw(*c, b, 1);
    if (reverse) { take(c->setScissor(0, 0, 65, 37)); draw(*c, b, 2); } take(c->endRenderPass());
    std::vector<Readback> initial; for (auto& t : targets) initial.push_back(readback(d, *c, t)); auto z0 = readback(d, *c, z);
    for (auto& t : targets) take(c->transition(t, S::RenderTarget)); take(c->transition(z, S::DepthRead)); for (auto& color : pass.colors) color.load = L::Load; pass.depth = {z, L::Load, 1, true};
    begin(*c, pass); take(c->setPipeline(p)); take(c->bindResources({{}, {{0, 0, b, 0, 256}}})); reject(c->draw(3), gfx::ErrorCode::InvalidState, "Read-only depth rejects writing pipeline without GPU call");
    take(c->setPipeline(ro)); take(c->setScissor(0, 0, 16, 37)); draw(*c, b, 3); take(c->endRenderPass());
    std::vector<Readback> loaded; for (auto& t : targets) loaded.push_back(readback(d, *c, t)); auto z1 = readback(d, *c, z);
    pass = {}; targets.clear(); z.reset(); b.reset(); p.reset(); ro.reset(); submit(d, c);
    for (unsigned i = 0; i < count; ++i) for (bool load : {false, true}) verify(load ? loaded[i] : initial[i], [=](unsigned x, unsigned) { Pixel p = x < 32 ? Pixel{1, 0, 0, 1} : Pixel{0, 1, 0, 1}; if (load && x < 16) p = {0, 0, 1, 1}; if (i % 2) for (auto& a : p) a = 1 - a; return p; });
    for (auto r : {z0, z1}) verifyDepth(r, [](unsigned x, unsigned) { return x < 32 ? .25f : .5f; });
}
void depthOnly(gfx::Device& d, unsigned w, unsigned h, bool loaded) {
    auto p = take(d.createGraphicsPipeline(depthTest::pipeline({{}, F::D32Float}, {true, true, C::Less}))); auto z = depth(d, w, h); auto c = take(d.createCommandList()); take(c->begin()); auto b = constants(d, *c);
    take(c->transition(z, S::DepthWrite)); begin(*c, {{}, {z, L::Clear, 1}}); if (!loaded) { take(c->setPipeline(p)); draw(*c, b, 0); } take(c->endRenderPass());
    if (loaded) { begin(*c, {{}, {z, L::Load}}); take(c->setPipeline(p)); draw(*c, b, 0); take(c->endRenderPass()); }
    auto r = readback(d, *c, z); z.reset(); b.reset(); p.reset(); submit(d, c); verifyDepth(r, [](unsigned, unsigned) { return .25f; });
}
void contracts(gfx::Device& d, const gfx::AdapterInfo& adapter, bool validation) {
    check(d.caps().depthAttachment, "Adapter supports D32 depth attachment and transfers");
    auto p = take(d.createGraphicsPipeline(depthTest::pipeline({{}, F::D32Float}, {true, true, C::Less})));
    auto bad = depthTest::pipeline({{F::RGBA8UNorm}, F::D32Float}, {true, true, C::Less}); bad.fragment = {};
    reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::InvalidArgument, "Color outputs require fragment shader");
    bad = depthTest::pipeline({{}, F::D32Float}, {false, true}); reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::InvalidArgument, "Depth write needs depth test");
    bad = depthTest::pipeline({{F::RGBA8UNorm}, {}}, {true, false}); reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::InvalidArgument, "Depth state requires attachment");
    auto other = take(gfx::createDevice(desc(adapter, validation))); auto foreign = depth(*other); auto z = depth(d), uninitialized = depth(d);
    auto t = take(d.createTexture({65, 37, F::RGBA8UNorm, true})); auto c = take(d.createCommandList()); take(c->begin());
    reject(c->transition(t, S::DepthWrite), gfx::ErrorCode::InvalidArgument, "Color cannot enter depth state");
    reject(c->transition(z, S::ShaderRead), gfx::ErrorCode::Unsupported, "Depth sampling is outside supported slice");
    reject(c->transition(z, S::RenderTarget), gfx::ErrorCode::InvalidArgument, "Depth cannot enter color-target state");
    take(c->transition(t, S::RenderTarget)); take(c->transition(z, S::DepthWrite)); take(c->transition(uninitialized, S::DepthWrite));
    reject(c->beginRenderPass({{{t, L::Clear, {1, 0, 0, 1}}}, {uninitialized, L::Load}}), gfx::ErrorCode::InvalidState, "Late uninitialized depth rejects color clear atomically");
    for (auto value : {-1.f, 1.01f, std::numeric_limits<float>::quiet_NaN()}) reject(c->beginRenderPass({{}, {z, L::Clear, value}}), gfx::ErrorCode::InvalidArgument, "Invalid depth clear");
    reject(c->beginRenderPass({{}, {z, L::Clear, 1, true}}), gfx::ErrorCode::InvalidArgument, "Read-only pass cannot clear depth");
    reject(c->beginRenderPass({{{t}}, {foreign}}), gfx::ErrorCode::WrongDevice, "Foreign depth rejected after color validation");
    auto small = depth(d, 3, 7); reject(c->beginRenderPass({{{t}}, {small}}), gfx::ErrorCode::InvalidArgument, "Depth extent mismatch");
    begin(*c, {{{t, L::Clear, {0, 0, 1, 1}}}, {z, L::Clear, .75f}});
    reject(c->setPipeline(p), gfx::ErrorCode::InvalidArgument, "Depth-only pipeline cannot bind color+depth pass");
    reject(c->transition(z, S::CopySource), gfx::ErrorCode::InvalidState, "Depth transition prohibited inside pass");
    take(c->endRenderPass()); auto color = readback(d, *c, t), dep = readback(d, *c, z); take(c->end()); take(d.queue().wait(take(d.queue().submit(*c))));
    verify(color, [](unsigned, unsigned) { return Pixel{0, 0, 1, 1}; }); verifyDepth(dep, [](unsigned, unsigned) { return .75f; });
    take(c->begin());
    for (unsigned i = 0; i < 32; ++i) {
        auto setup = take(d.createCommandList()); take(setup->begin()); auto temp = take(d.createTexture({65, 37, F::RGBA8UNorm, true})); take(setup->transition(temp, S::RenderTarget)); begin(*setup, {{{temp}}, {}}); take(setup->endRenderPass()); submit(d, setup);
        auto before = take(d.resourceStatistics()); reject(c->beginRenderPass({{{temp}}, {foreign}}), gfx::ErrorCode::WrongDevice, "Rejected pass does not retain early attachment"); temp.reset(); check(take(d.resourceStatistics()).uniqueResources + 1 == before.uniqueResources, "Late depth rejection releases early color allocation");
    }
    // No rejected pass captures t: a different command can transition it before empty submit.
    auto change = take(d.createCommandList()); take(change->begin()); take(change->transition(t, S::RenderTarget)); submit(d, change); take(c->end()); take(d.queue().wait(take(d.queue().submit(*c))));
    take(c->begin());
    reject(c->beginRenderPass({{{t, L::Clear, {1, 0, 0, 1}}}, {uninitialized, L::Load}}), gfx::ErrorCode::InvalidState, "Late depth rejection must not clear initialized color");
    begin(*c, {{{t, L::Load}}, {}}); take(c->endRenderPass()); auto preserved = readback(d, *c, t);
    take(c->transition(z, S::DepthRead));
    reject(c->beginRenderPass({{}, {z, L::Load}}), gfx::ErrorCode::InvalidState, "Writable pass requires DepthWrite");
    begin(*c, {{}, {z, L::Load, 1, true}}); auto noDepth = take(d.createGraphicsPipeline(depthTest::pipeline({{F::RGBA8UNorm}, {}})));
    reject(c->setPipeline(noDepth), gfx::ErrorCode::InvalidArgument, "Color-only pipeline cannot bind depth-only pass");
    bool wrongThread{}; std::thread worker([&] { auto r = c->setPipeline(p); wrongThread = !r && r.error().code == gfx::ErrorCode::InvalidState; }); worker.join(); check(wrongThread, "Depth pipeline requires creating thread");
    take(c->endRenderPass()); take(c->transition(z, S::DepthWrite)); reject(c->beginRenderPass({{}, {z, L::Load, 1, true}}), gfx::ErrorCode::InvalidState, "Read-only pass requires DepthRead");
    take(c->end()); take(d.queue().wait(take(d.queue().submit(*c)))); verify(preserved, [](unsigned, unsigned) { return Pixel{0, 0, 1, 1}; });
    // An accepted depth use participates in inter-command version rejection.
    take(c->begin()); take(c->transition(z, S::DepthWrite)); take(c->end()); auto competing = take(d.createCommandList()); take(competing->begin()); take(competing->transition(z, S::CopySource)); submit(d, competing);
    reject(d.queue().submit(*c), gfx::ErrorCode::InvalidState, "Accepted depth recording is versioned");
    c.reset(); p.reset(); noDepth.reset(); z.reset(); uninitialized.reset(); t.reset(); small.reset(); color.buffer.reset(); dep.buffer.reset(); preserved.buffer.reset(); foreign.reset(); reclaimed(d);
    check(take(d.validationErrorCount()) == 0 && take(other->validationErrorCount()) == 0, "Depth rejection controls remain validation-clean");
}
void retirement(const gfx::AdapterInfo& adapter, bool validation) {
    auto d = take(gfx::createDevice(desc(adapter, validation))); auto z = depth(*d, 3, 7); auto p = take(d->createGraphicsPipeline(depthTest::pipeline({{}, F::D32Float}, {true, true, C::Less})));
    auto c = take(d->createCommandList()); take(c->begin()); auto b = constants(*d, *c); take(c->transition(z, S::DepthWrite)); begin(*c, {{}, {z}}); take(c->setPipeline(p)); draw(*c, b, 0); take(c->endRenderPass()); take(c->end());
    take(d->queue().submit(*c)); d.reset(); check(z->description().depthStencil && z->footprint().rowBytes == 12, "Retired D32 description remains available"); reject(c->begin(), gfx::ErrorCode::DeviceLost, "Retired depth recording is inert");
}
}
int main(int argc, char** argv) try {
    if (argc != 1 && !(argc == 2 && std::string_view(argv[1]) == "--validation")) throw std::runtime_error("Usage: gfx_vulkan_depth_tests [--validation]");
    bool validation = argc == 2; std::cout << "source=" << gfx::buildFingerprint() << " validation=" << validation << std::endl;
    auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan)); check(!adapters.empty(), "Real Vulkan adapter required");
    for (const auto& a : adapters) {
        std::cout << "adapter=" << a.name << " id=" << a.id.value << std::endl; auto d = take(gfx::createDevice(desc(a, validation))); contracts(*d, a, validation);
        for (auto [w, h] : {std::pair{1u, 1u}, {3u, 7u}, {65u, 37u}, {1920u, 1080u}, {3840u, 2160u}}) for (bool device : {false, true}) { transfer(*d, w, h, device); reclaimed(*d); depthOnly(*d, w, h, device); reclaimed(*d); }
        for (auto f : {F::RGBA8UNorm, F::RGBA16Float}) { for (unsigned op = 0; op < 8; ++op) for (bool writes : {false, true}) { comparisons(*d, f, static_cast<C>(op), writes); reclaimed(*d); } comparisons(*d, f, C::Never, false, false); reclaimed(*d); }
        for (auto count : {2u, 8u}) for (bool reverse : {false, true}) { mrt(*d, count, reverse); reclaimed(*d); }
        check(take(d->validationErrorCount()) == 0, "No depth core/synchronization validation errors"); retirement(a, validation);
    }
    std::cout << "PASS depth_assertions=" << assertions << " passes=" << passes << " draws=" << draws << " images=" << images << " depth_images=" << depthImages << " verified_texel_bytes=" << verifiedBytes << " padded_images=" << paddedImages << " adapters=" << adapters.size() << std::endl; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << std::endl; return 1; }
