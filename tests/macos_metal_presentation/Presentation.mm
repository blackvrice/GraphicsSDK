#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <graphics/Graphics.hpp>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

@interface ObservedMetalLayer : CAMetalLayer {
@public
    std::shared_ptr<std::atomic_uint> presented;
}
@property BOOL forceUnavailable;
@end

@implementation ObservedMetalLayer
- (id<CAMetalDrawable>)nextDrawable {
    if (self.forceUnavailable) return nil;
    id<CAMetalDrawable> drawable = [super nextDrawable];
    const auto count = presented;
    if (count) {
        [drawable addPresentedHandler:^(id<MTLDrawable> shown) {
            if (shown.presentedTime > 0) count->fetch_add(1, std::memory_order_relaxed);
        }];
    }
    return drawable;
}
@end

@interface PresentationWindowDelegate : NSObject <NSWindowDelegate>
@property BOOL closeRequested;
@end

@implementation PresentationWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    self.closeRequested = YES;
    return NO;
}
@end

namespace {
using gfx::ErrorCode;
using gfx::ResourceState;
using namespace std::chrono_literals;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class T> T take(gfx::Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message);
    return std::move(*value);
}

void take(gfx::Result<void> value) {
    if (!value) throw std::runtime_error(value.error().message);
}

template<class T> void reject(const gfx::Result<T>& value, ErrorCode code, const char* message) {
    check(!value && value.error().code == code, message);
}

auto device() {
    gfx::DeviceDesc desc;
    desc.backend = gfx::Backend::Metal;
    return take(gfx::createDevice(desc));
}

gfx::Surface surface(CAMetalLayer* layer) {
    return {gfx::SurfaceKind::MetalLayer, (__bridge void*)layer};
}

void contracts() {
    auto gpu = device();
    check(gpu->caps().swapChain, "Metal presentation capability missing");
    reject(gpu->checkSurfaceSupport({}), ErrorCode::Unsupported, "Win32 surface accepted");
    reject(gpu->checkSurfaceSupport({gfx::SurfaceKind::MetalLayer}), ErrorCode::InvalidArgument, "Null layer accepted");
    NSObject* wrong = [NSObject new];
    reject(gpu->checkSurfaceSupport({gfx::SurfaceKind::MetalLayer, (__bridge void*)wrong}),
           ErrorCode::InvalidArgument, "Wrong Objective-C type accepted");
    ObservedMetalLayer* layer = [ObservedMetalLayer layer];
    layer.forceUnavailable = YES;
    const auto target = surface(layer);
    id<MTLDevice> previousDevice = layer.device;
    take(gpu->checkSurfaceSupport(target));
    check(layer.device == previousDevice, "Surface query changed the layer");
    reject(gpu->createSwapChain(target, {0, 31}), ErrorCode::InvalidArgument, "Empty extent accepted");
    reject(gpu->createSwapChain(target, {65, 31, 4}), ErrorCode::InvalidArgument, "Invalid drawable count accepted");
    auto chain = take(gpu->createSwapChain(target, {65, 31, 2, false}));
    check(layer.device && layer.maximumDrawableCount == 2 && !layer.displaySyncEnabled &&
          !layer.framebufferOnly && layer.pixelFormat == MTLPixelFormatBGRA8Unorm,
          "Layer configuration differs from descriptor");
    auto other = device();
    reject(other->createSwapChain(target, {65, 31}), ErrorCode::InvalidState, "Two live chains leased the same layer");
    reject(chain->present(), ErrorCode::InvalidState, "Present without acquire succeeded");
    reject(chain->acquire(), ErrorCode::Timeout, "Unavailable drawable did not map to timeout");
    reject(chain->resize(0, 31), ErrorCode::InvalidArgument, "Empty resize accepted");
    take(chain->resize(99, 47));
    check(layer.drawableSize.width == 99 && layer.drawableSize.height == 47, "Pixel resize did not reach layer");
    layer.framebufferOnly = YES;
    reject(chain->acquire(), ErrorCode::InvalidState, "External layer reconfiguration accepted");
    layer.framebufferOnly = NO;
    bool refused = false;
    std::thread worker([&] {
        gfx::DeviceDesc desc;
        desc.backend = gfx::Backend::Metal;
        auto threadDevice = gfx::createDevice(desc);
        if (!threadDevice) return;
        auto result = (*threadDevice)->checkSurfaceSupport(target);
        refused = !result && result.error().code == ErrorCode::InvalidState;
    });
    worker.join();
    check(refused, "Presentation accepted a worker-thread device");
    gpu.reset();
    reject(chain->resize(99, 47), ErrorCode::DeviceLost, "Retired swapchain remained active");
    auto replacement = take(other->createSwapChain(target, {77, 33, 3, true}));
    chain.reset();
    reject(other->createSwapChain(target, {65, 31}), ErrorCode::InvalidState, "Old chain destruction cleared a new lease");
    check(layer.maximumDrawableCount == 3 && layer.displaySyncEnabled, "Triple-buffered vsync not configured");
    replacement.reset();
    take(other->createSwapChain(target, {65, 31}));
    std::cout << "{\"contract\":\"metal_layer_contracts_v1\",\"SDK_source_fingerprint\":\""
              << gfx::buildFingerprint() << "\",\"presentation_executed\":false}\n";
}

gfx::PipelinePtr pipeline(gfx::Device& gpu) {
    constexpr std::string_view source = R"(
#include <metal_stdlib>
using namespace metal;
vertex float4 gfx_vertex(uint i [[vertex_id]]) {
    const float2 corners[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};
    return float4(corners[i], 0, 1);
}
fragment float4 gfx_fragment() { return float4(1, 0, 0, 1); }
)";
    auto shader = [&](gfx::ShaderStage stage) {
        gfx::ShaderBinary binary;
        binary.format = gfx::ShaderFormat::MSL;
        binary.stage = stage;
        binary.bytes.resize(source.size());
        std::memcpy(binary.bytes.data(), source.data(), source.size());
        return binary;
    };
    gfx::GraphicsPipelineDesc desc;
    desc.colorFormat = gfx::Format::BGRA8UNorm;
    desc.vertex = shader(gfx::ShaderStage::Vertex);
    desc.fragment = shader(gfx::ShaderStage::Fragment);
    return take(gpu.createGraphicsPipeline(desc));
}

void pump() {
    @autoreleasepool {
        NSDate* limit = [NSDate dateWithTimeIntervalSinceNow:0.003];
        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:limit
                                              inMode:NSDefaultRunLoopMode dequeue:YES];
        if (event) [NSApp sendEvent:event];
        [NSApp updateWindows];
        [CATransaction flush];
    }
}

unsigned waitForDisplay(const std::shared_ptr<std::atomic_uint>& count) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (count->load() == 0 && std::chrono::steady_clock::now() < deadline) pump();
    return count->load();
}

unsigned nativeDisplayControl(ObservedMetalLayer* layer) {
    layer->presented = std::make_shared<std::atomic_uint>(0);
    id<MTLCommandQueue> queue = [layer.device newCommandQueue];
    check(queue != nil, "Native display control has no command queue");
    for (unsigned i = 0; i < 3; ++i) {
        @autoreleasepool {
            id<CAMetalDrawable> drawable = [layer nextDrawable];
            check(drawable != nil, "Native display control has no drawable");
            auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = drawable.texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 1, 0, 1);
            id<MTLCommandBuffer> commands = [queue commandBuffer];
            auto encoder = [commands renderCommandEncoderWithDescriptor:pass];
            check(encoder != nil, "Native display control has no encoder");
            [encoder endEncoding];
            [commands presentDrawable:drawable];
            [commands commit];
            [commands waitUntilCompleted];
            check(commands.status == MTLCommandBufferStatusCompleted, "Native display control did not complete");
        }
        pump();
    }
    return waitForDisplay(layer->presented);
}

bool frame(gfx::Device& gpu, gfx::SwapChain& chain, const gfx::PipelinePtr& pso,
           std::uint32_t width, std::uint32_t height, std::uint32_t split, bool verify) {
    auto acquired = chain.acquire();
    if (!verify && !acquired && acquired.error().code == ErrorCode::Timeout) return false;
    auto back = take(std::move(acquired));
    check(back->description().width == width && back->description().height == height &&
          back->description().format == gfx::Format::BGRA8UNorm, "Acquired drawable has wrong shape");
    if (verify) {
        check(take(chain.acquire()) == back, "Repeated acquire replaced the current drawable");
        reject(chain.resize(width, height), ErrorCode::InvalidState, "Resize accepted held drawable");
        reject(chain.present(), ErrorCode::InvalidState, "Present accepted uninitialized drawable");
    }
    auto commands = take(gpu.createCommandList());
    take(commands->begin());
    take(commands->transition(back, ResourceState::RenderTarget));
    take(commands->beginRenderPass(back, {0, 0, 1, 1}));
    take(commands->setPipeline(pso));
    take(commands->setScissor(0, 0, split, height));
    take(commands->draw(3));
    take(commands->endRenderPass());
    const auto footprint = back->footprint();
    gfx::BufferPtr readback;
    if (verify) {
        readback = take(gpu.createBuffer({footprint.size, gfx::Memory::Readback}));
        take(commands->transition(back, ResourceState::CopySource));
        take(commands->copyTextureToBuffer(back, readback));
    }
    take(commands->transition(back, ResourceState::Present));
    take(commands->end());
    const auto fence = take(gpu.queue().submit(*commands));
    std::unique_ptr<gfx::CommandList> stale;
    if (verify) {
        stale = take(gpu.createCommandList());
        take(stale->begin());
        take(stale->transition(back, ResourceState::Present));
        take(stale->end());
    }
    take(chain.present());
    take(gpu.queue().wait(fence));
    if (!verify) return true;
    reject(gpu.queue().submit(*stale), ErrorCode::InvalidState, "Recording survived drawable present");
    take(stale->begin());
    reject(stale->transition(back, ResourceState::RenderTarget), ErrorCode::InvalidState, "Presented drawable remained usable");
    const auto pixels = take(readback->read(0, footprint.size));
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto offset = y * footprint.rowPitch + 4 * x;
            const auto color = x < split ? std::array{0, 0, 255, 255} : std::array{255, 0, 0, 255};
            for (unsigned c = 0; c < 4; ++c)
                check(std::to_integer<int>(pixels[offset + c]) == color[c], "Full BGRA drawable readback differs");
        }
    }
    return true;
}

int window(bool interactive, bool runner) {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    if (NSScreen.screens.count == 0) {
        std::cout << "{\"presentation_executed\":false,\"reason\":\"no AppKit screen\"}\n";
        return 77;
    }
    const auto style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(100, 100, 480, 280)
                                                 styleMask:style backing:NSBackingStoreBuffered defer:NO];
    window.releasedWhenClosed = NO;
    window.title = @"GraphicsSDK Metal — resize, minimize, restore, close";
    PresentationWindowDelegate* delegate = [PresentationWindowDelegate new];
    window.delegate = delegate;
    NSView* view = window.contentView;
    ObservedMetalLayer* layer = [ObservedMetalLayer layer];
    layer->presented = std::make_shared<std::atomic_uint>(0);
    layer.frame = view.bounds;
    layer.contentsScale = window.backingScaleFactor;
    view.layer = layer;
    view.wantsLayer = YES;
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    for (unsigned i = 0; i < 4; ++i) pump();

    auto gpu = device();
    auto pso = pipeline(*gpu);
    std::uint32_t width = 65, height = 31;
    auto chain = take(gpu->createSwapChain(surface(layer), {width, height, 3, true}));
    unsigned frames = 0;
    unsigned beforeResize = 0;
    if (interactive) {
        while (!delegate.closeRequested) {
            pump();
            if (delegate.closeRequested) break;
            if (window.miniaturized || !(window.occlusionState & NSWindowOcclusionStateVisible)) continue;
            const NSSize pixels = [view convertRectToBacking:view.bounds].size;
            if (pixels.width < 2 || pixels.height < 1 || pixels.width > 16384 || pixels.height > 16384) continue;
            if (width != unsigned(pixels.width) || height != unsigned(pixels.height)) {
                width = unsigned(pixels.width);
                height = unsigned(pixels.height);
                layer.frame = view.bounds;
                layer.contentsScale = window.backingScaleFactor;
                take(chain->resize(width, height));
            }
            if (frame(*gpu, *chain, pso, width, height, 1 + frames % (width - 1), false)) ++frames;
        }
    } else {
        for (unsigned i = 0; i < 4; ++i) {
            frame(*gpu, *chain, pso, width, height, width / 2, true);
            ++frames;
            pump();
        }
        beforeResize = waitForDisplay(layer->presented);
        auto back = take(chain->acquire());
        auto stale = take(gpu->createCommandList());
        take(stale->begin());
        take(stale->transition(back, ResourceState::Present));
        take(stale->end());
        back.reset();
        width = 99;
        height = 47;
        [window setContentSize:NSMakeSize(400, 240)];
        layer.frame = view.bounds;
        take(chain->resize(width, height));
        reject(gpu->queue().submit(*stale), ErrorCode::InvalidState, "Recording survived drawable resize");
        layer->presented = std::make_shared<std::atomic_uint>(0);
        for (unsigned i = 0; i < 4; ++i) {
            frame(*gpu, *chain, pso, width, height, width / 2, true);
            ++frames;
            pump();
        }
    }
    take(gpu->queue().waitIdle());
    const unsigned displayed = waitForDisplay(layer->presented);
    chain.reset();
    const bool displayObserved = beforeResize > 0 && displayed > 0;
    const bool needsControl = runner && !displayObserved;
    const unsigned nativeDisplayed = needsControl ? nativeDisplayControl(layer) : 0;
    [window close];
    std::cout << "{\"contract\":\"metal_window_v1\",\"SDK_source_fingerprint\":\""
              << gfx::buildFingerprint() << "\",\"frames\":" << frames << ",\"before_resize_presented\":"
              << beforeResize << ",\"after_resize_presented\":" << displayed
              << ",\"display_observed\":" << (displayObserved ? "true" : "false")
              << ",\"native_control_frames\":" << (needsControl ? 3 : 0)
              << ",\"native_control_presented\":" << nativeDisplayed
              << ",\"presentation_executed\":true,\"interactive\":"
              << (interactive ? "true" : "false") << "}\n";
    check(interactive || displayObserved || (needsControl && nativeDisplayed == 0),
          "SDK did not observe display before and after resize");
    return 0;
}
}

int main(int argc, char** argv) {
    @autoreleasepool {
        try {
            const std::string_view mode = argc == 2 ? argv[1] : "";
            if (mode == "--contracts") { contracts(); return 0; }
            if (mode == "--window" || mode == "--interactive" || mode == "--runner-window")
                return window(mode == "--interactive", mode == "--runner-window");
            throw std::runtime_error("Expected --contracts, --window, --interactive or --runner-window");
        } catch (const std::exception& error) {
            std::cerr << "FAIL: " << error.what() << '\n';
            return 1;
        }
    }
}
