#pragma once
// Included in the private Metal implementation namespace after MetalQueue.

Result<CAMetalLayer*> checkedLayer(State& owner, Surface surface) {
    if (auto ok = owner.check(); !ok) return std::unexpected(ok.error());
    if (surface.kind != SurfaceKind::MetalLayer)
        return fail(ErrorCode::Unsupported, "Metal presentation requires a CAMetalLayer surface");
    if (![NSThread isMainThread])
        return fail(ErrorCode::InvalidState, "Metal presentation requires the macOS main thread");
    if (!surface.window || surface.connection || surface.windowId)
        return fail(ErrorCode::InvalidArgument, "Invalid Metal surface fields");
    id object = (__bridge id)surface.window;
    if (![object isKindOfClass:[CAMetalLayer class]])
        return fail(ErrorCode::InvalidArgument, "Metal surface window must reference a live CAMetalLayer");
    CAMetalLayer* layer = object;
    if (layer.device && layer.device.registryID != owner.device.registryID)
        return fail(ErrorCode::WrongDevice, "CAMetalLayer is configured for a different Metal adapter");
    return layer;
}

bool validDrawableExtent(std::uint32_t width, std::uint32_t height) {
    return width && height && width <= 16384 && height <= 16384;
}

bool validMetalSwapPair(Format format, ColorSpace cs) {
    switch (cs) {
    case ColorSpace::SrgbNonlinear:
        return format == Format::BGRA8UNorm || format == Format::BGRA8SRGB || format == Format::RGB10A2UNorm;
    case ColorSpace::DisplayP3Nonlinear:
        return format == Format::BGRA8UNorm || format == Format::BGRA8SRGB || format == Format::RGB10A2UNorm;
    case ColorSpace::Hdr10St2084:
        return format == Format::RGB10A2UNorm;
    case ColorSpace::LinearSrgb:
    case ColorSpace::ExtendedLinearScRgb:
    case ColorSpace::Rec2020Linear:
        return format == Format::RGBA16Float;
    }
    return false;
}

CFStringRef metalColorSpaceName(ColorSpace cs) {
    switch (cs) {
    case ColorSpace::SrgbNonlinear: return kCGColorSpaceSRGB;
    case ColorSpace::LinearSrgb: return kCGColorSpaceLinearSRGB;
    case ColorSpace::DisplayP3Nonlinear: return kCGColorSpaceDisplayP3;
    case ColorSpace::Rec2020Linear: return kCGColorSpaceExtendedLinearITUR_2020;
    case ColorSpace::Hdr10St2084: return kCGColorSpaceITUR_2100_PQ;
    case ColorSpace::ExtendedLinearScRgb: return kCGColorSpaceExtendedLinearSRGB;
    }
    return kCGColorSpaceSRGB;
}

struct MetalSwapChain final : SwapChain {
    std::shared_ptr<State> owner;
    std::shared_ptr<DrawableScope> scope = std::make_shared<DrawableScope>();
    SwapChainDesc desc;
    PresentMode activeMode{PresentMode::Fifo};
    Format activeFormat{Format::BGRA8UNorm};
    MTLPixelFormat nativeFormat{MTLPixelFormatBGRA8Unorm};
    ColorSpace activeColorSpace{ColorSpace::SrgbNonlinear};
    std::uint32_t effectiveLatency{1};
    mutable detail::FramePacingTracker pacing;
    std::shared_ptr<MetalTexture> current;

    ~MetalSwapChain() override {
        @autoreleasepool {
            if (scope->layer && !owner->pending.empty()) [owner->pending.back().buffer waitUntilCompleted];
            scope->release();
        }
    }

    Result<void> check() const {
        if (auto ok = owner->poll(); !ok) return ok;
        CAMetalLayer* layer = scope->layer;
        const CGSize size = layer.drawableSize;
        const bool expectedSync = activeMode == PresentMode::Fifo;
        if (!layer || layer.device != owner->device || layer.pixelFormat != nativeFormat ||
            layer.framebufferOnly || layer.presentsWithTransaction || !layer.allowsNextDrawableTimeout ||
            layer.maximumDrawableCount != desc.buffers || bool(layer.displaySyncEnabled) != expectedSync ||
            size.width != desc.width || size.height != desc.height)
            return fail(ErrorCode::InvalidState, "CAMetalLayer presentation configuration changed outside the SDK");
        return {};
    }

    Result<TexturePtr> acquire() override {
        if (auto ok = check(); !ok) return std::unexpected(ok.error());
        if (current) return TexturePtr(current);
        @autoreleasepool {
            const auto t0 = detail::FramePacingTracker::Clock::now();
            id<CAMetalDrawable> drawable = [scope->layer nextDrawable];
            const auto t1 = detail::FramePacingTracker::Clock::now();
            if (!drawable) { pacing.recordStall(); return fail(ErrorCode::Timeout, "CAMetalLayer did not provide a drawable"); }
            pacing.recordAcquireWait(std::chrono::duration<double, std::milli>(t1 - t0).count(), t0);
            id<MTLTexture> texture = drawable.texture;
            if (!texture || texture.width != desc.width || texture.height != desc.height ||
                texture.pixelFormat != nativeFormat || texture.device != owner->device)
                return fail(ErrorCode::BackendFailure, "CAMetalLayer returned a drawable with unexpected device, extent or format");

            auto allocation = std::make_shared<Allocation>();
            allocation->texture = texture;
            allocation->textureDesc = {desc.width, desc.height, activeFormat, true};
            const std::uint32_t bpp = activeFormat == Format::RGBA16Float ? 8u : 4u;
            const auto rowBytes = desc.width * bpp;
            const auto rowPitch = (rowBytes + 255u) & ~255u;
            allocation->footprint = {std::uint64_t(rowPitch) * desc.height, rowPitch, rowBytes, desc.height};
            allocation->bytes = texture.allocatedSize;
            allocation->backbuffer = true;
            allocation->state = ResourceState::Present;

            auto wrapper = std::make_shared<MetalTexture>();
            wrapper->owner = owner;
            wrapper->allocation = allocation;
            wrapper->token = std::make_shared<int>();
            std::erase_if(owner->allocations, [](const auto& item) { return item.expired(); });
            std::erase_if(owner->objects, [](const auto& item) { return item.expired(); });
            owner->allocations.push_back(allocation);
            owner->objects.push_back(wrapper->token);
            scope->current = std::move(allocation);
            scope->drawable = drawable;
            current = std::move(wrapper);
            return TexturePtr(current);
        }
    }

    Result<void> present() override {
        if (auto ok = check(); !ok) return ok;
        if (!current || !scope->current->initialized || scope->current->state != ResourceState::Present)
            return fail(ErrorCode::InvalidState, "Metal present requires an initialized drawable submitted in Present state");
        if (owner->last == UINT64_MAX)
            return fail(ErrorCode::InvalidState, "Metal fence sequence exhausted");
        @autoreleasepool {
            const auto t0 = detail::FramePacingTracker::Clock::now();
            // The public API submits rendering before present. The same native queue
            // orders this command after rendering, and waitIdle includes both.
            id<MTLCommandBuffer> command = [owner->queue commandBuffer];
            if (!command) return fail(ErrorCode::OutOfMemory, "Metal presentation command buffer creation failed");
            owner->pending.push_back({owner->last + 1, command, {}, {}, scope->drawable});
            [command presentDrawable:scope->drawable];
            [command commit];
            const auto t1 = detail::FramePacingTracker::Clock::now();
            pacing.recordPresent(std::chrono::duration<double, std::milli>(t1 - t0).count(), t1);
            ++owner->last;
            scope->discard();
            current.reset();
        }
        return {};
    }

    PresentMode presentMode() const override { return activeMode; }
    FramePacingMode pacingMode() const override { return desc.pacingMode; }
    std::uint32_t maxFrameLatency() const override { return effectiveLatency; }

    Result<void> waitForNextFrame(std::chrono::milliseconds timeout) override {
        if (auto ok = check(); !ok) return ok;
        if (timeout.count() < 0 || timeout.count() > 4294967294ll)
            return fail(ErrorCode::InvalidArgument, "SwapChain wait timeout outside finite range");
        const auto t0 = detail::FramePacingTracker::Clock::now();
        const auto deadline = t0 + timeout;
        while (owner->pending.size() >= effectiveLatency) {
            if (auto ok = owner->pollQueue(QueueType::Graphics); !ok) return ok;
            if (owner->pending.size() < effectiveLatency) break;
            if (detail::FramePacingTracker::Clock::now() >= deadline) {
                pacing.recordStall();
                return fail(ErrorCode::Timeout, "Metal swapchain frame latency wait timed out");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto t1 = detail::FramePacingTracker::Clock::now();
        pacing.recordAcquireWait(std::chrono::duration<double, std::milli>(t1 - t0).count(), t0);
        return {};
    }

    Result<FramePacingStats> framePacingStats() const override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        return pacing.snapshot();
    }

    Result<void> resetFramePacingStats() override {
        if (auto ok = owner->check(); !ok) return ok;
        pacing.reset();
        return {};
    }
    Format format() const override { return activeFormat; }
    ColorSpace colorSpace() const override { return activeColorSpace; }
    Result<void> setHdrMetadata(const HdrMetadata& metadata) override {
        if (auto ok = owner->check(); !ok) return ok;
        if (auto ok = detail::validateHdrMetadata(metadata); !ok) return ok;
        return fail(ErrorCode::Unsupported, "Static HDR10 metadata is unsupported on Metal CAMetalLayer");
    }
    std::optional<HdrMetadata> hdrMetadata() const override { return std::nullopt; }
    std::uint32_t width() const override { return desc.width; }
    std::uint32_t height() const override { return desc.height; }
    Result<SurfaceMetrics> surfaceMetrics() const override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        CAMetalLayer* layer = scope->layer;
        if (!layer) return fail(ErrorCode::InvalidState, "CAMetalLayer is no longer valid");
        const double scale = layer.contentsScale > 0.0 ? static_cast<double>(layer.contentsScale) : 1.0;
        const CGSize bounds = layer.bounds.size;
        const CGSize drawable = layer.drawableSize;
        const std::uint32_t w = drawable.width > 0.0 ? static_cast<std::uint32_t>(std::llround(drawable.width))
                                                     : static_cast<std::uint32_t>(std::llround(bounds.width * scale));
        const std::uint32_t h = drawable.height > 0.0 ? static_cast<std::uint32_t>(std::llround(drawable.height))
                                                      : static_cast<std::uint32_t>(std::llround(bounds.height * scale));
        SurfaceMetrics out{};
        out.clientWidth = w;
        out.clientHeight = h;
        out.dpiScale = static_cast<float>(scale);
        out.dpi = static_cast<std::uint32_t>(std::llround(96.0 * scale));
        out.minimized = (w == 0u || h == 0u);
        out.occluded = false;
        return out;
    }
    Result<bool> syncExtent() override {
        auto m = surfaceMetrics(); if (!m) return std::unexpected(m.error());
        if (m->minimized) return fail(ErrorCode::InvalidState, "Surface is minimized; defer rendering until restored");
        if (m->clientWidth == desc.width && m->clientHeight == desc.height) return false;
        if (auto ok = resize(m->clientWidth, m->clientHeight); !ok) return std::unexpected(ok.error());
        return true;
    }
    Result<void> presentDirtyRects(std::span<const DirtyRect> rects) override {
        if (auto ok = check(); !ok) return ok;
        if (auto ok = detail::validateDirtyRects(rects, desc.width, desc.height, 64u); !ok) return ok;
        return fail(ErrorCode::Unsupported, "Metal CAMetalLayer does not expose dirty-rect partial presentation");
    }

    Result<void> resize(std::uint32_t width, std::uint32_t height) override {
        if (auto ok = check(); !ok) return ok;
        if (!validDrawableExtent(width, height))
            return fail(ErrorCode::InvalidArgument, "Invalid Metal drawable extent");
        if (current && current.use_count() != 1)
            return fail(ErrorCode::InvalidState, "Release the acquired Metal drawable before resizing");
        if (auto ok = MetalQueue(owner).waitIdle(); !ok) return ok;
        @autoreleasepool {
            scope->discard();
            current.reset();
            scope->layer.drawableSize = CGSizeMake(width, height);
            desc.width = width;
            desc.height = height;
        }
        return {};
    }
};

Result<SurfacePresentCapabilities> queryMetalSurfacePresentCapabilities(State& owner, Surface surface) {
    auto layer = checkedLayer(owner, surface);
    if (!layer) return std::unexpected(layer.error());
    SurfacePresentCapabilities caps{};
    caps.fifo = true;
    caps.immediate = true;
    caps.mailbox = false;
    caps.tearing = false;
    caps.frameLatencyWaitable = true;
    caps.minImageCount = 2u;
    caps.maxImageCount = 3u;
    caps.partialPresent = false;
    caps.maxDirtyRects = 0u;
    return caps;
}

Result<SurfaceColorCapabilities> queryMetalSurfaceColorCapabilities(State& owner, Surface surface) {
    auto layer = checkedLayer(owner, surface);
    if (!layer) return std::unexpected(layer.error());
    SurfaceColorCapabilities out{};
    out.supportedFormats = {
        {Format::BGRA8UNorm, ColorSpace::SrgbNonlinear},
        {Format::BGRA8SRGB, ColorSpace::SrgbNonlinear},
        {Format::RGB10A2UNorm, ColorSpace::SrgbNonlinear},
        {Format::BGRA8UNorm, ColorSpace::DisplayP3Nonlinear},
        {Format::BGRA8SRGB, ColorSpace::DisplayP3Nonlinear},
        {Format::RGB10A2UNorm, ColorSpace::DisplayP3Nonlinear},
        {Format::RGB10A2UNorm, ColorSpace::Hdr10St2084},
        {Format::RGBA16Float, ColorSpace::LinearSrgb},
        {Format::RGBA16Float, ColorSpace::ExtendedLinearScRgb},
        {Format::RGBA16Float, ColorSpace::Rec2020Linear},
    };
    out.srgbNonlinear = true;
    out.linearSrgb = true;
    out.displayP3 = true;
    out.rec2020Linear = true;
    out.hdr10St2084 = true;
    out.extendedLinearScRgb = true;
    out.tenBitOutput = true;
    out.hdrMetadata = false;
    out.activeDisplayColorSpace = ColorSpace::SrgbNonlinear;
    out.displayHdrEnabled = false;
    return out;
}

Result<SurfaceMetrics> queryMetalSurfaceMetrics(State& owner, Surface surface) {
    auto layer = checkedLayer(owner, surface);
    if (!layer) return std::unexpected(layer.error());
    const double scale = (*layer).contentsScale > 0.0 ? static_cast<double>((*layer).contentsScale) : 1.0;
    const CGSize bounds = (*layer).bounds.size;
    const CGSize drawable = (*layer).drawableSize;
    const std::uint32_t w = drawable.width > 0.0 ? static_cast<std::uint32_t>(std::llround(drawable.width))
                                                 : static_cast<std::uint32_t>(std::llround(bounds.width * scale));
    const std::uint32_t h = drawable.height > 0.0 ? static_cast<std::uint32_t>(std::llround(drawable.height))
                                                  : static_cast<std::uint32_t>(std::llround(bounds.height * scale));
    SurfaceMetrics out{};
    out.clientWidth = w;
    out.clientHeight = h;
    out.dpiScale = static_cast<float>(scale);
    out.dpi = static_cast<std::uint32_t>(std::llround(96.0 * scale));
    out.minimized = (w == 0u || h == 0u);
    out.occluded = false;
    return out;
}

Result<std::unique_ptr<SwapChain>> makeMetalSwapChain(const std::shared_ptr<State>& owner,
                                                     Surface surface, SwapChainDesc desc) {
    auto layer = checkedLayer(*owner, surface);
    if (!layer) return std::unexpected(layer.error());
    if (!validDrawableExtent(desc.width, desc.height) || (desc.buffers != 2 && desc.buffers != 3))
        return fail(ErrorCode::InvalidArgument, "Metal swapchain requires a positive extent and two or three drawables");
    auto latency = detail::resolveFrameLatency(desc);
    if (!latency) return std::unexpected(latency.error());
    const PresentMode mode = desc.presentMode.value_or(desc.vsync ? PresentMode::Fifo : PresentMode::Immediate);
    if (mode == PresentMode::Mailbox || mode == PresentMode::Tearing)
        return fail(ErrorCode::Unsupported, "Metal CAMetalLayer supports Fifo and Immediate present modes only");
    if (desc.hdrMetadata)
        return fail(ErrorCode::Unsupported, "Static HDR10 metadata is unsupported on Metal CAMetalLayer");
    const Format targetFormat = desc.format.value_or(
        desc.colorSpace == ColorSpace::Hdr10St2084 ? Format::RGB10A2UNorm
        : (desc.colorSpace == ColorSpace::ExtendedLinearScRgb || desc.colorSpace == ColorSpace::LinearSrgb ||
           desc.colorSpace == ColorSpace::Rec2020Linear) ? Format::RGBA16Float
        : Format::BGRA8UNorm);
    if (!validMetalSwapPair(targetFormat, desc.colorSpace))
        return fail(ErrorCode::Unsupported, "Requested Metal swapchain format and color space pair is unsupported");
    const MTLPixelFormat mtlFmt = pixelFormat(targetFormat);
    if (mtlFmt == MTLPixelFormatInvalid)
        return fail(ErrorCode::Unsupported, "Requested Metal swapchain pixel format is unsupported");
    if (objc_getAssociatedObject(*layer, &layerLeaseKey))
        return fail(ErrorCode::InvalidState, "CAMetalLayer already belongs to a live Metal swapchain");
    auto chain = std::make_unique<MetalSwapChain>();
    chain->owner = owner;
    chain->desc = desc;
    chain->activeMode = mode;
    chain->activeFormat = targetFormat;
    chain->nativeFormat = mtlFmt;
    chain->activeColorSpace = desc.colorSpace;
    chain->effectiveLatency = *latency;
    chain->pacing.configure(mode, desc.pacingMode, *latency, desc.stallThresholdMilliseconds);
    std::erase_if(owner->surfaces, [](const auto& item) { return item.expired(); });
    owner->surfaces.push_back(chain->scope);
    @autoreleasepool {
        chain->scope->layer = *layer;
        objc_setAssociatedObject(*layer, &layerLeaseKey, @YES, OBJC_ASSOCIATION_RETAIN);
        (*layer).device = owner->device;
        (*layer).pixelFormat = mtlFmt;
        (*layer).framebufferOnly = NO;
        (*layer).presentsWithTransaction = NO;
        (*layer).allowsNextDrawableTimeout = YES;
        (*layer).maximumDrawableCount = desc.buffers;
        (*layer).displaySyncEnabled = (mode == PresentMode::Fifo);
        (*layer).drawableSize = CGSizeMake(desc.width, desc.height);
        const bool edr = desc.colorSpace == ColorSpace::Hdr10St2084 ||
                         desc.colorSpace == ColorSpace::ExtendedLinearScRgb ||
                         desc.colorSpace == ColorSpace::Rec2020Linear;
        (*layer).wantsExtendedDynamicRangeContent = edr ? YES : NO;
        CGColorSpaceRef csRef = CGColorSpaceCreateWithName(metalColorSpaceName(desc.colorSpace));
        if (csRef) {
            (*layer).colorspace = csRef;
            CGColorSpaceRelease(csRef);
        }
    }
    return std::unique_ptr<SwapChain>(std::move(chain));
}
