#include "VulkanBindingSupport.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using F = gfx::Format; using S = gfx::ResourceState; using Pixel = std::array<float, 4>;
unsigned assertions{}, draws{}, images{}, paddedImages{}, snapshots{};
std::uint64_t verifiedBytes{};
void check(bool value, const char* m) { ++assertions; if (!value) throw std::runtime_error(m); }
template<class T> T take(gfx::Result<T> r) { if (!r) throw std::runtime_error(r.error().message + " native=" + std::to_string(r.error().nativeCode)); return std::move(*r); }
void take(gfx::Result<void> r) { if (!r) throw std::runtime_error(r.error().message); }
template<class T> void reject(gfx::Result<T> r, gfx::ErrorCode code, const char* m) { check(!r && r.error().code == code, m); }
gfx::DeviceDesc desc(const gfx::AdapterInfo& a, bool validation) { gfx::DeviceDesc d; d.backend = gfx::Backend::Vulkan; d.adapter = a.id; d.softwareAdapter = a.softwareAdapter; d.validation = validation; return d; }
void reclaimed(gfx::Device& d) { const auto s = take(d.resourceStatistics()); check(!s.trackedObjects && !s.uniqueResources && !s.resourceAllocationBytes && !s.pendingSubmissions && !s.pendingObjectReferences, "All resource/snapshot native references reclaimed"); }
void put(std::vector<std::byte>& b, std::size_t at, const Pixel& p) { std::memcpy(b.data() + at, p.data(), 16); }
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
    check(pixels, "Every sampled/bound pixel agrees with independent CPU reference"); check(padding, "Resource draw preserves readback row padding");
    ++images; if (r.f.rowPitch != r.f.rowBytes) ++paddedImages; verifiedBytes += std::uint64_t(r.f.rowBytes) * r.f.height;
}

Pixel pattern(int x, int y, unsigned variant) { return {float((x + variant) % 4) * .25f, float((y + variant) % 4) * .25f, float((x + y + variant) % 4) * .25f, 1}; }
gfx::TexturePtr texture(gfx::Device& d, gfx::CommandList& c, F format, unsigned variant) {
    auto t = take(d.createTexture({4, 4, format})); auto f = t->footprint(); std::vector<std::byte> data(static_cast<std::size_t>(f.size));
    const std::array<std::uint16_t, 5> halves{0, 0x3400, 0x3800, 0x3a00, 0x3c00};
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) { auto p = pattern(x, y, variant); for (unsigned k = 0; k < 4; ++k) {
        auto at = y * f.rowPitch + x * (format == F::RGBA8UNorm ? 4 : 8);
        if (format == F::RGBA8UNorm) data[at + k] = std::byte(static_cast<unsigned char>(std::round(p[k] * 255)));
        else std::memcpy(data.data() + at + k * 2, &halves[unsigned(p[k] * 4)], 2);
    } }
    auto u = take(d.createBuffer({f.size, gfx::Memory::Upload})); take(u->write(0, data)); take(c.transition(t, S::CopyDestination)); take(c.copyBufferToTexture(u, t)); take(c.transition(t, S::ShaderRead)); return t;
}
gfx::BufferPtr constant(gfx::Device& d, gfx::CommandList& c, Pixel value, bool device = false, bool large = false, Pixel bias = {}) {
    std::vector<std::byte> data(1024); put(data, 256, value); put(data, 256 + (large ? 256 : 16), bias);
    auto u = take(d.createBuffer({data.size(), gfx::Memory::Upload})); take(u->write(0, data));
    if (!device) { take(c.transition(u, S::ConstantBuffer)); return u; }
    auto b = take(d.createBuffer({data.size()})); take(c.transition(b, S::CopyDestination)); take(c.copyBuffer(u, b, data.size())); take(c.transition(b, S::ConstantBuffer)); return b;
}
Pixel sample(float u, float v, unsigned variant, F source, gfx::StaticSampler s) {
    auto texel = [&](int x, int y) {
        auto address = [&](int n) { return s.address == gfx::SamplerAddress::Clamp ? std::clamp(n, 0, 3) : ((n % 4) + 4) % 4; };
        auto p = pattern(address(x), address(y), variant);
        if (source == F::RGBA8UNorm) for (auto& a : p) a = std::round(a * 255) / 255;
        return p;
    };
    if (s.filter == gfx::SamplerFilter::Point) return texel(int(std::floor(u * 4)), int(std::floor(v * 4)));
    const float x = u * 4 - .5f, y = v * 4 - .5f, fx = x - std::floor(x), fy = y - std::floor(y);
    const int ix = int(std::floor(x)), iy = int(std::floor(y)); Pixel p{};
    const auto a = texel(ix, iy), b = texel(ix + 1, iy), c = texel(ix, iy + 1), e = texel(ix + 1, iy + 1);
    for (unsigned k = 0; k < 4; ++k) p[k] = (a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + e[k] * fx) * fy;
    return p;
}
gfx::ResourceBindings resources(const gfx::TexturePtr& a, const gfx::TexturePtr& b, const gfx::BufferPtr& w0, const gfx::BufferPtr& w1, const gfx::BufferPtr& tr, bool large = false) {
    return {{{0, 1, b}, {0, 0, a}}, {{1, 1, w1, 256, large ? 512u : 256u}, {2, 0, tr, 256, 256}, {1, 0, w0, 256, large ? 512u : 256u}}};
}
void imageCase(gfx::Device& d, F source, F target, bool linear, bool repeat, bool device, bool large, unsigned width = 68, unsigned height = 36) {
    auto definition = bindingTest::pipeline(true, large, target);
    auto& s0 = definition.bindingLayout.samplers[0]; auto& s1 = definition.bindingLayout.samplers[1];
    s0.filter = linear ? gfx::SamplerFilter::Linear : gfx::SamplerFilter::Point; s0.address = repeat ? gfx::SamplerAddress::Repeat : gfx::SamplerAddress::Clamp;
    s1.filter = linear ? gfx::SamplerFilter::Point : gfx::SamplerFilter::Linear; s1.address = repeat ? gfx::SamplerAddress::Clamp : gfx::SamplerAddress::Repeat;
    auto p = take(d.createGraphicsPipeline(definition)); auto c = take(d.createCommandList()); take(c->begin());
    auto a = texture(d, *c, source, 0), b = texture(d, *c, source, 1);
    auto w0 = constant(d, *c, {.25f, .25f, .25f, .25f}, device, large, {.0625f, 0, 0, 0});
    auto w1 = constant(d, *c, {.5f, .5f, .5f, .5f}, device, large, {0, .0625f, 0, .25f});
    auto tr = constant(d, *c, {1.125f, .875f, .0625f, -.125f}, device);
    auto t = take(d.createTexture({width, height, target, true})); take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {})); take(c->setPipeline(p));
    reject(c->draw(3), gfx::ErrorCode::InvalidState, "Bound pipeline requires resource snapshot");
    auto set = resources(a, b, w0, w1, tr, large); take(c->bindResources(set)); ++snapshots;
    take(c->setScissor(0, 0, width / 2, height)); take(c->draw(3)); ++draws;
    // New set reverses images. The first draw must retain its original descriptors.
    set.textures[0].texture = a; set.textures[1].texture = b; take(c->bindResources(set)); ++snapshots;
    auto bad = set; bad.constants.back().size = 1; reject(c->bindResources(bad), gfx::ErrorCode::InvalidArgument, "Rejected set preserves previous snapshot");
    take(c->setScissor(width / 2, 0, width - width / 2, height)); take(c->draw(3)); ++draws; take(c->endRenderPass());
    auto r = readback(d, *c, t); p.reset(); a.reset(); b.reset(); w0.reset(); w1.reset(); tr.reset(); t.reset(); set = {}; bad = {}; submit(d, c);
    verify(r, [=](unsigned x, unsigned y) { const float u = (float(x) + .5f) / width * 1.125f + .0625f, v = (1 - (float(y) + .5f) / height) * .875f - .125f;
        const bool right = x >= width / 2; auto a = sample(u, v, right ? 1 : 0, source, s0), b = sample(u, v, right ? 0 : 1, source, s1); Pixel p{};
        for (unsigned k = 0; k < 4; ++k) p[k] = .25f * a[k] + .5f * b[k] + (k == 0 || k == 1 ? .0625f : k == 3 ? .25f : 0); return p; });
}
void legacy(gfx::Device& d, F format, bool rendered = false) {
    auto def = bindingTest::pipeline(false, false, format); auto p = take(d.createGraphicsPipeline(def)); auto c = take(d.createCommandList()); take(c->begin()); auto a = texture(d, *c, format, 2);
    if (rendered) {
        a = take(d.createTexture({3, 7, format, true})); take(c->transition(a, S::RenderTarget)); take(c->beginRenderPass(a, {.25f, .5f, .75f, 1})); take(c->endRenderPass()); take(c->transition(a, S::ShaderRead));
    }
    auto t = take(d.createTexture({68, 36, format, true})); take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {})); take(c->setPipeline(p)); take(c->bindTexture(a)); ++snapshots; take(c->draw(3)); ++draws; take(c->endRenderPass());
    auto r = readback(d, *c, t); a.reset(); t.reset(); p.reset(); submit(d, c);
    verify(r, [=](unsigned x, unsigned y) { return rendered ? Pixel{.25f, .5f, .75f, 1} : sample((x + .5f) / 68, 1 - (y + .5f) / 36, 2, format, {}); });
}
gfx::BufferPtr constantForRejected(gfx::Device& d) {
    auto c = take(d.createCommandList()); take(c->begin()); auto b = constant(d, *c, {}); submit(d, c); return b;
}
void contracts(gfx::Device& d, const gfx::AdapterInfo& adapter, bool validation) {
    check(d.caps().resourceBinding && d.caps().bindingLayouts, "Binding capabilities are advertised");
    auto def = bindingTest::pipeline(true, true); auto p = take(d.createGraphicsPipeline(def)); auto badDef = def;
    badDef.bindingLayout.entries[0].count = 1; reject(d.createGraphicsPipeline(badDef), gfx::ErrorCode::InvalidArgument, "Shader array count mismatch");
    badDef = def; badDef.bindingLayout.entries[2].visibility = gfx::ShaderVisibility::Fragment; reject(d.createGraphicsPipeline(badDef), gfx::ErrorCode::InvalidArgument, "Shader visibility mismatch");
    badDef = def; badDef.sampledTexture = true; reject(d.createGraphicsPipeline(badDef), gfx::ErrorCode::InvalidArgument, "Legacy/layout conflict");
    auto other = take(gfx::createDevice(desc(adapter, validation))); auto foreign = take(other->createBuffer({1024, gfx::Memory::Upload})); auto foreignTexture = take(other->createTexture({4, 4}));
    auto c = take(d.createCommandList()); reject(c->bindResources({}), gfx::ErrorCode::InvalidState, "Binding before recording"); take(c->begin()); reject(c->bindResources({}), gfx::ErrorCode::InvalidState, "Binding requires pipeline");
    auto a = texture(d, *c, F::RGBA8UNorm, 0), b = texture(d, *c, F::RGBA16Float, 1);
    auto w0 = constant(d, *c, {.25f, .25f, .25f, .25f}, false, true), w1 = constant(d, *c, {.5f, .5f, .5f, .5f}, false, true), tr = constant(d, *c, {1, 1, 0, 0});
    auto unread = take(d.createBuffer({1024, gfx::Memory::Readback})); auto uninitialized = take(d.createTexture({4, 4})); take(c->transition(uninitialized, S::ShaderRead));
    auto t = take(d.createTexture({68, 36, F::RGBA8UNorm, true})); take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {})); take(c->setPipeline(p));
    auto good = resources(a, b, w0, w1, tr, true); take(c->bindResources(good)); ++snapshots;
    reject(c->bindTexture(a), gfx::ErrorCode::InvalidState, "Legacy helper refuses explicit layout");
    for (unsigned n = 0; n < 32; ++n) {
        auto temp = take(d.createBuffer({1024, gfx::Memory::Upload})); auto rejected = good; rejected.constants[0].buffer = temp; rejected.constants[0].offset = 1;
        const auto before = take(d.resourceStatistics()); reject(c->bindResources(rejected), gfx::ErrorCode::InvalidArgument, "Invalid view does not retain"); rejected = {}; take(temp->write(0, {})); temp.reset();
        check(take(d.resourceStatistics()).uniqueResources + 1 == before.uniqueResources, "Rejected view releases allocation");
    }
    for (unsigned variant = 0; variant < 13; ++variant) {
        auto bad = good; auto code = gfx::ErrorCode::InvalidArgument;
        switch (variant) {
        case 0: bad.textures.pop_back(); break;
        case 1: bad.textures[0].element = 0; break;
        case 2: bad.textures[0].binding = 3; break;
        case 3: bad.textures[0].texture = {}; break;
        case 4: bad.textures[0].texture = foreignTexture; code = gfx::ErrorCode::WrongDevice; break;
        case 5: bad.textures[0].texture = t; code = gfx::ErrorCode::InvalidState; break;
        case 6: bad.textures[0].texture = uninitialized; code = gfx::ErrorCode::InvalidState; break;
        case 7: bad.constants[0].buffer = foreign; code = gfx::ErrorCode::WrongDevice; break;
        case 8: bad.constants[0].buffer = unread; break;
        case 9: bad.constants[0].size = 256; break;
        case 10: bad.constants[0].offset = UINT64_MAX; break;
        case 11: bad.constants[0].size = 65536 + 256; break;
        case 12: bad.constants[0].buffer = {}; break;
        }
        reject(c->bindResources(bad), code, "Malformed resource snapshot rejected");
    }
    bool wrongThread{}; std::thread worker([&] { auto r = c->bindResources(good); wrongThread = !r && r.error().code == gfx::ErrorCode::InvalidState; }); worker.join(); check(wrongThread, "Bindings require creating thread");
    for (unsigned n = 1; n < 51; ++n) { take(c->bindResources(good)); ++snapshots; }
    reject(c->bindResources(good), gfx::ErrorCode::InvalidState, "Descriptor budget stops before exceeding 256"); take(c->draw(3)); ++draws;
    take(c->setPipeline(p)); reject(c->draw(3), gfx::ErrorCode::InvalidState, "Pipeline selection resets resource snapshot"); take(c->endRenderPass()); take(c->end()); take(d.queue().wait(take(d.queue().submit(*c))));
    // Rejected late constants must not capture a previously valid Upload argument.
    take(c->begin()); take(c->beginRenderPass({{{t, gfx::LoadOp::Load}}, {}})); take(c->setPipeline(p));
    auto temp = constantForRejected(d); auto rejected = good; rejected.constants[0].buffer = temp; rejected.constants.back().size = 1;
    reject(c->bindResources(rejected), gfx::ErrorCode::InvalidArgument, "Late rejection leaves no observed version"); take(temp->write(0, {})); take(c->endRenderPass()); take(c->end()); take(d.queue().wait(take(d.queue().submit(*c)))); rejected = {}; temp.reset();
    take(c->begin()); take(c->beginRenderPass({{{t, gfx::LoadOp::Load}}, {}})); take(c->setPipeline(p)); take(c->bindResources(good)); ++snapshots; take(c->endRenderPass()); take(c->end()); take(w0->write(0, {})); reject(d.queue().submit(*c), gfx::ErrorCode::InvalidState, "Accepted constant snapshots its upload version");
    c.reset(); good = {}; a.reset(); b.reset(); w0.reset(); w1.reset(); tr.reset(); t.reset(); p.reset(); unread.reset(); uninitialized.reset(); foreign.reset(); foreignTexture.reset(); reclaimed(d);
    check(take(d.validationErrorCount()) == 0 && take(other->validationErrorCount()) == 0, "Resource rejection controls are native-validation clean");
}
void retirement(const gfx::AdapterInfo& adapter, bool validation) {
    auto d = take(gfx::createDevice(desc(adapter, validation))); auto c = take(d->createCommandList()); take(c->begin()); auto a = texture(*d, *c, F::RGBA8UNorm, 0);
    auto p = take(d->createGraphicsPipeline(bindingTest::pipeline())); auto t = take(d->createTexture({3, 7, F::RGBA8UNorm, true}));
    take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {})); take(c->setPipeline(p)); take(c->bindTexture(a)); ++snapshots; take(c->draw(3)); ++draws; take(c->endRenderPass()); take(c->end());
    take(d->queue().submit(*c)); d.reset(); reject(c->begin(), gfx::ErrorCode::DeviceLost, "Retired descriptor recording is inert"); check(a->description().width == 4, "Retired sampled wrapper retains description");
}
}
int main(int argc, char** argv) try {
    if (argc != 1 && !(argc == 2 && std::string_view(argv[1]) == "--validation")) throw std::runtime_error("Usage: gfx_vulkan_binding_tests [--validation]");
    const bool validation = argc == 2; std::cout << "source=" << gfx::buildFingerprint() << " validation=" << validation << std::endl;
    auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan)); check(!adapters.empty(), "Real Vulkan adapter required");
    for (const auto& a : adapters) {
        std::cout << "adapter=" << a.name << " id=" << a.id.value << std::endl; auto d = take(gfx::createDevice(desc(a, validation))); contracts(*d, a, validation);
        for (auto source : {F::RGBA8UNorm, F::RGBA16Float}) for (auto target : {F::RGBA8UNorm, F::RGBA16Float})
            for (bool linear : {false, true}) for (bool repeat : {false, true}) for (bool device : {false, true}) { imageCase(*d, source, target, linear, repeat, device, device); reclaimed(*d); }
        for (auto format : {F::RGBA8UNorm, F::RGBA16Float}) { legacy(*d, format); reclaimed(*d); legacy(*d, format, true); reclaimed(*d); imageCase(*d, format, format, true, false, true, true, 1920, 1080); reclaimed(*d); imageCase(*d, format, format, false, true, false, false, 3840, 2160); reclaimed(*d); }
        check(take(d->validationErrorCount()) == 0, "No resource core/synchronization validation errors");
        retirement(a, validation);
    }
    std::cout << "PASS binding_assertions=" << assertions << " draws=" << draws << " snapshots=" << snapshots << " images=" << images << " verified_texel_bytes=" << verifiedBytes << " padded_images=" << paddedImages << " adapters=" << adapters.size() << std::endl; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << std::endl; return 1; }
