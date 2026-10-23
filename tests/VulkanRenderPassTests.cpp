#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using F = gfx::Format; using S = gfx::ResourceState; using L = gfx::LoadOp;
unsigned assertions{}, images{}, passes{}, paddingImages{};
std::uint64_t verifiedBytes{};
void check(bool value, const char* message) { ++assertions; if (!value) throw std::runtime_error(message); }
template<class T> T take(gfx::Result<T> r) {
    if (!r) throw std::runtime_error(r.error().message + " native=" + std::to_string(r.error().nativeCode));
    return std::move(*r);
}
void take(gfx::Result<void> r) { if (!r) throw std::runtime_error(r.error().message); }
template<class T> void reject(gfx::Result<T> r, gfx::ErrorCode code, const char* message) { check(!r && r.error().code == code, message); }
gfx::DeviceDesc description(const gfx::AdapterInfo& a, bool validation) {
    gfx::DeviceDesc d; d.backend = gfx::Backend::Vulkan; d.adapter = a.id; d.softwareAdapter = a.softwareAdapter; d.validation = validation; return d;
}
void reclaimed(gfx::Device& d) {
    const auto s = take(d.resourceStatistics());
    check(!s.trackedObjects && !s.uniqueResources && !s.resourceAllocationBytes && !s.pendingSubmissions && !s.pendingObjectReferences,
        "All pass recording/image allocations reclaimed");
}
void pass(gfx::CommandList& c, const gfx::RenderPassDesc& p) { take(c.beginRenderPass(p)); take(c.endRenderPass()); ++passes; }
void submit(gfx::Device& d, gfx::CommandList& c) { take(c.end()); take(d.queue().wait(take(d.queue().submit(c)))); }
struct Readback { gfx::BufferPtr buffer; gfx::TextureFootprint f; F format; };
Readback readback(gfx::Device& d, gfx::CommandList& c, const gfx::TexturePtr& t) {
    const auto f = t->footprint(); auto b = take(d.createBuffer({f.size, gfx::Memory::Readback}));
    auto sentinel = take(d.createBuffer({f.size, gfx::Memory::Upload}));
    take(sentinel->write(0, std::vector<std::byte>(static_cast<std::size_t>(f.size), std::byte{0xa7})));
    take(c.copyBuffer(sentinel, b, f.size));
    take(c.transition(t, S::CopySource)); take(c.copyTextureToBuffer(t, b)); return {b, f, t->description().format};
}
float half(std::uint16_t bits) {
    const auto e = (bits >> 10) & 31, fraction = bits & 1023;
    return std::ldexp(e ? 1.f + fraction / 1024.f : fraction / 1024.f, e ? int(e) - 15 : -14) * (bits & 0x8000 ? -1.f : 1.f);
}
void verify(const Readback& r, gfx::Color expected) {
    const auto bytes = take(r.buffer->read(0, r.f.size)); const auto texel = r.format == F::RGBA8UNorm ? 4u : 8u;
    const std::array color{expected.r, expected.g, expected.b, expected.a}; bool pixels = true, padding = true;
    for (unsigned y = 0; y < r.f.height; ++y) {
        const auto base = std::size_t(y) * r.f.rowPitch;
        for (unsigned x = 0; x < r.f.rowBytes / texel; ++x) for (unsigned component = 0; component < 4; ++component) {
            float value;
            if (texel == 4) value = std::to_integer<unsigned char>(bytes[base + x * 4 + component]) / 255.f;
            else { std::uint16_t bits; std::memcpy(&bits, bytes.data() + base + x * 8 + component * 2, 2); value = half(bits); }
            pixels &= std::isfinite(value) && std::abs(value - color[component]) <= (texel == 4 ? 1.01f / 255 : .001f);
        }
        padding &= std::all_of(bytes.begin() + base + r.f.rowBytes, bytes.begin() + base + r.f.rowPitch, [](auto b) { return b == std::byte{0xa7}; });
    }
    check(pixels, "Every cleared/loaded texel agrees with CPU reference"); check(padding, "Full readback preserves row/final-row padding");
    ++images; if (r.f.rowPitch != r.f.rowBytes) ++paddingImages; verifiedBytes += std::uint64_t(r.f.rowBytes) * r.f.height;
}
void clearLoad(gfx::Device& d, F format, unsigned width, unsigned height) {
    auto t = take(d.createTexture({width, height, format, true})); auto c = take(d.createCommandList());
    const gfx::Color first{.25f, .5f, .75f, 1}, second = format == F::RGBA16Float ? gfx::Color{-2, .5f, 4, 1} : gfx::Color{1, 0, .5f, .25f};
    take(c->begin()); take(c->transition(t, S::RenderTarget));
    take(c->beginRenderPass(t, first)); take(c->endRenderPass()); ++passes;
    pass(*c, {{{t, L::Load}}, {}}); // Consecutive same-layout load/store.
    auto a = readback(d, *c, t); submit(d, *c); verify(a, first);
    take(c->begin()); take(c->transition(t, S::RenderTarget));
    pass(*c, {{{t, L::Load}}, {}}); // Initialization/layout committed by earlier submission.
    auto b = readback(d, *c, t); submit(d, *c); verify(b, first);
    take(c->begin()); take(c->transition(t, S::RenderTarget));
    pass(*c, {{{t, L::Clear, second}}, {}}); auto e = readback(d, *c, t); submit(d, *c); verify(e, second);
}
void mrt(gfx::Device& d, unsigned count) {
    std::vector<gfx::TexturePtr> targets; gfx::RenderPassDesc description;
    std::vector<gfx::Color> colors;
    for (unsigned n = 0; n < count; ++n) {
        targets.push_back(take(d.createTexture({65, 17, n % 2 ? F::RGBA16Float : F::RGBA8UNorm, true})));
        colors.push_back({float(n & 1), float((n >> 1) & 1), float((n >> 2) & 1), 1});
        description.colors.push_back({targets.back(), L::Clear, colors.back()});
    }
    auto c = take(d.createCommandList()); take(c->begin());
    for (auto& t : targets) take(c->transition(t, S::RenderTarget));
    pass(*c, description);
    for (auto& a : description.colors) a.load = L::Load;
    pass(*c, description); // Mixed formats and distinct attachment clears/stores.
    std::vector<Readback> reads; for (auto& t : targets) reads.push_back(readback(d, *c, t));
    take(c->end());
    std::weak_ptr<gfx::Texture> wrapper = targets.front(); targets.clear(); description = {};
    check(wrapper.expired(), "Framebuffer retention owns allocations rather than wrappers");
    auto fence = take(d.queue().submit(*c)); c.reset(); take(d.queue().wait(fence));
    for (unsigned n = 0; n < count; ++n) verify(reads[n], colors[n]);
}
void uploadedLoad(gfx::Device& d, F format) {
    auto t = take(d.createTexture({65, 17, format, true})); const auto f = t->footprint();
    auto up = take(d.createBuffer({f.size, gfx::Memory::Upload}));
    std::vector<std::byte> expected(static_cast<std::size_t>(f.size), std::byte{0});
    for (unsigned y = 0; y < f.height; ++y) for (unsigned x = 0; x < f.rowBytes; ++x) {
        if (format == F::RGBA8UNorm) expected[std::size_t(y) * f.rowPitch + x] = std::byte((x * 13 + y * 19) & 255);
        else if (!(x & 1)) { const std::uint16_t h = std::array<std::uint16_t, 4>{0, 0x3400, 0x3800, 0x3c00}[(x / 2 + y) % 4]; std::memcpy(expected.data() + std::size_t(y) * f.rowPitch + x, &h, 2); }
    }
    take(up->write(0, expected)); auto c = take(d.createCommandList()); take(c->begin());
    take(c->transition(t, S::CopyDestination)); take(c->copyBufferToTexture(up, t)); take(c->transition(t, S::RenderTarget));
    pass(*c, {{{t, L::Load}}, {}}); auto r = readback(d, *c, t); submit(d, *c);
    const auto bytes = take(r.buffer->read(0, f.size)); bool same = true;
    for (unsigned y = 0; y < f.height; ++y) {
        const auto base = std::size_t(y) * f.rowPitch;
        same &= std::equal(bytes.begin() + base, bytes.begin() + base + f.rowBytes, expected.begin() + base);
        same &= std::all_of(bytes.begin() + base + f.rowBytes, bytes.begin() + base + f.rowPitch, [](auto b) { return b == std::byte{0xa7}; });
    }
    check(same, "Load/store preserves all uploaded texels and readback sentinel padding");
    ++images; ++paddingImages; verifiedBytes += std::uint64_t(f.rowBytes) * f.height;
}
void contracts(gfx::Device& d, const gfx::AdapterInfo& a, bool validation) {
    check(d.caps().multipleRenderTargets && !d.caps().depthAttachment && !d.caps().resourceBinding, "Color-pass capability scope");
    auto other = take(gfx::createDevice(description(a, validation)));
    auto t = take(d.createTexture({3, 7, F::RGBA8UNorm, true})), fresh = take(d.createTexture({3, 7, F::RGBA16Float, true}));
    auto foreign = take(other->createTexture({3, 7, F::RGBA8UNorm, true}));
    auto plain = take(d.createTexture({3, 7})), mismatch = take(d.createTexture({4, 7, F::RGBA8UNorm, true}));
    auto up = take(d.createBuffer({t->footprint().size, gfx::Memory::Upload})); auto read = take(d.createBuffer({t->footprint().size, gfx::Memory::Readback}));
    auto c = take(d.createCommandList());
    reject(c->beginRenderPass(t, {}), gfx::ErrorCode::InvalidState, "Pass outside recording"); take(c->begin());
    reject(c->endRenderPass(), gfx::ErrorCode::InvalidState, "No active pass");
    reject(c->transition(plain, S::RenderTarget), gfx::ErrorCode::InvalidArgument, "RenderTarget requires attachment usage");
    reject(c->beginRenderPass(t, {}), gfx::ErrorCode::InvalidState, "Attachment needs explicit state");
    take(c->transition(t, S::RenderTarget)); take(c->transition(fresh, S::RenderTarget));
    reject(c->beginRenderPass({{{t, L::Load}}, {}}), gfx::ErrorCode::InvalidState, "Layout initialization does not initialize contents");
    gfx::RenderPassDesc good{{{t, L::Clear, {1, 0, 0, 1}}}, {}};
    for (unsigned n = 0; n < 8; ++n) {
        auto bad = good;
        switch (n) {
        case 0: bad.colors.clear(); break; case 1: bad.colors[0].texture.reset(); break;
        case 2: bad.colors.push_back(bad.colors[0]); break; case 3: bad.colors.push_back({mismatch}); break;
        case 4: bad.colors[0].load = static_cast<L>(99); break;
        case 5: bad.colors[0].clear.r = std::numeric_limits<float>::infinity(); break;
        case 6: bad.colors[0].texture = plain; break;
        case 7: bad.colors = std::vector<gfx::ColorAttachment>(9, good.colors[0]); break;
        }
        reject(c->beginRenderPass(bad), gfx::ErrorCode::InvalidArgument, "Malformed pass rejected atomically");
    }
    const auto before = take(d.resourceStatistics());
    for (unsigned n = 0; n < 32; ++n) {
        auto temp = take(d.createTexture({3, 7, F::RGBA8UNorm, true}));
        reject(c->beginRenderPass({{{temp}, {foreign}}, {}}), gfx::ErrorCode::InvalidState, "First untransitioned attachment rejected");
        reject(c->beginRenderPass({{{t}, {foreign}}, {}}), gfx::ErrorCode::WrongDevice, "Foreign later attachment rejected before capturing earlier one");
        temp.reset(); const auto after = take(d.resourceStatistics());
        check(after.trackedObjects == before.trackedObjects && after.uniqueResources == before.uniqueResources && after.resourceAllocationBytes == before.resourceAllocationBytes,
            "Repeated invalid pass retains no framebuffer/image allocation");
    }
    reject(c->beginRenderPass({{{t}, {fresh, L::Load}}, {}}), gfx::ErrorCode::InvalidState, "Later Load failure must not initialize earlier Clear");
    reject(c->beginRenderPass({{{t, L::Load}}, {}}), gfx::ErrorCode::InvalidState, "Rejected pass did not clear earlier attachment");
    take(c->beginRenderPass(good)); ++passes;
    reject(c->beginRenderPass(good), gfx::ErrorCode::InvalidState, "Nested pass");
    reject(c->end(), gfx::ErrorCode::InvalidState, "Cannot end recording with open pass");
    reject(d.queue().submit(*c), gfx::ErrorCode::InvalidState, "Cannot submit open pass");
    reject(c->transition(t, S::CopySource), gfx::ErrorCode::InvalidState, "No image barrier inside pass");
    reject(c->transition(up, S::CopySource), gfx::ErrorCode::InvalidState, "No buffer barrier inside pass");
    reject(c->copyBuffer(up, read, t->footprint().size), gfx::ErrorCode::InvalidState, "No buffer copy inside pass");
    reject(c->copyBufferToTexture(up, t), gfx::ErrorCode::InvalidState, "No image upload inside pass");
    reject(c->copyTextureToBuffer(t, read), gfx::ErrorCode::InvalidState, "No image readback inside pass");
    reject(c->draw(3), gfx::ErrorCode::Unsupported, "Draw awaits graphics pipelines");
    bool wrongThread{}; std::thread worker([&] { auto r = c->endRenderPass(); wrongThread = !r && r.error().code == gfx::ErrorCode::InvalidState; }); worker.join();
    check(wrongThread, "Pass operations require creating thread");
    take(c->endRenderPass()); reject(c->endRenderPass(), gfx::ErrorCode::InvalidState, "Double end pass");
    auto r = readback(d, *c, t); submit(d, *c); verify(r, {1, 0, 0, 1}); r.buffer.reset();
    take(c->begin()); take(c->transition(t, S::RenderTarget)); pass(*c, {{{t, L::Load}}, {}}); take(c->end());
    auto sibling = take(d.createCommandList()); take(sibling->begin()); take(sibling->transition(t, S::RenderTarget)); pass(*sibling, good); take(sibling->end());
    take(d.queue().wait(take(d.queue().submit(*c)))); reject(d.queue().submit(*sibling), gfx::ErrorCode::InvalidState, "Pass submission invalidates sibling versions");
    c.reset(); sibling.reset(); t.reset(); fresh.reset(); foreign.reset(); plain.reset(); mismatch.reset(); good = {}; up.reset(); read.reset();
    reclaimed(d); check(take(d.validationErrorCount()) == 0 && take(other->validationErrorCount()) == 0, "Contract operations are native-validation clean");
    // Unsubmitted pass destruction must release its framebuffer before attachment views.
    auto discarded = take(d.createCommandList()); auto held = take(d.createTexture({3, 7, F::RGBA8UNorm, true}));
    take(discarded->begin()); take(discarded->transition(held, S::RenderTarget)); pass(*discarded, {{{held}}, {}}); take(discarded->end());
    std::weak_ptr<gfx::Texture> weak = held; held.reset();
    check(weak.expired() && take(d.resourceStatistics()).uniqueResources == 1, "Unsubmitted framebuffer keeps native attachment");
    discarded.reset(); reclaimed(d);
}
void retirement(const gfx::AdapterInfo& a, bool validation) {
    auto d = take(gfx::createDevice(description(a, validation))); auto c = take(d->createCommandList());
    auto t = take(d->createTexture({3, 7, F::RGBA16Float, true})); take(c->begin()); take(c->transition(t, S::RenderTarget));
    pass(*c, {{{t}}, {}}); take(c->end()); take(d->queue().submit(*c));
    auto unsubmitted = take(d->createCommandList()); auto second = take(d->createTexture({3, 7, F::RGBA8UNorm, true}));
    take(unsubmitted->begin()); take(unsubmitted->transition(second, S::RenderTarget)); pass(*unsubmitted, {{{second}}, {}}); take(unsubmitted->end());
    d.reset(); check(t->description().renderTarget && second->footprint().rowBytes == 12, "Retired attachments preserve descriptions");
    reject(c->begin(), gfx::ErrorCode::DeviceLost, "Submitted pass retired"); reject(unsubmitted->begin(), gfx::ErrorCode::DeviceLost, "Unsubmitted pass retired");
}
}
int main(int argc, char** argv) try {
    if (argc != 1 && !(argc == 2 && std::string_view(argv[1]) == "--validation")) throw std::runtime_error("Usage: gfx_vulkan_render_tests [--validation]");
    const bool validation = argc == 2;
    std::cout << "source=" << gfx::buildFingerprint() << " validation=" << validation << std::endl;
    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan)); check(!adapters.empty(), "Real Vulkan adapter required");
    for (const auto& a : adapters) {
        std::cout << "adapter=" << a.name << " id=" << a.id.value << std::endl;
        auto d = take(gfx::createDevice(description(a, validation))); contracts(*d, a, validation);
        for (auto format : {F::RGBA8UNorm, F::RGBA16Float}) {
            for (auto [width, height] : {std::pair{1u, 1u}, {3u, 7u}, {65u, 17u}, {1920u, 1080u}, {3840u, 2160u}}) { clearLoad(*d, format, width, height); reclaimed(*d); }
            uploadedLoad(*d, format); reclaimed(*d);
        }
        for (unsigned count = 1; count <= 8; ++count) { mrt(*d, count); reclaimed(*d); }
        check(take(d->validationErrorCount()) == 0, "No normal SDK core/sync validation errors"); retirement(a, validation);
    }
    std::cout << "PASS assertions=" << assertions << " passes=" << passes << " images=" << images << " verified_texel_bytes=" << verifiedBytes
        << " padded_images=" << paddingImages << " adapters=" << adapters.size() << std::endl; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << std::endl; return 1; }
