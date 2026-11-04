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

struct MetalSwapChain final : SwapChain {
    std::shared_ptr<State> owner;
    std::shared_ptr<DrawableScope> scope = std::make_shared<DrawableScope>();
    SwapChainDesc desc;
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
        if (!layer || layer.device != owner->device || layer.pixelFormat != MTLPixelFormatBGRA8Unorm ||
            layer.framebufferOnly || layer.presentsWithTransaction || !layer.allowsNextDrawableTimeout ||
            layer.maximumDrawableCount != desc.buffers || bool(layer.displaySyncEnabled) != desc.vsync ||
            size.width != desc.width || size.height != desc.height)
            return fail(ErrorCode::InvalidState, "CAMetalLayer presentation configuration changed outside the SDK");
        return {};
    }

    Result<TexturePtr> acquire() override {
        if (auto ok = check(); !ok) return std::unexpected(ok.error());
        if (current) return TexturePtr(current);
        @autoreleasepool {
            id<CAMetalDrawable> drawable = [scope->layer nextDrawable];
            if (!drawable) return fail(ErrorCode::Timeout, "CAMetalLayer did not provide a drawable");
            id<MTLTexture> texture = drawable.texture;
            if (!texture || texture.width != desc.width || texture.height != desc.height ||
                texture.pixelFormat != MTLPixelFormatBGRA8Unorm || texture.device != owner->device)
                return fail(ErrorCode::BackendFailure, "CAMetalLayer returned a drawable with unexpected device, extent or format");

            auto allocation = std::make_shared<Allocation>();
            allocation->texture = texture;
            allocation->textureDesc = {desc.width, desc.height, Format::BGRA8UNorm, true};
            const auto rowBytes = desc.width * 4;
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
            // The public API submits rendering before present. The same native queue
            // orders this command after rendering, and waitIdle includes both.
            id<MTLCommandBuffer> command = [owner->queue commandBuffer];
            if (!command) return fail(ErrorCode::OutOfMemory, "Metal presentation command buffer creation failed");
            owner->pending.push_back({owner->last + 1, command, {}, {}, scope->drawable});
            [command presentDrawable:scope->drawable];
            [command commit];
            ++owner->last;
            scope->discard();
            current.reset();
        }
        return {};
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

Result<std::unique_ptr<SwapChain>> makeMetalSwapChain(const std::shared_ptr<State>& owner,
                                                     Surface surface, SwapChainDesc desc) {
    auto layer = checkedLayer(*owner, surface);
    if (!layer) return std::unexpected(layer.error());
    if (!validDrawableExtent(desc.width, desc.height) || (desc.buffers != 2 && desc.buffers != 3))
        return fail(ErrorCode::InvalidArgument, "Metal swapchain requires a positive extent and two or three drawables");
    if (objc_getAssociatedObject(*layer, &layerLeaseKey))
        return fail(ErrorCode::InvalidState, "CAMetalLayer already belongs to a live Metal swapchain");
    auto chain = std::make_unique<MetalSwapChain>();
    chain->owner = owner;
    chain->desc = desc;
    std::erase_if(owner->surfaces, [](const auto& item) { return item.expired(); });
    owner->surfaces.push_back(chain->scope);
    @autoreleasepool {
        chain->scope->layer = *layer;
        objc_setAssociatedObject(*layer, &layerLeaseKey, @YES, OBJC_ASSOCIATION_RETAIN);
        (*layer).device = owner->device;
        (*layer).pixelFormat = MTLPixelFormatBGRA8Unorm;
        (*layer).framebufferOnly = NO;
        (*layer).presentsWithTransaction = NO;
        (*layer).allowsNextDrawableTimeout = YES;
        (*layer).maximumDrawableCount = desc.buffers;
        (*layer).displaySyncEnabled = desc.vsync;
        (*layer).drawableSize = CGSizeMake(desc.width, desc.height);
    }
    return std::unique_ptr<SwapChain>(std::move(chain));
}
