#include "VulkanDepthSupport.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using F = gfx::Format; using S = gfx::ResourceState; using Pixel = std::array<float, 4>;
unsigned assertions{}, pairs{}, dropped{}, images{}, depthImages{}, paddedImages{}, draws{};
std::uint64_t verifiedBytes{}, bufferBytes{};
void check(bool value, const char* m) { ++assertions; if (!value) throw std::runtime_error(m); }
template<class T> T take(gfx::Result<T> r) { if (!r) throw std::runtime_error(r.error().message + " native=" + std::to_string(r.error().nativeCode)); return std::move(*r); }
void take(gfx::Result<void> r) { if (!r) throw std::runtime_error(r.error().message); }
template<class T> void reject(gfx::Result<T> r, gfx::ErrorCode code, const char* m) { check(!r && r.error().code == code, m); }
gfx::DeviceDesc desc(const gfx::AdapterInfo& a, bool validation) { gfx::DeviceDesc d; d.backend = gfx::Backend::Vulkan; d.adapter = a.id; d.softwareAdapter = a.softwareAdapter; d.validation = validation; return d; }
void reclaimed(gfx::Device& d) { const auto s = take(d.resourceStatistics()); check(!s.trackedObjects && !s.uniqueResources && !s.resourceAllocationBytes && !s.pendingSubmissions && !s.pendingObjectReferences, "All query/recording/resource references reclaimed"); }
double result(gfx::CommandList& c, bool positive = false) {
    auto ms = take(c.elapsedGpuMilliseconds()); check(std::isfinite(ms) && ms >= 0 && (!positive || ms > 0), "Actual GPU timestamps finite and correct workloads advance clock");
    check(take(c.elapsedGpuMilliseconds()) == ms, "Repeated results are stable");
    std::cout << "TIMESTAMP_RESULT pair=" << pairs << " ms=" << std::setprecision(17) << ms << " require_positive=" << positive << '\n';
    ++pairs; return ms;
}
void immediate(gfx::CommandList& c) {
    auto r = c.elapsedGpuMilliseconds(); check((r && std::isfinite(*r) && *r >= 0) || (!r && r.error().code == gfx::ErrorCode::InvalidState), "An immediate read either observes completion or reports not-ready");
    std::cout << "TIMESTAMP_IMMEDIATE ready=" << bool(r) << '\n';
}
void contracts(gfx::Device& d) {
    check(d.caps().timestampQueries, "Actual graphics queue supports timestamps"); auto c = take(d.createCommandList());
    reject(c->elapsedGpuMilliseconds(), gfx::ErrorCode::InvalidState, "Read before begin"); reject(c->writeTimestamp(0), gfx::ErrorCode::InvalidState, "Write before begin"); reject(c->resolveTimestamps(), gfx::ErrorCode::InvalidState, "Resolve before begin");
    take(c->begin()); auto s = take(d.resourceStatistics()); check(s.uniqueResources == 0 && s.resourceAllocationBytes == 0, "Query pool is not a fabricated resource allocation/readback buffer");
    reject(c->writeTimestamp(2), gfx::ErrorCode::InvalidArgument, "Slot range"); reject(c->writeTimestamp(UINT32_MAX), gfx::ErrorCode::InvalidArgument, "Large slot"); reject(c->resolveTimestamps(), gfx::ErrorCode::InvalidState, "Resolve requires both slots");
    take(c->writeTimestamp(0)); reject(c->writeTimestamp(0), gfx::ErrorCode::InvalidArgument, "Duplicate start"); reject(c->resolveTimestamps(), gfx::ErrorCode::InvalidState, "Partial query pair");
    reject(c->elapsedGpuMilliseconds(), gfx::ErrorCode::InvalidState, "Unsubmitted query not ready"); reject(c->begin(), gfx::ErrorCode::InvalidState, "Open recording cannot replace query pool");
    auto t = take(d.createTexture({3, 7, F::RGBA8UNorm, true})); take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {.25f, .5f, .75f, 1}));
    take(c->writeTimestamp(1)); reject(c->resolveTimestamps(), gfx::ErrorCode::InvalidState, "Resolve outside pass required"); take(c->endRenderPass()); take(c->resolveTimestamps());
    reject(c->resolveTimestamps(), gfx::ErrorCode::InvalidState, "Resolve once"); reject(c->writeTimestamp(1), gfx::ErrorCode::InvalidArgument, "Write after resolve");
    take(c->end()); reject(c->elapsedGpuMilliseconds(), gfx::ErrorCode::InvalidState, "Ended but unsubmitted is not ready");
    const auto hostStart = std::chrono::steady_clock::now(); auto fence = take(d.queue().submit(*c)); immediate(*c); take(d.queue().wait(fence)); auto ms = result(*c);
    const auto hostMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - hostStart).count(); check(ms <= hostMs + 1, "GPU interval fits enclosing host submission/wait interval");
    // Resetting begins a new pool/result generation, including after rejected/partial/reverse recordings.
    take(c->begin()); reject(c->elapsedGpuMilliseconds(), gfx::ErrorCode::InvalidState, "begin discards old result"); take(c->writeTimestamp(1)); take(c->end()); take(d.queue().wait(take(d.queue().submit(*c)))); reject(c->elapsedGpuMilliseconds(), gfx::ErrorCode::InvalidState, "Unresolved partial submission has no result");
    take(c->begin()); take(c->writeTimestamp(1)); take(c->writeTimestamp(0)); take(c->resolveTimestamps()); take(c->end()); take(d.queue().wait(take(d.queue().submit(*c)))); reject(c->elapsedGpuMilliseconds(), gfx::ErrorCode::InvalidState, "Reverse slot order is rejected even with equal/wrapped counters");
    take(c->begin()); take(c->writeTimestamp(0)); take(c->writeTimestamp(1)); take(c->resolveTimestamps()); take(c->end());
    take(c->begin()); take(c->end()); take(d.queue().wait(take(d.queue().submit(*c)))); reject(c->elapsedGpuMilliseconds(), gfx::ErrorCode::InvalidState, "Discarded unsubmitted pair cannot leak result into empty generation");
    for (unsigned i = 0; i < 16; ++i) {
        take(c->begin()); take(c->writeTimestamp(0)); reject(c->writeTimestamp(2), gfx::ErrorCode::InvalidArgument, "Rejected write preserves valid pair"); take(c->writeTimestamp(1)); take(c->resolveTimestamps()); take(c->end());
        auto f = take(d.queue().submit(*c)); immediate(*c); take(d.queue().wait(f)); result(*c);
    }
    bool writeThread{}, readThread{}; take(c->begin()); std::thread worker([&] {
        auto w = c->writeTimestamp(0); auto r = c->elapsedGpuMilliseconds(); writeThread = !w && w.error().code == gfx::ErrorCode::InvalidState; readThread = !r && r.error().code == gfx::ErrorCode::InvalidState;
    }); worker.join(); check(writeThread && readThread, "Timestamp native operations require creating thread"); take(c->end());
    c.reset(); t.reset(); reclaimed(d);
}
struct Work { std::unique_ptr<gfx::CommandList> commands; gfx::BufferPtr readback; std::vector<std::byte> expected; };
Work copyWork(gfx::Device& d, unsigned bytes, unsigned seed) {
    Work w; w.commands = take(d.createCommandList()); w.expected.resize(bytes); for (unsigned i = 0; i < bytes; ++i) w.expected[i] = std::byte((i * 73u + seed) % 251u);
    auto upload = take(d.createBuffer({bytes, gfx::Memory::Upload})), device = take(d.createBuffer({bytes})); w.readback = take(d.createBuffer({bytes, gfx::Memory::Readback})); take(upload->write(0, w.expected));
    auto& c = *w.commands; take(c.begin()); take(c.transition(device, S::CopyDestination)); take(c.writeTimestamp(0)); take(c.copyBuffer(upload, device, bytes)); take(c.transition(device, S::CopySource)); take(c.copyBuffer(device, w.readback, bytes)); take(c.writeTimestamp(1)); take(c.resolveTimestamps()); take(c.end()); return w;
}
void verifyBuffer(const Work& w) { check(take(w.readback->read(0, w.expected.size())) == w.expected, "Complete timed copy payload matches independent changing byte pattern"); bufferBytes += w.expected.size(); }
void copies(gfx::Device& d) {
    for (unsigned bytes : {4099u, 1048579u, 16777216u}) for (bool drop : {false, true}) {
        auto w = copyWork(d, bytes, bytes + unsigned(drop)); auto f = take(d.queue().submit(*w.commands));
        if (drop) { w.commands.reset(); ++dropped; } else immediate(*w.commands);
        auto s = take(d.resourceStatistics()); check(!s.pendingSubmissions || s.pendingObjectReferences >= s.pendingSubmissions * 2, "Pending query pool is included in retained object statistics");
        take(d.queue().wait(f)); if (!drop) result(*w.commands, true); verifyBuffer(w);
    }
    auto a = copyWork(d, 65539, 13), b = copyWork(d, 2097155, 37);
    auto fa = take(d.queue().submit(*a.commands)), fb = take(d.queue().submit(*b.commands)); take(d.queue().wait(fb)); take(d.queue().wait(fa));
    result(*a.commands, true); result(*b.commands, true); verifyBuffer(a); verifyBuffer(b);
}
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

void graphics(gfx::Device& d, unsigned width, unsigned height) {
    auto c = take(d.createCommandList()); take(c->begin()); auto p = take(d.createGraphicsPipeline(depthTest::pipeline({{F::RGBA8UNorm, F::RGBA16Float}, F::D32Float}, {true, true, gfx::CompareOp::Less})));
    auto a = take(d.createTexture({width, height, F::RGBA8UNorm, true})), b = take(d.createTexture({width, height, F::RGBA16Float, true})), z = take(d.createTexture({width, height, F::D32Float, false, true}));
    std::array<float, 64> data{}; data[0] = .25f; data[1] = .5f; data[2] = .75f; data[3] = 1; data[4] = .25f;
    auto constant = take(d.createBuffer({256, gfx::Memory::Upload})); take(constant->write(0, std::as_bytes(std::span(data)))); take(c->transition(constant, S::ConstantBuffer));
    take(c->transition(a, S::RenderTarget)); take(c->transition(b, S::RenderTarget)); take(c->transition(z, S::DepthWrite)); take(c->writeTimestamp(0));
    take(c->beginRenderPass({{{a}, {b}}, {z}})); take(c->setPipeline(p)); take(c->bindResources({{}, {{0, 0, constant, 0, 256}}})); take(c->draw(3)); ++draws; take(c->writeTimestamp(1)); take(c->endRenderPass()); take(c->resolveTimestamps());
    auto ar = readback(d, *c, a), br = readback(d, *c, b), zr = readback(d, *c, z); take(c->end()); auto f = take(d.queue().submit(*c)); a.reset(); b.reset(); z.reset(); p.reset(); constant.reset(); immediate(*c); take(d.queue().wait(f)); result(*c, width >= 1920); c.reset();
    verify(ar, [](unsigned, unsigned) { return Pixel{.25f, .5f, .75f, 1}; }); verify(br, [](unsigned, unsigned) { return Pixel{.75f, .5f, .25f, 0}; }); verifyDepth(zr, [](unsigned, unsigned) { return .25f; });
}
void retirement(const gfx::AdapterInfo& a, bool validation, bool cached) {
    auto d = take(gfx::createDevice(desc(a, validation))); auto c = take(d->createCommandList()); take(c->begin()); take(c->writeTimestamp(0)); take(c->writeTimestamp(1)); take(c->resolveTimestamps()); take(c->end()); auto f = take(d->queue().submit(*c));
    if (cached) { take(d->queue().wait(f)); result(*c); } d.reset(); reject(c->elapsedGpuMilliseconds(), gfx::ErrorCode::DeviceLost, "Retirement blocks pending and cached timestamp access"); reject(c->begin(), gfx::ErrorCode::DeviceLost, "Retired query recording inert");
}
}
int main(int argc, char** argv) try {
    if (argc != 1 && !(argc == 2 && std::string_view(argv[1]) == "--validation")) throw std::runtime_error("Usage: gfx_vulkan_timestamp_tests [--validation]");
    const bool validation = argc == 2; std::cout << "source=" << gfx::buildFingerprint() << " validation=" << validation << std::endl;
    auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan)); check(!adapters.empty(), "Real Vulkan adapter required");
    for (const auto& a : adapters) {
        std::cout << "adapter=" << a.name << " id=" << a.id.value << std::endl; auto d = take(gfx::createDevice(desc(a, validation))); contracts(*d); copies(*d); reclaimed(*d);
        for (auto [w, h] : {std::pair{65u, 37u}, {1920u, 1080u}, {3840u, 2160u}}) { graphics(*d, w, h); reclaimed(*d); }
        check(take(d->validationErrorCount()) == 0, "Timestamp core/synchronization errors zero"); retirement(a, validation, false); retirement(a, validation, true);
    }
    std::cout << "PASS timestamp_assertions=" << assertions << " measured_pairs=" << pairs << " dropped_query_recordings=" << dropped << " draws=" << draws << " images=" << images << " depth_images=" << depthImages << " verified_texel_bytes=" << verifiedBytes << " verified_buffer_bytes=" << bufferBytes << " padded_images=" << paddedImages << " adapters=" << adapters.size() << std::endl; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << std::endl; return 1; }
