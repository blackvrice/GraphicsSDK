// Private implementation included inside Vulkan.cpp's anonymous namespace.
// Win32 WSI deliberately uses host-fenced acquisition; no throughput claim.
Result<std::shared_ptr<NativeSurface>> makeSurface(const std::shared_ptr<State>& owner, Surface surface) {
    auto& in = *owner->native->instance;
#ifdef _WIN32
    if (surface.kind == SurfaceKind::Xcb) return fail(ErrorCode::Unsupported, "XCB surfaces require Linux XCB support");
    DWORD process{}; const auto thread = GetWindowThreadProcessId(static_cast<HWND>(surface.window), &process);
    if (surface.kind != SurfaceKind::Win32 || !surface.window || surface.connection || surface.windowId ||
        !IsWindow(static_cast<HWND>(surface.window)) || thread != GetCurrentThreadId() || process != GetCurrentProcessId())
        return fail(ErrorCode::InvalidArgument, "Surface requires a live Win32 window on the creating thread");
    if (!in.presentation) return fail(ErrorCode::Unsupported, "Win32 Vulkan surface extensions unavailable");
    auto value = std::make_shared<NativeSurface>(); value->native = owner->native; value->window = surface.window;
    VkWin32SurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR}; info.hinstance = GetModuleHandleW(nullptr); info.hwnd = static_cast<HWND>(surface.window);
    if (auto ok = owner->accept(vulkan::acquireOutput(value->handle, in.CreateWin32SurfaceKHR, in.handle, &info, nullptr), "Create Win32 surface"); !ok) return std::unexpected(ok.error());
#elif defined(GFX_HAS_VULKAN_XCB)
    if (surface.kind == SurfaceKind::Win32) return fail(ErrorCode::Unsupported, "Win32 surfaces require Windows");
    if (surface.kind != SurfaceKind::Xcb || surface.window || !surface.connection || !surface.windowId)
        return fail(ErrorCode::InvalidArgument, "XCB surface requires connection and 32-bit window ID only");
    if (!in.presentation) return fail(ErrorCode::Unsupported, "Linux XCB Vulkan surface extensions or procedures unavailable");
    auto window = xcbWindow(surface.connection, surface.windowId);
    if (!window) return std::unexpected(window.error());
    auto connection = static_cast<xcb_connection_t*>(surface.connection);
    auto value = std::make_shared<NativeSurface>(); value->native = owner->native;
    value->connection = surface.connection; value->windowId = surface.windowId;
    VkXcbSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR}; info.connection = connection; info.window = surface.windowId;
    if (auto ok = owner->accept(vulkan::acquireOutput(value->handle, in.CreateXcbSurfaceKHR, in.handle, &info, nullptr), "Create XCB surface"); !ok) return std::unexpected(ok.error());
    const auto visualSupported = in.GetPhysicalDeviceXcbPresentationSupportKHR(owner->physical, owner->family, connection, window->visual);
#else
    (void)owner; (void)surface; (void)in;
    return fail(ErrorCode::Unsupported, "Native surface support requires Win32 or GFX_VULKAN_XCB");
#endif
#if defined(_WIN32) || defined(GFX_HAS_VULKAN_XCB)
    VkBool32 supported{};
    if (auto ok = owner->accept(in.GetPhysicalDeviceSurfaceSupportKHR(owner->physical, owner->family, value->handle, &supported), "Query surface queue support"); !ok) return std::unexpected(ok.error());
#ifdef GFX_HAS_VULKAN_XCB
    if (!visualSupported) return fail(ErrorCode::Unsupported, "Selected graphics queue cannot present to the XCB visual");
#endif
    if (!supported) return fail(ErrorCode::Unsupported, "Selected graphics queue cannot present to this surface");
    return value;
#endif
}

struct VulkanSwapchain final : SwapChain {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeSurface> surface;
    std::shared_ptr<NativeSwapchain> native;
    std::shared_ptr<int> token;
    SwapChainDesc desc;
    std::vector<std::shared_ptr<VulkanTexture>> buffers;
    Result<void> surfaceResult(VkResult result, const char* operation) {
        if (result == VK_ERROR_SURFACE_LOST_KHR) {
            surface->lost = true;
            if (native) native->invalid = true;
            return fail(ErrorCode::InvalidState, "Surface lost; create a new swapchain with a new native surface", result);
        }
        return owner->accept(result, operation);
    }
    ~VulkanSwapchain() override {
        if (!owner->retired) {
            owner->native->DeviceWaitIdle(owner->native->handle);
            (void)owner->poll();
        }
        if (native) native->alive = false;
        buffers.clear();
    }
    Result<VkSwapchainCreateInfoKHR> configuration(std::uint32_t width, std::uint32_t height) {
        if (surface->lost) return fail(ErrorCode::InvalidState, "Surface lost; create a new swapchain with a new native surface", VK_ERROR_SURFACE_LOST_KHR);
        if (!surfaceWindowReady(*surface)) return fail(ErrorCode::InvalidState, "Swapchain window expired; create a new swapchain");
        if (!width || !height || desc.buffers < 2 || desc.buffers > 4)
            return fail(ErrorCode::InvalidArgument, "Swapchain requires nonzero extent and 2..4 requested buffers");
#ifdef _WIN32
        if (!IsWindow(static_cast<HWND>(surface->window))) return fail(ErrorCode::InvalidState, "Swapchain window expired");
#elif defined(GFX_HAS_VULKAN_XCB)
        auto window = xcbWindow(surface->connection, surface->windowId);
        if (!window) return fail(ErrorCode::InvalidState, "Swapchain XCB window unavailable");
        if (window->width != width || window->height != height) return fail(ErrorCode::InvalidArgument, "Swapchain extent must match XCB window geometry");
#endif
        auto& in = *owner->native->instance;
        VkSurfaceCapabilitiesKHR caps{};
        if (auto ok = surfaceResult(in.GetPhysicalDeviceSurfaceCapabilitiesKHR(owner->physical, surface->handle, &caps), "Query surface capabilities"); !ok) return std::unexpected(ok.error());
        if ((caps.currentExtent.width != UINT32_MAX && (width != caps.currentExtent.width || height != caps.currentExtent.height)) ||
            width < caps.minImageExtent.width || width > caps.maxImageExtent.width || height < caps.minImageExtent.height || height > caps.maxImageExtent.height ||
            width > owner->maxFramebufferWidth || height > owner->maxFramebufferHeight || width > (UINT32_MAX - 255u) / 4u)
            return fail(ErrorCode::InvalidArgument, "Requested extent must match the native window client extent and surface limits");
        constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if ((caps.supportedUsageFlags & usage) != usage) return fail(ErrorCode::Unsupported, "Surface requires color attachment and transfer support");
        std::uint32_t count{};
        auto r = in.GetPhysicalDeviceSurfaceFormatsKHR(owner->physical, surface->handle, &count, nullptr);
        if (auto ok = surfaceResult(r, "Query surface formats"); !ok) return std::unexpected(ok.error());
        std::vector<VkSurfaceFormatKHR> formats(count);
        r = in.GetPhysicalDeviceSurfaceFormatsKHR(owner->physical, surface->handle, &count, formats.data());
        if (auto ok = surfaceResult(r, "Query surface formats"); !ok) return std::unexpected(ok.error());
        formats.resize(count);
        auto imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
        auto supports = [&](VkFormat format) { return std::any_of(formats.begin(), formats.end(), [&](auto f) { return (f.format == format || f.format == VK_FORMAT_UNDEFINED) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; }); };
        if (!supports(imageFormat)) {
#ifdef GFX_HAS_VULKAN_XCB
            imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
            if (!supports(imageFormat))
#endif
                return fail(ErrorCode::Unsupported, "Surface requires supported UNORM with SRGB nonlinear color space");
        }
        r = in.GetPhysicalDeviceSurfacePresentModesKHR(owner->physical, surface->handle, &count, nullptr);
        if (auto ok = surfaceResult(r, "Query present modes"); !ok) return std::unexpected(ok.error());
        std::vector<VkPresentModeKHR> modes(count);
        r = in.GetPhysicalDeviceSurfacePresentModesKHR(owner->physical, surface->handle, &count, modes.data());
        if (auto ok = surfaceResult(r, "Query present modes"); !ok) return std::unexpected(ok.error());
        modes.resize(count);
        auto mode = VK_PRESENT_MODE_FIFO_KHR;
        if (!desc.vsync) {
            if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) != modes.end()) mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) mode = VK_PRESENT_MODE_MAILBOX_KHR;
            else return fail(ErrorCode::Unsupported, "Surface has no unthrottled present mode");
        }
        // Maintenance1 image-count limits can depend on the selected present mode.
        VkSurfacePresentModeEXT selectedMode{VK_STRUCTURE_TYPE_SURFACE_PRESENT_MODE_EXT}; selectedMode.presentMode = mode;
        VkPhysicalDeviceSurfaceInfo2KHR query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR}; query.surface = surface->handle; query.pNext = &selectedMode;
        VkSurfaceCapabilities2KHR selectedCaps{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};
        if (auto ok = surfaceResult(in.GetPhysicalDeviceSurfaceCapabilities2KHR(owner->physical, &query, &selectedCaps), "Query selected present-mode capabilities"); !ok) return std::unexpected(ok.error());
        caps.minImageCount = std::max(caps.minImageCount, selectedCaps.surfaceCapabilities.minImageCount);
        const auto selectedMax = selectedCaps.surfaceCapabilities.maxImageCount;
        if (selectedMax) caps.maxImageCount = caps.maxImageCount ? std::min(caps.maxImageCount, selectedMax) : selectedMax;
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface->handle; info.minImageCount = std::max(desc.buffers, caps.minImageCount);
        if (caps.maxImageCount && info.minImageCount > caps.maxImageCount) return fail(ErrorCode::Unsupported, "Requested image count exceeds surface maximum");
        info.imageFormat = imageFormat; info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        info.imageExtent = {width, height}; info.imageArrayLayers = 1; info.imageUsage = usage;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; info.preTransform = caps.currentTransform;
        for (auto a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR})
            if (caps.supportedCompositeAlpha & a) { info.compositeAlpha = a; break; }
        info.presentMode = mode; info.clipped = VK_TRUE; return info;
    }
    Result<void> rebuild(const VkSwapchainCreateInfoKHR& configuration) {
        auto next = std::make_shared<NativeSwapchain>(); next->surface = surface;
        auto info = configuration; info.oldSwapchain = native ? native->handle : VkSwapchainKHR{};
        VkSwapchainPresentModesCreateInfoEXT modes{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT};
        modes.presentModeCount = 1; modes.pPresentModes = &info.presentMode; info.pNext = &modes;
        // Calling native recreation retires oldSwapchain even when creation fails.
        if (native) native->alive = false;
        buffers.clear();
        auto& n = *owner->native;
        auto r = vulkan::acquireOutput(next->handle, n.CreateSwapchainKHR, n.handle, &info, nullptr);
        native.reset();
        if (r != VK_SUCCESS) return surfaceResult(r, "vkCreateSwapchainKHR (retry resize after failure)");
        std::uint32_t count{};
        r = n.GetSwapchainImagesKHR(n.handle, next->handle, &count, nullptr);
        if (r != VK_SUCCESS) return owner->accept(r, "Get swapchain image count");
        std::vector<VkImage> images(count);
        r = n.GetSwapchainImagesKHR(n.handle, next->handle, &count, images.data());
        if (r != VK_SUCCESS) return owner->accept(r, "Get swapchain images");
        images.resize(count); next->finished.resize(count);
        std::vector<std::shared_ptr<VulkanTexture>> created;
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        std::erase_if(owner->swapchains, [](auto& w) { return w.expired(); }); owner->swapchains.push_back(next);
        for (std::uint32_t i = 0; i < count; ++i) {
            auto a = std::make_shared<Allocation>(); a->native = owner->native; a->swapchain = next; a->imageIndex = i; a->image = images[i];
            a->textureDesc = {info.imageExtent.width, info.imageExtent.height, info.imageFormat == VK_FORMAT_B8G8R8A8_UNORM ? Format::BGRA8UNorm : Format::RGBA8UNorm, true}; a->state = ResourceState::Present;
            const auto row = info.imageExtent.width * 4, pitch = (row + 255u) & ~255u;
            a->footprint = {std::uint64_t(pitch) * info.imageExtent.height, pitch, row, info.imageExtent.height};
            VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image = a->image; view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = info.imageFormat; view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (auto ok = owner->accept(vulkan::acquireOutput(a->view, n.CreateImageView, n.handle, &view, nullptr), "Swapchain image view"); !ok) return ok;
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            if (auto ok = owner->accept(vulkan::acquireOutput(next->finished[i], n.CreateSemaphore, n.handle, &semaphore, nullptr), "Present semaphore"); !ok) return ok;
            auto t = std::make_shared<VulkanTexture>(); t->owner = owner; t->allocation = a; t->token = std::make_shared<int>();
            owner->allocations.push_back(a); owner->objects.push_back(t->token); created.push_back(std::move(t));
        }
        native = std::move(next); buffers = std::move(created); desc.width = info.imageExtent.width; desc.height = info.imageExtent.height;
        return {};
    }
    Result<TexturePtr> acquire() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (surface->lost) return fail(ErrorCode::InvalidState, "Surface lost; create a new swapchain with a new native surface", VK_ERROR_SURFACE_LOST_KHR);
        if (!surfaceWindowReady(*surface)) return fail(ErrorCode::InvalidState, "Swapchain window expired; create a new swapchain");
        if (!native || !native->alive || native->invalid) return fail(ErrorCode::InvalidState, "Swapchain requires resize");
        if (!native->acquired) {
            auto& n = *owner->native; VkFence fence{}; VkFenceCreateInfo ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            if (auto ok = owner->accept(vulkan::acquireOutput(fence, n.CreateFence, n.handle, &ci, nullptr), "Acquire fence"); !ok) return std::unexpected(ok.error());
            auto r = n.AcquireNextImageKHR(n.handle, native->handle, 1000000000ull, {}, fence, &native->current);
            if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
                r = n.WaitForFences(n.handle, 1, &fence, VK_TRUE, UINT64_MAX);
                native->acquired = r == VK_SUCCESS;
            } else if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR) {
                native->invalid = true;
                if (r == VK_ERROR_SURFACE_LOST_KHR) surface->lost = true;
            }
            n.DestroyFence(n.handle, fence, nullptr);
            if (r != VK_SUCCESS) {
                if (surface->lost) return std::unexpected(surfaceResult(r, "Acquire surface lost").error());
                if (native->invalid) return fail(ErrorCode::InvalidState, "Surface out of date; resize required", r);
                if (r == VK_NOT_READY) return fail(ErrorCode::Timeout, "Acquire image not ready", r);
                return std::unexpected(owner->accept(r, "vkAcquireNextImageKHR / acquire wait").error());
            }
            auto& a = *buffers[native->current]->allocation;
            if (!a.imageInitialized) {
                native->invalid = true; // Partial initialization requires explicit recreation.
                // Establish Present layout for a valid present-without-rendering call.
                // The acquisition fence has completed all presentation reads of this image.
                Recording init; init.native = owner->native;
                VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex = owner->family;
                if (auto ok = owner->accept(vulkan::acquireOutput(init.pool, n.CreateCommandPool, n.handle, &pool, nullptr), "Initial present pool"); !ok) return std::unexpected(ok.error());
                VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; allocate.commandPool = init.pool; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
                if (auto ok = owner->accept(vulkan::acquireOutput(init.command, n.AllocateCommandBuffers, n.handle, &allocate), "Initial present command"); !ok) return std::unexpected(ok.error());
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                if (auto ok = owner->accept(n.BeginCommandBuffer(init.command, &begin), "Initial present begin"); !ok) return std::unexpected(ok.error());
                VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.image = a.image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                n.CmdPipelineBarrier(init.command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                if (auto ok = owner->accept(n.EndCommandBuffer(init.command), "Initial present end"); !ok) return std::unexpected(ok.error());
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount = 1; submit.pCommandBuffers = &init.command;
                if (auto ok = owner->accept(n.QueueSubmit(n.queue, 1, &submit, {}), "Initial present submit"); !ok) return std::unexpected(ok.error());
                if (auto ok = owner->accept(n.QueueWaitIdle(n.queue), "Initial present wait"); !ok) return std::unexpected(ok.error());
                a.imageInitialized = true;
                native->invalid = false;
            }
        }
        return TexturePtr(buffers[native->current]);
    }
    Result<void> present() override {
        auto image = acquire(); if (!image) return std::unexpected(image.error());
        auto& a = *buffers[native->current]->allocation;
        if (a.state != ResourceState::Present || a.version == UINT64_MAX) return fail(ErrorCode::InvalidState, "Present requires submitted Present state and available version");
        auto& n = *owner->native;
        VkFence fence{}; VkFenceCreateInfo ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (auto ok = owner->accept(vulkan::acquireOutput(fence, n.CreateFence, n.handle, &ci, nullptr), "Present completion fence"); !ok) return ok;
        // One semaphore per native image, reused only after reacquiring that image.
        // An empty ordered submission signals completion of all earlier SDK rendering.
        auto semaphore = native->finished[native->current]; VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &semaphore;
        if (auto ok = owner->accept(n.QueueSubmit(n.queue, 1, &submit, {}), "Signal presentation completion"); !ok) { n.DestroyFence(n.handle, fence, nullptr); return ok; }
        VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR}; info.waitSemaphoreCount = 1; info.pWaitSemaphores = &semaphore;
        info.swapchainCount = 1; info.pSwapchains = &native->handle; info.pImageIndices = &native->current;
        VkSwapchainPresentFenceInfoEXT completion{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT}; completion.swapchainCount = 1; completion.pFences = &fence;
        info.pNext = &completion;
        auto r = n.QueuePresentKHR(n.queue, &info); ++a.version; native->acquired = false;
        if (r == VK_ERROR_SURFACE_LOST_KHR) { surface->lost = true; native->invalid = true; }
        // Completion here permits safe semaphore/swapchain destruction even without
        // another acquisition. WaitIdle alone cannot prove presentation completion.
        auto waited = VK_SUCCESS;
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR)
            waited = n.WaitForFences(n.handle, 1, &fence, VK_TRUE, UINT64_MAX);
        n.DestroyFence(n.handle, fence, nullptr);
        if (auto ok = owner->accept(waited, "Wait presentation resource completion"); !ok) { native->invalid = true; return ok; }
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) return {};
        native->invalid = true;
        if (r == VK_ERROR_SURFACE_LOST_KHR) return surfaceResult(r, "Present surface lost");
        if (r == VK_ERROR_OUT_OF_DATE_KHR) return fail(ErrorCode::InvalidState, "Surface out of date; resize required", r);
        return owner->accept(r, "vkQueuePresentKHR");
    }
    Result<void> resize(std::uint32_t width, std::uint32_t height) override {
        if (auto ok = owner->poll(); !ok) return ok;
        auto info = configuration(width, height); if (!info) return std::unexpected(info.error());
        for (auto& t : buffers) if (t.use_count() != 1)
            return fail(ErrorCode::InvalidState, "Release backbuffer references before resizing");
        if (auto ok = owner->accept(owner->native->DeviceWaitIdle(owner->native->handle), "Resize wait"); !ok) return ok;
        if (auto ok = owner->poll(); !ok) return ok;
        // A pending SDK submission can be the allocation's only extra owner.
        // Drain it before distinguishing retained public recordings from queued work.
        for (auto& t : buffers) if (t->allocation.use_count() != 1)
            return fail(ErrorCode::InvalidState, "Release backbuffer recordings before resizing");
        return rebuild(*info);
    }
};
Result<std::unique_ptr<SwapChain>> makeSwapchain(const std::shared_ptr<State>& owner, Surface surface, SwapChainDesc desc) {
#if defined(_WIN32) || defined(GFX_HAS_VULKAN_XCB)
    if (!owner->native->presentation) return fail(ErrorCode::Unsupported, "Vulkan presentation requires surface support and swapchainMaintenance1 completion fences");
    auto prepared = makeSurface(owner, surface); if (!prepared) return std::unexpected(prepared.error());
    auto value = std::make_unique<VulkanSwapchain>(); value->owner = owner; value->desc = desc; value->token = std::make_shared<int>();
    value->surface = std::move(*prepared);
    auto config = value->configuration(desc.width, desc.height); if (!config) return std::unexpected(config.error());
    if (auto ok = value->rebuild(*config); !ok) return std::unexpected(ok.error());
    owner->objects.push_back(value->token); return std::unique_ptr<SwapChain>(std::move(value));
#else
    (void)owner; (void)surface; (void)desc;
    return fail(ErrorCode::Unsupported, "Vulkan presentation currently requires Win32");
#endif
}
