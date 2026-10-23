#include "VulkanVertexSupport.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using F = gfx::Format; using S = gfx::ResourceState; using Pixel = std::array<float, 4>;
unsigned assertions{}, draws{}, indexedDraws{}, images{}, paddedImages{};
std::uint64_t verifiedBytes{};
void check(bool value, const char* m) { ++assertions; if (!value) throw std::runtime_error(m); }
template<class T> T take(gfx::Result<T> r) { if (!r) throw std::runtime_error(r.error().message + " native=" + std::to_string(r.error().nativeCode)); return std::move(*r); }
void take(gfx::Result<void> r) { if (!r) throw std::runtime_error(r.error().message); }
template<class T> void reject(gfx::Result<T> r, gfx::ErrorCode c, const char* m) { check(!r && r.error().code == c, m); }
gfx::DeviceDesc desc(const gfx::AdapterInfo& a, bool validation) { gfx::DeviceDesc d; d.backend = gfx::Backend::Vulkan; d.adapter = a.id; d.softwareAdapter = a.softwareAdapter; d.validation = validation; return d; }
void reclaimed(gfx::Device& d) { const auto s = take(d.resourceStatistics()); check(!s.trackedObjects && !s.uniqueResources && !s.resourceAllocationBytes && !s.pendingSubmissions && !s.pendingObjectReferences, "All native input/image/recording references reclaimed"); }
void put(std::vector<std::byte>& b, std::size_t offset, float value) { std::memcpy(b.data() + offset, &value, 4); }
std::vector<std::byte> vertices(unsigned n, bool normalized, bool quad = false, bool gradient = false) {
    const auto l = vertexTest::layout(n, normalized); const auto stride = l.buffers[0].stride; const unsigned fields = n == 1 ? 2 : n, count = quad ? 5 : 4;
    std::vector<std::byte> b(16 + count * stride + 16, std::byte{0xe7});
    const std::array<std::array<float, 2>, 4> xy = quad ? std::array<std::array<float, 2>, 4>{{{-1, -1}, {-1, 1}, {1, -1}, {1, 1}}} : std::array<std::array<float, 2>, 4>{{{-.75f, -.75f}, {-.75f, .75f}, {0, -.75f}, {0, 0}}};
    for (unsigned i = 1; i < count; ++i) {
        const auto at = 16 + i * stride; put(b, at, xy[i - 1][0]); put(b, at + 4, xy[i - 1][1]);
        if (n >= 3) put(b, at + 8, .5f); if (n == 4) put(b, at + 12, 1);
        const Pixel color = gradient ? Pixel{float(i == 1), float(i == 2), float(i == 3), 1} : Pixel{1, 0, 0, 1};
        for (unsigned c = 0; c < 4; ++c) {
            if (normalized) b[at + fields * 4 + c] = std::byte(static_cast<unsigned char>(color[c] * 255));
            else put(b, at + fields * 4 + c * 4, color[c]);
        }
    }
    return b;
}
gfx::BufferPtr buffer(gfx::Device& d, gfx::CommandList& c, const std::vector<std::byte>& bytes, bool device, S state) {
    auto upload = take(d.createBuffer({bytes.size(), gfx::Memory::Upload})); take(upload->write(0, bytes));
    if (!device) { take(c.transition(upload, state)); return upload; }
    auto result = take(d.createBuffer({bytes.size()})); take(c.transition(result, S::CopyDestination)); take(c.copyBuffer(upload, result, bytes.size())); take(c.transition(result, state)); return result;
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
                pixels &= std::isfinite(value) && std::abs(value - p[c]) <= (texel == 4 ? 1.01f / 255 : .002f);
            }
        }
        padding &= std::all_of(bytes.begin() + at + r.f.rowBytes, bytes.begin() + at + r.f.rowPitch, [](auto b) { return b == std::byte{0xb3}; });
    }
    check(pixels, "Every vertex/index/interpolated pixel agrees with independent CPU reference"); check(padding, "Input draw preserves readback row padding");
    ++images; if (r.f.rowPitch != r.f.rowBytes) ++paddedImages; verifiedBytes += std::uint64_t(r.f.rowBytes) * r.f.height;
}
Pixel reference(unsigned x, unsigned y, unsigned width, unsigned height, bool gradient = false, float shift = 0, Pixel color = {1, 0, 0, 1}) {
    const double nx = (2.0 * x + 1) / width - 1 - shift, ny = 1 - (2.0 * y + 1) / height;
    const double u = (nx + .75) / .75, v = (ny + .75) / 1.5;
    if (u < 0 || v < 0 || u + v > 1) return {0, 0, 1, 1};
    return gradient ? Pixel{float(1 - u - v), float(v), float(u), 1} : color;
}
void nonindexed(gfx::Device& d, F format, unsigned n, bool normalized, bool device) {
    auto p = take(d.createGraphicsPipeline(vertexTest::pipeline(n, normalized, format))); auto t = take(d.createTexture({65, 37, format, true})); auto c = take(d.createCommandList()); take(c->begin());
    auto v = buffer(d, *c, vertices(n, normalized, false, true), device, S::VertexBuffer); const auto l = vertexTest::layout(n, normalized);
    const auto extent = l.attributes.back().offset + (normalized ? 4 : 16), bytes = 3 * l.buffers[0].stride + extent;
    take(c->bindVertexBuffer(2, v, l.buffers[0].stride, 16, bytes)); take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {0, 0, 1, 1})); take(c->setPipeline(p));
    take(c->draw(3, 1, 1)); ++draws; take(c->endRenderPass()); auto r = readback(d, *c, t);
    std::weak_ptr<gfx::Buffer> weak = v; v.reset(); p.reset(); t.reset(); check(weak.expired(), "Native input retention does not retain buffer wrappers"); submit(d, c);
    verify(r, [](unsigned x, unsigned y) { return reference(x, y, 65, 37, true); });
}
void indexed(gfx::Device& d, F format, gfx::IndexFormat indexFormat, int base, bool device, bool combined = false, bool large = false) {
    auto p = take(d.createGraphicsPipeline(vertexTest::pipeline(4, true, format))); const unsigned width = large ? (indexFormat == gfx::IndexFormat::UInt16 ? 1920 : 3840) : 65, height = large ? (indexFormat == gfx::IndexFormat::UInt16 ? 1080 : 2160) : 37;
    auto t = take(d.createTexture({width, height, format, true})); auto c = take(d.createCommandList()); take(c->begin());
    auto data = vertices(4, true, large); const auto l = vertexTest::layout(4, true); const auto stride = l.buffers[0].stride, element = indexFormat == gfx::IndexFormat::UInt16 ? 2u : 4u;
    const std::vector<unsigned> indices = large ? std::vector<unsigned>{99, 1, 2, 3, 3, 2, 4} : std::vector<unsigned>{99, unsigned(1 - base), unsigned(2 - base), unsigned(3 - base)};
    std::vector<std::byte> indexBytes(4 + indices.size() * element + 4, std::byte{0xd4});
    for (unsigned i = 0; i < indices.size(); ++i) { if (element == 2) { const auto value = static_cast<std::uint16_t>(indices[i]); std::memcpy(indexBytes.data() + 4 + i * element, &value, element); } else std::memcpy(indexBytes.data() + 4 + i * element, &indices[i], element); }
    gfx::BufferPtr v, index; std::uint64_t indexOffset = 4;
    if (combined) { indexOffset = (data.size() + 3) & ~std::uint64_t(3); data.resize(indexOffset); data.insert(data.end(), indexBytes.begin(), indexBytes.end()); indexOffset += 4; v = index = buffer(d, *c, data, device, S::VertexAndIndexBuffer); }
    else { v = buffer(d, *c, data, device, S::VertexBuffer); index = buffer(d, *c, indexBytes, device, S::IndexBuffer); }
    const auto count = large ? 5u : 4u; take(c->bindVertexBuffer(2, v, stride, 16, (count - 1) * stride + 20));
    take(c->bindIndexBuffer(index, indexFormat, indexOffset, indices.size() * element));
    take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {0, 0, 1, 1})); take(c->setPipeline(p));
    take(c->drawIndexed(large ? 6 : 3, 1, 1, large ? 0 : base)); ++draws; ++indexedDraws; take(c->endRenderPass()); auto r = readback(d, *c, t); v.reset(); index.reset(); p.reset(); t.reset(); submit(d, c);
    verify(r, [=](unsigned x, unsigned y) { return large ? Pixel{1, 0, 0, 1} : reference(x, y, width, height); });
}
void instance(gfx::Device& d, F format, bool device, bool indexed) {
    auto desc = vertexTest::pipeline(2, false, format); desc.vertex = vertexTest::binary(vertexFixtures::instance2, gfx::ShaderStage::Vertex);
    desc.vertexLayout = {{{2, 12}, {5, 24, gfx::VertexInputRate::Instance}}, {{"POSITION", 0, gfx::VertexFormat::Float2, 2, 0}, {"OFFSET", 0, gfx::VertexFormat::Float2, 5, 0}, {"COLOR", 0, gfx::VertexFormat::Float4, 5, 8}}};
    auto p = take(d.createGraphicsPipeline(desc)); auto t = take(d.createTexture({65, 37, format, true})); auto c = take(d.createCommandList()); take(c->begin());
    std::vector<std::byte> position(4 * 12), inst(3 * 24); const std::array xy{-.75f, -.75f, -.75f, .75f, 0.f, -.75f};
    for (unsigned i = 0; i < 3; ++i) { put(position, (i + 1) * 12, xy[i * 2]); put(position, (i + 1) * 12 + 4, xy[i * 2 + 1]); }
    for (unsigned i = 0; i < 3; ++i) { put(inst, i * 24, i == 2 ? 1.f : 0.f); put(inst, i * 24 + 8, i == 1 ? 1.f : 0.f); put(inst, i * 24 + 12, i == 2 ? 1.f : 0.f); put(inst, i * 24 + 20, 1); }
    auto v = buffer(d, *c, position, device, S::VertexBuffer), instances = buffer(d, *c, inst, device, S::VertexBuffer);
    take(c->bindVertexBuffer(2, v, 12)); take(c->bindVertexBuffer(5, instances, 24, 0, 2 * 24));
    gfx::BufferPtr index; if (indexed) { const std::array<std::uint16_t, 3> values{1, 2, 3}; const auto bytes = std::as_bytes(std::span(values)); index = buffer(d, *c, {bytes.begin(), bytes.end()}, device, S::IndexBuffer); take(c->bindIndexBuffer(index, gfx::IndexFormat::UInt16)); }
    take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {0, 0, 1, 1})); take(c->setPipeline(p));
    if (indexed) reject(c->drawIndexed(3, 2, 0, 0, 1), gfx::ErrorCode::InvalidArgument, "Indexed instance view is bounded");
    else reject(c->draw(3, 2, 1, 1), gfx::ErrorCode::InvalidArgument, "Instance view is bounded");
    take(c->bindVertexBuffer(5, instances, 24));
    if (indexed) { take(c->drawIndexed(3, 2, 0, 0, 1)); ++indexedDraws; } else take(c->draw(3, 2, 1, 1)); ++draws;
    take(c->endRenderPass()); auto r = readback(d, *c, t); v.reset(); instances.reset(); index.reset(); p.reset(); t.reset(); submit(d, c);
    verify(r, [](unsigned x, unsigned y) { auto left = reference(x, y, 65, 37); return left[0] ? left : reference(x, y, 65, 37, false, 1, {0, 1, 0, 1}); });
}
void contracts(gfx::Device& d, const gfx::AdapterInfo& a, bool validation) {
    check(d.caps().vertexInput && d.caps().indexedDrawing && d.caps().resourceBinding, "Input capabilities are explicit");
    auto description = vertexTest::pipeline(2, true, F::RGBA8UNorm); auto bad = description;
    bad.vertexLayout.attributes[0].format = gfx::VertexFormat::Float3; reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::InvalidArgument, "Vertex layout/type mismatch");
    bad = description; bad.fragment = vertexTest::binary(vertexFixtures::mismatch3, gfx::ShaderStage::Fragment); reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::InvalidArgument, "Varying type mismatch rejected before driver");
    bad = description; bad.vertexLayout.attributes[0].offset = 1; reject(d.createGraphicsPipeline(bad), gfx::ErrorCode::InvalidArgument, "Malformed alignment rejected before driver");
    auto other = take(gfx::createDevice(desc(a, validation))); auto foreign = take(other->createBuffer({256, gfx::Memory::Upload}));
    auto v = take(d.createBuffer({256, gfx::Memory::Upload})); take(v->write(0, vertices(2, true))); const auto stride = description.vertexLayout.buffers[0].stride;
    auto index = take(d.createBuffer({8, gfx::Memory::Upload})); const std::array<std::uint16_t, 4> values{99, 1, 2, 3}; take(index->write(0, std::as_bytes(std::span(values))));
    auto read = take(d.createBuffer({256, gfx::Memory::Readback})); auto c = take(d.createCommandList());
    reject(c->bindVertexBuffer(2, v, stride), gfx::ErrorCode::InvalidState, "Bind before begin"); take(c->begin());
    reject(c->bindVertexBuffer(2, v, stride), gfx::ErrorCode::InvalidState, "Bind requires vertex state");
    for (unsigned n = 0; n < 32; ++n) {
        auto temp = take(d.createBuffer({256, gfx::Memory::Upload})); const auto before = take(d.resourceStatistics());
        reject(c->bindVertexBuffer(2, temp, 0), gfx::ErrorCode::InvalidArgument, "Rejected zero stride does not retain");
        reject(c->bindIndexBuffer(temp, static_cast<gfx::IndexFormat>(99)), gfx::ErrorCode::InvalidArgument, "Rejected index format does not retain");
        take(temp->write(0, {})); temp.reset(); const auto after = take(d.resourceStatistics()); check(after.uniqueResources + 1 == before.uniqueResources, "Rejected bindings reclaim native allocations");
    }
    take(v->write(0, {})); take(c->end()); take(d.queue().wait(take(d.queue().submit(*c))));
    take(c->begin()); take(c->transition(v, S::VertexBuffer)); take(c->bindVertexBuffer(2, v, stride, 16)); take(c->end());
    take(v->write(0, {})); reject(d.queue().submit(*c), gfx::ErrorCode::InvalidState, "Accepted vertex binding snapshots upload version");
    take(c->begin()); take(c->transition(v, S::VertexBuffer)); take(c->transition(index, S::IndexBuffer)); take(c->bindVertexBuffer(2, v, stride, 16)); take(c->bindIndexBuffer(index, gfx::IndexFormat::UInt16));
    reject(c->bindVertexBuffer(32, v, stride), gfx::ErrorCode::InvalidArgument, "Portable slot bound"); reject(c->bindVertexBuffer(2, foreign, stride), gfx::ErrorCode::WrongDevice, "Foreign vertex buffer");
    reject(c->bindVertexBuffer(2, {}, stride), gfx::ErrorCode::InvalidArgument, "Null vertex buffer"); reject(c->bindVertexBuffer(2, read, stride), gfx::ErrorCode::InvalidArgument, "Readback cannot be vertex input");
    reject(c->bindVertexBuffer(2, v, stride, UINT64_MAX), gfx::ErrorCode::InvalidArgument, "Overflowing vertex offset"); reject(c->bindVertexBuffer(2, v, stride, 256), gfx::ErrorCode::InvalidArgument, "Empty vertex view");
    reject(c->bindIndexBuffer(index, gfx::IndexFormat::UInt16, 1), gfx::ErrorCode::InvalidArgument, "Index offset alignment"); reject(c->bindIndexBuffer(index, gfx::IndexFormat::UInt16, 0, 7), gfx::ErrorCode::InvalidArgument, "Index size alignment");
    reject(c->bindIndexBuffer(foreign, gfx::IndexFormat::UInt16), gfx::ErrorCode::WrongDevice, "Foreign index buffer"); reject(c->bindIndexBuffer(read, gfx::IndexFormat::UInt16), gfx::ErrorCode::InvalidArgument, "Readback cannot be index input");
    auto t = take(d.createTexture({65, 37, F::RGBA8UNorm, true})); auto p = take(d.createGraphicsPipeline(description)); take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {0, 0, 1, 1})); take(c->setPipeline(p));
    bool wrongThread{}; std::thread worker([&] { auto r = c->bindVertexBuffer(2, v, stride, 16); wrongThread = !r && r.error().code == gfx::ErrorCode::InvalidState; }); worker.join(); check(wrongThread, "Input binding requires creating thread");
    take(c->drawIndexed(3, 1, 1)); ++draws; ++indexedDraws;
    take(c->bindVertexBuffer(2, v, stride + 4, 16)); reject(c->draw(3, 1, 1), gfx::ErrorCode::InvalidState, "Stride mismatch");
    take(c->bindVertexBuffer(2, v, stride, 17)); reject(c->draw(3, 1, 1), gfx::ErrorCode::InvalidArgument, "View alignment checked at draw");
    take(c->bindVertexBuffer(2, v, stride, 16, stride)); reject(c->draw(3, 1, 1), gfx::ErrorCode::InvalidArgument, "Vertex view bound");
    take(c->bindVertexBuffer(2, v, stride, 16)); reject(c->drawIndexed(4, 1, 1), gfx::ErrorCode::InvalidArgument, "Index range bound");
    reject(c->drawIndexed(3, 1, 1, 999), gfx::ErrorCode::InvalidArgument, "Positive base vertex bound");
    take(c->drawIndexed(3, 1, 1)); ++draws; ++indexedDraws; take(c->endRenderPass());
    // Bound views survive passes, but a shadow transition invalidates their read state.
    take(c->transition(v, S::CopySource)); take(c->beginRenderPass({{{t, gfx::LoadOp::Load}}, {}})); take(c->setPipeline(p)); reject(c->draw(3, 1, 1), gfx::ErrorCode::InvalidState, "Bound vertex state changed"); take(c->endRenderPass());
    take(c->transition(v, S::VertexBuffer)); auto r = readback(d, *c, t); v.reset(); index.reset(); read.reset(); foreign.reset(); p.reset(); t.reset(); submit(d, c); verify(r, [](unsigned x, unsigned y) { return reference(x, y, 65, 37); }); r.buffer.reset(); reclaimed(d);
    check(take(d.validationErrorCount()) == 0 && take(other->validationErrorCount()) == 0, "Input rejections remain native-validation clean");
}
void retirement(const gfx::AdapterInfo& a, bool validation) {
    auto d = take(gfx::createDevice(desc(a, validation))); auto c = take(d->createCommandList()); take(c->begin()); auto v = buffer(*d, *c, vertices(2, true), false, S::VertexBuffer);
    const std::array<std::uint16_t, 3> values{1, 2, 3}; const auto indexBytes = std::as_bytes(std::span(values)); auto index = buffer(*d, *c, {indexBytes.begin(), indexBytes.end()}, false, S::IndexBuffer);
    auto t = take(d->createTexture({3, 7, F::RGBA8UNorm, true})); auto p = take(d->createGraphicsPipeline(vertexTest::pipeline(2, true, F::RGBA8UNorm)));
    take(c->bindVertexBuffer(2, v, vertexTest::layout(2, true).buffers[0].stride, 16)); take(c->bindIndexBuffer(index, gfx::IndexFormat::UInt16)); take(c->transition(t, S::RenderTarget)); take(c->beginRenderPass(t, {})); take(c->setPipeline(p)); take(c->drawIndexed(3)); ++draws; ++indexedDraws; take(c->endRenderPass()); take(c->end()); take(d->queue().submit(*c)); d.reset();
    reject(v->write(0, {}), gfx::ErrorCode::DeviceLost, "Retired vertex buffer inert"); reject(index->write(0, {}), gfx::ErrorCode::DeviceLost, "Retired index buffer inert"); reject(c->begin(), gfx::ErrorCode::DeviceLost, "Retired input recording inert"); check(v->description().memory == gfx::Memory::Upload, "Retired buffer description preserved");
}
}
int main(int argc, char** argv) try {
    if (argc != 1 && !(argc == 2 && std::string_view(argv[1]) == "--validation")) throw std::runtime_error("Usage: gfx_vulkan_vertex_tests [--validation]");
    const bool validation = argc == 2; std::cout << "source=" << gfx::buildFingerprint() << " validation=" << validation << std::endl;
    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan)); check(!adapters.empty(), "Real Vulkan adapter required");
    for (const auto& a : adapters) {
        std::cout << "adapter=" << a.name << " id=" << a.id.value << std::endl; auto d = take(gfx::createDevice(desc(a, validation))); contracts(*d, a, validation);
        for (auto format : {F::RGBA8UNorm, F::RGBA16Float}) {
            for (bool device : {false, true}) {
                for (unsigned n = 1; n <= 4; ++n) for (bool normalized : {false, true}) { nonindexed(*d, format, n, normalized, device); reclaimed(*d); }
                for (auto index : {gfx::IndexFormat::UInt16, gfx::IndexFormat::UInt32}) for (int base : {-1, 0, 1}) { indexed(*d, format, index, base, device); reclaimed(*d); }
                for (bool index : {false, true}) { instance(*d, format, device, index); reclaimed(*d); }
            }
            for (auto index : {gfx::IndexFormat::UInt16, gfx::IndexFormat::UInt32}) { indexed(*d, format, index, 0, false, true); reclaimed(*d); indexed(*d, format, index, 0, true, false, true); reclaimed(*d); }
        }
        check(take(d->validationErrorCount()) == 0, "No vertex/index core or synchronization errors"); retirement(a, validation);
    }
    std::cout << "PASS vertex_assertions=" << assertions << " draws=" << draws << " indexed_draws=" << indexedDraws << " images=" << images << " verified_texel_bytes=" << verifiedBytes << " padded_images=" << paddedImages << " adapters=" << adapters.size() << std::endl; return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << std::endl; return 1; }
