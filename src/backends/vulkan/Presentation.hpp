// Private implementation included inside Vulkan.cpp's anonymous namespace.
// Win32 WSI deliberately uses host-fenced acquisition; no throughput claim.
Result<std::shared_ptr<NativeSurface>> makeSurface(const std::shared_ptr<State>& owner, Surface surface) {
    if (surface.kind == SurfaceKind::MetalLayer) return fail(ErrorCode::Unsupported, "CAMetalLayer surfaces require Metal");
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

std::optional<VkColorSpaceKHR> vkColorSpaceFor(ColorSpace cs, bool extColorSpace) {
    switch (cs) {
    case ColorSpace::SrgbNonlinear: return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    case ColorSpace::DisplayP3Nonlinear:
        return extColorSpace ? std::optional<VkColorSpaceKHR>{VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT} : std::nullopt;
    case ColorSpace::Rec2020Linear:
        return extColorSpace ? std::optional<VkColorSpaceKHR>{VK_COLOR_SPACE_BT2020_LINEAR_EXT} : std::nullopt;
    case ColorSpace::Hdr10St2084:
        return extColorSpace ? std::optional<VkColorSpaceKHR>{VK_COLOR_SPACE_HDR10_ST2084_EXT} : std::nullopt;
    case ColorSpace::LinearSrgb:
    case ColorSpace::ExtendedLinearScRgb:
        return extColorSpace ? std::optional<VkColorSpaceKHR>{VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT} : std::nullopt;
    }
    return std::nullopt;
}
std::optional<Format> fromVkSwapFormat(VkFormat f) {
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8UNorm;
    case VK_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8UNorm;
    case VK_FORMAT_R8G8B8A8_SRGB: return Format::RGBA8SRGB;
    case VK_FORMAT_B8G8R8A8_SRGB: return Format::BGRA8SRGB;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return Format::RGB10A2UNorm;
    case VK_FORMAT_R16G16B16A16_SFLOAT: return Format::RGBA16Float;
    default: return std::nullopt;
    }
}

Result<SurfaceMetrics> queryNativeSurfaceMetrics(const NativeSurface& s) {
#ifdef _WIN32
    HWND hwnd = static_cast<HWND>(s.window);
    if (!hwnd || !IsWindow(hwnd)) {
        return fail(ErrorCode::InvalidState, "Swapchain window expired; create a new swapchain");
    }
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) {
        return fail(ErrorCode::BackendFailure, "GetClientRect failed");
    }
    const std::uint32_t w = rc.right > rc.left ? static_cast<std::uint32_t>(rc.right - rc.left) : 0u;
    const std::uint32_t h = rc.bottom > rc.top ? static_cast<std::uint32_t>(rc.bottom - rc.top) : 0u;
    UINT dpi = 96u;
    using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
    static const auto getDpiForWindow = []() -> GetDpiForWindowFn {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        return user32 ? reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
    }();
    if (getDpiForWindow) {
        const UINT queried = getDpiForWindow(hwnd);
        if (queried > 0u) dpi = queried;
    }
    SurfaceMetrics out{};
    out.clientWidth = w;
    out.clientHeight = h;
    out.dpi = dpi;
    out.dpiScale = static_cast<float>(dpi) / 96.0f;
    out.minimized = (IsIconic(hwnd) != FALSE) || w == 0u || h == 0u;
    out.occluded = false;
    return out;
#elif defined(GFX_HAS_VULKAN_XCB)
    auto window = xcbWindow(s.connection, s.windowId);
    if (!window) return std::unexpected(window.error());
    SurfaceMetrics out{};
    out.clientWidth = window->width;
    out.clientHeight = window->height;
    out.dpi = 96u;
    out.dpiScale = 1.0f;
    out.minimized = (out.clientWidth == 0u || out.clientHeight == 0u);
    out.occluded = false;
    return out;
#else
    (void)s;
    return fail(ErrorCode::Unsupported, "Native surface metrics require Win32 or GFX_VULKAN_XCB");
#endif
}

struct VulkanSwapchain final : SwapChain {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeSurface> surface;
    std::shared_ptr<NativeSwapchain> native;
    std::shared_ptr<int> token;
    SwapChainDesc desc;
    PresentMode activeMode{PresentMode::Fifo};
    Format activeFormat{Format::RGBA8UNorm};
    ColorSpace activeColorSpace{ColorSpace::SrgbNonlinear};
    std::optional<HdrMetadata> activeMetadata;
    std::uint32_t effectiveLatency{1};
    mutable detail::FramePacingTracker pacing;
    std::vector<std::shared_ptr<VulkanTexture>> buffers;
    Result<void> applyMetadataNative(const HdrMetadata& metadata) {
        if (!owner->native->hdrMetadata || !owner->native->SetHdrMetadataEXT) {
            return fail(ErrorCode::Unsupported, "VK_EXT_hdr_metadata is unavailable on this Vulkan device");
        }
        if (!native || !native->handle) {
            return fail(ErrorCode::InvalidState, "Swapchain handle unavailable for HDR metadata");
        }
        VkHdrMetadataEXT hdr{VK_STRUCTURE_TYPE_HDR_METADATA_EXT};
        hdr.displayPrimaryRed = {metadata.redPrimaryX, metadata.redPrimaryY};
        hdr.displayPrimaryGreen = {metadata.greenPrimaryX, metadata.greenPrimaryY};
        hdr.displayPrimaryBlue = {metadata.bluePrimaryX, metadata.bluePrimaryY};
        hdr.whitePoint = {metadata.whitePointX, metadata.whitePointY};
        hdr.maxLuminance = metadata.maxMasteringLuminance;
        hdr.minLuminance = metadata.minMasteringLuminance;
        hdr.maxContentLightLevel = metadata.maxContentLightLevel;
        hdr.maxFrameAverageLightLevel = metadata.maxFrameAverageLightLevel;
        owner->native->SetHdrMetadataEXT(owner->native->handle, 1u, &native->handle, &hdr);
        activeMetadata = metadata;
        return {};
    }
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
        const auto targetVkColorSpace = vkColorSpaceFor(desc.colorSpace, in.swapchainColorSpace);
        if (!targetVkColorSpace) {
            return fail(ErrorCode::Unsupported, "Requested swapchain color space requires VK_EXT_swapchain_colorspace");
        }
        auto supports = [&](VkFormat format, VkColorSpaceKHR cs) {
            return std::any_of(formats.begin(), formats.end(), [&](auto f) {
                return (f.format == format || f.format == VK_FORMAT_UNDEFINED) && f.colorSpace == cs;
            });
        };
        Format selectedFormat = Format::RGBA8UNorm;
        VkFormat imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
        if (desc.format) {
            selectedFormat = *desc.format;
            imageFormat = textureFormat(selectedFormat);
            if (!fromVkSwapFormat(imageFormat) || !supports(imageFormat, *targetVkColorSpace)) {
                return fail(ErrorCode::Unsupported, "Requested swapchain format and color space pair is unsupported by this surface");
            }
        } else if (desc.colorSpace == ColorSpace::Hdr10St2084) {
            selectedFormat = Format::RGB10A2UNorm;
            imageFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            if (!supports(imageFormat, *targetVkColorSpace)) {
                return fail(ErrorCode::Unsupported, "Surface does not support 10-bit HDR10 ST2084 swapchain format");
            }
        } else if (desc.colorSpace == ColorSpace::ExtendedLinearScRgb || desc.colorSpace == ColorSpace::LinearSrgb) {
            selectedFormat = Format::RGBA16Float;
            imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
            if (!supports(imageFormat, *targetVkColorSpace)) {
                return fail(ErrorCode::Unsupported, "Surface does not support RGBA16Float linear/scRGB swapchain format");
            }
        } else {
            selectedFormat = Format::RGBA8UNorm;
            imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
            if (!supports(imageFormat, *targetVkColorSpace)) {
                selectedFormat = Format::BGRA8UNorm;
                imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
                if (!supports(imageFormat, *targetVkColorSpace)) {
                    return fail(ErrorCode::Unsupported, "Surface does not support requested color space with 8-bit UNORM format");
                }
            }
        }
        if (width > (UINT32_MAX - 255u) / texelBytes(selectedFormat)) {
            return fail(ErrorCode::InvalidArgument, "Requested swapchain width overflows row pitch");
        }
        activeFormat = selectedFormat;
        activeColorSpace = desc.colorSpace;
        r = in.GetPhysicalDeviceSurfacePresentModesKHR(owner->physical, surface->handle, &count, nullptr);
        if (auto ok = surfaceResult(r, "Query present modes"); !ok) return std::unexpected(ok.error());
        std::vector<VkPresentModeKHR> modes(count);
        r = in.GetPhysicalDeviceSurfacePresentModesKHR(owner->physical, surface->handle, &count, modes.data());
        if (auto ok = surfaceResult(r, "Query present modes"); !ok) return std::unexpected(ok.error());
        modes.resize(count);
        auto hasMode = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        if (desc.presentMode) {
            const PresentMode requested = *desc.presentMode;
            if (requested == PresentMode::Fifo) {
                if (!hasMode(VK_PRESENT_MODE_FIFO_KHR)) return fail(ErrorCode::Unsupported, "Surface does not support FIFO present mode");
                mode = VK_PRESENT_MODE_FIFO_KHR;
                activeMode = PresentMode::Fifo;
            } else if (requested == PresentMode::Immediate || requested == PresentMode::Tearing) {
                if (!hasMode(VK_PRESENT_MODE_IMMEDIATE_KHR)) return fail(ErrorCode::Unsupported, "Surface does not support Immediate/Tearing present mode");
                mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                activeMode = requested;
            } else if (requested == PresentMode::Mailbox) {
                if (!hasMode(VK_PRESENT_MODE_MAILBOX_KHR)) return fail(ErrorCode::Unsupported, "Surface does not support Mailbox present mode");
                mode = VK_PRESENT_MODE_MAILBOX_KHR;
                activeMode = PresentMode::Mailbox;
            } else {
                return fail(ErrorCode::InvalidArgument, "Unknown PresentMode");
            }
        } else if (!desc.vsync) {
            if (hasMode(VK_PRESENT_MODE_IMMEDIATE_KHR)) { mode = VK_PRESENT_MODE_IMMEDIATE_KHR; activeMode = PresentMode::Immediate; }
            else if (hasMode(VK_PRESENT_MODE_MAILBOX_KHR)) { mode = VK_PRESENT_MODE_MAILBOX_KHR; activeMode = PresentMode::Mailbox; }
            else return fail(ErrorCode::Unsupported, "Surface has no unthrottled present mode");
        } else {
            activeMode = PresentMode::Fifo;
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
        info.imageFormat = imageFormat; info.imageColorSpace = *targetVkColorSpace;
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
            a->textureDesc = {info.imageExtent.width, info.imageExtent.height, activeFormat, true}; a->state = ResourceState::Present;
            const auto row = info.imageExtent.width * texelBytes(activeFormat), pitch = (row + 255u) & ~255u;
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
        if (activeMetadata) { (void)applyMetadataNative(*activeMetadata); }
        return {};
    }
    Result<TexturePtr> acquire() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (surface->lost) return fail(ErrorCode::InvalidState, "Surface lost; create a new swapchain with a new native surface", VK_ERROR_SURFACE_LOST_KHR);
        if (!surfaceWindowReady(*surface)) return fail(ErrorCode::InvalidState, "Swapchain window expired; create a new swapchain");
        if (!native || !native->alive || native->invalid) return fail(ErrorCode::InvalidState, "Swapchain requires resize");
        if (!native->acquired) {
            const auto acqT0 = detail::FramePacingTracker::Clock::now();
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
                if (r == VK_NOT_READY) { pacing.recordStall(); return fail(ErrorCode::Timeout, "Acquire image not ready", r); }
                return std::unexpected(owner->accept(r, "vkAcquireNextImageKHR / acquire wait").error());
            }
            const auto acqT1 = detail::FramePacingTracker::Clock::now();
            pacing.recordAcquireWait(std::chrono::duration<double, std::milli>(acqT1 - acqT0).count(), acqT0);
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
        const auto presT0 = detail::FramePacingTracker::Clock::now();
        auto r = n.QueuePresentKHR(n.queue, &info); ++a.version; native->acquired = false;
        if (r == VK_ERROR_SURFACE_LOST_KHR) { surface->lost = true; native->invalid = true; }
        // Completion here permits safe semaphore/swapchain destruction even without
        // another acquisition. WaitIdle alone cannot prove presentation completion.
        auto waited = VK_SUCCESS;
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR)
            waited = n.WaitForFences(n.handle, 1, &fence, VK_TRUE, UINT64_MAX);
        const auto presT1 = detail::FramePacingTracker::Clock::now();
        n.DestroyFence(n.handle, fence, nullptr);
        if (auto ok = owner->accept(waited, "Wait presentation resource completion"); !ok) { native->invalid = true; return ok; }
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
            pacing.recordPresent(std::chrono::duration<double, std::milli>(presT1 - presT0).count(), presT1);
            return {};
        }
        native->invalid = true;
        if (r == VK_ERROR_SURFACE_LOST_KHR) return surfaceResult(r, "Present surface lost");
        if (r == VK_ERROR_OUT_OF_DATE_KHR) return fail(ErrorCode::InvalidState, "Surface out of date; resize required", r);
        return owner->accept(r, "vkQueuePresentKHR");
    }
    PresentMode presentMode() const override { return activeMode; }
    FramePacingMode pacingMode() const override { return desc.pacingMode; }
    std::uint32_t maxFrameLatency() const override { return effectiveLatency; }
    Result<void> waitForNextFrame(std::chrono::milliseconds timeout) override {
        if (auto ok = owner->poll(); !ok) return ok;
        if (timeout.count() < 0 || timeout.count() > 4294967294ll)
            return fail(ErrorCode::InvalidArgument, "SwapChain wait timeout outside finite range");
        if (surface->lost) return fail(ErrorCode::InvalidState, "Surface lost; create a new swapchain with a new native surface", VK_ERROR_SURFACE_LOST_KHR);
        if (!surfaceWindowReady(*surface)) return fail(ErrorCode::InvalidState, "Swapchain window expired; create a new swapchain");
        if (!native || !native->alive || native->invalid) return fail(ErrorCode::InvalidState, "Swapchain requires resize");
        // Host-fenced acquisition bounds queue depth; drain pending graphics work when
        // in-flight submissions reach effectiveLatency.
        const auto t0 = detail::FramePacingTracker::Clock::now();
        auto& q = owner->queues[0];
        while (q.pending.size() >= effectiveLatency) {
            const auto r = owner->native->WaitForFences(
                owner->native->handle, 1, &q.pending.front().fence, VK_TRUE,
                static_cast<std::uint64_t>(timeout.count()) * 1000000ull);
            if (r == VK_TIMEOUT) {
                pacing.recordStall();
                return fail(ErrorCode::Timeout, "SwapChain frame latency wait timed out", r);
            }
            if (auto ok = owner->accept(r, "SwapChain frame latency wait"); !ok) return ok;
            if (auto ok = owner->pollQueue(QueueType::Graphics); !ok) return ok;
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
        return applyMetadataNative(metadata);
    }
    std::optional<HdrMetadata> hdrMetadata() const override { return activeMetadata; }
    std::uint32_t width() const override { return desc.width; }
    std::uint32_t height() const override { return desc.height; }
    Result<SurfaceMetrics> surfaceMetrics() const override {
        if (auto ok = owner->check(); !ok) return std::unexpected(ok.error());
        if (!surface || surface->lost) return fail(ErrorCode::InvalidState, "Surface lost; create a new swapchain with a new native surface");
        return queryNativeSurfaceMetrics(*surface);
    }
    Result<bool> syncExtent() override {
        auto m = surfaceMetrics(); if (!m) return std::unexpected(m.error());
        if (m->minimized) return fail(ErrorCode::InvalidState, "Surface is minimized; defer rendering until restored");
        if (m->clientWidth == desc.width && m->clientHeight == desc.height) return false;
        if (auto ok = resize(m->clientWidth, m->clientHeight); !ok) return std::unexpected(ok.error());
        return true;
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
Result<SurfacePresentCapabilities> queryVulkanSurfacePresentCapabilities(const std::shared_ptr<State>& owner, Surface surface) {
#if defined(_WIN32) || defined(GFX_HAS_VULKAN_XCB)
    if (!owner->native->presentation) return fail(ErrorCode::Unsupported, "Vulkan presentation requires surface support and swapchainMaintenance1");
    auto prepared = makeSurface(owner, surface); if (!prepared) return std::unexpected(prepared.error());
    auto& in = *owner->native->instance;
    VkSurfaceCapabilitiesKHR caps{};
    if (auto ok = owner->accept(in.GetPhysicalDeviceSurfaceCapabilitiesKHR(owner->physical, (*prepared)->handle, &caps), "Query surface capabilities"); !ok)
        return std::unexpected(ok.error());
    std::uint32_t count{};
    if (auto ok = owner->accept(in.GetPhysicalDeviceSurfacePresentModesKHR(owner->physical, (*prepared)->handle, &count, nullptr), "Query present modes"); !ok)
        return std::unexpected(ok.error());
    std::vector<VkPresentModeKHR> modes(count);
    if (count > 0u) {
        if (auto ok = owner->accept(in.GetPhysicalDeviceSurfacePresentModesKHR(owner->physical, (*prepared)->handle, &count, modes.data()), "Query present modes"); !ok)
            return std::unexpected(ok.error());
        modes.resize(count);
    }
    auto hasMode = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
    SurfacePresentCapabilities out{};
    out.fifo = hasMode(VK_PRESENT_MODE_FIFO_KHR);
    out.immediate = hasMode(VK_PRESENT_MODE_IMMEDIATE_KHR);
    out.mailbox = hasMode(VK_PRESENT_MODE_MAILBOX_KHR);
    out.tearing = out.immediate;
    out.frameLatencyWaitable = true;
    out.minImageCount = caps.minImageCount;
    out.maxImageCount = caps.maxImageCount ? caps.maxImageCount : 4u;
    return out;
#else
    (void)owner; (void)surface;
    return fail(ErrorCode::Unsupported, "Vulkan presentation currently requires Win32 or XCB");
#endif
}
Result<SurfaceColorCapabilities> queryVulkanSurfaceColorCapabilities(const std::shared_ptr<State>& owner, Surface surface) {
#if defined(_WIN32) || defined(GFX_HAS_VULKAN_XCB)
    if (!owner->native->presentation) return fail(ErrorCode::Unsupported, "Vulkan presentation requires surface support and swapchainMaintenance1");
    auto prepared = makeSurface(owner, surface); if (!prepared) return std::unexpected(prepared.error());
    auto& in = *owner->native->instance;
    std::uint32_t count{};
    if (auto ok = owner->accept(in.GetPhysicalDeviceSurfaceFormatsKHR(owner->physical, (*prepared)->handle, &count, nullptr), "Query surface formats"); !ok)
        return std::unexpected(ok.error());
    std::vector<VkSurfaceFormatKHR> formats(count);
    if (count > 0u) {
        if (auto ok = owner->accept(in.GetPhysicalDeviceSurfaceFormatsKHR(owner->physical, (*prepared)->handle, &count, formats.data()), "Query surface formats"); !ok)
            return std::unexpected(ok.error());
        formats.resize(count);
    }
    SurfaceColorCapabilities out{};
    out.srgbNonlinear = false;
    out.hdrMetadata = owner->native->hdrMetadata && owner->native->SetHdrMetadataEXT != nullptr;
    auto pushUnique = [&](Format f, ColorSpace cs) {
        SurfaceColorFormatCapability entry{f, cs};
        if (std::find(out.supportedFormats.begin(), out.supportedFormats.end(), entry) == out.supportedFormats.end()) {
            out.supportedFormats.push_back(entry);
        }
        if (cs == ColorSpace::SrgbNonlinear) out.srgbNonlinear = true;
        if (cs == ColorSpace::LinearSrgb) out.linearSrgb = true;
        if (cs == ColorSpace::DisplayP3Nonlinear) out.displayP3 = true;
        if (cs == ColorSpace::Rec2020Linear) out.rec2020Linear = true;
        if (cs == ColorSpace::Hdr10St2084) out.hdr10St2084 = true;
        if (cs == ColorSpace::ExtendedLinearScRgb) out.extendedLinearScRgb = true;
        if (f == Format::RGB10A2UNorm) out.tenBitOutput = true;
    };
    for (const auto& sf : formats) {
        const auto mappedFmt = fromVkSwapFormat(sf.format);
        if (!mappedFmt) continue;
        if (sf.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            pushUnique(*mappedFmt, ColorSpace::SrgbNonlinear);
        } else if (in.swapchainColorSpace) {
            if (sf.colorSpace == VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT) {
                pushUnique(*mappedFmt, ColorSpace::DisplayP3Nonlinear);
            } else if (sf.colorSpace == VK_COLOR_SPACE_BT2020_LINEAR_EXT) {
                pushUnique(*mappedFmt, ColorSpace::Rec2020Linear);
            } else if (sf.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
                pushUnique(*mappedFmt, ColorSpace::Hdr10St2084);
            } else if (sf.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) {
                pushUnique(*mappedFmt, ColorSpace::LinearSrgb);
                pushUnique(*mappedFmt, ColorSpace::ExtendedLinearScRgb);
            }
        }
    }
    out.activeDisplayColorSpace = ColorSpace::SrgbNonlinear;
    out.displayHdrEnabled = false;
    return out;
#else
    (void)owner; (void)surface;
    return fail(ErrorCode::Unsupported, "Vulkan presentation currently requires Win32 or XCB");
#endif
}
Result<SurfaceMetrics> queryVulkanSurfaceMetrics(const std::shared_ptr<State>& owner, Surface surface) {
#if defined(_WIN32) || defined(GFX_HAS_VULKAN_XCB)
    if (!owner->native->presentation) return fail(ErrorCode::Unsupported, "Vulkan presentation requires surface support and swapchainMaintenance1");
    auto prepared = makeSurface(owner, surface); if (!prepared) return std::unexpected(prepared.error());
    return queryNativeSurfaceMetrics(**prepared);
#else
    (void)owner; (void)surface;
    return fail(ErrorCode::Unsupported, "Vulkan presentation currently requires Win32 or XCB");
#endif
}
Result<std::unique_ptr<SwapChain>> makeSwapchain(const std::shared_ptr<State>& owner, Surface surface, SwapChainDesc desc) {
#if defined(_WIN32) || defined(GFX_HAS_VULKAN_XCB)
    if (!owner->native->presentation) return fail(ErrorCode::Unsupported, "Vulkan presentation requires surface support and swapchainMaintenance1 completion fences");
    auto latency = detail::resolveFrameLatency(desc);
    if (!latency) return std::unexpected(latency.error());
    auto prepared = makeSurface(owner, surface); if (!prepared) return std::unexpected(prepared.error());
    auto value = std::make_unique<VulkanSwapchain>(); value->owner = owner; value->desc = desc; value->effectiveLatency = *latency; value->token = std::make_shared<int>();
    value->surface = std::move(*prepared);
    auto config = value->configuration(desc.width, desc.height); if (!config) return std::unexpected(config.error());
    value->pacing.configure(value->activeMode, desc.pacingMode, *latency, desc.stallThresholdMilliseconds);
    if (auto ok = value->rebuild(*config); !ok) return std::unexpected(ok.error());
    if (desc.hdrMetadata) {
        if (auto ok = value->setHdrMetadata(*desc.hdrMetadata); !ok) return std::unexpected(ok.error());
    }
    owner->objects.push_back(value->token); return std::unique_ptr<SwapChain>(std::move(value));
#else
    (void)owner; (void)surface; (void)desc;
    return fail(ErrorCode::Unsupported, "Vulkan presentation currently requires Win32");
#endif
}
