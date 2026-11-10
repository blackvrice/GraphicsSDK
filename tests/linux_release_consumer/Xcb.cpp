#include <graphics/Graphics.hpp>
#include <xcb/xcb.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using gfx::Format;
using gfx::ResourceState;
using Error = std::unique_ptr<xcb_generic_error_t, decltype(&std::free)>;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class T> T take(gfx::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}

void take(gfx::Result<void> result) {
    if (!result) throw std::runtime_error(result.error().message);
}

void request(xcb_connection_t* connection, xcb_void_cookie_t cookie) {
    const Error error(xcb_request_check(connection, cookie), std::free);
    check(!error, "XCB checked request failed");
}

unsigned frame(gfx::Device& device, gfx::SwapChain& chain, unsigned width, unsigned height,
               const std::array<unsigned, 3>& rgb) {
    auto target = take(chain.acquire());
    const auto desc = target->description();
    check(desc.width == width && desc.height == height &&
          (desc.format == Format::RGBA8UNorm || desc.format == Format::BGRA8UNorm),
          "Unexpected acquired format or extent");
    const auto footprint = target->footprint();
    check(footprint.rowBytes == width * 4 && footprint.height == height && footprint.rowPitch >= footprint.rowBytes &&
          footprint.size >= std::uint64_t(footprint.rowPitch) * (height - 1) + footprint.rowBytes,
          "Unexpected image footprint");
    auto readback = take(device.createBuffer({footprint.size, gfx::Memory::Readback}));
    auto command = take(device.createCommandList());
    take(command->begin());
    take(command->transition(target, ResourceState::RenderTarget));
    take(command->beginRenderPass(target, {float(rgb[0]), float(rgb[1]), float(rgb[2]), 1}));
    take(command->endRenderPass());
    take(command->transition(target, ResourceState::CopySource));
    take(command->transition(readback, ResourceState::CopyDestination));
    take(command->copyTextureToBuffer(target, readback));
    take(command->transition(target, ResourceState::Present));
    take(command->end());
    take(device.queue().wait(take(device.queue().submit(*command))));
    const auto pixels = take(readback->read(0, footprint.size));
    check(pixels.size() == footprint.size, "Readback is truncated");
    const bool bgra = desc.format == Format::BGRA8UNorm;
    const std::array expected{rgb[bgra ? 2 : 0] * 255, rgb[1] * 255, rgb[bgra ? 0 : 2] * 255, 255u};
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            for (unsigned c = 0; c < expected.size(); ++c)
                check(std::to_integer<unsigned>(pixels[std::size_t(y) * footprint.rowPitch + x * 4 + c]) == expected[c],
                      "Installed swapchain image differs");
    command.reset();
    target.reset();
    take(chain.present());
    return width * height * 4;
}
}

int main() try {
    int index{};
    const std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection(xcb_connect(nullptr, &index),
                                                                                 xcb_disconnect);
    check(connection && !xcb_connection_has_error(connection.get()), "XCB display connection failed");
    auto screen = xcb_setup_roots_iterator(xcb_get_setup(connection.get()));
    while (index-- > 0 && screen.rem) xcb_screen_next(&screen);
    check(screen.rem, "XCB screen missing");
    const auto window = xcb_generate_id(connection.get());
    request(connection.get(), xcb_create_window_checked(connection.get(), XCB_COPY_FROM_PARENT, window, screen.data->root,
            0, 0, 17, 9, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen.data->root_visual, 0, nullptr));
    request(connection.get(), xcb_map_window_checked(connection.get(), window));

    const auto adapters = take(gfx::enumerateAdapters(gfx::Backend::Vulkan));
    check(adapters.size() == 1 && adapters.front().softwareAdapter, "Expected the explicit software adapter");
    gfx::DeviceDesc deviceDesc;
    deviceDesc.backend = gfx::Backend::Vulkan;
    deviceDesc.softwareAdapter = true;
    deviceDesc.adapter = adapters.front().id;
    auto device = take(gfx::createDevice(deviceDesc));
    check(device->caps().swapChain, "XCB presentation support missing");
    const gfx::Surface surface{gfx::SurfaceKind::Xcb, nullptr, connection.get(), window};
    take(device->checkSurfaceSupport(surface));
    auto chain = take(device->createSwapChain(surface, {17, 9, 2, true}));
    unsigned bytes = frame(*device, *chain, 17, 9, {1, 0, 0});
    const std::array<std::uint32_t, 2> extent{31, 13};
    request(connection.get(), xcb_configure_window_checked(connection.get(), window,
            XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, extent.data()));
    take(chain->resize(extent[0], extent[1]));
    bytes += frame(*device, *chain, extent[0], extent[1], {0, 1, 0});
    bytes += frame(*device, *chain, extent[0], extent[1], {0, 0, 1});
    chain.reset();
    const auto stats = take(device->resourceStatistics());
    check(!stats.trackedObjects && !stats.uniqueResources && !stats.resourceAllocationBytes &&
          !stats.pendingSubmissions && !stats.pendingObjectReferences, "Installed XCB resources remain");
    device.reset();
    request(connection.get(), xcb_destroy_window_checked(connection.get(), window));
    std::cout << "PASS source=" << gfx::buildFingerprint() << " presents=3 resizes=1 verified_bytes=" << bytes << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
