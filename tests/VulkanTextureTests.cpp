#include <graphics/Graphics.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
unsigned assertions{}, roundtrips{}, paddingChecks{};
std::uint64_t verifiedBytes{};
void check(bool value, const char* text) { ++assertions; if (!value) throw std::runtime_error(text); }
template<class T> T take(gfx::Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message + " native=" + std::to_string(value.error().nativeCode));
    return std::move(*value);
}
void take(gfx::Result<void> value) { if (!value) throw std::runtime_error(value.error().message); }
template<class T> void reject(gfx::Result<T> value, gfx::ErrorCode code, const char* text) {
    check(!value && value.error().code == code, text);
}
gfx::DeviceDesc description(const gfx::AdapterInfo& adapter, bool validation) {
    gfx::DeviceDesc d; d.backend = gfx::Backend::Vulkan; d.adapter = adapter.id;
    d.softwareAdapter = adapter.softwareAdapter; d.validation = validation; return d;
}
void reclaimed(gfx::Device& d) {
    const auto s = take(d.resourceStatistics());
    check(!s.trackedObjects && !s.uniqueResources && !s.resourceAllocationBytes && !s.pendingSubmissions && !s.pendingObjectReferences,
        "Every wrapper/native allocation/recording reclaimed");
}
void roundtrip(gfx::Device& d, gfx::TextureDesc desc, unsigned seed) {
    auto image = take(d.createTexture(desc));
    auto second = take(d.createTexture(desc));
    const auto f = image->footprint();
    const unsigned texelBytes = desc.format == gfx::Format::RGBA8UNorm ? 4 : 8;
    check(f.rowBytes == desc.width * texelBytes && f.height == desc.height && f.rowPitch >= f.rowBytes &&
        !(f.rowPitch % 256) && f.size == std::uint64_t(f.rowPitch) * f.height, "Bounded padded footprint");
    const auto actualDesc = image->description();
    check(actualDesc.width == desc.width && actualDesc.height == desc.height && actualDesc.format == desc.format &&
        !actualDesc.renderTarget && !actualDesc.depthStencil, "Texture description preserved");
    auto upload = take(d.createBuffer({f.size, gfx::Memory::Upload}));
    auto sentinel = take(d.createBuffer({f.size, gfx::Memory::Upload}));
    auto staging = take(d.createBuffer({f.size}));
    auto readback = take(d.createBuffer({f.size, gfx::Memory::Readback}));
    std::vector<std::byte> expected(static_cast<std::size_t>(f.size), std::byte{0x71});
    for (unsigned y = 0; y < f.height; ++y)
        for (unsigned x = 0; x < f.rowBytes; ++x)
            expected[std::size_t(y) * f.rowPitch + x] = std::byte((x * 37u + y * 19u + seed * 53u + x / 251) & 255);
    take(upload->write(0, expected));
    take(sentinel->write(0, std::vector<std::byte>(static_cast<std::size_t>(f.size), std::byte{0xd3})));
    auto cmd = take(d.createCommandList()); take(cmd->begin());
    take(cmd->copyBuffer(sentinel, readback, f.size));
    take(cmd->transition(image, gfx::ResourceState::CopyDestination));
    take(cmd->copyBufferToTexture(upload, image));
    take(cmd->copyBufferToTexture(upload, image)); // Same-layout WAW dependency.
    take(cmd->transition(image, gfx::ResourceState::Common));
    take(cmd->transition(image, gfx::ResourceState::CopySource));
    take(cmd->transition(staging, gfx::ResourceState::CopyDestination));
    take(cmd->copyTextureToBuffer(image, staging));
    take(cmd->transition(staging, gfx::ResourceState::CopySource));
    take(cmd->transition(second, gfx::ResourceState::CopyDestination));
    take(cmd->copyBufferToTexture(staging, second));
    take(cmd->transition(second, gfx::ResourceState::CopySource));
    take(cmd->copyTextureToBuffer(second, readback));
    take(cmd->copyTextureToBuffer(second, readback)); // Same-layout WAW on host buffer.
    take(cmd->end());
    std::weak_ptr<gfx::Texture> firstWrapper = image, secondWrapper = second;
    image.reset(); second.reset(); upload.reset(); sentinel.reset(); staging.reset();
    check(firstWrapper.expired() && secondWrapper.expired(), "Recording keeps native images, not public wrappers");
    const auto held = take(d.resourceStatistics());
    check(held.trackedObjects == 2 && held.uniqueResources == 6 && held.resourceAllocationBytes >= f.size * 4,
        "Ended recording retains all six native allocations");
    const auto fence = take(d.queue().submit(*cmd)); cmd.reset();
    const auto poll = d.queue().wait(fence, std::chrono::milliseconds(0));
    check(poll || poll.error().code == gfx::ErrorCode::Timeout, "Zero timeout polls or times out");
    take(d.queue().wait(fence));
    const auto bytes = take(readback->read(0, f.size));
    bool pixels = true, padding = true;
    for (unsigned y = 0; y < f.height; ++y) {
        const auto base = std::size_t(y) * f.rowPitch;
        pixels &= std::equal(bytes.begin() + base, bytes.begin() + base + f.rowBytes, expected.begin() + base);
        padding &= std::all_of(bytes.begin() + base + f.rowBytes, bytes.begin() + base + f.rowPitch, [](auto b) { return b == std::byte{0xd3}; });
    }
    check(pixels, "Every texel byte matches through two native images and device staging");
    check(padding, "Image copies leave every row's sentinel padding unchanged");
    if (f.rowPitch > f.rowBytes) ++paddingChecks;
    verifiedBytes += std::uint64_t(f.rowBytes) * f.height; ++roundtrips;
    readback.reset(); reclaimed(d);
}
void reuse(gfx::Device& d) {
    auto image = take(d.createTexture({65, 17})); const auto f = image->footprint();
    auto upload = take(d.createBuffer({f.size, gfx::Memory::Upload}));
    auto readback = take(d.createBuffer({f.size, gfx::Memory::Readback}));
    auto cmd = take(d.createCommandList());
    for (unsigned seed = 0; seed < 5; ++seed) {
        std::vector<std::byte> data(static_cast<std::size_t>(f.size), std::byte(seed * 31)); take(upload->write(0, data));
        take(cmd->begin()); take(cmd->transition(image, gfx::ResourceState::CopyDestination));
        take(cmd->copyBufferToTexture(upload, image)); take(cmd->transition(image, gfx::ResourceState::CopySource));
        take(cmd->copyTextureToBuffer(image, readback)); take(cmd->end()); take(d.queue().wait(take(d.queue().submit(*cmd))));
        const auto bytes = take(readback->read(0, f.size)); bool match = true;
        for (unsigned y = 0; y < f.height; ++y)
            match &= std::equal(bytes.begin() + std::size_t(y) * f.rowPitch, bytes.begin() + std::size_t(y) * f.rowPitch + f.rowBytes,
                data.begin() + std::size_t(y) * f.rowPitch);
        check(match, "Submitted image layout/version survives changing-payload reuse");
        ++roundtrips; verifiedBytes += std::uint64_t(f.rowBytes) * f.height;
    }
}
void contracts(gfx::Device& d, const gfx::AdapterInfo& adapter, bool validation) {
    reject(d.createTexture({0, 1}), gfx::ErrorCode::InvalidArgument, "Zero texture extent");
    reject(d.createTexture({16385, 1}), gfx::ErrorCode::InvalidArgument, "Bounded texture extent");
    reject(d.createTexture({1, 1, static_cast<gfx::Format>(99)}), gfx::ErrorCode::InvalidArgument, "Unknown texture format");
    reject(d.createTexture({1, 1, gfx::Format::D32Float}), gfx::ErrorCode::InvalidArgument, "Depth format needs depth usage");
    reject(d.createTexture({1, 1, gfx::Format::D32Float, false, true}), gfx::ErrorCode::Unsupported, "Depth transfers not implemented");
    reject(d.createTexture({1, 1, gfx::Format::RGBA8UNorm, false, true}), gfx::ErrorCode::InvalidArgument, "Color cannot have depth usage");
    auto other = take(gfx::createDevice(description(adapter, validation)));
    auto image = take(d.createTexture({3, 7})); const auto f = image->footprint();
    auto foreign = take(other->createTexture({3, 7}));
    auto upload = take(d.createBuffer({f.size, gfx::Memory::Upload}));
    auto readback = take(d.createBuffer({f.size, gfx::Memory::Readback}));
    auto small = take(d.createBuffer({f.size - 1, gfx::Memory::Upload}));
    auto smallRead = take(d.createBuffer({f.size - 1, gfx::Memory::Readback}));
    auto cmd = take(d.createCommandList());
    reject(cmd->transition(image, gfx::ResourceState::CopyDestination), gfx::ErrorCode::InvalidState, "Image transition outside recording");
    take(cmd->begin());
    reject(cmd->transition(gfx::TexturePtr{}, gfx::ResourceState::CopySource), gfx::ErrorCode::InvalidArgument, "Null texture transition");
    reject(cmd->copyBufferToTexture(upload, foreign), gfx::ErrorCode::WrongDevice, "Foreign image destination");
    reject(cmd->copyTextureToBuffer(foreign, readback), gfx::ErrorCode::WrongDevice, "Foreign image source");
    reject(cmd->transition(foreign, gfx::ResourceState::CopyDestination), gfx::ErrorCode::WrongDevice, "Foreign image transition");
    reject(cmd->copyBufferToTexture({}, image), gfx::ErrorCode::InvalidArgument, "Null upload");
    reject(cmd->copyTextureToBuffer(image, {}), gfx::ErrorCode::InvalidArgument, "Null readback");
    reject(cmd->copyTextureToBuffer({}, readback), gfx::ErrorCode::InvalidArgument, "Null image");
    const auto before = take(d.resourceStatistics());
    for (unsigned n = 0; n < 32; ++n) {
        auto temp = take(d.createTexture({3, 7})); std::weak_ptr<gfx::Texture> weak = temp;
        reject(cmd->copyBufferToTexture(upload, temp), gfx::ErrorCode::InvalidState, "Missing layout rejected");
        reject(cmd->transition(temp, gfx::ResourceState::Present), gfx::ErrorCode::Unsupported, "Presentation unimplemented");
        reject(cmd->transition(temp, static_cast<gfx::ResourceState>(99)), gfx::ErrorCode::Unsupported, "Unknown image state rejected");
        temp.reset(); const auto after = take(d.resourceStatistics());
        check(weak.expired() && before.trackedObjects == after.trackedObjects && before.uniqueResources == after.uniqueResources &&
            before.resourceAllocationBytes == after.resourceAllocationBytes, "Rejection keeps no image references");
    }
    // Rejections may not snapshot either source or destination versions.
    take(upload->write(0, std::vector<std::byte>(static_cast<std::size_t>(f.size), std::byte{1})));
    take(cmd->end()); take(d.queue().wait(take(d.queue().submit(*cmd))));
    take(cmd->begin()); take(cmd->transition(image, gfx::ResourceState::CopyDestination));
    take(cmd->end()); take(d.queue().wait(take(d.queue().submit(*cmd))));
    take(cmd->begin());
    reject(cmd->copyBufferToTexture(small, image), gfx::ErrorCode::InvalidArgument, "Reject undersized full footprint");
    reject(cmd->copyBufferToTexture(readback, image), gfx::ErrorCode::InvalidArgument, "Readback cannot upload image");
    take(small->write(0, std::array{std::byte{2}}));
    auto change = take(d.createCommandList()); take(change->begin()); take(change->transition(image, gfx::ResourceState::Common));
    take(change->end()); take(d.queue().wait(take(d.queue().submit(*change))));
    take(cmd->end()); take(d.queue().wait(take(d.queue().submit(*cmd)))); // No captured image/small-buffer version.
    take(cmd->begin()); take(cmd->transition(image, gfx::ResourceState::CopyDestination));
    take(cmd->copyBufferToTexture(upload, image)); take(cmd->end());
    take(upload->write(0, std::array{std::byte{3}}));
    reject(d.queue().submit(*cmd), gfx::ErrorCode::InvalidState, "CPU upload write invalidates image recording");
    take(cmd->begin()); take(cmd->transition(image, gfx::ResourceState::CopyDestination));
    take(cmd->copyBufferToTexture(upload, image)); take(cmd->transition(image, gfx::ResourceState::CopySource));
    reject(cmd->copyTextureToBuffer(image, smallRead), gfx::ErrorCode::InvalidArgument, "Undersized download");
    reject(cmd->copyTextureToBuffer(image, upload), gfx::ErrorCode::InvalidArgument, "Upload cannot receive image");
    take(cmd->copyTextureToBuffer(image, readback)); take(cmd->end());
    auto sibling = take(d.createCommandList()); take(sibling->begin()); take(sibling->transition(image, gfx::ResourceState::Common)); take(sibling->end());
    take(d.queue().wait(take(d.queue().submit(*cmd))));
    reject(d.queue().submit(*sibling), gfx::ErrorCode::InvalidState, "Submitted image invalidates sibling recording");
    bool rejected{};
    std::thread worker([&] { auto result = d.createTexture({1, 1}); rejected = !result && result.error().code == gfx::ErrorCode::InvalidState; }); worker.join();
    check(rejected, "Texture creation requires creating thread");
    cmd.reset(); sibling.reset(); change.reset(); image.reset(); foreign.reset(); upload.reset(); readback.reset(); small.reset(); smallRead.reset();
    take(d.queue().waitIdle()); reclaimed(d);
    check(take(d.validationErrorCount()) == 0 && take(other->validationErrorCount()) == 0, "Contracts produce no native validation errors");
}
void retirement(const gfx::AdapterInfo& adapter, bool validation) {
    auto d = take(gfx::createDevice(description(adapter, validation)));
    auto image = take(d->createTexture({65, 7})); const auto f = image->footprint();
    auto up = take(d->createBuffer({f.size, gfx::Memory::Upload}));
    auto read = take(d->createBuffer({f.size, gfx::Memory::Readback}));
    auto cmd = take(d->createCommandList()); take(cmd->begin());
    take(cmd->transition(image, gfx::ResourceState::CopyDestination)); take(cmd->copyBufferToTexture(up, image));
    take(cmd->transition(image, gfx::ResourceState::CopySource)); take(cmd->copyTextureToBuffer(image, read)); take(cmd->end());
    take(d->queue().submit(*cmd)); d.reset();
    check(image->description().width == 65 && image->footprint().size == f.size, "Retired image preserves CPU description/footprint");
    reject(cmd->begin(), gfx::ErrorCode::DeviceLost, "Retired image recording inert");
    reject(read->read(0, f.size), gfx::ErrorCode::DeviceLost, "Retired readback inert");
}
}
int main(int argc, char** argv) try {
    if (argc != 1 && !(argc == 2 && std::string_view(argv[1]) == "--validation"))
        throw std::runtime_error("Usage: gfx_vulkan_texture_tests [--validation]");
    const bool validation = argc == 2;
    std::cout << "source=" << gfx::buildFingerprint() << " validation=" << validation << std::endl;
    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan)); check(!adapters.empty(), "Real Vulkan adapter required");
    for (const auto& adapter : adapters) {
        std::cout << "adapter=" << adapter.name << " id=" << adapter.id.value << std::endl;
        auto d = take(gfx::createDevice(description(adapter, validation))); contracts(*d, adapter, validation);
        for (auto format : {gfx::Format::RGBA8UNorm, gfx::Format::RGBA16Float})
            for (auto [width, height] : {std::pair{1u, 1u}, {3u, 7u}, {63u, 5u}, {64u, 1u}, {65u, 17u}, {257u, 129u}, {1920u, 1080u}, {3840u, 2160u}})
                for (unsigned seed = 0; seed < 2; ++seed) roundtrip(*d, {width, height, format}, seed);
        reuse(*d); reclaimed(*d); check(take(d->validationErrorCount()) == 0, "No texture validation errors");
        retirement(adapter, validation);
    }
    std::cout << "PASS assertions=" << assertions << " roundtrips=" << roundtrips << " verified_texel_bytes=" << verifiedBytes
        << " padded_roundtrips=" << paddingChecks << " adapters=" << adapters.size() << std::endl;
    return 0;
} catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << std::endl; return 1; }
