#include <graphics/Graphics.hpp>
#include "../../foundation/DeviceGeneration.hpp"
#include "../../shader/D3D12Shader.hpp"
#include "FenceWait.hpp"
#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace gfx {
Result<std::unique_ptr<Device>> createD3D12Device(DeviceDesc);
namespace {
using Microsoft::WRL::ComPtr;
auto invalid(std::string text, ErrorCode code = ErrorCode::InvalidState) { return std::unexpected(Error{code, std::move(text)}); }
auto failed(HRESULT code, std::string operation) {
    auto kind = code == E_OUTOFMEMORY ? ErrorCode::OutOfMemory :
        code == DXGI_ERROR_DEVICE_REMOVED || code == DXGI_ERROR_DEVICE_RESET || code == DXGI_ERROR_DEVICE_HUNG || code == DXGI_ERROR_DRIVER_INTERNAL_ERROR ? ErrorCode::DeviceLost : ErrorCode::BackendFailure;
    return std::unexpected(Error{kind, std::move(operation), code});
}
AdapterId adapterId(LUID luid) {
    return {Backend::D3D12,(static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart))<<32)|luid.LowPart};
}
LUID adapterLuid(AdapterId id) {
    return {static_cast<DWORD>(id.value),std::bit_cast<LONG>(static_cast<DWORD>(id.value>>32))};
}
Result<AdapterInfo> adapterInfo(IDXGIAdapter1& adapter) {
    DXGI_ADAPTER_DESC1 info{};
    const auto hr=adapter.GetDesc1(&info);
    if (FAILED(hr)) return failed(hr,"Read adapter description failed");
    const auto count=WideCharToMultiByte(CP_UTF8,0,info.Description,-1,nullptr,0,nullptr,nullptr);
    if (count<=0) return failed(E_FAIL,"Adapter name conversion failed");
    std::string name(static_cast<std::size_t>(count),'\0');
    if (WideCharToMultiByte(CP_UTF8,0,info.Description,-1,name.data(),count,nullptr,nullptr)!=count)
        return failed(E_FAIL,"Adapter name conversion failed");
    name.pop_back();
    return AdapterInfo{adapterId(info.AdapterLuid),std::move(name),(info.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0,
                       info.VendorId,info.DeviceId,info.DedicatedVideoMemory,info.SharedSystemMemory};
}
DXGI_FORMAT native(Format format) {
    switch (format) {
    case Format::RGBA8UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::D32Float: return DXGI_FORMAT_D32_FLOAT;
    case Format::BGRA8UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::BC1UNorm: return DXGI_FORMAT_BC1_UNORM;
    case Format::BC2UNorm: return DXGI_FORMAT_BC2_UNORM;
    case Format::BC3UNorm: return DXGI_FORMAT_BC3_UNORM;
    case Format::BC4UNorm: return DXGI_FORMAT_BC4_UNORM;
    case Format::BC5UNorm: return DXGI_FORMAT_BC5_UNORM;
    case Format::BC6HUFloat: return DXGI_FORMAT_BC6H_UF16;
    case Format::BC7UNorm: return DXGI_FORMAT_BC7_UNORM;
    case Format::RGBA8SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::BGRA8SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case Format::RGB10A2UNorm: return DXGI_FORMAT_R10G10B10A2_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
DXGI_FORMAT dxgiSwapChainStorageFormat(Format format) {
    switch (format) {
    case Format::RGBA8UNorm:
    case Format::RGBA8SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::BGRA8UNorm:
    case Format::BGRA8SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::RGB10A2UNorm: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case Format::RGBA16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
std::optional<DXGI_COLOR_SPACE_TYPE> dxgiColorSpaceFor(ColorSpace cs) {
    switch (cs) {
    case ColorSpace::SrgbNonlinear: return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    case ColorSpace::LinearSrgb:
    case ColorSpace::ExtendedLinearScRgb: return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    case ColorSpace::Hdr10St2084: return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    case ColorSpace::DisplayP3Nonlinear:
    case ColorSpace::Rec2020Linear:
        return std::nullopt;
    }
    return std::nullopt;
}
bool validD3D12SwapPair(Format format, ColorSpace cs) {
    switch (cs) {
    case ColorSpace::SrgbNonlinear:
        return format == Format::RGBA8UNorm || format == Format::BGRA8UNorm ||
               format == Format::RGBA8SRGB || format == Format::BGRA8SRGB ||
               format == Format::RGB10A2UNorm;
    case ColorSpace::LinearSrgb:
    case ColorSpace::ExtendedLinearScRgb:
        return format == Format::RGBA16Float;
    case ColorSpace::Hdr10St2084:
        return format == Format::RGB10A2UNorm;
    case ColorSpace::DisplayP3Nonlinear:
    case ColorSpace::Rec2020Linear:
        return false;
    }
    return false;
}
DXGI_HDR_METADATA_HDR10 makeDxgiHdr10Metadata(const HdrMetadata& m) {
    auto chromaticity = [](float v) -> UINT16 {
        const float scaled = std::clamp(v, 0.0f, 1.0f) * 50000.0f;
        return static_cast<UINT16>(std::lround(scaled));
    };
    auto clampU16 = [](float v) -> UINT16 {
        return static_cast<UINT16>(std::clamp<double>(std::round(static_cast<double>(v)), 0.0, 65535.0));
    };
    DXGI_HDR_METADATA_HDR10 out{};
    out.RedPrimary[0] = chromaticity(m.redPrimaryX);
    out.RedPrimary[1] = chromaticity(m.redPrimaryY);
    out.GreenPrimary[0] = chromaticity(m.greenPrimaryX);
    out.GreenPrimary[1] = chromaticity(m.greenPrimaryY);
    out.BluePrimary[0] = chromaticity(m.bluePrimaryX);
    out.BluePrimary[1] = chromaticity(m.bluePrimaryY);
    out.WhitePoint[0] = chromaticity(m.whitePointX);
    out.WhitePoint[1] = chromaticity(m.whitePointY);
    out.MaxMasteringLuminance = static_cast<UINT>(std::clamp<double>(std::round(static_cast<double>(m.maxMasteringLuminance)), 0.0, 100000.0));
    out.MinMasteringLuminance = static_cast<UINT>(std::clamp<double>(std::round(static_cast<double>(m.minMasteringLuminance) * 10000.0), 0.0, 100000000.0));
    out.MaxContentLightLevel = clampU16(m.maxContentLightLevel);
    out.MaxFrameAverageLightLevel = clampU16(m.maxFrameAverageLightLevel);
    return out;
}
Result<SurfaceMetrics> queryWin32Metrics(HWND hwnd, bool occluded) {
    if (!hwnd || !IsWindow(hwnd)) {
        return invalid("Win32 window is no longer valid", ErrorCode::InvalidState);
    }
    RECT rc{};
    if (!GetClientRect(hwnd, &rc)) {
        return failed(HRESULT_FROM_WIN32(GetLastError()), "GetClientRect failed");
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
    out.occluded = occluded;
    return out;
}
DXGI_FORMAT native(VertexFormat format) {
    switch(format) {
    case VertexFormat::Float:return DXGI_FORMAT_R32_FLOAT;
    case VertexFormat::Float2:return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::Float3:return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::Float4:return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case VertexFormat::UNorm8x4:return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}
bool vertexState(ResourceState state) { return state==ResourceState::VertexBuffer || state==ResourceState::VertexAndIndexBuffer; }
bool indexState(ResourceState state) { return state==ResourceState::IndexBuffer || state==ResourceState::VertexAndIndexBuffer; }
D3D12_RESOURCE_STATES native(ResourceState state) {
    switch (state) {
    case ResourceState::Common: return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::CopySource: return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case ResourceState::CopyDestination: return D3D12_RESOURCE_STATE_COPY_DEST;
    case ResourceState::ShaderRead: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case ResourceState::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case ResourceState::Present: return D3D12_RESOURCE_STATE_PRESENT;
    case ResourceState::VertexBuffer:return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::IndexBuffer:return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case ResourceState::VertexAndIndexBuffer:return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER|D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case ResourceState::ConstantBuffer:return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::DepthWrite:return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case ResourceState::DepthRead:return D3D12_RESOURCE_STATE_DEPTH_READ;
    case ResourceState::UnorderedAccess:return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case ResourceState::ResolveSource:return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    case ResourceState::ResolveDestination:return D3D12_RESOURCE_STATE_RESOLVE_DEST;
    case ResourceState::IndirectArgument:return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}
bool validState(ResourceState state) { return state >= ResourceState::Common && state <= ResourceState::IndirectArgument; }
D3D12_SHADER_VISIBILITY native(ShaderVisibility visibility) {
    return visibility==ShaderVisibility::Vertex?D3D12_SHADER_VISIBILITY_VERTEX:visibility==ShaderVisibility::Fragment?D3D12_SHADER_VISIBILITY_PIXEL:D3D12_SHADER_VISIBILITY_ALL;
}
bool validQueueType(QueueType t) { return t == QueueType::Graphics || t == QueueType::Compute || t == QueueType::Copy; }
std::size_t queueIndex(QueueType t) { return static_cast<std::size_t>(t); }
D3D12_COMMAND_LIST_TYPE d3dQueueType(QueueType t) {
    return t == QueueType::Compute ? D3D12_COMMAND_LIST_TYPE_COMPUTE
         : t == QueueType::Copy ? D3D12_COMMAND_LIST_TYPE_COPY
         : D3D12_COMMAND_LIST_TYPE_DIRECT;
}
bool isSampledTextureBinding(BindingType type) {
    return type == BindingType::Texture2D || type == BindingType::Texture1D ||
           type == BindingType::Texture3D || type == BindingType::Texture2DArray ||
           type == BindingType::TextureCube;
}
BindingType bindingTypeForDimension(TextureDimension dim) {
    switch (dim) {
    case TextureDimension::Texture1D: return BindingType::Texture1D;
    case TextureDimension::Texture3D: return BindingType::Texture3D;
    case TextureDimension::Texture2DArray: return BindingType::Texture2DArray;
    case TextureDimension::TextureCube: return BindingType::TextureCube;
    case TextureDimension::Texture2D: default: return BindingType::Texture2D;
    }
}
BindingType bindingTypeForResource(ShaderResourceType type) {
    switch (type) {
    case ShaderResourceType::Texture1D: return BindingType::Texture1D;
    case ShaderResourceType::Texture2D: return BindingType::Texture2D;
    case ShaderResourceType::Texture3D: return BindingType::Texture3D;
    case ShaderResourceType::Texture2DArray: return BindingType::Texture2DArray;
    case ShaderResourceType::TextureCube: return BindingType::TextureCube;
    case ShaderResourceType::ConstantBuffer: return BindingType::ConstantBuffer;
    case ShaderResourceType::StorageBuffer: return BindingType::StorageBuffer;
    case ShaderResourceType::StorageTexture2D: default: return BindingType::StorageTexture2D;
    }
}
D3D12_SHADER_RESOURCE_VIEW_DESC fillD3D12SrvDesc(
    Format fmt, TextureDimension dim, UINT baseMip, UINT mipCount, UINT baseLayer, UINT layerCount) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = native(fmt);
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    switch (dim) {
    case TextureDimension::Texture1D:
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
        srv.Texture1D.MostDetailedMip = baseMip;
        srv.Texture1D.MipLevels = mipCount;
        break;
    case TextureDimension::Texture3D:
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srv.Texture3D.MostDetailedMip = baseMip;
        srv.Texture3D.MipLevels = mipCount;
        break;
    case TextureDimension::Texture2DArray:
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srv.Texture2DArray.MostDetailedMip = baseMip;
        srv.Texture2DArray.MipLevels = mipCount;
        srv.Texture2DArray.FirstArraySlice = baseLayer;
        srv.Texture2DArray.ArraySize = layerCount;
        break;
    case TextureDimension::TextureCube:
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MostDetailedMip = baseMip;
        srv.TextureCube.MipLevels = mipCount;
        break;
    case TextureDimension::Texture2D:
    default:
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = baseMip;
        srv.Texture2D.MipLevels = mipCount;
        break;
    }
    return srv;
}
// Non-owning presentation identity: retaining a texture/command cannot keep a
// destroyed swapchain usable or retain the public wrapper through this identity.
struct SwapChainScope { IDXGISwapChain3* chain{}; };
struct Tracker {
    ResourceState state{ResourceState::Common};
    D3D12_RESOURCE_STATES nativeState{D3D12_RESOURCE_STATE_COMMON};
    std::uint64_t version{};
    std::array<std::uint64_t, 3> lastUse{};
    bool backbuffer{}; UINT bufferIndex{}; std::weak_ptr<SwapChainScope> swapchain;
    ComPtr<ID3D12Heap> placedHeap;
    std::shared_ptr<std::atomic<bool>> heapResident;
    bool available() const {
        if(!backbuffer)return true;
        const auto scope=swapchain.lock();
        return scope&&scope->chain&&scope->chain->GetCurrentBackBufferIndex()==bufferIndex;
    }
};
struct Batch { std::uint64_t fence{}; std::vector<ComPtr<IUnknown>> objects; std::unordered_set<ID3D12Resource*> placedResources; };
struct Retirable {
    virtual ~Retirable()=default;
    virtual void releaseNative()=0;
    virtual void appendResources(std::vector<ComPtr<ID3D12Resource>>&) const {}
    virtual void appendHeaps(std::vector<ComPtr<ID3D12Heap>>&) const {}
};
struct QueueState {
    detail::FenceWaiter waiter;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    std::uint64_t id{}, last{};
    std::deque<Batch> pending;
};
struct State {
    ComPtr<IDXGIFactory6> factory;
    ComPtr<ID3D12Device> device;
    std::array<QueueState, 3> queues;
    ComPtr<ID3D12CommandQueue>& queue{queues[0].queue};
    ComPtr<ID3D12InfoQueue> info;
    ComPtr<ID3D12InfoQueue1> messageSource;
    DWORD messageCookie{};
    bool messageCallbackRegistered{};
    std::atomic<std::uint64_t> validationErrors{};
    static void CALLBACK onMessage(D3D12_MESSAGE_CATEGORY,D3D12_MESSAGE_SEVERITY severity,D3D12_MESSAGE_ID,LPCSTR,void* context) {
        if(severity!=D3D12_MESSAGE_SEVERITY_ERROR && severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION)return;
        auto& count=static_cast<State*>(context)->validationErrors;
        auto previous=count.load(std::memory_order_relaxed);
        while(previous!=UINT64_MAX && !count.compare_exchange_weak(previous,previous+1,std::memory_order_relaxed)){}
    }
    void detachMessages() {
        // Unregister waits for outstanding callbacks before State or its context can die.
        if(messageCallbackRegistered){(void)messageSource->UnregisterMessageCallback(messageCookie);messageCallbackRegistered=false;}
        messageSource.Reset();
    }
    DeviceCaps caps;
    DeviceDesc description;
    std::shared_ptr<detail::ProfilingTracker> profiler;
    std::atomic<HRESULT> lostReason{S_OK};
    std::atomic<bool> retired{false};
    bool recoverySucceeded{};
    std::vector<std::weak_ptr<Retirable>> children;
    unsigned registrations{};
    std::mutex indirectSigMutex;
    std::unordered_map<std::uint64_t, ComPtr<ID3D12CommandSignature>> indirectSignatures;
    Result<ComPtr<ID3D12CommandSignature>> indirectSignature(std::uint32_t kind, UINT stride) {
        const std::uint64_t key = (static_cast<std::uint64_t>(kind) << 32) | stride;
        std::scoped_lock lock(indirectSigMutex);
        if (auto it = indirectSignatures.find(key); it != indirectSignatures.end()) return it->second;
        D3D12_INDIRECT_ARGUMENT_DESC arg{};
        arg.Type = kind == 0u ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW
                 : kind == 1u ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED
                 : D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        D3D12_COMMAND_SIGNATURE_DESC sigDesc{};
        sigDesc.ByteStride = stride;
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs = &arg;
        ComPtr<ID3D12CommandSignature> sig;
        const HRESULT hr = device->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&sig));
        if (FAILED(hr)) return failure(hr, "CreateCommandSignature failed");
        indirectSignatures.emplace(key, sig);
        return sig;
    }
    std::thread::id thread{std::this_thread::get_id()};
    std::uint64_t id{};
    auto loss() const {return std::unexpected(Error{ErrorCode::DeviceLost,"Device generation is lost; recreate resources on the recovered device",lostReason.load(std::memory_order_acquire)});}
    Result<void> checkAliveForWorker() const {
        if (retired.load(std::memory_order_acquire) || FAILED(lostReason.load(std::memory_order_acquire))) return loss();
        return {};
    }
    auto failure(HRESULT code,std::string operation) {
        if(FAILED(lostReason.load(std::memory_order_acquire)))return loss();
        const auto removed=device?device->GetDeviceRemovedReason():S_OK;
        if(FAILED(removed))lostReason.store(removed, std::memory_order_release);
        else if(code==DXGI_ERROR_DEVICE_REMOVED || code==DXGI_ERROR_DEVICE_RESET || code==DXGI_ERROR_DEVICE_HUNG || code==DXGI_ERROR_DRIVER_INTERNAL_ERROR)lostReason.store(code, std::memory_order_release);
        if(FAILED(lostReason.load(std::memory_order_acquire)))return loss();
        return failed(code,std::move(operation));
    }
    void track(const std::shared_ptr<Retirable>& object) {
        if(++registrations%256==0)std::erase_if(children,[](const auto& child){return child.expired();});
        children.push_back(object);
    }
    void retire() {
        // Lock the entire snapshot before releasing native references. Releasing one child can
        // otherwise destroy another wrapper. Weak registration does not create a State cycle.
        std::vector<std::shared_ptr<Retirable>> live;
        for(auto& child:children)if(auto object=child.lock())live.push_back(std::move(object));
        retired.store(true, std::memory_order_release);
        detachMessages();
        for(auto& child:live)child->releaseNative();
        { std::scoped_lock lock(indirectSigMutex); indirectSignatures.clear(); }
        for(auto& q:queues){q.pending.clear();q.queue.Reset();q.fence.Reset();q.waiter.reset();}
        children.clear();info.Reset();device.Reset();factory.Reset();
    }
    Result<void> check() {
        if (thread != std::this_thread::get_id()) return invalid("Device operations must run on the creating thread");
        if(FAILED(lostReason.load(std::memory_order_acquire)))return loss();
        HRESULT status = device->GetDeviceRemovedReason();
        if (FAILED(status)) {lostReason.store(status, std::memory_order_release);return loss();}
        return {};
    }
    Result<std::uint64_t> completed(QueueType qType = QueueType::Graphics) {
        auto status = check(); if (!status) return std::unexpected(status.error());
        auto& q = queues[queueIndex(qType)];
        auto value = q.fence->GetCompletedValue();
        if (value == UINT64_MAX) {auto reason=device->GetDeviceRemovedReason();lostReason.store(FAILED(reason)?reason:DXGI_ERROR_DEVICE_REMOVED, std::memory_order_release);return loss();}
        while (!q.pending.empty() && q.pending.front().fence <= value) q.pending.pop_front();
        std::erase_if(children,[](const auto& child){return child.expired();});
        return value;
    }
    Result<std::array<std::uint64_t, 3>> completedAll() {
        auto status = check(); if (!status) return std::unexpected(status.error());
        std::array<std::uint64_t, 3> out{};
        for (std::size_t i = 0; i < 3; ++i) {
            auto& q = queues[i];
            if (!q.fence) continue;
            auto value = q.fence->GetCompletedValue();
            if (value == UINT64_MAX) {auto reason=device->GetDeviceRemovedReason();lostReason.store(FAILED(reason)?reason:DXGI_ERROR_DEVICE_REMOVED, std::memory_order_release);return loss();}
            while (!q.pending.empty() && q.pending.front().fence <= value) q.pending.pop_front();
            out[i] = value;
        }
        std::erase_if(children,[](const auto& child){return child.expired();});
        return out;
    }
    Result<void> wait(QueueType qType, std::uint64_t value, std::chrono::milliseconds timeout) {
        auto status = check(); if (!status) return status;
        auto& q = queues[queueIndex(qType)];
        if (value > q.last) return invalid("Cannot wait for a fence that has not been submitted", ErrorCode::InvalidArgument);
        const bool track = profiler && profiler->isEnabled();
        const auto t0 = track ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        auto waited = q.waiter.wait(*q.fence.Get(), value, timeout, [this, qType]{return completed(qType);},
                             [this](HRESULT code,std::string operation){return failure(code,std::move(operation));});
        if (waited && track) {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
            profiler->recordQueueWait(ns > 0 ? static_cast<std::uint64_t>(ns) : 0u);
        }
        return waited;
    }
    Result<void> idleQueue(QueueType qType) {
        auto status = check(); if (!status) return status;
        auto& q = queues[queueIndex(qType)];
        if (!q.queue || !q.fence) return {};
        if (q.last >= UINT64_MAX-1) return invalid("Fence timeline exhausted");
        // Present may enqueue DXGI work after the last render submission.
        HRESULT result = q.queue->Signal(q.fence.Get(), q.last+1);
        if (FAILED(result)) return failure(result, "Idle queue signal failed");
        ++q.last;
        return wait(qType, q.last, std::chrono::seconds(30));
    }
    Result<void> idle() {
        for (QueueType t : {QueueType::Graphics, QueueType::Compute, QueueType::Copy}) {
            if (auto ok = idleQueue(t); !ok) return ok;
        }
        return {};
    }
    ~State() {
        // Resource batches contain native COM objects only; no shared_ptr cycle back to State.
        for (auto& q : queues) {
            if (q.queue && q.fence && q.last && q.fence->GetCompletedValue() < q.last && device && SUCCEEDED(device->GetDeviceRemovedReason())) {
                HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (event) {
                    if (SUCCEEDED(q.fence->SetEventOnCompletion(q.last, event))) WaitForSingleObject(event, INFINITE);
                    CloseHandle(event);
                } else {
                    while (q.fence->GetCompletedValue() < q.last && SUCCEEDED(device->GetDeviceRemovedReason())) SwitchToThread();
                }
            }
        }
        detachMessages();
    }
};
D3D12_RESOURCE_DESC bufferDescription(std::uint64_t size) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = size; desc.Height = 1;
    desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}
D3D12_RESOURCE_DESC textureNativeDesc(const TextureDesc& desc) {
    D3D12_RESOURCE_DESC nativeDesc{};
    nativeDesc.Dimension = desc.dimension == TextureDimension::Texture1D ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
                         : desc.dimension == TextureDimension::Texture3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                         : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    nativeDesc.Width = desc.width;
    nativeDesc.Height = desc.dimension == TextureDimension::Texture1D ? 1u : desc.height;
    nativeDesc.DepthOrArraySize = static_cast<UINT16>(desc.dimension == TextureDimension::Texture3D ? desc.depth : desc.arrayLayers);
    nativeDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
    nativeDesc.Format = native(desc.format);
    nativeDesc.SampleDesc.Count = desc.sampleCount;
    nativeDesc.SampleDesc.Quality = 0;
    if (desc.renderTarget) nativeDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (desc.storage) nativeDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (desc.depthStencil) nativeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    return nativeDesc;
}
struct DXMemoryHeap final : MemoryHeap, Retirable {
    struct PlacedEntry {
        std::weak_ptr<Tracker> tracker;
        std::uint64_t offset{};
        std::uint64_t size{};
    };
    std::shared_ptr<State> owner;
    ComPtr<ID3D12Heap> heap;
    MemoryHeapDesc desc{};
    std::uint64_t heapAlignment{D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT};
    std::shared_ptr<std::atomic<bool>> residentFlag{std::make_shared<std::atomic<bool>>(true)};
    mutable std::vector<PlacedEntry> entries;
    void releaseNative() override { heap.Reset(); }
    void appendHeaps(std::vector<ComPtr<ID3D12Heap>>& out) const override {
        if (heap) out.push_back(heap);
    }
    MemoryHeapDesc description() const override { return desc; }
    Result<HeapFragmentationStats> fragmentationStats() const override {
        auto status = owner->check(); if (!status) return std::unexpected(status.error());
        std::erase_if(entries, [](const PlacedEntry& e) { return e.tracker.expired(); });
        std::vector<HeapAllocationRange> ranges;
        ranges.reserve(entries.size());
        for (const auto& e : entries) ranges.push_back({e.offset, e.size});
        return computeHeapFragmentationStats(desc.size, ranges, residentFlag->load(std::memory_order_acquire));
    }
    Result<void> setResident(bool resident) override {
        auto completed = owner->completedAll(); if (!completed) return std::unexpected(completed.error());
        std::erase_if(entries, [](const PlacedEntry& e) { return e.tracker.expired(); });
        const bool cur = residentFlag->load(std::memory_order_acquire);
        if (cur == resident) return {};
        if (!resident) {
            for (const auto& e : entries) {
                if (auto tr = e.tracker.lock()) {
                    for (std::size_t q = 0; q < 3; ++q) {
                        if ((*completed)[q] < tr->lastUse[q]) {
                            return invalid("Cannot evict MemoryHeap while placed resources are in flight");
                        }
                    }
                }
            }
            ID3D12Pageable* pageable = heap.Get();
            const HRESULT hr = owner->device->Evict(1, &pageable);
            if (FAILED(hr)) return owner->failure(hr, "ID3D12Device::Evict failed");
            residentFlag->store(false, std::memory_order_release);
            return {};
        }
        ID3D12Pageable* pageable = heap.Get();
        const HRESULT hr = owner->device->MakeResident(1, &pageable);
        if (FAILED(hr)) return owner->failure(hr, "ID3D12Device::MakeResident failed");
        residentFlag->store(true, std::memory_order_release);
        return {};
    }
    Result<bool> isResident() const override {
        auto status = owner->check(); if (!status) return std::unexpected(status.error());
        return residentFlag->load(std::memory_order_acquire);
    }
};
Result<ComPtr<ID3D12Resource>> makeBuffer(State& state, std::uint64_t size, Memory memory, bool storage = false, bool shared = false) {
    auto desc = bufferDescription(size);
    if (storage) desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = memory == Memory::Upload ? D3D12_HEAP_TYPE_UPLOAD : memory == Memory::Readback ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT;
    auto initial = memory == Memory::Upload ? D3D12_RESOURCE_STATE_GENERIC_READ : memory == Memory::Readback ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12Resource> resource;
    const D3D12_HEAP_FLAGS heapFlags = shared ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE;
    HRESULT result = state.device->CreateCommittedResource(&heap, heapFlags, &desc, initial, nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(result)) return state.failure(result, "CreateCommittedResource(buffer) failed");
    return resource;
}
struct DXBuffer final : Buffer, Retirable {
    std::shared_ptr<State> owner;
    std::shared_ptr<Tracker> tracker{std::make_shared<Tracker>()};
    ComPtr<ID3D12Resource> resource;
    std::shared_ptr<DXMemoryHeap> placedHeap;
    BufferDesc desc;
    void releaseNative() override {resource.Reset();if(tracker)tracker->placedHeap.Reset();}
    void appendResources(std::vector<ComPtr<ID3D12Resource>>& out) const override {if(resource && !placedHeap)out.push_back(resource);}
    void appendHeaps(std::vector<ComPtr<ID3D12Heap>>& out) const override {if(placedHeap && placedHeap->heap)out.push_back(placedHeap->heap);}
    BufferDesc description() const override { return desc; }
    Result<void> write(std::uint64_t offset, std::span<const std::byte> bytes) override {
        auto status = owner->completedAll(); if (!status) return std::unexpected(status.error());
        if (desc.memory != Memory::Upload || offset > desc.size || bytes.size() > desc.size - offset) return invalid("Invalid upload buffer write", ErrorCode::InvalidArgument);
        for (std::size_t q = 0; q < 3; ++q) if ((*status)[q] < tracker->lastUse[q]) return invalid("Cannot overwrite a buffer in flight");
        if (tracker->version == UINT64_MAX) return invalid("Resource version exhausted");
        D3D12_RANGE range{0, 0}; void* data{};
        HRESULT result = resource->Map(0, &range, &data);
        if (FAILED(result)) return owner->failure(result, "Upload Map failed");
        if (!bytes.empty()) std::memcpy(static_cast<std::byte*>(data) + offset, bytes.data(), bytes.size());
        D3D12_RANGE written{static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + bytes.size())};
        resource->Unmap(0, &written); ++tracker->version;
        return {};
    }
    Result<std::vector<std::byte>> read(std::uint64_t offset, std::uint64_t size) override {
        auto status = owner->completedAll(); if (!status) return std::unexpected(status.error());
        if (desc.memory != Memory::Readback || offset > desc.size || size > desc.size - offset || size > SIZE_MAX) return invalid("Invalid readback buffer range", ErrorCode::InvalidArgument);
        for (std::size_t q = 0; q < 3; ++q) if ((*status)[q] < tracker->lastUse[q]) return invalid("Readback fence has not completed");
        std::vector<std::byte> output(static_cast<size_t>(size));
        D3D12_RANGE range{static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + size)}; void* data{};
        HRESULT result = resource->Map(0, &range, &data);
        if (FAILED(result)) return owner->failure(result, "Readback Map failed");
        if (size) std::memcpy(output.data(), static_cast<std::byte*>(data) + offset, static_cast<size_t>(size));
        D3D12_RANGE written{0, 0}; resource->Unmap(0, &written);
        return output;
    }
};
struct DXTexture final : Texture, Retirable {
    std::shared_ptr<State> owner;
    std::shared_ptr<Tracker> tracker{std::make_shared<Tracker>()};
    ComPtr<ID3D12Resource> resource;
    std::shared_ptr<DXMemoryHeap> placedHeap;
    ComPtr<ID3D12DescriptorHeap> rtv;
    ComPtr<ID3D12DescriptorHeap> dsv;
    TextureDesc desc;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed{};
    TextureFootprint layout;
    bool backbuffer{};
    void releaseNative() override {resource.Reset();rtv.Reset();dsv.Reset();if(tracker)tracker->placedHeap.Reset();}
    void appendResources(std::vector<ComPtr<ID3D12Resource>>& out) const override {if(resource && !placedHeap)out.push_back(resource);}
    void appendHeaps(std::vector<ComPtr<ID3D12Heap>>& out) const override {if(placedHeap && placedHeap->heap)out.push_back(placedHeap->heap);}
    TextureDesc description() const override { return desc; }
    TextureFootprint footprint() const override { return layout; }
    Result<void> initViews() {
        auto nativeDesc = resource->GetDesc();
        if (desc.sampleCount == 1u) {
            UINT rows{}; UINT64 rowSize{}, total{};
            owner->device->GetCopyableFootprints(&nativeDesc, 0, 1, 0, &placed, &rows, &rowSize, &total);
            layout = {total, placed.Footprint.RowPitch, static_cast<UINT>(rowSize), rows};
        } else {
            placed = {};
            layout = {};
        }
        if (desc.renderTarget && desc.dimension == TextureDimension::Texture2D) {
            D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.NumDescriptors = 1;
            HRESULT result = owner->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtv));
            if (FAILED(result)) return owner->failure(result, "Create RTV heap failed");
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = native(desc.format);
            if (desc.sampleCount > 1u) {
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            } else {
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2D.MipSlice = 0;
            }
            owner->device->CreateRenderTargetView(resource.Get(), &rtvDesc, rtv->GetCPUDescriptorHandleForHeapStart());
        }
        if (desc.depthStencil && desc.dimension == TextureDimension::Texture2D) {
            D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;heap.NumDescriptors=2;
            HRESULT result=owner->device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&dsv));
            if(FAILED(result))return owner->failure(result,"Create DSV heap failed");
            auto handle=dsv->GetCPUDescriptorHandleForHeapStart();
            D3D12_DEPTH_STENCIL_VIEW_DESC view{};
            view.Format=DXGI_FORMAT_D32_FLOAT;
            view.ViewDimension=desc.sampleCount>1u?D3D12_DSV_DIMENSION_TEXTURE2DMS:D3D12_DSV_DIMENSION_TEXTURE2D;
            owner->device->CreateDepthStencilView(resource.Get(),&view,handle);
            handle.ptr+=owner->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);view.Flags=D3D12_DSV_FLAG_READ_ONLY_DEPTH;
            owner->device->CreateDepthStencilView(resource.Get(),&view,handle);
        }
        return {};
    }
};
struct DXTextureView final : TextureView, Retirable {
    std::shared_ptr<State> owner;
    std::shared_ptr<DXTexture> parent;
    TextureViewDesc desc;
    ComPtr<ID3D12DescriptorHeap> rtv;
    ComPtr<ID3D12DescriptorHeap> dsv;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    void releaseNative() override { rtv.Reset(); dsv.Reset(); }
    void appendResources(std::vector<ComPtr<ID3D12Resource>>& out) const override {
        if (parent && parent->resource && !parent->placedHeap) out.push_back(parent->resource);
    }
    void appendHeaps(std::vector<ComPtr<ID3D12Heap>>& out) const override {
        if (parent && parent->placedHeap && parent->placedHeap->heap) out.push_back(parent->placedHeap->heap);
    }
    TextureViewDesc description() const override { return desc; }
    TexturePtr texture() const override { return parent; }
};
struct VertexRequirement { UINT slot{},stride{},extent{},alignment{}; VertexInputRate rate{}; };
struct DXPipeline final : Pipeline, Retirable {
    std::shared_ptr<State> owner;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12RootSignature> root;
    RenderTargetLayout renderTargets;
    DepthState depthState;
    bool sampled{};
    bool isCompute{};
    BindingLayout bindings;
    std::vector<UINT> minimumBytes;
    UINT descriptorSize{};
    std::vector<VertexRequirement> vertexInputs;
    void releaseNative() override {pso.Reset();root.Reset();}
};
struct Use { std::shared_ptr<Tracker> tracker; ComPtr<ID3D12Resource> resource; std::uint64_t observedVersion{}; ResourceState state{}; D3D12_RESOURCE_STATES nativeState{D3D12_RESOURCE_STATE_COMMON}; Memory memory{Memory::Device}; bool mutated{}; };
struct NativeCommands final : Retirable {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    ComPtr<ID3D12QueryHeap> query;
    ComPtr<ID3D12Resource> queryReadback;
    ComPtr<ID3D12Resource> scopeReadback;
    std::vector<ComPtr<IUnknown>> references;
    std::vector<Use> uses;
    void releaseNative() override {list.Reset();allocator.Reset();descriptors.Reset();query.Reset();queryReadback.Reset();scopeReadback.Reset();references.clear();uses.clear();}
    void appendResources(std::vector<ComPtr<ID3D12Resource>>& out) const override {
        if(queryReadback)out.push_back(queryReadback);
        if(scopeReadback)out.push_back(scopeReadback);
        for(const auto& use:uses)if(use.resource && (!use.tracker || !use.tracker->placedHeap))out.push_back(use.resource);
    }
    void appendHeaps(std::vector<ComPtr<ID3D12Heap>>& out) const override {
        for(const auto& use:uses)if(use.tracker && use.tracker->placedHeap)out.push_back(use.tracker->placedHeap);
    }
};
struct DXTimelineFence final : TimelineFence, Retirable {
    std::shared_ptr<State> owner;
    ComPtr<ID3D12Fence> fence;
    detail::FenceWaiter waiter;
    std::uint64_t lastSignaled{};
    bool shared{};
    void releaseNative() override { fence.Reset(); waiter.reset(); }
    Result<std::uint64_t> completedValue() override {
        auto status = owner->check(); if (!status) return std::unexpected(status.error());
        const auto value = fence->GetCompletedValue();
        if (value == UINT64_MAX) {
            auto reason = owner->device->GetDeviceRemovedReason();
            owner->lostReason.store(FAILED(reason) ? reason : DXGI_ERROR_DEVICE_REMOVED, std::memory_order_release);
            return owner->loss();
        }
        return value;
    }
    Result<void> signal(std::uint64_t value) override {
        auto current = completedValue(); if (!current) return std::unexpected(current.error());
        if (value <= *current || value <= lastSignaled || value == UINT64_MAX) {
            return invalid("TimelineFence signal value must strictly advance", ErrorCode::InvalidArgument);
        }
        const HRESULT hr = fence->Signal(value);
        if (FAILED(hr)) return owner->failure(hr, "TimelineFence CPU signal failed");
        lastSignaled = value;
        return {};
    }
    Result<void> wait(std::uint64_t value, std::chrono::milliseconds timeout) override {
        auto status = owner->check(); if (!status) return status;
        const bool track = owner->profiler && owner->profiler->isEnabled();
        const auto t0 = track ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
        auto waited = waiter.wait(*fence.Get(), value, timeout,
                           [this] { return completedValue(); },
                           [this](HRESULT code, std::string op) { return owner->failure(code, std::move(op)); });
        if (!waited) return waited;
        auto retired = owner->completedAll();
        if (!retired) return std::unexpected(retired.error());
        if (track) {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
            owner->profiler->recordTimelineWait(ns > 0 ? static_cast<std::uint64_t>(ns) : 0u);
        }
        return {};
    }
};
struct DXCommands;
struct DXCommandPoolState final : Retirable {
    std::mutex mutex;
    ComPtr<ID3D12CommandAllocator> allocator;
    QueueType qType{QueueType::Graphics};
    std::atomic<std::thread::id> recordingThread{};
    std::uint64_t epoch{1};
    std::uint64_t lastSubmitted{};
    std::uint32_t openCount{};
    bool ownerAlive{true};
    std::vector<DXCommands*> liveLists;
    void releaseNative() override { std::scoped_lock lock(mutex); allocator.Reset(); }
};
struct DXCommands final : CommandList {
    std::shared_ptr<State> owner;
    std::shared_ptr<DXCommandPoolState> pool;
    QueueType qType{QueueType::Graphics};
    std::shared_ptr<NativeCommands> objects{std::make_shared<NativeCommands>()};
    ComPtr<ID3D12CommandAllocator>& allocator{objects->allocator};
    ComPtr<ID3D12GraphicsCommandList>& list{objects->list};
    ComPtr<ID3D12DescriptorHeap>& descriptors{objects->descriptors};
    ComPtr<ID3D12QueryHeap>& query{objects->query};
    ComPtr<ID3D12Resource>& queryReadback{objects->queryReadback};
    ComPtr<ID3D12Resource>& scopeReadback{objects->scopeReadback};
    std::vector<ComPtr<IUnknown>>& references{objects->references};
    std::vector<Use>& uses{objects->uses};
    std::unordered_set<IUnknown*> retained;
    std::shared_ptr<DXPipeline> pipeline;
    std::shared_ptr<DXPipeline> computePipeline;
    std::vector<std::shared_ptr<DXTexture>> colorTargets;
    std::shared_ptr<DXTexture> depthTarget;
    UINT passWidth{},passHeight{},passSampleCount{1};
    bool depthReadOnly{};
    struct VertexBinding { std::shared_ptr<DXBuffer> buffer; UINT stride{},size{}; std::uint64_t offset{}; };
    std::array<VertexBinding,32> vertexBuffers{};
    std::shared_ptr<DXBuffer> indexBuffer;
    UINT indexSize{},indexElementSize{};
    std::uint64_t submitted{};
    std::uint64_t recordedEpoch{};
    UINT descriptorCount{};
    struct BoundResource { std::shared_ptr<DXTexture> texture; std::shared_ptr<DXTextureView> textureView; std::shared_ptr<DXBuffer> buffer; std::uint64_t offset{},size{}; bool isStorage{}; };
    std::vector<BoundResource> boundResources;
    std::vector<BoundResource> boundComputeResources;
    bool recording{}, ended{}, renderPass{}, textureBound{}, computeBound{}, timestampsResolved{};
    std::array<bool, 2> timestampWritten{};
    struct RecordedScope {
        std::string name;
        std::uint32_t depth{};
        std::uint32_t beginSlot{};
        std::uint32_t endSlot{};
        bool closed{};
    };
    std::vector<RecordedScope> scopes;
    std::vector<std::uint32_t> openScopeStack;
    std::uint32_t nextScopeSlot{};
    bool scopesResolved{};
    std::optional<std::vector<TimestampScopeResult>> cachedScopeResults;
    QueueType queueType() const override { return qType; }
    ~DXCommands() override {
        if (!pool) return;
        std::scoped_lock lock(pool->mutex);
        std::erase(pool->liveLists, this);
        if (recording) {
            if (pool->openCount > 0) --pool->openCount;
            // Close an orphaned open list before dropping so allocator->Reset() remains valid.
            if (list && owner && !owner->retired.load(std::memory_order_acquire) && SUCCEEDED(owner->lostReason.load(std::memory_order_acquire))) {
                (void)list->Close();
            }
            recording = false;
        }
    }
    void clearRecordingState() {
        references.clear(); retained.clear(); uses.clear(); pipeline.reset(); computePipeline.reset();
        colorTargets.clear(); depthTarget.reset();
        passWidth = passHeight = 0; passSampleCount = 1; depthReadOnly = false;
        boundResources.clear(); boundComputeResources.clear();
        vertexBuffers = {}; indexBuffer.reset(); indexSize = indexElementSize = 0;
        descriptorCount = 0; submitted = 0; renderPass = false; recording = false; ended = false;
        textureBound = false; computeBound = false; timestampsResolved = false; timestampWritten = {};
        scopes.clear(); openScopeStack.clear(); nextScopeSlot = 0; scopesResolved = false; cachedScopeResults.reset();
    }
    Result<void> ready(bool outsidePass = false) {
        // Device removal is checked at begin/submit/wait, rather than polling the
        // driver for every recorded draw. Recording itself does not execute GPU work.
        if (pool) {
            if (pool->recordingThread.load(std::memory_order_acquire) != std::this_thread::get_id())
                return invalid("Command recording must run on the pool's bound recording thread");
            if (auto alive = owner->checkAliveForWorker(); !alive) return alive;
            if (!pool->ownerAlive) return invalid("Owning CommandPool has been destroyed");
        } else {
            if (owner->thread != std::this_thread::get_id()) return invalid("Command recording must run on the creating thread");
            if (auto alive = owner->checkAliveForWorker(); !alive) return alive;
        }
        if (!recording || (outsidePass && renderPass)) return invalid("Command operation is outside its recording/pass scope");
        return {};
    }
    template<class T> void retain(const ComPtr<T>& object) {
        IUnknown* ptr = static_cast<IUnknown*>(object.Get());
        if (retained.insert(ptr).second) { ComPtr<IUnknown> ref; object.As(&ref); references.push_back(std::move(ref)); }
    }
    Use& use(const std::shared_ptr<Tracker>& tracker, const ComPtr<ID3D12Resource>& resource, Memory memory = Memory::Device, bool markMutated = false) {
        auto found = std::find_if(uses.begin(), uses.end(), [&](const Use& value) { return value.tracker == tracker; });
        if (found != uses.end()) {
            if (markMutated) found->mutated = true;
            return *found;
        }
        uses.push_back({tracker, resource, tracker->version, tracker->state, tracker->nativeState, memory, markMutated});
        retain(resource);
        if (tracker->placedHeap) retain(tracker->placedHeap);
        return uses.back();
    }
    template<class T> Result<std::shared_ptr<T>> owned(const std::shared_ptr<typename std::conditional_t<std::is_same_v<T, DXBuffer>, Buffer, Texture>>& ptr) {
        auto object = std::dynamic_pointer_cast<T>(ptr);
        if (!object || object->owner != owner) return invalid("Null or foreign resource", ErrorCode::WrongDevice);
        if (!object->tracker->available()) return invalid("Backbuffer is not current or its swapchain has been destroyed");
        return object;
    }
    Result<std::shared_ptr<DXTextureView>> ownedView(const TextureViewPtr& ptr) {
        if (!ptr) return invalid("Null texture view", ErrorCode::InvalidArgument);
        auto object = std::dynamic_pointer_cast<DXTextureView>(ptr);
        if (!object || object->owner != owner) return invalid("Texture view belongs to another device", ErrorCode::WrongDevice);
        if (!object->parent || !object->parent->tracker->available()) return invalid("Backbuffer is not current or its swapchain has been destroyed");
        return object;
    }
    template<class T> ResourceState current(const T& buffer) const {
        auto entry=std::find_if(uses.begin(),uses.end(),[&](const Use& u){return u.tracker==buffer.tracker;});
        return entry==uses.end()?buffer.tracker->state:entry->state;
    }
    Result<void> change(Use& usage, ResourceState next) {
        if (usage.state != next) usage.mutated = true;
        if (usage.memory == Memory::Upload || usage.memory == Memory::Readback) {
            usage.state = next;
            if (owner->profiler) owner->profiler->recordBarrier();
            return {};
        }
        const auto targetNative = native(next);
        if (qType == QueueType::Copy) {
            // D3D12 Copy queues implicitly promote resources from COMMON on copy operations.
            // Transitioning directly to COPY_SOURCE/COPY_DEST can trigger Debug Layer #1334
            // on CopyTextureRegion, so switching away from a promoted state within the same
            // Copy list transitions back to COMMON to synchronize and allow re-promotion.
            if (usage.nativeState != D3D12_RESOURCE_STATE_COMMON && usage.nativeState != targetNative) {
                D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = usage.resource.Get(); barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                barrier.Transition.StateBefore = usage.nativeState; barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
                list->ResourceBarrier(1, &barrier);
                usage.nativeState = D3D12_RESOURCE_STATE_COMMON;
            }
            usage.state = next;
            if (owner->profiler) owner->profiler->recordBarrier();
            return {};
        }
        if (usage.nativeState != targetNative) {
            D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = usage.resource.Get(); barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = usage.nativeState; barrier.Transition.StateAfter = targetNative;
            list->ResourceBarrier(1, &barrier);
            usage.nativeState = targetNative;
        }
        usage.state = next;
        if (owner->profiler) owner->profiler->recordBarrier();
        return {};
    }
    Result<void> begin() override {
        auto& qState = owner->queues[queueIndex(qType)];
        if (pool) {
            std::scoped_lock lock(pool->mutex);
            if (auto alive = owner->checkAliveForWorker(); !alive) return alive;
            if (!pool->ownerAlive) return invalid("Owning CommandPool has been destroyed");
            if (pool->recordingThread.load(std::memory_order_acquire) != std::this_thread::get_id())
                return invalid("Command recording must run on the pool's bound recording thread");
            if (recording) return invalid("Command list is already recording");
            if (pool->openCount > 0) return invalid("Another command list from this CommandPool is currently recording");
            if (recordedEpoch == pool->epoch) return invalid("Pooled command list requires CommandPool::reset() before re-recording");
            const auto completedVal = qState.fence->GetCompletedValue();
            if (completedVal == UINT64_MAX) return owner->failure(DXGI_ERROR_DEVICE_REMOVED, "Device lost while checking fence");
            if (submitted && completedVal < submitted) return invalid("Command allocator is still in flight");
            HRESULT result = list->Reset(pool->allocator.Get(), nullptr);
            if (FAILED(result)) return owner->failure(result, "Command list reset failed");
            clearRecordingState();
            recording = true;
            recordedEpoch = pool->epoch;
            ++pool->openCount;
            return {};
        }
        auto done = owner->completed(qType); if (!done) return std::unexpected(done.error());
        if (recording || (submitted && *done < submitted)) return invalid("Command allocator is still recording or in flight");
        HRESULT result = allocator->Reset(); if (FAILED(result)) return owner->failure(result, "Allocator reset failed");
        result = list->Reset(allocator.Get(), nullptr); if (FAILED(result)) return owner->failure(result, "Command list reset failed");
        clearRecordingState();
        recording = true;
        return {};
    }
    Result<void> end() override {
        auto status = ready(true); if (!status) return status;
        if (!openScopeStack.empty()) return invalid("Unclosed timestamp scope before CommandList::end()");
        HRESULT result = list->Close(); if (FAILED(result)) return owner->failure(result, "Command list close failed");
        recording = false; ended = true;
        if (pool) {
            std::scoped_lock lock(pool->mutex);
            if (pool->openCount > 0) --pool->openCount;
        }
        return {};
    }
    Result<void> transition(const BufferPtr& ptr, ResourceState next) override {
        auto status = ready(true); if (!status) return status;
        if (next != ResourceState::Common && next != ResourceState::CopySource && next != ResourceState::CopyDestination && !vertexState(next) && !indexState(next) && next!=ResourceState::ConstantBuffer && next!=ResourceState::UnorderedAccess && next!=ResourceState::IndirectArgument) return invalid("Unsupported buffer state", ErrorCode::InvalidArgument);
        if (qType == QueueType::Copy && next != ResourceState::Common && next != ResourceState::CopySource && next != ResourceState::CopyDestination)
            return invalid("Copy command list only supports Common, CopySource, and CopyDestination buffer states", ErrorCode::InvalidArgument);
        if (qType == QueueType::Compute && (vertexState(next) || indexState(next)))
            return invalid("Compute command list does not support vertex/index buffer states", ErrorCode::InvalidArgument);
        auto object = owned<DXBuffer>(ptr); if (!object) return std::unexpected(object.error());
        // Validate before use() captures ownership and the observed version.
        if (next == ResourceState::UnorderedAccess && !(*object)->desc.storage)
            return invalid("UnorderedAccess state requires a storage buffer", ErrorCode::InvalidArgument);
        if ((*object)->desc.memory == Memory::Upload && next != ResourceState::CopySource && !vertexState(next) && !indexState(next) && next != ResourceState::ConstantBuffer && next != ResourceState::IndirectArgument)
            return invalid("Upload memory stays in GENERIC_READ", ErrorCode::InvalidArgument);
        if ((*object)->desc.memory == Memory::Readback && next != ResourceState::CopyDestination)
            return invalid("Readback memory stays in COPY_DEST", ErrorCode::InvalidArgument);
        const auto before = current(**object);
        if (qType == QueueType::Copy && (*object)->desc.memory == Memory::Device &&
            before != ResourceState::Common && before != ResourceState::CopySource && before != ResourceState::CopyDestination)
            return invalid("Copy command list cannot transition buffer from a non-copy state");
        if (qType == QueueType::Compute && (vertexState(before) || indexState(before)))
            return invalid("Compute command list cannot transition buffer from a vertex/index state");
        return change(use((*object)->tracker, (*object)->resource, (*object)->desc.memory), next);
    }
    Result<void> transition(const TexturePtr& ptr, ResourceState next) override {
        auto status = ready(true); if (!status) return status;
        if (qType == QueueType::Copy && next != ResourceState::Common && next != ResourceState::CopySource && next != ResourceState::CopyDestination)
            return invalid("Copy command list only supports Common, CopySource, and CopyDestination texture states", ErrorCode::InvalidArgument);
        if (qType == QueueType::Compute && (next == ResourceState::RenderTarget || next == ResourceState::DepthWrite || next == ResourceState::DepthRead || next == ResourceState::Present || next == ResourceState::ResolveSource || next == ResourceState::ResolveDestination))
            return invalid("Compute command list does not support graphics/present/resolve texture states", ErrorCode::InvalidArgument);
        auto object = owned<DXTexture>(ptr); if (!object) return std::unexpected(object.error());
        const bool depth=(*object)->desc.depthStencil;
        const bool multisampled=(*object)->desc.sampleCount>1u;
        if (next == ResourceState::ShaderRead && multisampled)
            return invalid("Multisampled texture sampling is unsupported", ErrorCode::Unsupported);
        if ((next == ResourceState::ResolveSource || next == ResourceState::ResolveDestination) && (depth || (*object)->backbuffer))
            return invalid("Depth or swapchain texture resolve transition is unsupported", ErrorCode::Unsupported);
        if (next == ResourceState::ResolveDestination && multisampled)
            return invalid("Multisampled texture cannot be a resolve destination", ErrorCode::InvalidArgument);
        if ((next == ResourceState::CopySource || next == ResourceState::CopyDestination) && multisampled)
            return invalid("Multisampled texture does not support direct copy states; use ResolveSource", ErrorCode::InvalidArgument);
        if (!validState(next) || vertexState(next) || indexState(next) || next==ResourceState::ConstantBuffer || next==ResourceState::IndirectArgument || (next == ResourceState::RenderTarget && !(*object)->desc.renderTarget) ||
            (next == ResourceState::UnorderedAccess && !(*object)->desc.storage) ||
            (next == ResourceState::Present && !(*object)->backbuffer) || (next == ResourceState::ShaderRead && ((*object)->backbuffer || depth)) ||
            ((next==ResourceState::DepthWrite || next==ResourceState::DepthRead) && !depth))
            return invalid("Invalid texture state", ErrorCode::InvalidArgument);
        const auto before = current(**object);
        if (qType == QueueType::Copy &&
            before != ResourceState::Common && before != ResourceState::CopySource && before != ResourceState::CopyDestination)
            return invalid("Copy command list cannot transition texture from a non-copy state");
        if (qType == QueueType::Compute &&
            (before == ResourceState::RenderTarget || before == ResourceState::DepthWrite || before == ResourceState::DepthRead ||
             before == ResourceState::Present || before == ResourceState::ResolveSource || before == ResourceState::ResolveDestination))
            return invalid("Compute command list cannot transition texture from a graphics/present/resolve state");
        return change(use((*object)->tracker, (*object)->resource), next);
    }
    Result<void> aliasingBarrier(const BufferPtr& before, const BufferPtr& after) override {
        auto status = ready(true); if (!status) return status;
        if (!after) return invalid("Aliasing barrier requires a non-null destination buffer", ErrorCode::InvalidArgument);
        auto dst = owned<DXBuffer>(after); if (!dst) return std::unexpected(dst.error());
        if (!(*dst)->placedHeap) return invalid("Aliasing barrier requires placed resources", ErrorCode::InvalidArgument);
        std::shared_ptr<DXBuffer> src;
        if (before) {
            auto checked = owned<DXBuffer>(before); if (!checked) return std::unexpected(checked.error());
            src = std::move(*checked);
            if (!src->placedHeap) return invalid("Aliasing barrier requires placed resources", ErrorCode::InvalidArgument);
            if (src == *dst) return invalid("Aliasing barrier requires distinct before and after resources", ErrorCode::InvalidArgument);
        }
        if (src) use(src->tracker, src->resource, src->desc.memory);
        use((*dst)->tracker, (*dst)->resource, (*dst)->desc.memory, true);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Aliasing.pResourceBefore = src ? src->resource.Get() : nullptr;
        barrier.Aliasing.pResourceAfter = (*dst)->resource.Get();
        list->ResourceBarrier(1, &barrier);
        if (owner->profiler) owner->profiler->recordBarrier();
        return {};
    }
    Result<void> aliasingBarrier(const TexturePtr& before, const TexturePtr& after) override {
        auto status = ready(true); if (!status) return status;
        if (!after) return invalid("Aliasing barrier requires a non-null destination texture", ErrorCode::InvalidArgument);
        auto dst = owned<DXTexture>(after); if (!dst) return std::unexpected(dst.error());
        if (!(*dst)->placedHeap) return invalid("Aliasing barrier requires placed resources", ErrorCode::InvalidArgument);
        std::shared_ptr<DXTexture> src;
        if (before) {
            auto checked = owned<DXTexture>(before); if (!checked) return std::unexpected(checked.error());
            src = std::move(*checked);
            if (!src->placedHeap) return invalid("Aliasing barrier requires placed resources", ErrorCode::InvalidArgument);
            if (src == *dst) return invalid("Aliasing barrier requires distinct before and after resources", ErrorCode::InvalidArgument);
        }
        if (src) use(src->tracker, src->resource);
        use((*dst)->tracker, (*dst)->resource, Memory::Device, true);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Aliasing.pResourceBefore = src ? src->resource.Get() : nullptr;
        barrier.Aliasing.pResourceAfter = (*dst)->resource.Get();
        list->ResourceBarrier(1, &barrier);
        if (owner->profiler) owner->profiler->recordBarrier();
        return {};
    }
    Result<void> copyBuffer(const BufferPtr& src, const BufferPtr& dst, std::uint64_t bytes) override {
        return copyBufferRegion(src, 0, dst, 0, bytes);
    }
    Result<void> copyBufferRegion(const BufferPtr& src, std::uint64_t srcOffset, const BufferPtr& dst, std::uint64_t dstOffset, std::uint64_t bytes) override {
        auto status = ready(true); if (!status) return status;
        auto source = owned<DXBuffer>(src); auto destination = owned<DXBuffer>(dst);
        if (!source) return std::unexpected(source.error()); if (!destination) return std::unexpected(destination.error());
        if (src == dst || !bytes ||
            srcOffset > (*source)->desc.size || bytes > (*source)->desc.size - srcOffset ||
            dstOffset > (*destination)->desc.size || bytes > (*destination)->desc.size - dstOffset ||
            (*source)->desc.memory == Memory::Readback || (*destination)->desc.memory == Memory::Upload)
            return invalid("Invalid buffer copy", ErrorCode::InvalidArgument);
        if (current(**source) != ResourceState::CopySource) return invalid("Source is not CopySource");
        if (current(**destination) != ResourceState::CopyDestination) return invalid("Destination is not CopyDestination");
        auto& srcUse = use((*source)->tracker, (*source)->resource, (*source)->desc.memory);
        auto& dstUse = use((*destination)->tracker, (*destination)->resource, (*destination)->desc.memory, true);
        if (qType == QueueType::Copy) {
            if ((*source)->desc.memory == Memory::Device && srcUse.nativeState == D3D12_RESOURCE_STATE_COMMON)
                srcUse.nativeState = D3D12_RESOURCE_STATE_COPY_SOURCE;
            if ((*destination)->desc.memory == Memory::Device && dstUse.nativeState == D3D12_RESOURCE_STATE_COMMON)
                dstUse.nativeState = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        list->CopyBufferRegion((*destination)->resource.Get(), dstOffset, (*source)->resource.Get(), srcOffset, bytes);
        if (owner->profiler) owner->profiler->recordCopy();
        return {};
    }
    Result<void> copyBufferToTexture(const BufferPtr& src, const TexturePtr& dst) override {
        auto status = ready(true); if (!status) return status;
        auto source = owned<DXBuffer>(src); auto destination = owned<DXTexture>(dst);
        if (!source) return std::unexpected(source.error()); if (!destination) return std::unexpected(destination.error());
        if ((*destination)->desc.sampleCount > 1u) return invalid("Direct buffer copy is invalid for multisampled textures; resolve first", ErrorCode::InvalidArgument);
        if ((*source)->desc.memory == Memory::Readback || (*source)->desc.size < (*destination)->layout.size) return invalid("Upload buffer is too small or not copyable", ErrorCode::InvalidArgument);
        if (current(**source) != ResourceState::CopySource || current(**destination) != ResourceState::CopyDestination)
            return invalid("Texture upload requires CopySource/CopyDestination");
        auto& srcUse = use((*source)->tracker, (*source)->resource, (*source)->desc.memory);
        auto& dstUse = use((*destination)->tracker, (*destination)->resource, Memory::Device, true);
        if (qType == QueueType::Copy) {
            if ((*source)->desc.memory == Memory::Device && srcUse.nativeState == D3D12_RESOURCE_STATE_COMMON)
                srcUse.nativeState = D3D12_RESOURCE_STATE_COPY_SOURCE;
            if (dstUse.nativeState == D3D12_RESOURCE_STATE_COMMON)
                dstUse.nativeState = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        D3D12_TEXTURE_COPY_LOCATION a{}, b{};
        a.pResource = (*source)->resource.Get(); a.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; a.PlacedFootprint = (*destination)->placed;
        b.pResource = (*destination)->resource.Get(); b.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&b, 0, 0, 0, &a, nullptr);
        if (owner->profiler) owner->profiler->recordCopy();
        return {};
    }
    Result<void> copyTextureToBuffer(const TexturePtr& src, const BufferPtr& dst) override {
        auto status = ready(true); if (!status) return status;
        auto source = owned<DXTexture>(src); auto destination = owned<DXBuffer>(dst);
        if (!source) return std::unexpected(source.error()); if (!destination) return std::unexpected(destination.error());
        if ((*source)->desc.sampleCount > 1u) return invalid("Direct buffer copy is invalid for multisampled textures; resolve first", ErrorCode::InvalidArgument);
        if ((*destination)->desc.memory == Memory::Upload || (*destination)->desc.size < (*source)->layout.size) return invalid("Readback buffer is too small or not copyable", ErrorCode::InvalidArgument);
        if (current(**source) != ResourceState::CopySource || current(**destination) != ResourceState::CopyDestination)
            return invalid("Texture download requires CopySource/CopyDestination");
        auto& srcUse = use((*source)->tracker, (*source)->resource);
        auto& dstUse = use((*destination)->tracker, (*destination)->resource, (*destination)->desc.memory, true);
        if (qType == QueueType::Copy) {
            if (srcUse.nativeState == D3D12_RESOURCE_STATE_COMMON)
                srcUse.nativeState = D3D12_RESOURCE_STATE_COPY_SOURCE;
            if ((*destination)->desc.memory == Memory::Device && dstUse.nativeState == D3D12_RESOURCE_STATE_COMMON)
                dstUse.nativeState = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        D3D12_TEXTURE_COPY_LOCATION a{}, b{};
        a.pResource = (*source)->resource.Get(); a.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        b.pResource = (*destination)->resource.Get(); b.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; b.PlacedFootprint = (*source)->placed;
        list->CopyTextureRegion(&b, 0, 0, 0, &a, nullptr);
        if (owner->profiler) owner->profiler->recordCopy();
        return {};
    }
    Result<void> copyTextureRegion(const std::shared_ptr<DXTexture>& image, const std::shared_ptr<DXBuffer>& buffer, const TextureCopyRegion& region, bool upload) {
        const auto& desc = image->desc;
        if (desc.sampleCount > 1u) {
            return invalid("Direct buffer copy is invalid for multisampled textures; resolve first", ErrorCode::InvalidArgument);
        }
        if (region.mipLevel >= desc.mipLevels || region.arrayLayer >= desc.arrayLayers || !region.depth) {
            return invalid("Texture copy region subresource or depth invalid", ErrorCode::InvalidArgument);
        }
        const std::uint32_t mipW = std::max(1u, desc.width >> region.mipLevel);
        const std::uint32_t mipH = desc.dimension == TextureDimension::Texture1D ? 1u : std::max(1u, desc.height >> region.mipLevel);
        const std::uint32_t mipD = desc.dimension == TextureDimension::Texture3D ? std::max(1u, desc.depth >> region.mipLevel) : 1u;
        auto blockInfo = queryFormatBlockInfo(desc.format);
        if (!blockInfo) return std::unexpected(blockInfo.error());
        auto footprint = computeTextureRegionFootprint(desc.format, region.width, region.height, region.bufferRowPitch);
        if (!footprint) return std::unexpected(footprint.error());
        if (region.x > mipW || region.width > mipW - region.x ||
            region.y > mipH || region.height > mipH - region.y ||
            region.z > mipD || region.depth > mipD - region.z) {
            return invalid("Texture copy region exceeds subresource dimensions", ErrorCode::InvalidArgument);
        }
        if (blockInfo->compressed) {
            if ((region.x % blockInfo->blockWidth) != 0u || (region.y % blockInfo->blockHeight) != 0u) {
                return invalid("Compressed texture copy region origin must be block-aligned", ErrorCode::InvalidArgument);
            }
            const bool alignedW = (region.width % blockInfo->blockWidth) == 0u || (region.x + region.width == mipW);
            const bool alignedH = (region.height % blockInfo->blockHeight) == 0u || (region.y + region.height == mipH);
            if (!alignedW || !alignedH) {
                return invalid("Compressed texture copy region extent must be block-aligned or reach the subresource edge", ErrorCode::InvalidArgument);
            }
        }
        if (footprint->size > UINT64_MAX / region.depth) {
            return invalid("Texture copy region byte size overflow", ErrorCode::InvalidArgument);
        }
        const std::uint64_t totalBytes = footprint->size * region.depth;
        const bool fullExtent = region.x == 0 && region.y == 0 && region.z == 0 &&
            region.width == mipW && region.height == mipH && region.depth == mipD;
        if (desc.depthStencil && !fullExtent) {
            return invalid("Partial sub-region copy is unsupported for depth textures", ErrorCode::Unsupported);
        }
        if ((region.bufferOffset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT) != 0 ||
            region.bufferOffset > buffer->desc.size || totalBytes > buffer->desc.size - region.bufferOffset ||
            buffer->desc.memory == (upload ? Memory::Readback : Memory::Upload)) {
            return invalid("Texture region copy buffer alignment, bounds or memory kind invalid", ErrorCode::InvalidArgument);
        }
        if (current(*buffer) != (upload ? ResourceState::CopySource : ResourceState::CopyDestination) ||
            current(*image) != (upload ? ResourceState::CopyDestination : ResourceState::CopySource)) {
            return invalid("Texture region copy requires CopySource/CopyDestination states");
        }
        auto& bufUse = use(buffer->tracker, buffer->resource, buffer->desc.memory, !upload);
        auto& imgUse = use(image->tracker, image->resource, Memory::Device, upload);
        if (qType == QueueType::Copy) {
            if (buffer->desc.memory == Memory::Device && bufUse.nativeState == D3D12_RESOURCE_STATE_COMMON)
                bufUse.nativeState = upload ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COPY_DEST;
            if (imgUse.nativeState == D3D12_RESOURCE_STATE_COMMON)
                imgUse.nativeState = upload ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COPY_SOURCE;
        }
        const UINT copyW = blockInfo->compressed
            ? ((region.width + blockInfo->blockWidth - 1u) / blockInfo->blockWidth) * blockInfo->blockWidth
            : region.width;
        const UINT copyH = blockInfo->compressed
            ? ((region.height + blockInfo->blockHeight - 1u) / blockInfo->blockHeight) * blockInfo->blockHeight
            : region.height;
        D3D12_TEXTURE_COPY_LOCATION bufferLocation{};
        bufferLocation.pResource = buffer->resource.Get();
        bufferLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        bufferLocation.PlacedFootprint.Offset = region.bufferOffset;
        bufferLocation.PlacedFootprint.Footprint.Format = native(desc.format);
        bufferLocation.PlacedFootprint.Footprint.Width = copyW;
        bufferLocation.PlacedFootprint.Footprint.Height = copyH;
        bufferLocation.PlacedFootprint.Footprint.Depth = region.depth;
        bufferLocation.PlacedFootprint.Footprint.RowPitch = footprint->rowPitch;
        D3D12_TEXTURE_COPY_LOCATION textureLocation{};
        textureLocation.pResource = image->resource.Get();
        textureLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        textureLocation.SubresourceIndex = region.mipLevel + region.arrayLayer * desc.mipLevels;
        if (upload) {
            list->CopyTextureRegion(&textureLocation, region.x, region.y, region.z, &bufferLocation, nullptr);
        } else {
            D3D12_BOX sourceBox{region.x, region.y, region.z, region.x + copyW, region.y + copyH, region.z + region.depth};
            list->CopyTextureRegion(&bufferLocation, 0, 0, 0, &textureLocation, fullExtent ? nullptr : &sourceBox);
        }
        if (owner->profiler) owner->profiler->recordCopy();
        return {};
    }
    Result<void> copyBufferToTextureRegion(const BufferPtr& src, const TexturePtr& dst, const TextureCopyRegion& region) override {
        auto status = ready(true); if (!status) return status;
        auto source = owned<DXBuffer>(src); auto destination = owned<DXTexture>(dst);
        if (!source) return std::unexpected(source.error()); if (!destination) return std::unexpected(destination.error());
        return copyTextureRegion(*destination, *source, region, true);
    }
    Result<void> copyTextureToBufferRegion(const TexturePtr& src, const BufferPtr& dst, const TextureCopyRegion& region) override {
        auto status = ready(true); if (!status) return status;
        auto source = owned<DXTexture>(src); auto destination = owned<DXBuffer>(dst);
        if (!source) return std::unexpected(source.error()); if (!destination) return std::unexpected(destination.error());
        return copyTextureRegion(*source, *destination, region, false);
    }
    Result<void> resolveTexture(const TexturePtr& src, const TexturePtr& dst) override {
        auto status = ready(true); if (!status) return status;
        if (qType != QueueType::Graphics) return invalid("resolveTexture requires a Graphics command list", ErrorCode::InvalidArgument);
        auto source = owned<DXTexture>(src); auto destination = owned<DXTexture>(dst);
        if (!source) return std::unexpected(source.error()); if (!destination) return std::unexpected(destination.error());
        if (*source == *destination) return invalid("Resolve source and destination must be distinct textures", ErrorCode::InvalidArgument);
        if ((*source)->desc.depthStencil || (*destination)->desc.depthStencil)
            return invalid("Depth texture resolve is unsupported", ErrorCode::Unsupported);
        if ((*source)->backbuffer || (*destination)->backbuffer)
            return invalid("Swapchain backbuffer resolve is unsupported", ErrorCode::Unsupported);
        if ((*source)->desc.sampleCount <= 1u || (*destination)->desc.sampleCount != 1u)
            return invalid("resolveTexture requires multisampled source and single-sampled destination", ErrorCode::InvalidArgument);
        if ((*source)->desc.width != (*destination)->desc.width ||
            (*source)->desc.height != (*destination)->desc.height ||
            (*source)->desc.format != (*destination)->desc.format)
            return invalid("Resolve source and destination extent/format must match", ErrorCode::InvalidArgument);
        if (current(**source) != ResourceState::ResolveSource || current(**destination) != ResourceState::ResolveDestination)
            return invalid("resolveTexture requires ResolveSource and ResolveDestination states");
        use((*source)->tracker, (*source)->resource);
        use((*destination)->tracker, (*destination)->resource, Memory::Device, true);
        list->ResolveSubresource((*destination)->resource.Get(), 0, (*source)->resource.Get(), 0, native((*source)->desc.format));
        if (owner->profiler) owner->profiler->recordCopy();
        return {};
    }
    Result<void> beginRenderPass(const TexturePtr& ptr, Color clear) override {
        auto status = ready(true); if (!status) return status;
        if (qType != QueueType::Graphics) return invalid("beginRenderPass requires a Graphics command list", ErrorCode::InvalidArgument);
        auto object = owned<DXTexture>(ptr); if (!object) return std::unexpected(object.error());
        return beginRenderPass(RenderPassDesc{{{ptr,LoadOp::Clear,clear}}, {}});
    }
    Result<void> beginRenderPass(const RenderPassDesc& pass) override {
        auto status=ready(true);if(!status)return status;
        if (qType != QueueType::Graphics) return invalid("beginRenderPass requires a Graphics command list", ErrorCode::InvalidArgument);
        status=validateRenderPass(pass);if(!status)return status;
        std::vector<std::shared_ptr<DXTexture>> colors;
        std::vector<ComPtr<ID3D12DescriptorHeap>> colorHeaps;
        std::shared_ptr<DXTexture> depth;
        ComPtr<ID3D12DescriptorHeap> depthHeap;
        // Validate all attachments before clearing or mutating the current pass.
        for(const auto& attachment:pass.colors) {
            if (attachment.view) {
                auto view = ownedView(attachment.view); if (!view) return std::unexpected(view.error());
                if ((*view)->desc.usage != TextureViewUsage::RenderTarget || !(*view)->rtv)
                    return invalid("Color attachment view requires RenderTarget usage", ErrorCode::InvalidArgument);
                if (current(*(*view)->parent) != ResourceState::RenderTarget)
                    return invalid("Color attachment requires RenderTarget state");
                colors.push_back((*view)->parent);
                colorHeaps.push_back((*view)->rtv);
            } else {
                auto object=owned<DXTexture>(attachment.texture);if(!object)return std::unexpected(object.error());
                if(current(**object)!=ResourceState::RenderTarget)return invalid("Color attachment requires RenderTarget state");
                colors.push_back(*object);
                colorHeaps.push_back((*object)->rtv);
            }
        }
        if(pass.depth.view) {
            auto view = ownedView(pass.depth.view); if (!view) return std::unexpected(view.error());
            if ((*view)->desc.usage != TextureViewUsage::DepthStencil || !(*view)->dsv)
                return invalid("Depth attachment view requires DepthStencil usage", ErrorCode::InvalidArgument);
            if (current(*(*view)->parent) != (pass.depth.readOnly ? ResourceState::DepthRead : ResourceState::DepthWrite))
                return invalid("Depth attachment state does not match pass access");
            depth = (*view)->parent;
            depthHeap = (*view)->dsv;
        } else if(pass.depth.texture) {
            auto object=owned<DXTexture>(pass.depth.texture);if(!object)return std::unexpected(object.error());
            if(current(**object)!=(pass.depth.readOnly?ResourceState::DepthRead:ResourceState::DepthWrite))return invalid("Depth attachment state does not match pass access");
            depth=*object;
            depthHeap=(*object)->dsv;
        }
        const auto& extent=colors.empty()?depth:colors.front();
        const std::uint32_t firstMip = !pass.colors.empty()
            ? (pass.colors.front().view ? pass.colors.front().view->description().baseMipLevel : 0u)
            : (pass.depth.view ? pass.depth.view->description().baseMipLevel : 0u);
        passWidth = std::max(1u, extent->desc.width >> firstMip);
        passHeight = std::max(1u, extent->desc.height >> firstMip);
        passSampleCount = extent->desc.sampleCount;
        std::array<D3D12_CPU_DESCRIPTOR_HANDLE,8> handles{};
        for(UINT i=0;i<colors.size();++i) {
            use(colors[i]->tracker,colors[i]->resource,Memory::Device,true);retain(colorHeaps[i]);handles[i]=colorHeaps[i]->GetCPUDescriptorHandleForHeapStart();
            if(pass.colors[i].load==LoadOp::Clear){const auto c=pass.colors[i].clear;float clear[]{c.r,c.g,c.b,c.a};list->ClearRenderTargetView(handles[i],clear,0,nullptr);}
        }
        D3D12_CPU_DESCRIPTOR_HANDLE depthHandle{};
        if(depth) {
            use(depth->tracker,depth->resource,Memory::Device,!pass.depth.readOnly);retain(depthHeap);depthHandle=depthHeap->GetCPUDescriptorHandleForHeapStart();
            if(pass.depth.readOnly)depthHandle.ptr+=owner->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            if(pass.depth.load==LoadOp::Clear)list->ClearDepthStencilView(depthHandle,D3D12_CLEAR_FLAG_DEPTH,pass.depth.clear,0,0,nullptr);
        }
        list->OMSetRenderTargets(static_cast<UINT>(colors.size()),colors.empty()?nullptr:handles.data(),FALSE,depth?&depthHandle:nullptr);
        colorTargets=std::move(colors);depthTarget=std::move(depth);depthReadOnly=pass.depth.readOnly;renderPass=true;
        D3D12_VIEWPORT viewport{0,0,static_cast<float>(passWidth),static_cast<float>(passHeight),0,1};
        D3D12_RECT scissor{0,0,static_cast<LONG>(passWidth),static_cast<LONG>(passHeight)};
        list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &scissor);
        if (owner->profiler) owner->profiler->recordRenderPass();
        return {};
    }
    Result<void> endRenderPass() override {
        auto status = ready(); if (!status) return status;
        if (!renderPass) return invalid("No active render pass"); renderPass = false;colorTargets.clear();depthTarget.reset();passWidth=passHeight=0;passSampleCount=1;depthReadOnly=false;return {};
    }
    Result<void> setPipeline(const PipelinePtr& ptr) override {
        auto status = ready(); if (!status) return status;
        if (qType != QueueType::Graphics) return invalid("setPipeline requires a Graphics command list", ErrorCode::InvalidArgument);
        auto object = std::dynamic_pointer_cast<DXPipeline>(ptr);
        if (!object || object->owner != owner) return invalid("Null or foreign pipeline", ErrorCode::WrongDevice);
        if (object->isCompute) return invalid("Graphics setPipeline rejects a compute pipeline", ErrorCode::InvalidArgument);
        pipeline = object; retain(object->root); retain(object->pso); textureBound = false;
        boundResources.clear();
        list->SetPipelineState(object->pso.Get()); list->SetGraphicsRootSignature(object->root.Get());
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); return {};
    }
    Result<void> bindTexture(const TexturePtr& ptr) override {
        auto status = ready(); if (!status) return status;
        if (!pipeline || !pipeline->sampled) return invalid("bindTexture requires the legacy sampledTexture pipeline");
        return bindResources({{{0,0,ptr}}, {}});
    }
    Result<void> bindTextureView(const TextureViewPtr& ptr) override {
        auto status = ready(); if (!status) return status;
        if (!pipeline || !pipeline->sampled) return invalid("bindTextureView requires the legacy sampledTexture pipeline");
        if (!ptr) return invalid("Null texture view", ErrorCode::InvalidArgument);
        return bindResources({{{0,0,nullptr,ptr}}, {}});
    }
    Result<void> bindResources(const ResourceBindings& bindings) override {
        auto status=ready();if(!status)return status;
        if(!pipeline)return invalid("Resource binding requires a pipeline");
        if(pipeline->descriptorSize>256-descriptorCount)return invalid("Command descriptor capacity exhausted; split the command list");
        if(!bindings.storageBuffers.empty() || !bindings.storageTextures.empty())
            return invalid("Storage bindings require bindComputeResources", ErrorCode::InvalidArgument);
        if(bindings.textures.size()+bindings.constants.size()!=pipeline->descriptorSize)return invalid("Every layout array element must be bound exactly once",ErrorCode::InvalidArgument);
        std::vector<BoundResource> snapshot(pipeline->descriptorSize);
        auto slot=[&](UINT binding,UINT element,BindingType type)->Result<UINT> {
            if(binding>=pipeline->bindings.entries.size())return invalid("Unknown layout entry",ErrorCode::InvalidArgument);
            const auto& entry=pipeline->bindings.entries[binding];
            if(entry.type!=type || element>=entry.count)return invalid("Binding type or array element mismatch",ErrorCode::InvalidArgument);
            UINT at=element;for(UINT i=0;i<binding;++i)at+=pipeline->bindings.entries[i].count;
            if(snapshot[at].texture || snapshot[at].buffer)return invalid("Duplicate binding element",ErrorCode::InvalidArgument);
            return at;
        };
        // Validate the entire set before consuming descriptors or changing native bindings.
        for(const auto& binding:bindings.textures) {
            if (binding.texture && binding.view)
                return invalid("Specify either texture or view in TextureBinding, not both", ErrorCode::InvalidArgument);
            if (binding.view) {
                auto view = ownedView(binding.view); if (!view) return std::unexpected(view.error());
                if ((*view)->desc.usage != TextureViewUsage::ShaderRead)
                    return invalid("Texture binding view requires ShaderRead usage", ErrorCode::InvalidArgument);
                const auto& parent = (*view)->parent;
                const auto viewDim = (*view)->desc.dimension.value_or(parent->desc.dimension);
                auto at = slot(binding.binding, binding.element, bindingTypeForDimension(viewDim));
                if (!at) return std::unexpected(at.error());
                if (parent->desc.sampleCount > 1u)
                    return invalid("Multisampled texture sampling is unsupported", ErrorCode::Unsupported);
                if (parent->backbuffer || parent->desc.depthStencil || current(*parent) != ResourceState::ShaderRead)
                    return invalid("Texture binding requires a color texture in ShaderRead");
                snapshot[*at].texture = parent;
                snapshot[*at].textureView = *view;
            } else {
                auto object=owned<DXTexture>(binding.texture);if(!object)return std::unexpected(object.error());
                auto at = slot(binding.binding, binding.element, bindingTypeForDimension((*object)->desc.dimension));
                if (!at) return std::unexpected(at.error());
                if ((*object)->desc.sampleCount > 1u)
                    return invalid("Multisampled texture sampling is unsupported", ErrorCode::Unsupported);
                if((*object)->backbuffer || (*object)->desc.depthStencil || current(**object)!=ResourceState::ShaderRead)return invalid("Texture binding requires a color texture in ShaderRead");
                snapshot[*at].texture=*object;
            }
        }
        for(const auto& binding:bindings.constants) {
            auto at=slot(binding.binding,binding.element,BindingType::ConstantBuffer);if(!at)return std::unexpected(at.error());
            auto object=owned<DXBuffer>(binding.buffer);if(!object)return std::unexpected(object.error());
            const auto& buffer=**object;
            if(buffer.desc.memory==Memory::Readback || binding.offset%256 || !binding.size || binding.size%256 || binding.size>65536 ||
               binding.offset>buffer.desc.size || binding.size>buffer.desc.size-binding.offset || binding.size<pipeline->minimumBytes[binding.binding])
                return invalid("Invalid constant buffer view: alignment, bounds or shader size",ErrorCode::InvalidArgument);
            if(current(buffer)!=ResourceState::ConstantBuffer)return invalid("Constant buffer binding requires ConstantBuffer state");
            snapshot[*at]={nullptr,nullptr,*object,binding.offset,binding.size};
        }
        for(const auto& resource:snapshot) {
            if(!resource.texture && !resource.buffer)return invalid("Missing binding array element",ErrorCode::InvalidArgument);
            if(resource.texture)use(resource.texture->tracker,resource.texture->resource);
            else use(resource.buffer->tracker,resource.buffer->resource,resource.buffer->desc.memory);
        }
        const UINT increment=owner->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto cpu=descriptors->GetCPUDescriptorHandleForHeapStart();auto gpu=descriptors->GetGPUDescriptorHandleForHeapStart();
        cpu.ptr+=static_cast<SIZE_T>(descriptorCount)*increment;gpu.ptr+=static_cast<UINT64>(descriptorCount)*increment;
        if(!snapshot.empty()) {ID3D12DescriptorHeap* heaps[]{descriptors.Get()};list->SetDescriptorHeaps(1,heaps);}
        UINT at=0;
        for(UINT i=0;i<pipeline->bindings.entries.size();++i) {
            list->SetGraphicsRootDescriptorTable(i,gpu);
            for(UINT element=0;element<pipeline->bindings.entries[i].count;++element) {
                const auto& resource=snapshot[at++];
                if(resource.texture) {
                    if (resource.textureView) {
                        owner->device->CreateShaderResourceView(resource.texture->resource.Get(), &resource.textureView->srvDesc, cpu);
                    } else {
                        const auto& td = resource.texture->desc;
                        const auto view = fillD3D12SrvDesc(td.format, td.dimension, 0, td.mipLevels, 0, td.arrayLayers);
                        owner->device->CreateShaderResourceView(resource.texture->resource.Get(), &view, cpu);
                    }
                } else {
                    D3D12_CONSTANT_BUFFER_VIEW_DESC view{resource.buffer->resource->GetGPUVirtualAddress()+resource.offset,static_cast<UINT>(resource.size)};
                    owner->device->CreateConstantBufferView(&view,cpu);
                }
                cpu.ptr+=increment;gpu.ptr+=increment;
            }
        }
        descriptorCount+=pipeline->descriptorSize;boundResources=std::move(snapshot);textureBound=true;return {};
    }
    Result<void> setViewport(const Viewport& viewport) override {
        auto status = ready(); if (!status) return status;
        if (!renderPass) return invalid("Viewport requires an active render pass");
        if (auto ok = validateViewport(viewport, passWidth, passHeight); !ok) return ok;
        D3D12_VIEWPORT nativeViewport{
            viewport.x, viewport.y, viewport.width, viewport.height, viewport.minDepth, viewport.maxDepth};
        list->RSSetViewports(1, &nativeViewport);
        return {};
    }
    Result<void> setScissor(UINT x, UINT y, UINT width, UINT height) override {
        auto status = ready(); if (!status) return status;
        if (!renderPass || !width || !height || x > passWidth || y > passHeight || width > passWidth-x || height > passHeight-y)
            return invalid("Scissor outside target", ErrorCode::InvalidArgument);
        D3D12_RECT rect{static_cast<LONG>(x), static_cast<LONG>(y), static_cast<LONG>(x+width), static_cast<LONG>(y+height)};
        list->RSSetScissorRects(1, &rect); return {};
    }
    Result<void> bindVertexBuffer(UINT slot,const BufferPtr& ptr,UINT stride,std::uint64_t offset,std::uint64_t size) override {
        auto status=ready();if(!status)return status;
        if (qType != QueueType::Graphics) return invalid("bindVertexBuffer requires a Graphics command list", ErrorCode::InvalidArgument);
        auto object=owned<DXBuffer>(ptr);if(!object)return std::unexpected(object.error());
        const auto& buffer=**object;
        if(slot>=vertexBuffers.size() || !stride || stride>2048 || offset>buffer.desc.size || size>buffer.desc.size-offset || buffer.desc.memory==Memory::Readback)
            return invalid("Invalid vertex buffer view",ErrorCode::InvalidArgument);
        const auto bytes=size?size:buffer.desc.size-offset;
        if(!bytes || bytes>UINT_MAX)return invalid("Vertex buffer view must contain 1..UINT32_MAX bytes",ErrorCode::InvalidArgument);
        if(!vertexState(current(buffer)))return invalid("Vertex buffer must be in a vertex read state");
        use(buffer.tracker,buffer.resource,buffer.desc.memory);
        D3D12_VERTEX_BUFFER_VIEW view{buffer.resource->GetGPUVirtualAddress()+offset,static_cast<UINT>(bytes),stride};
        list->IASetVertexBuffers(slot,1,&view);vertexBuffers[slot]={*object,stride,static_cast<UINT>(bytes),offset};return {};
    }
    Result<void> bindIndexBuffer(const BufferPtr& ptr,IndexFormat format,std::uint64_t offset,std::uint64_t size) override {
        auto status=ready();if(!status)return status;
        if (qType != QueueType::Graphics) return invalid("bindIndexBuffer requires a Graphics command list", ErrorCode::InvalidArgument);
        auto object=owned<DXBuffer>(ptr);if(!object)return std::unexpected(object.error());
        if(format!=IndexFormat::UInt16 && format!=IndexFormat::UInt32)return invalid("Invalid index format",ErrorCode::InvalidArgument);
        const auto element=format==IndexFormat::UInt16?2u:4u;const auto& buffer=**object;
        if(offset>buffer.desc.size || size>buffer.desc.size-offset || offset%element || buffer.desc.memory==Memory::Readback)
            return invalid("Invalid index buffer view",ErrorCode::InvalidArgument);
        const auto bytes=size?size:buffer.desc.size-offset;
        if(!bytes || bytes>UINT_MAX || bytes%element)return invalid("Invalid index buffer view size/alignment",ErrorCode::InvalidArgument);
        if(!indexState(current(buffer)))return invalid("Index buffer must be in an index read state");
        use(buffer.tracker,buffer.resource,buffer.desc.memory);
        D3D12_INDEX_BUFFER_VIEW view{buffer.resource->GetGPUVirtualAddress()+offset,static_cast<UINT>(bytes),format==IndexFormat::UInt16?DXGI_FORMAT_R16_UINT:DXGI_FORMAT_R32_UINT};
        list->IASetIndexBuffer(&view);indexBuffer=*object;indexSize=static_cast<UINT>(bytes);indexElementSize=element;return {};
    }
    Result<void> drawReady() {
        auto status = ready(); if (!status) return status;
        if (qType != QueueType::Graphics) return invalid("Draw requires a Graphics command list", ErrorCode::InvalidArgument);
        if (!renderPass || !pipeline || (pipeline->descriptorSize && !textureBound)) return invalid("Draw requires compatible target, pipeline and bindings");
        if(pipeline->renderTargets.colors.size()!=colorTargets.size() || pipeline->renderTargets.depth.has_value()!=static_cast<bool>(depthTarget))return invalid("Pipeline attachment count/depth does not match pass");
        if(pipeline->renderTargets.sampleCount!=passSampleCount)return invalid("Pipeline sample count does not match pass");
        for(size_t i=0;i<colorTargets.size();++i)if(pipeline->renderTargets.colors[i]!=colorTargets[i]->desc.format)return invalid("Pipeline color formats do not match pass");
        if(depthTarget && (*pipeline->renderTargets.depth!=depthTarget->desc.format || (depthReadOnly && pipeline->depthState.writeEnabled)))return invalid("Pipeline depth format/access does not match pass");
        for(const auto& resource:boundResources) {
            if(resource.texture && current(*resource.texture)!=ResourceState::ShaderRead)return invalid("Bound texture state changed; restore ShaderRead before draw");
            if(resource.buffer && current(*resource.buffer)!=ResourceState::ConstantBuffer)return invalid("Bound constant buffer state changed; restore ConstantBuffer before draw");
        }
        return {};
    }
    Result<void> inputReady(UINT vertices,UINT instances,UINT firstVertex,UINT firstInstance,bool indexed=false,INT baseVertex=0) {
        // No input requirements for the existing SV_VertexID-only fast path.
        for(const auto& input:pipeline->vertexInputs) {
            const auto& binding=vertexBuffers[input.slot];
            if(!binding.buffer || binding.stride!=input.stride || !vertexState(current(*binding.buffer)))return invalid("Missing, mismatched or stale vertex buffer binding");
            if(binding.offset%input.alignment || binding.size<input.extent)return invalid("Vertex view alignment/extent does not fit layout",ErrorCode::InvalidArgument);
            const auto last=(binding.size-input.extent)/input.stride;
            if(input.rate==VertexInputRate::Instance) {
                if(static_cast<std::uint64_t>(firstInstance)+instances-1>last)return invalid("Instance input exceeds buffer view",ErrorCode::InvalidArgument);
            } else if(!indexed) {
                if(static_cast<std::uint64_t>(firstVertex)+vertices-1>last)return invalid("Vertex input exceeds buffer view",ErrorCode::InvalidArgument);
            } else if(baseVertex>0 && static_cast<UINT>(baseVertex)>last)return invalid("Base vertex exceeds buffer view",ErrorCode::InvalidArgument);
        }
        return {};
    }
    Result<void> draw(UINT vertices,UINT instances,UINT firstVertex,UINT firstInstance) override {
        auto status=drawReady();if(!status)return status;
        if (!vertices || !instances) return invalid("Empty draw", ErrorCode::InvalidArgument);
        if(static_cast<std::uint64_t>(firstVertex)+vertices>std::uint64_t{UINT_MAX}+1 || static_cast<std::uint64_t>(firstInstance)+instances>std::uint64_t{UINT_MAX}+1)return invalid("Draw range overflow",ErrorCode::InvalidArgument);
        status=inputReady(vertices,instances,firstVertex,firstInstance);if(!status)return status;
        list->DrawInstanced(vertices, instances, firstVertex, firstInstance);
        if (owner->profiler) owner->profiler->recordDraw(false, instances);
        return {};
    }
    Result<void> drawIndexed(UINT indices,UINT instances,UINT firstIndex,INT baseVertex,UINT firstInstance) override {
        auto status=drawReady();if(!status)return status;
        if(!indices || !instances)return invalid("Empty indexed draw",ErrorCode::InvalidArgument);
        if(!indexBuffer || !indexState(current(*indexBuffer)))return invalid("Missing or stale index buffer binding");
        if(static_cast<std::uint64_t>(firstIndex)+indices>indexSize/indexElementSize || static_cast<std::uint64_t>(firstInstance)+instances>std::uint64_t{UINT_MAX}+1)
            return invalid("Indexed draw range exceeds buffer view",ErrorCode::InvalidArgument);
        status=inputReady(0,instances,0,firstInstance,true,baseVertex);if(!status)return status;
        list->DrawIndexedInstanced(indices,instances,firstIndex,baseVertex,firstInstance);
        if (owner->profiler) owner->profiler->recordDraw(true, instances);
        return {};
    }
    Result<void> indirectInputReady() {
        for (const auto& input : pipeline->vertexInputs) {
            const auto& binding = vertexBuffers[input.slot];
            if (!binding.buffer || binding.stride != input.stride || !vertexState(current(*binding.buffer)))
                return invalid("Missing, mismatched or stale vertex buffer binding");
            if (binding.offset % input.alignment || binding.size < input.extent)
                return invalid("Vertex view alignment/extent does not fit layout", ErrorCode::InvalidArgument);
        }
        return {};
    }
    Result<void> drawIndirect(const BufferPtr& argsBuffer, std::uint64_t argsOffset, UINT drawCount, UINT stride) override {
        auto status = drawReady(); if (!status) return status;
        if (!drawCount || drawCount > owner->caps.maxDrawIndirectCount)
            return invalid("drawIndirect drawCount out of supported range", ErrorCode::InvalidArgument);
        if (drawCount > 1u && !owner->caps.multiDrawIndirect)
            return invalid("Multi-Draw Indirect is unsupported on this device", ErrorCode::Unsupported);
        if (stride < sizeof(DrawIndirectCommand) || (stride % 4u) != 0u || (argsOffset % 4u) != 0u)
            return invalid("drawIndirect offset and stride must be 4-byte aligned and >= 16 bytes", ErrorCode::InvalidArgument);
        auto argBuf = owned<DXBuffer>(argsBuffer); if (!argBuf) return std::unexpected(argBuf.error());
        if ((*argBuf)->desc.memory == Memory::Readback)
            return invalid("Readback buffer cannot be used as an indirect argument buffer", ErrorCode::InvalidArgument);
        if (current(**argBuf) != ResourceState::IndirectArgument)
            return invalid("drawIndirect requires ResourceState::IndirectArgument");
        const std::uint64_t spanBytes = static_cast<std::uint64_t>(drawCount - 1u) * stride + sizeof(DrawIndirectCommand);
        if (argsOffset > (*argBuf)->desc.size || spanBytes > (*argBuf)->desc.size - argsOffset)
            return invalid("drawIndirect argument range exceeds buffer size", ErrorCode::InvalidArgument);
        status = indirectInputReady(); if (!status) return status;
        auto sig = owner->indirectSignature(0u, stride); if (!sig) return std::unexpected(sig.error());
        use((*argBuf)->tracker, (*argBuf)->resource, (*argBuf)->desc.memory);
        retain(*sig);
        list->ExecuteIndirect(sig->Get(), drawCount, (*argBuf)->resource.Get(), argsOffset, nullptr, 0);
        if (owner->profiler) owner->profiler->recordDraw(false, drawCount);
        return {};
    }
    Result<void> drawIndexedIndirect(const BufferPtr& argsBuffer, std::uint64_t argsOffset, UINT drawCount, UINT stride) override {
        auto status = drawReady(); if (!status) return status;
        if (!drawCount || drawCount > owner->caps.maxDrawIndirectCount)
            return invalid("drawIndexedIndirect drawCount out of supported range", ErrorCode::InvalidArgument);
        if (drawCount > 1u && !owner->caps.multiDrawIndirect)
            return invalid("Multi-Draw Indirect is unsupported on this device", ErrorCode::Unsupported);
        if (stride < sizeof(DrawIndexedIndirectCommand) || (stride % 4u) != 0u || (argsOffset % 4u) != 0u)
            return invalid("drawIndexedIndirect offset and stride must be 4-byte aligned and >= 20 bytes", ErrorCode::InvalidArgument);
        if (!indexBuffer || !indexState(current(*indexBuffer)))
            return invalid("Missing or stale index buffer binding");
        auto argBuf = owned<DXBuffer>(argsBuffer); if (!argBuf) return std::unexpected(argBuf.error());
        if ((*argBuf)->desc.memory == Memory::Readback)
            return invalid("Readback buffer cannot be used as an indirect argument buffer", ErrorCode::InvalidArgument);
        if (current(**argBuf) != ResourceState::IndirectArgument)
            return invalid("drawIndexedIndirect requires ResourceState::IndirectArgument");
        const std::uint64_t spanBytes = static_cast<std::uint64_t>(drawCount - 1u) * stride + sizeof(DrawIndexedIndirectCommand);
        if (argsOffset > (*argBuf)->desc.size || spanBytes > (*argBuf)->desc.size - argsOffset)
            return invalid("drawIndexedIndirect argument range exceeds buffer size", ErrorCode::InvalidArgument);
        status = indirectInputReady(); if (!status) return status;
        auto sig = owner->indirectSignature(1u, stride); if (!sig) return std::unexpected(sig.error());
        use((*argBuf)->tracker, (*argBuf)->resource, (*argBuf)->desc.memory);
        retain(*sig);
        list->ExecuteIndirect(sig->Get(), drawCount, (*argBuf)->resource.Get(), argsOffset, nullptr, 0);
        if (owner->profiler) owner->profiler->recordDraw(true, drawCount);
        return {};
    }
    Result<void> setComputePipeline(const PipelinePtr& ptr) override {
        auto status = ready(true); if (!status) return status;
        if (qType == QueueType::Copy) return invalid("Compute operations are not supported on a Copy command list", ErrorCode::InvalidArgument);
        auto object = std::dynamic_pointer_cast<DXPipeline>(ptr);
        if (!object || object->owner != owner) return invalid("Null or foreign compute pipeline", ErrorCode::WrongDevice);
        if (!object->isCompute) return invalid("setComputePipeline requires a compute pipeline", ErrorCode::InvalidArgument);
        computePipeline = object;
        retain(object->root);
        retain(object->pso);
        boundComputeResources.clear();
        computeBound = false;
        list->SetPipelineState(object->pso.Get());
        list->SetComputeRootSignature(object->root.Get());
        return {};
    }
    Result<void> bindComputeResources(const ResourceBindings& bindings) override {
        auto status = ready(true); if (!status) return status;
        if (qType == QueueType::Copy) return invalid("Compute operations are not supported on a Copy command list", ErrorCode::InvalidArgument);
        if (!computePipeline) return invalid("Compute resource binding requires a compute pipeline");
        if (computePipeline->descriptorSize > 256 - descriptorCount) {
            return invalid("Command descriptor capacity exhausted; split the command list");
        }
        const std::size_t totalBindings = bindings.textures.size() + bindings.constants.size() +
            bindings.storageBuffers.size() + bindings.storageTextures.size();
        if (totalBindings != computePipeline->descriptorSize) {
            return invalid("Every compute layout array element must be bound exactly once", ErrorCode::InvalidArgument);
        }
        std::vector<BoundResource> snapshot(computePipeline->descriptorSize);
        auto slot = [&](UINT binding, UINT element, BindingType type) -> Result<UINT> {
            if (binding >= computePipeline->bindings.entries.size()) return invalid("Unknown compute layout entry", ErrorCode::InvalidArgument);
            const auto& entry = computePipeline->bindings.entries[binding];
            if (entry.type != type || element >= entry.count) return invalid("Compute binding type or array element mismatch", ErrorCode::InvalidArgument);
            UINT at = element;
            for (UINT i = 0; i < binding; ++i) at += computePipeline->bindings.entries[i].count;
            if (snapshot[at].texture || snapshot[at].buffer) return invalid("Duplicate compute binding element", ErrorCode::InvalidArgument);
            return at;
        };
        for (const auto& binding : bindings.textures) {
            if (binding.texture && binding.view) return invalid("Specify either texture or view in TextureBinding, not both", ErrorCode::InvalidArgument);
            if (binding.view) {
                auto view = ownedView(binding.view); if (!view) return std::unexpected(view.error());
                if ((*view)->desc.usage != TextureViewUsage::ShaderRead) return invalid("Texture binding view requires ShaderRead usage", ErrorCode::InvalidArgument);
                const auto& parent = (*view)->parent;
                const auto viewDim = (*view)->desc.dimension.value_or(parent->desc.dimension);
                auto at = slot(binding.binding, binding.element, bindingTypeForDimension(viewDim));
                if (!at) return std::unexpected(at.error());
                if (parent->backbuffer || parent->desc.depthStencil || current(*parent) != ResourceState::ShaderRead)
                    return invalid("Compute texture binding requires a color texture in ShaderRead");
                snapshot[*at] = {parent, *view, nullptr, 0, 0, false};
            } else {
                auto object = owned<DXTexture>(binding.texture); if (!object) return std::unexpected(object.error());
                auto at = slot(binding.binding, binding.element, bindingTypeForDimension((*object)->desc.dimension));
                if (!at) return std::unexpected(at.error());
                if ((*object)->backbuffer || (*object)->desc.depthStencil || current(**object) != ResourceState::ShaderRead)
                    return invalid("Compute texture binding requires a color texture in ShaderRead");
                snapshot[*at] = {*object, nullptr, nullptr, 0, 0, false};
            }
        }
        for (const auto& binding : bindings.constants) {
            auto at = slot(binding.binding, binding.element, BindingType::ConstantBuffer); if (!at) return std::unexpected(at.error());
            auto object = owned<DXBuffer>(binding.buffer); if (!object) return std::unexpected(object.error());
            const auto& buffer = **object;
            if (buffer.desc.memory == Memory::Readback || binding.offset % 256 || !binding.size || binding.size % 256 || binding.size > 65536 ||
                binding.offset > buffer.desc.size || binding.size > buffer.desc.size - binding.offset || binding.size < computePipeline->minimumBytes[binding.binding]) {
                return invalid("Invalid compute constant buffer view", ErrorCode::InvalidArgument);
            }
            if (current(buffer) != ResourceState::ConstantBuffer) return invalid("Compute constant buffer binding requires ConstantBuffer state");
            snapshot[*at] = {nullptr, nullptr, *object, binding.offset, binding.size, false};
        }
        for (const auto& binding : bindings.storageBuffers) {
            auto at = slot(binding.binding, binding.element, BindingType::StorageBuffer); if (!at) return std::unexpected(at.error());
            auto object = owned<DXBuffer>(binding.buffer); if (!object) return std::unexpected(object.error());
            const auto& buffer = **object;
            if (!buffer.desc.storage || buffer.desc.memory != Memory::Device) {
                return invalid("StorageBuffer binding requires a device storage buffer", ErrorCode::InvalidArgument);
            }
            if (binding.offset % 256 || binding.offset > buffer.desc.size) {
                return invalid("StorageBuffer offset must be a 256-byte multiple within bounds", ErrorCode::InvalidArgument);
            }
            const std::uint64_t size = binding.size ? binding.size : buffer.desc.size - binding.offset;
            if (!size || size % 4 || size > buffer.desc.size - binding.offset || (size / 4) > UINT_MAX) {
                return invalid("StorageBuffer size must be a positive 4-byte multiple within bounds", ErrorCode::InvalidArgument);
            }
            if (current(buffer) != ResourceState::UnorderedAccess) {
                return invalid("StorageBuffer binding requires UnorderedAccess state");
            }
            snapshot[*at] = {nullptr, nullptr, *object, binding.offset, size, true};
        }
        for (const auto& binding : bindings.storageTextures) {
            auto at = slot(binding.binding, binding.element, BindingType::StorageTexture2D); if (!at) return std::unexpected(at.error());
            if (binding.texture && binding.view) return invalid("Specify either texture or view in StorageTextureBinding, not both", ErrorCode::InvalidArgument);
            if (binding.view) {
                auto view = ownedView(binding.view); if (!view) return std::unexpected(view.error());
                if ((*view)->desc.usage != TextureViewUsage::Storage) return invalid("StorageTextureBinding view requires Storage usage", ErrorCode::InvalidArgument);
                const auto& parent = (*view)->parent;
                if (!parent->desc.storage || parent->desc.depthStencil || current(*parent) != ResourceState::UnorderedAccess)
                    return invalid("StorageTextureBinding requires a storage texture in UnorderedAccess state");
                snapshot[*at] = {parent, *view, nullptr, 0, 0, true};
            } else {
                auto object = owned<DXTexture>(binding.texture); if (!object) return std::unexpected(object.error());
                if (!(*object)->desc.storage || (*object)->desc.depthStencil || current(**object) != ResourceState::UnorderedAccess)
                    return invalid("StorageTextureBinding requires a storage texture in UnorderedAccess state");
                snapshot[*at] = {*object, nullptr, nullptr, 0, 0, true};
            }
        }
        for (const auto& resource : snapshot) {
            if (!resource.texture && !resource.buffer) return invalid("Missing compute binding element", ErrorCode::InvalidArgument);
            if (resource.texture) use(resource.texture->tracker, resource.texture->resource, Memory::Device, resource.isStorage);
            else use(resource.buffer->tracker, resource.buffer->resource, resource.buffer->desc.memory, resource.isStorage);
        }
        const UINT increment = owner->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto cpu = descriptors->GetCPUDescriptorHandleForHeapStart();
        auto gpu = descriptors->GetGPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(descriptorCount) * increment;
        gpu.ptr += static_cast<UINT64>(descriptorCount) * increment;
        if (!snapshot.empty()) {
            ID3D12DescriptorHeap* heaps[]{descriptors.Get()};
            list->SetDescriptorHeaps(1, heaps);
        }
        UINT at = 0;
        for (UINT i = 0; i < computePipeline->bindings.entries.size(); ++i) {
            list->SetComputeRootDescriptorTable(i, gpu);
            for (UINT element = 0; element < computePipeline->bindings.entries[i].count; ++element) {
                const auto& resource = snapshot[at++];
                if (resource.texture) {
                    if (resource.isStorage) {
                        if (resource.textureView) {
                            owner->device->CreateUnorderedAccessView(resource.texture->resource.Get(), nullptr, &resource.textureView->uavDesc, cpu);
                        } else {
                            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                            uav.Format = native(resource.texture->desc.format);
                            uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                            owner->device->CreateUnorderedAccessView(resource.texture->resource.Get(), nullptr, &uav, cpu);
                        }
                    } else if (resource.textureView) {
                        owner->device->CreateShaderResourceView(resource.texture->resource.Get(), &resource.textureView->srvDesc, cpu);
                    } else {
                        const auto& td = resource.texture->desc;
                        const auto srv = fillD3D12SrvDesc(td.format, td.dimension, 0, td.mipLevels, 0, td.arrayLayers);
                        owner->device->CreateShaderResourceView(resource.texture->resource.Get(), &srv, cpu);
                    }
                } else if (resource.isStorage) {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
                    uav.Format = DXGI_FORMAT_R32_TYPELESS;
                    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                    uav.Buffer.FirstElement = resource.offset / 4u;
                    uav.Buffer.NumElements = static_cast<UINT>(resource.size / 4u);
                    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                    owner->device->CreateUnorderedAccessView(resource.buffer->resource.Get(), nullptr, &uav, cpu);
                } else {
                    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv{resource.buffer->resource->GetGPUVirtualAddress() + resource.offset, static_cast<UINT>(resource.size)};
                    owner->device->CreateConstantBufferView(&cbv, cpu);
                }
                cpu.ptr += increment;
                gpu.ptr += increment;
            }
        }
        descriptorCount += computePipeline->descriptorSize;
        boundComputeResources = std::move(snapshot);
        computeBound = true;
        return {};
    }
    Result<void> dispatch(UINT groupCountX, UINT groupCountY, UINT groupCountZ) override {
        auto status = ready(true); if (!status) return status;
        if (qType == QueueType::Copy) return invalid("Compute operations are not supported on a Copy command list", ErrorCode::InvalidArgument);
        if (!computePipeline) return invalid("dispatch requires an active compute pipeline");
        if (!groupCountX || !groupCountY || !groupCountZ ||
            groupCountX > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION ||
            groupCountY > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION ||
            groupCountZ > D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION) {
            return invalid("dispatch group counts must be 1..65535", ErrorCode::InvalidArgument);
        }
        if (computePipeline->descriptorSize && !computeBound) {
            return invalid("Compute pipeline requires complete bindComputeResources snapshot");
        }
        for (const auto& resource : boundComputeResources) {
            if (resource.texture) {
                const auto expected = resource.isStorage ? ResourceState::UnorderedAccess : ResourceState::ShaderRead;
                if (current(*resource.texture) != expected) return invalid("Bound compute texture left its required state");
            } else if (resource.buffer) {
                const auto expected = resource.isStorage ? ResourceState::UnorderedAccess : ResourceState::ConstantBuffer;
                if (current(*resource.buffer) != expected) return invalid("Bound compute buffer left its required state");
            }
        }
        list->Dispatch(groupCountX, groupCountY, groupCountZ);
        if (owner->profiler) owner->profiler->recordDispatch();
        return {};
    }
    Result<void> dispatchIndirect(const BufferPtr& argsBuffer, std::uint64_t argsOffset) override {
        auto status = ready(true); if (!status) return status;
        if (qType == QueueType::Copy) return invalid("Compute operations are not supported on a Copy command list", ErrorCode::InvalidArgument);
        if (!computePipeline) return invalid("dispatchIndirect requires an active compute pipeline");
        if ((argsOffset % 4u) != 0u) return invalid("dispatchIndirect offset must be 4-byte aligned", ErrorCode::InvalidArgument);
        if (computePipeline->descriptorSize && !computeBound) {
            return invalid("Compute pipeline requires complete bindComputeResources snapshot");
        }
        for (const auto& resource : boundComputeResources) {
            if (resource.texture) {
                const auto expected = resource.isStorage ? ResourceState::UnorderedAccess : ResourceState::ShaderRead;
                if (current(*resource.texture) != expected) return invalid("Bound compute texture left its required state");
            } else if (resource.buffer) {
                const auto expected = resource.isStorage ? ResourceState::UnorderedAccess : ResourceState::ConstantBuffer;
                if (current(*resource.buffer) != expected) return invalid("Bound compute buffer left its required state");
            }
        }
        auto argBuf = owned<DXBuffer>(argsBuffer); if (!argBuf) return std::unexpected(argBuf.error());
        if ((*argBuf)->desc.memory == Memory::Readback)
            return invalid("Readback buffer cannot be used as an indirect argument buffer", ErrorCode::InvalidArgument);
        if (current(**argBuf) != ResourceState::IndirectArgument)
            return invalid("dispatchIndirect requires ResourceState::IndirectArgument");
        if (argsOffset > (*argBuf)->desc.size || sizeof(DispatchIndirectCommand) > (*argBuf)->desc.size - argsOffset)
            return invalid("dispatchIndirect argument range exceeds buffer size", ErrorCode::InvalidArgument);
        auto sig = owner->indirectSignature(2u, sizeof(DispatchIndirectCommand));
        if (!sig) return std::unexpected(sig.error());
        use((*argBuf)->tracker, (*argBuf)->resource, (*argBuf)->desc.memory);
        retain(*sig);
        list->ExecuteIndirect(sig->Get(), 1u, (*argBuf)->resource.Get(), argsOffset, nullptr, 0);
        if (owner->profiler) owner->profiler->recordDispatch();
        return {};
    }
    Result<void> uavBarrier(const BufferPtr& ptr) override {
        auto status = ready(true); if (!status) return status;
        if (qType == QueueType::Copy) return invalid("uavBarrier is not supported on a Copy command list", ErrorCode::InvalidArgument);
        auto object = owned<DXBuffer>(ptr); if (!object) return std::unexpected(object.error());
        if (!(*object)->desc.storage) return invalid("uavBarrier requires a storage buffer", ErrorCode::InvalidArgument);
        if (current(**object) != ResourceState::UnorderedAccess) return invalid("uavBarrier requires UnorderedAccess state");
        use((*object)->tracker, (*object)->resource, (*object)->desc.memory, true);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = (*object)->resource.Get();
        list->ResourceBarrier(1, &barrier);
        if (owner->profiler) owner->profiler->recordUavBarrier();
        return {};
    }
    Result<void> uavBarrier(const TexturePtr& ptr) override {
        auto status = ready(true); if (!status) return status;
        if (qType == QueueType::Copy) return invalid("uavBarrier is not supported on a Copy command list", ErrorCode::InvalidArgument);
        auto object = owned<DXTexture>(ptr); if (!object) return std::unexpected(object.error());
        if (!(*object)->desc.storage) return invalid("uavBarrier requires a storage texture", ErrorCode::InvalidArgument);
        if (current(**object) != ResourceState::UnorderedAccess) return invalid("uavBarrier requires UnorderedAccess state");
        use((*object)->tracker, (*object)->resource, Memory::Device, true);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = (*object)->resource.Get();
        list->ResourceBarrier(1, &barrier);
        if (owner->profiler) owner->profiler->recordUavBarrier();
        return {};
    }
    Result<void> writeTimestamp(UINT index) override {
        auto status = ready(); if (!status) return status;
        if (!query) return invalid("Timestamps are unsupported on Copy command lists", ErrorCode::Unsupported);
        if (index > 1 || timestampWritten[index] || timestampsResolved) return invalid("Timestamp slot invalid or already written", ErrorCode::InvalidArgument);
        list->EndQuery(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index); timestampWritten[index] = true; return {};
    }
    Result<void> resolveTimestamps() override {
        auto status = ready(true); if (!status) return status;
        if (!query) return invalid("Timestamps are unsupported on Copy command lists", ErrorCode::Unsupported);
        if (!timestampWritten[0] || !timestampWritten[1] || timestampsResolved) return invalid("Two timestamps must be written once before resolving");
        list->ResolveQueryData(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, queryReadback.Get(), 0); timestampsResolved = true; return {};
    }
    Result<double> elapsedGpuMilliseconds() override {
        auto done = owner->completed(qType); if (!done) return std::unexpected(done.error());
        if (!query) return invalid("Timestamps are unsupported on Copy command lists", ErrorCode::Unsupported);
        if (!submitted || *done < submitted || !timestampsResolved) return invalid("GPU timestamps are not ready");
        UINT64 frequency{}; HRESULT result = owner->queues[queueIndex(qType)].queue->GetTimestampFrequency(&frequency);
        if (FAILED(result) || !frequency) return owner->failure(FAILED(result) ? result : E_FAIL, "Timestamp frequency unavailable");
        void* data{}; D3D12_RANGE range{0, 16}; result = queryReadback->Map(0, &range, &data);
        if (FAILED(result)) return owner->failure(result, "Timestamp Map failed");
        UINT64 values[2]{}; std::memcpy(values, data, 16); D3D12_RANGE written{0, 0}; queryReadback->Unmap(0, &written);
        if (values[1] < values[0]) return invalid("Timestamps written in reverse order");
        return 1000.0 * static_cast<double>(values[1] - values[0]) / static_cast<double>(frequency);
    }
    Result<void> beginTimestampScope(std::string_view name) override {
        auto status = ready(); if (!status) return status;
        if (!query) return invalid("Timestamps are unsupported on Copy command lists", ErrorCode::Unsupported);
        if (name.empty() || name.size() > 256 || name.find('\0') != std::string_view::npos) {
            return invalid("Timestamp scope name must be non-empty NUL-free UTF-8 up to 256 bytes", ErrorCode::InvalidArgument);
        }
        if (scopesResolved || scopes.size() >= kMaxTimestampScopesPerCommandList ||
            nextScopeSlot + 2u > kMaxTimestampScopesPerCommandList * 2u) {
            return invalid("Timestamp scope capacity exceeded or already resolved");
        }
        if (!scopeReadback) {
            auto rb = makeBuffer(*owner, static_cast<std::uint64_t>(kMaxTimestampScopesPerCommandList) * 2u * sizeof(UINT64), Memory::Readback);
            if (!rb) return std::unexpected(rb.error());
            scopeReadback = std::move(*rb);
        }
        const std::uint32_t beginSlot = nextScopeSlot++;
        list->EndQuery(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2u + beginSlot);
        const std::uint32_t depth = static_cast<std::uint32_t>(openScopeStack.size());
        scopes.push_back(RecordedScope{std::string(name), depth, beginSlot, 0u, false});
        openScopeStack.push_back(static_cast<std::uint32_t>(scopes.size() - 1u));
        return {};
    }
    Result<void> endTimestampScope() override {
        auto status = ready(); if (!status) return status;
        if (!query) return invalid("Timestamps are unsupported on Copy command lists", ErrorCode::Unsupported);
        if (openScopeStack.empty() || scopesResolved) {
            return invalid("No open timestamp scope to end");
        }
        const std::uint32_t idx = openScopeStack.back();
        openScopeStack.pop_back();
        const std::uint32_t endSlot = nextScopeSlot++;
        list->EndQuery(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2u + endSlot);
        scopes[idx].endSlot = endSlot;
        scopes[idx].closed = true;
        return {};
    }
    Result<void> resolveTimestampScopes() override {
        auto status = ready(true); if (!status) return status;
        if (!query) return invalid("Timestamps are unsupported on Copy command lists", ErrorCode::Unsupported);
        if (scopes.empty() || !openScopeStack.empty() || scopesResolved || !scopeReadback) {
            return invalid("Timestamp scopes must be non-empty, balanced, and unresolved before resolving");
        }
        list->ResolveQueryData(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2u, nextScopeSlot, scopeReadback.Get(), 0);
        scopesResolved = true;
        return {};
    }
    Result<std::vector<TimestampScopeResult>> timestampScopes() override {
        auto done = owner->completed(qType); if (!done) return std::unexpected(done.error());
        if (!query) return invalid("Timestamps are unsupported on Copy command lists", ErrorCode::Unsupported);
        if (!submitted || *done < submitted || !scopesResolved || !scopeReadback) {
            return invalid("GPU timestamp scopes are not ready");
        }
        if (cachedScopeResults) return *cachedScopeResults;
        UINT64 frequency{};
        HRESULT result = owner->queues[queueIndex(qType)].queue->GetTimestampFrequency(&frequency);
        if (FAILED(result) || !frequency) {
            return owner->failure(FAILED(result) ? result : E_FAIL, "Timestamp frequency unavailable");
        }
        const std::size_t byteCount = static_cast<std::size_t>(nextScopeSlot) * sizeof(UINT64);
        void* data{};
        D3D12_RANGE range{0, byteCount};
        result = scopeReadback->Map(0, &range, &data);
        if (FAILED(result)) return owner->failure(result, "Timestamp scope Map failed");
        std::vector<UINT64> ticks(nextScopeSlot, 0u);
        std::memcpy(ticks.data(), data, byteCount);
        D3D12_RANGE written{0, 0};
        scopeReadback->Unmap(0, &written);

        std::vector<TimestampScopeResult> out;
        out.reserve(scopes.size());
        for (const auto& s : scopes) {
            const UINT64 startTick = ticks[s.beginSlot];
            const UINT64 endTick = ticks[s.endSlot];
            std::optional<double> gpuMs;
            if (endTick >= startTick) {
                gpuMs = 1000.0 * static_cast<double>(endTick - startTick) / static_cast<double>(frequency);
            }
            out.push_back(TimestampScopeResult{s.name, qType, s.depth, startTick, endTick, gpuMs});
        }
        cachedScopeResults = out;
        if (owner->profiler) owner->profiler->appendGpuScopes(out);
        return out;
    }
    Result<void> debugMarker(std::string_view text) override {
        auto status = ready(); if (!status) return status;
        if (text.size() > UINT_MAX) return invalid("Marker too long", ErrorCode::InvalidArgument);
        list->SetMarker(0, text.data(), static_cast<UINT>(text.size())); return {};
    }
};
Result<std::unique_ptr<CommandList>> makeDXCommands(
    const std::shared_ptr<State>& state,
    std::shared_ptr<DXCommandPoolState> pool = {},
    QueueType qType = QueueType::Graphics) {
    auto commands = std::make_unique<DXCommands>();
    commands->owner = state;
    commands->pool = std::move(pool);
    commands->qType = commands->pool ? commands->pool->qType : qType;
    const D3D12_COMMAND_LIST_TYPE nativeType = d3dQueueType(commands->qType);
    ID3D12CommandAllocator* activeAllocator = nullptr;
    if (commands->pool) {
        activeAllocator = commands->pool->allocator.Get();
    } else {
        HRESULT result = state->device->CreateCommandAllocator(
            nativeType, IID_PPV_ARGS(&commands->allocator));
        if (FAILED(result)) return state->failure(result, "Create allocator failed");
        activeAllocator = commands->allocator.Get();
    }
    HRESULT result = state->device->CreateCommandList(
        0, nativeType, activeAllocator, nullptr, IID_PPV_ARGS(&commands->list));
    if (FAILED(result)) return state->failure(result, "Create command list failed");
    result = commands->list->Close();
    if (FAILED(result)) return state->failure(result, "Initial command list close failed");
    if (commands->qType != QueueType::Copy) {
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 256;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        result = state->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&commands->descriptors));
        if (FAILED(result)) return state->failure(result, "Create command descriptor heap failed");
        D3D12_QUERY_HEAP_DESC queries{};
        queries.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queries.Count = 2u + kMaxTimestampScopesPerCommandList * 2u;
        result = state->device->CreateQueryHeap(&queries, IID_PPV_ARGS(&commands->query));
        if (FAILED(result)) return state->failure(result, "Create timestamp query heap failed");
        auto readback = makeBuffer(*state, 16, Memory::Readback);
        if (!readback) return std::unexpected(readback.error());
        commands->queryReadback = *readback;
    }
    state->track(commands->objects);
    if (commands->pool) {
        commands->pool->liveLists.push_back(commands.get());
    }
    return std::unique_ptr<CommandList>(std::move(commands));
}
struct DXCommandPool final : CommandPool {
    std::shared_ptr<State> owner;
    std::shared_ptr<DXCommandPoolState> pool;
    ~DXCommandPool() override {
        if (pool) {
            std::scoped_lock lock(pool->mutex);
            pool->ownerAlive = false;
        }
    }
    QueueType queueType() const override { return pool->qType; }
    Result<std::unique_ptr<CommandList>> createCommandList() override {
        auto status = owner->check(); if (!status) return std::unexpected(status.error());
        std::scoped_lock lock(pool->mutex);
        if (pool->openCount > 0) {
            return invalid("Cannot allocate from a CommandPool while a command list from it is recording");
        }
        return makeDXCommands(owner, pool, pool->qType);
    }
    Result<void> reset() override {
        auto done = owner->completed(pool->qType); if (!done) return std::unexpected(done.error());
        std::scoped_lock lock(pool->mutex);
        if (pool->openCount > 0) {
            return invalid("Command pool cannot reset while a command list is recording");
        }
        if (pool->lastSubmitted && *done < pool->lastSubmitted) {
            return invalid("Command pool has in-flight submissions");
        }
        HRESULT result = pool->allocator->Reset();
        if (FAILED(result)) return owner->failure(result, "Command pool allocator reset failed");
        for (DXCommands* cmd : pool->liveLists) {
            cmd->clearRecordingState();
            cmd->recordedEpoch = 0;
        }
        pool->lastSubmitted = 0;
        ++pool->epoch;
        return {};
    }
    Result<void> bindRecordingThread() override {
        std::scoped_lock lock(pool->mutex);
        if (auto alive = owner->checkAliveForWorker(); !alive) return alive;
        if (!pool->ownerAlive) return invalid("Owning CommandPool has been destroyed");
        if (pool->openCount > 0) {
            return invalid("Command pool cannot rebind recording thread while a command list is recording");
        }
        if (pool->lastSubmitted) {
            const auto completedVal = owner->queues[queueIndex(pool->qType)].fence->GetCompletedValue();
            if (completedVal == UINT64_MAX) return owner->failure(DXGI_ERROR_DEVICE_REMOVED, "Device lost while checking fence");
            if (completedVal < pool->lastSubmitted) {
                return invalid("Command pool cannot rebind recording thread while submissions are in flight");
            }
        }
        pool->recordingThread.store(std::this_thread::get_id(), std::memory_order_release);
        return {};
    }
};
struct NativeSwapChain final : Retirable {
    std::shared_ptr<SwapChainScope> scope{std::make_shared<SwapChainScope>()};
    ComPtr<IDXGISwapChain3> chain;
    HANDLE waitableObject{};
    void closeWaitable() {
        if (waitableObject) { CloseHandle(waitableObject); waitableObject = nullptr; }
    }
    void releaseNative() override { scope->chain = nullptr; closeWaitable(); chain.Reset(); }
    ~NativeSwapChain() override { scope->chain = nullptr; closeWaitable(); }
};
struct DXSwapChain final : SwapChain {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeSwapChain> objects{std::make_shared<NativeSwapChain>()};
    ComPtr<IDXGISwapChain3>& chain{objects->chain};
    HWND window{};
    bool lastOccluded{};
    bool hasFullPresent{};
    SwapChainDesc desc;
    PresentMode activeMode{PresentMode::Fifo};
    Format activeFormat{Format::RGBA8UNorm};
    DXGI_FORMAT nativeStorageFormat{DXGI_FORMAT_R8G8B8A8_UNORM};
    ColorSpace activeColorSpace{ColorSpace::SrgbNonlinear};
    DXGI_COLOR_SPACE_TYPE dxgiColorSpace{DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709};
    std::optional<HdrMetadata> activeMetadata;
    UINT swapFlags{};
    std::uint32_t effectiveLatency{1};
    mutable detail::FramePacingTracker pacing;
    std::vector<std::shared_ptr<DXTexture>> buffers;
    Result<void> applyMetadataNative(const HdrMetadata& metadata) {
        ComPtr<IDXGISwapChain4> chain4;
        if (FAILED(chain.As(&chain4))) {
            return invalid("IDXGISwapChain4 HDR metadata interface unavailable", ErrorCode::Unsupported);
        }
        DXGI_HDR_METADATA_HDR10 hdr10 = makeDxgiHdr10Metadata(metadata);
        const HRESULT hr = chain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(hdr10), &hdr10);
        if (hr == DXGI_ERROR_UNSUPPORTED || hr == E_NOINTERFACE) {
            return invalid("Swapchain does not support HDR10 static metadata", ErrorCode::Unsupported);
        }
        if (FAILED(hr)) return owner->failure(hr, "SetHDRMetaData failed");
        activeMetadata = metadata;
        return {};
    }
    Result<void> rebuild() {
        for (UINT i = 0; i < desc.buffers; ++i) {
            auto texture = std::make_shared<DXTexture>(); texture->owner = owner;
            texture->desc = {desc.width, desc.height, activeFormat, true}; texture->backbuffer = true;
            texture->tracker->state = ResourceState::Present;
            texture->tracker->backbuffer=true;texture->tracker->bufferIndex=i;texture->tracker->swapchain=objects->scope;
            HRESULT result = chain->GetBuffer(i, IID_PPV_ARGS(&texture->resource));
            if (FAILED(result)) return owner->failure(result, "Get swapchain buffer failed");
            auto status = texture->initViews(); if (!status) return status;
            owner->track(texture);
            buffers.push_back(std::move(texture));
        }
        return {};
    }
    Result<TexturePtr> acquire() override {
        auto status = owner->check(); if (!status) return std::unexpected(status.error());
        if (buffers.size() != desc.buffers) return invalid("Swapchain buffers unavailable");
        if (!pacing.hasAcquireStart) {
            pacing.recordAcquireWait(0.0, detail::FramePacingTracker::Clock::now());
        }
        return buffers[chain->GetCurrentBackBufferIndex()];
    }
    Result<void> present() override {
        auto status = owner->check(); if (!status) return status;
        if (buffers.size() != desc.buffers) return invalid("Swapchain buffers unavailable");
        auto texture = buffers[chain->GetCurrentBackBufferIndex()];
        if (texture->tracker->state != ResourceState::Present) return invalid("Backbuffer must be submitted in Present state");
        if (texture->tracker->version == UINT64_MAX) return invalid("Backbuffer version exhausted");
        const UINT syncInterval = activeMode == PresentMode::Fifo ? 1u : 0u;
        const UINT flags = activeMode == PresentMode::Tearing ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        const auto t0 = detail::FramePacingTracker::Clock::now();
        HRESULT result = chain->Present(syncInterval, flags);
        const auto t1 = detail::FramePacingTracker::Clock::now();
        if (FAILED(result)) return owner->failure(result, "Present failed");
        lastOccluded = (result == DXGI_STATUS_OCCLUDED);
        hasFullPresent = true;
        const double presentMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        pacing.recordPresent(presentMs, t1);
        // Invalidate recordings from before presentation, even after the ring
        // rotates back to this same buffer without another application submit.
        ++texture->tracker->version;return {};
    }
    Result<void> presentDirtyRects(std::span<const DirtyRect> rects) override {
        auto status = owner->check(); if (!status) return status;
        if (buffers.size() != desc.buffers) return invalid("Swapchain buffers unavailable");
        if (auto ok = detail::validateDirtyRects(rects, desc.width, desc.height, 64u); !ok) return ok;
        if (activeMode == PresentMode::Tearing) {
            return invalid("Dirty-rect partial present is unsupported with Tearing present mode", ErrorCode::Unsupported);
        }
        auto texture = buffers[chain->GetCurrentBackBufferIndex()];
        if (texture->tracker->state != ResourceState::Present) return invalid("Backbuffer must be submitted in Present state");
        if (texture->tracker->version == UINT64_MAX) return invalid("Backbuffer version exhausted");
        std::vector<RECT> nativeRects;
        nativeRects.reserve(rects.size());
        for (const auto& r : rects) {
            nativeRects.push_back(RECT{
                static_cast<LONG>(r.x),
                static_cast<LONG>(r.y),
                static_cast<LONG>(r.x + r.width),
                static_cast<LONG>(r.y + r.height)
            });
        }
        DXGI_PRESENT_PARAMETERS params{};
        if (hasFullPresent) {
            params.DirtyRectsCount = static_cast<UINT>(nativeRects.size());
            params.pDirtyRects = nativeRects.data();
        }
        const UINT syncInterval = activeMode == PresentMode::Fifo ? 1u : 0u;
        const auto t0 = detail::FramePacingTracker::Clock::now();
        HRESULT result = chain->Present1(syncInterval, 0u, &params);
        const auto t1 = detail::FramePacingTracker::Clock::now();
        if (FAILED(result)) return owner->failure(result, "Present1 (dirty rects) failed");
        lastOccluded = (result == DXGI_STATUS_OCCLUDED);
        hasFullPresent = true;
        const double presentMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        pacing.recordPresent(presentMs, t1);
        ++texture->tracker->version;
        return {};
    }
    Result<void> resize(UINT width, UINT height) override {
        auto status = owner->check(); if (!status) return status;
        if (!width || !height || width > 16384 || height > 16384) return invalid("Invalid swapchain extent", ErrorCode::InvalidArgument);
        for (const auto& buffer : buffers) if (buffer.use_count() != 1 || buffer->tracker.use_count() != 1) return invalid("Release acquired backbuffers and command references before resizing");
        status = owner->idle(); if (!status) return status;
        buffers.clear();
        HRESULT result = chain->ResizeBuffers(desc.buffers, width, height, nativeStorageFormat, swapFlags);
        if (FAILED(result)) {
            auto error=owner->failure(result,"ResizeBuffers failed");
            if(error.error().code==ErrorCode::DeviceLost)return error;
            auto restored=rebuild();if(!restored && restored.error().code==ErrorCode::DeviceLost)return restored;
            return error;
        }
        (void)chain->SetColorSpace1(dxgiColorSpace);
        if (activeMetadata) { (void)applyMetadataNative(*activeMetadata); }
        hasFullPresent = false;
        desc.width = width; desc.height = height; return rebuild();
    }
    PresentMode presentMode() const override { return activeMode; }
    FramePacingMode pacingMode() const override { return desc.pacingMode; }
    std::uint32_t maxFrameLatency() const override { return effectiveLatency; }
    Format format() const override { return activeFormat; }
    ColorSpace colorSpace() const override { return activeColorSpace; }
    Result<void> setHdrMetadata(const HdrMetadata& metadata) override {
        auto status = owner->check(); if (!status) return status;
        if (auto ok = detail::validateHdrMetadata(metadata); !ok) return ok;
        return applyMetadataNative(metadata);
    }
    std::optional<HdrMetadata> hdrMetadata() const override { return activeMetadata; }
    std::uint32_t width() const override { return desc.width; }
    std::uint32_t height() const override { return desc.height; }
    Result<SurfaceMetrics> surfaceMetrics() const override {
        auto status = owner->check(); if (!status) return std::unexpected(status.error());
        return queryWin32Metrics(window, lastOccluded);
    }
    Result<bool> syncExtent() override {
        auto m = surfaceMetrics(); if (!m) return std::unexpected(m.error());
        if (m->minimized) return invalid("Surface is minimized; defer rendering until restored", ErrorCode::InvalidState);
        if (m->clientWidth == desc.width && m->clientHeight == desc.height) return false;
        if (auto ok = resize(m->clientWidth, m->clientHeight); !ok) return std::unexpected(ok.error());
        return true;
    }
    Result<void> waitForNextFrame(std::chrono::milliseconds timeout) override {
        auto status = owner->check(); if (!status) return status;
        if (timeout.count() < 0 || timeout.count() > 4294967294ll)
            return invalid("SwapChain wait timeout outside finite range", ErrorCode::InvalidArgument);
        if (!objects->waitableObject) return {};
        const auto t0 = detail::FramePacingTracker::Clock::now();
        const DWORD waitResult = WaitForSingleObjectEx(objects->waitableObject, static_cast<DWORD>(timeout.count()), FALSE);
        const auto t1 = detail::FramePacingTracker::Clock::now();
        if (waitResult == WAIT_TIMEOUT) {
            pacing.recordStall();
            return invalid("SwapChain frame latency wait timed out", ErrorCode::Timeout);
        }
        if (waitResult != WAIT_OBJECT_0) {
            return invalid("SwapChain frame latency wait failed", ErrorCode::BackendFailure);
        }
        const double waitMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        pacing.recordAcquireWait(waitMs, t0);
        return {};
    }
    Result<FramePacingStats> framePacingStats() const override {
        auto status = owner->check(); if (!status) return std::unexpected(status.error());
        return pacing.snapshot();
    }
    Result<void> resetFramePacingStats() override {
        auto status = owner->check(); if (!status) return status;
        pacing.reset();
        return {};
    }
    ~DXSwapChain() override {
        if (owner) { auto waited = owner->idle(); (void)waited; }
    }
};
struct DXQueue final : CommandQueue {
    std::shared_ptr<State> state;
    QueueType qType{QueueType::Graphics};
    QueueType type() const override { return qType; }
    Result<Fence> submit(CommandList& commands) override {
        CommandList* single = &commands;
        return submit(std::span<CommandList* const>{&single, 1});
    }
    Result<Fence> submit(std::span<CommandList* const> lists) override {
        SubmitDesc desc{};
        desc.commandLists = lists;
        return submit(desc);
    }
    Result<Fence> submit(const SubmitDesc& desc) override {
        auto status = state->completed(qType); if (!status) return std::unexpected(status.error());
        if (desc.commandLists.empty()) return invalid("Batch submit requires at least one command list", ErrorCode::InvalidArgument);
        auto& q = state->queues[queueIndex(qType)];
        if (q.last >= UINT64_MAX - 1) return invalid("Fence timeline exhausted");
        std::vector<DXCommands*> validated;
        validated.reserve(desc.commandLists.size());
        std::unordered_set<DXCommands*> seenLists;
        std::unordered_set<Tracker*> seenRead;
        std::unordered_set<Tracker*> seenMutated;
        for (CommandList* item : desc.commandLists) {
            if (!item) return invalid("Null command list in batch submit", ErrorCode::InvalidArgument);
            auto* cmd = dynamic_cast<DXCommands*>(item);
            if (!cmd || cmd->owner != state) return invalid("Command list belongs to another device", ErrorCode::WrongDevice);
            if (cmd->qType != qType) return invalid("Command list queue type does not match target queue", ErrorCode::InvalidArgument);
            if (!seenLists.insert(cmd).second) return invalid("Duplicate command list in batch submit", ErrorCode::InvalidArgument);
            if (cmd->pool) {
                std::scoped_lock lock(cmd->pool->mutex);
                if (!cmd->pool->ownerAlive) return invalid("Owning CommandPool has been destroyed");
                if (cmd->pool->openCount > 0) return invalid("Command pool has a command list currently recording");
                if (cmd->recordedEpoch != cmd->pool->epoch) return invalid("Pooled command list was reset and must be re-recorded");
            }
            if (!cmd->ended || cmd->recording || cmd->submitted) return invalid("Command list is not executable or was already submitted");
            for (const auto& usage : cmd->uses) {
                if (!usage.tracker->available()) return invalid("Recorded backbuffer is not current or its swapchain has been destroyed");
                if (usage.tracker->heapResident && !usage.tracker->heapResident->load(std::memory_order_acquire))
                    return invalid("Cannot submit command list referencing an evicted MemoryHeap");
                if (usage.observedVersion != usage.tracker->version || usage.tracker->version == UINT64_MAX)
                    return invalid("Resource changed after recording; re-record the command list");
                Tracker* key = usage.tracker.get();
                if (usage.mutated) {
                    if (seenMutated.contains(key) || seenRead.contains(key))
                        return invalid("Batch submit has conflicting resource mutations across command lists");
                    seenMutated.insert(key);
                } else {
                    if (seenMutated.contains(key))
                        return invalid("Batch submit has conflicting resource mutation and read across command lists");
                    seenRead.insert(key);
                }
            }
            validated.push_back(cmd);
        }
        std::vector<std::pair<std::shared_ptr<DXTimelineFence>, std::uint64_t>> waitFences;
        waitFences.reserve(desc.waits.size());
        for (const auto& w : desc.waits) {
            if (!w.fence) return invalid("Null timeline fence in submit waits", ErrorCode::InvalidArgument);
            auto dxFence = std::dynamic_pointer_cast<DXTimelineFence>(w.fence);
            if (!dxFence || dxFence->owner != state) return invalid("Timeline fence belongs to another device", ErrorCode::WrongDevice);
            waitFences.emplace_back(std::move(dxFence), w.value);
        }
        std::vector<std::pair<std::shared_ptr<DXTimelineFence>, std::uint64_t>> signalFences;
        signalFences.reserve(desc.signals.size());
        std::unordered_map<DXTimelineFence*, std::uint64_t> pendingSignalMax;
        for (const auto& s : desc.signals) {
            if (!s.fence) return invalid("Null timeline fence in submit signals", ErrorCode::InvalidArgument);
            auto dxFence = std::dynamic_pointer_cast<DXTimelineFence>(s.fence);
            if (!dxFence || dxFence->owner != state) return invalid("Timeline fence belongs to another device", ErrorCode::WrongDevice);
            const auto completedVal = dxFence->completedValue();
            if (!completedVal) return std::unexpected(completedVal.error());
            const auto it = pendingSignalMax.find(dxFence.get());
            const std::uint64_t prev = it != pendingSignalMax.end()
                ? it->second
                : std::max(dxFence->lastSignaled, *completedVal);
            if (s.value <= prev || s.value == UINT64_MAX) {
                return invalid("TimelineFence signal value must strictly advance", ErrorCode::InvalidArgument);
            }
            pendingSignalMax[dxFence.get()] = s.value;
            signalFences.emplace_back(std::move(dxFence), s.value);
        }
        Batch batch; batch.fence = q.last + 1;
        std::unordered_set<IUnknown*> retainedBatch;
        auto addUnique = [&](const ComPtr<IUnknown>& ref) {
            if (ref && retainedBatch.insert(ref.Get()).second) batch.objects.push_back(ref);
        };
        auto addCom = [&](const auto& ptr) {
            ComPtr<IUnknown> ref;
            if (ptr && SUCCEEDED(ptr.As(&ref))) addUnique(ref);
        };
        std::vector<ID3D12CommandList*> nativeLists;
        nativeLists.reserve(validated.size());
        for (DXCommands* cmd : validated) {
            for (const auto& ref : cmd->references) addUnique(ref);
            for (const auto& usage : cmd->uses) {
                if (usage.resource && usage.tracker && usage.tracker->placedHeap)
                    batch.placedResources.insert(usage.resource.Get());
            }
            addCom(cmd->pool ? cmd->pool->allocator : cmd->allocator);
            addCom(cmd->list); addCom(cmd->descriptors); addCom(cmd->query); addCom(cmd->queryReadback); addCom(cmd->scopeReadback);
            nativeLists.push_back(cmd->list.Get());
        }
        for (const auto& [wf, _] : waitFences) addCom(wf->fence);
        for (const auto& [sf, _] : signalFences) addCom(sf->fence);
        // Allocate retention before issuing GPU queue work; failure cannot orphan submitted GPU work.
        q.pending.push_back(std::move(batch));
        for (const auto& [wf, val] : waitFences) {
            HRESULT hr = q.queue->Wait(wf->fence.Get(), val);
            if (FAILED(hr)) return state->failure(hr, "Queue timeline wait failed");
        }
        q.queue->ExecuteCommandLists(static_cast<UINT>(nativeLists.size()), nativeLists.data());
        ++q.last;
        const std::size_t qi = queueIndex(qType);
        std::unordered_set<Tracker*> versionBumped;
        for (DXCommands* cmd : validated) {
            cmd->submitted = q.last;
            if (cmd->pool) {
                std::scoped_lock lock(cmd->pool->mutex);
                cmd->pool->lastSubmitted = std::max(cmd->pool->lastSubmitted, q.last);
            }
            for (auto& usage : cmd->uses) {
                if (usage.mutated) {
                    usage.tracker->state = usage.state;
                    usage.tracker->nativeState = usage.nativeState;
                }
                if (qType == QueueType::Copy && usage.memory == Memory::Device) {
                    // D3D12 Copy queue decays default-heap resources back to COMMON after ExecuteCommandLists.
                    usage.tracker->nativeState = D3D12_RESOURCE_STATE_COMMON;
                }
                usage.tracker->lastUse[qi] = q.last;
                if (versionBumped.insert(usage.tracker.get()).second) ++usage.tracker->version;
            }
        }
        // Signal the internal queue fence before user timeline fences so observing a
        // timeline signal guarantees the queue's internal fence has also retired.
        HRESULT result = q.queue->Signal(q.fence.Get(), q.last);
        if (FAILED(result)) return state->failure(result, "Queue signal failed after execution");
        for (const auto& [sf, val] : signalFences) {
            result = q.queue->Signal(sf->fence.Get(), val);
            if (FAILED(result)) return state->failure(result, "Queue timeline signal failed");
            sf->lastSignaled = val;
        }
        if (state->profiler) state->profiler->recordSubmit();
        return Fence{q.last, q.id};
    }
    Result<void> wait(Fence fence, std::chrono::milliseconds timeout) override {
        auto& q = state->queues[queueIndex(qType)];
        if (fence.device != q.id) return invalid("Fence belongs to another device or queue", ErrorCode::WrongDevice);
        return state->wait(qType, fence.value, timeout);
    }
    Result<void> waitIdle() override { return state->idleQueue(qType); }
    Result<std::uint64_t> completedValue() override { return state->completed(qType); }
};
class DXDevice final : public Device {
    std::shared_ptr<State> state;
    std::array<DXQueue, 3> queueViews;
public:
    explicit DXDevice(std::shared_ptr<State> value) : state(std::move(value)) {
        state->profiler = profilingTracker();
        const std::array<QueueType, 3> types{QueueType::Graphics, QueueType::Compute, QueueType::Copy};
        for (std::size_t i = 0; i < 3; ++i) {
            queueViews[i].state = state;
            queueViews[i].qType = types[i];
        }
    }
    const DeviceCaps& caps() const override { return state->caps; }
    Result<FormatSupport> queryFormatSupport(Format format) override {
        auto status = state->check();
        if (!status) return std::unexpected(status.error());
        const auto blockInfo = queryFormatBlockInfo(format);
        if (!blockInfo) {
            return invalid("Unknown texture format", ErrorCode::InvalidArgument);
        }
        const DXGI_FORMAT dxgiFormat = native(format);
        FormatSupport result{};
        result.format = format;
        if (dxgiFormat == DXGI_FORMAT_UNKNOWN) {
            result.sampleCounts = 0u;
            return result;
        }
        D3D12_FEATURE_DATA_FORMAT_SUPPORT nativeSupport{};
        nativeSupport.Format = dxgiFormat;
        const HRESULT hr = state->device->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT, &nativeSupport, sizeof(nativeSupport));
        if (FAILED(hr)) return state->failure(hr, "Query D3D12 format support failed");
        const auto s1 = nativeSupport.Support1;
        const bool isDepth = format == Format::D32Float;
        const bool isCompressed = blockInfo->compressed;
        result.texture2D = (s1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0;
        result.renderTarget = !isDepth && !isCompressed && (s1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0;
        result.depthStencil = isDepth && (s1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) != 0;
        result.sampled = !isDepth && (s1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0;
        result.linearFilter = result.sampled;
        result.blendable = result.renderTarget && (s1 & D3D12_FORMAT_SUPPORT1_BLENDABLE) != 0;
        result.copySource = result.texture2D;
        result.copyDestination = result.texture2D;
        result.partialRegionCopy = result.texture2D && !isDepth;
        result.storage = !isDepth && !isCompressed && (s1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0;
        result.sampleCounts = result.texture2D ? 1u : 0u;
        const bool msaaEligible = isDepth
            ? result.depthStencil
            : (result.renderTarget &&
               (s1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET) != 0 &&
               (s1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE) != 0);
        if (msaaEligible) {
            for (const UINT count : {2u, 4u, 8u}) {
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaa{};
                msaa.Format = dxgiFormat;
                msaa.SampleCount = count;
                if (SUCCEEDED(state->device->CheckFeatureSupport(
                        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaa, sizeof(msaa))) &&
                    msaa.NumQualityLevels > 0) {
                    result.sampleCounts |= count;
                }
            }
        }
        return result;
    }
    Result<void> checkStatus() override {return state->check();}
    Result<MemoryBudget> queryMemoryBudget(MemorySegment segment) override {
        auto status=state->check();if(!status)return std::unexpected(status.error());
        if(segment!=MemorySegment::Local && segment!=MemorySegment::NonLocal)
            return invalid("Invalid memory segment",ErrorCode::InvalidArgument);
        ComPtr<IDXGIAdapter3> adapter;
        auto hr=state->factory->EnumAdapterByLuid(state->device->GetAdapterLuid(),IID_PPV_ARGS(&adapter));
        if(hr==E_NOINTERFACE || hr==DXGI_ERROR_UNSUPPORTED)
            return std::unexpected(Error{ErrorCode::Unsupported,"Adapter memory budget query is unavailable",hr});
        if(FAILED(hr))return state->failure(hr,"Memory budget adapter lookup failed");
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        hr=adapter->QueryVideoMemoryInfo(0,segment==MemorySegment::Local?DXGI_MEMORY_SEGMENT_GROUP_LOCAL:DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&info);
        if(hr==DXGI_ERROR_UNSUPPORTED || hr==E_NOTIMPL)
            return std::unexpected(Error{ErrorCode::Unsupported,"Memory segment query is unavailable",hr});
        if(FAILED(hr))return state->failure(hr,"Memory budget query failed");
        return MemoryBudget{adapterId(state->device->GetAdapterLuid()),segment,info.Budget,info.CurrentUsage,info.AvailableForReservation,info.CurrentReservation};
    }
    Result<ResourceStatistics> resourceStatistics() override {
        auto done = state->completedAll(); if (!done) return std::unexpected(done.error());
        ResourceStatistics result;
        std::vector<ComPtr<ID3D12Resource>> resources;
        std::vector<ComPtr<ID3D12Heap>> heaps;
        for (const auto& child : state->children) if (auto live = child.lock()) {
            ++result.trackedObjects; live->appendResources(resources); live->appendHeaps(heaps);
        }
        std::unordered_set<ID3D12Resource*> placedSet;
        for (const auto& q : state->queues) {
            result.pendingSubmissions += q.pending.size();
            for (const auto& batch : q.pending) {
                placedSet.insert(batch.placedResources.begin(), batch.placedResources.end());
                for (const auto& object : batch.objects) {
                    ++result.pendingObjectReferences;
                    ComPtr<ID3D12Resource> resource;
                    if (object && SUCCEEDED(object.As(&resource))) {
                        resources.push_back(std::move(resource));
                        continue;
                    }
                    ComPtr<ID3D12Heap> heap;
                    if (object && SUCCEEDED(object.As(&heap))) heaps.push_back(std::move(heap));
                }
            }
        }
        std::unordered_set<ID3D12Resource*> unique;
        for (const auto& resource : resources) {
            if (placedSet.contains(resource.Get())) continue;
            if (unique.insert(resource.Get()).second) {
                const auto desc = resource->GetDesc();
                const auto bytes = state->device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
                if (bytes == UINT64_MAX || bytes > UINT64_MAX - result.resourceAllocationBytes)
                    return invalid("Resource allocation statistics overflow", ErrorCode::BackendFailure);
                ++result.uniqueResources; result.resourceAllocationBytes += bytes;
            }
        }
        std::unordered_set<ID3D12Heap*> uniqueHeaps;
        for (const auto& heap : heaps) if (uniqueHeaps.insert(heap.Get()).second) {
            const auto bytes = heap->GetDesc().SizeInBytes;
            if (bytes > UINT64_MAX - result.resourceAllocationBytes)
                return invalid("Resource allocation statistics overflow", ErrorCode::BackendFailure);
            ++result.uniqueResources; result.resourceAllocationBytes += bytes;
        }
        return result;
    }
    Result<void> removeForTesting() override {
        auto status=state->check();if(!status)return status;
        if(!state->description.enableDeviceLossTesting)return invalid("Native removal requires enableDeviceLossTesting",ErrorCode::Unsupported);
        ComPtr<ID3D12Device5> device5;
        if(FAILED(state->device.As(&device5)))return invalid("Native device removal is unavailable",ErrorCode::Unsupported);
        device5->RemoveDevice();
        auto removed=state->check();
        if(removed)return invalid("Native removal did not report device loss",ErrorCode::BackendFailure);
        return {};
    }
    Result<std::unique_ptr<Device>> recover() override {
        if(state->thread!=std::this_thread::get_id())return invalid("Recovery must run on the creating thread");
        if(state->recoverySucceeded)return invalid("This generation has already been recovered");
        auto status=state->check();
        if(status || status.error().code!=ErrorCode::DeviceLost)return invalid("Recovery requires an observed device loss");
        if(!state->retired.load(std::memory_order_acquire))state->retire();
        auto fresh=createD3D12Device(state->description);
        if(fresh)state->recoverySucceeded=true;
        return fresh;
    }
    CommandQueue& queue() override { return queueViews[0]; }
    Result<CommandQueue*> queue(QueueType type) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!validQueueType(type)) return invalid("Unknown queue type", ErrorCode::InvalidArgument);
        return &queueViews[queueIndex(type)];
    }
    Result<BufferPtr> createBuffer(BufferDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!desc.size || desc.size > SIZE_MAX || desc.size > (1ull << 40) || (desc.memory != Memory::Device && desc.memory != Memory::Upload && desc.memory != Memory::Readback) ||
            (desc.storage && desc.memory != Memory::Device)) return invalid("Invalid buffer description", ErrorCode::InvalidArgument);
        if (desc.shared && desc.memory != Memory::Device) return invalid("Shared interop buffers require Device memory", ErrorCode::Unsupported);
        auto resource = makeBuffer(*state, desc.size, desc.memory, desc.storage, desc.shared); if (!resource) return std::unexpected(resource.error());
        auto buffer = std::make_shared<DXBuffer>(); buffer->owner = state; buffer->resource = std::move(*resource); buffer->desc = desc;
        if (desc.memory == Memory::Upload) buffer->tracker->state = ResourceState::CopySource;
        if (desc.memory == Memory::Readback) buffer->tracker->state = ResourceState::CopyDestination;
        state->track(buffer);
        return buffer;
    }
    Result<void> validateTextureForDevice(const TextureDesc& desc) {
        auto status = validateTextureDescription(desc);
        if (!status) return status;
        const DXGI_FORMAT dxgiFormat = native(desc.format);
        if (dxgiFormat == DXGI_FORMAT_UNKNOWN) {
            return invalid("D3D12 adapter does not support this compressed texture format", ErrorCode::Unsupported);
        }
        const auto blockInfo = queryFormatBlockInfo(desc.format);
        if (blockInfo && blockInfo->compressed) {
            auto support = queryFormatSupport(desc.format);
            if (!support) return std::unexpected(support.error());
            if (!support->texture2D || !support->sampled) {
                return invalid("D3D12 adapter does not support requested compressed texture format", ErrorCode::Unsupported);
            }
        }
        if (desc.sampleCount > 1u) {
            auto support = queryFormatSupport(desc.format);
            if (!support) return std::unexpected(support.error());
            if ((support->sampleCounts & desc.sampleCount) == 0u) {
                return invalid("Adapter does not support requested MSAA sample count for this format", ErrorCode::Unsupported);
            }
        }
        if (desc.storage) {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
            support.Format = dxgiFormat;
            const HRESULT hr = state->device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
            if (FAILED(hr)) return state->failure(hr, "Query D3D12 storage format support failed");
            if ((support.Support1 & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) == 0) {
                return invalid("Adapter does not support typed UAV storage for this texture format", ErrorCode::Unsupported);
            }
        }
        return {};
    }
    Result<TexturePtr> createTexture(TextureDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        status = validateTextureForDevice(desc); if (!status) return std::unexpected(status.error());
        auto texture = std::make_shared<DXTexture>(); texture->owner = state; texture->desc = desc;
        auto nativeDesc = textureNativeDesc(desc);
        if (desc.shared && !desc.depthStencil && desc.sampleCount == 1u) {
            nativeDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        }
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        const D3D12_HEAP_FLAGS heapFlags = desc.shared ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE;
        HRESULT result = state->device->CreateCommittedResource(&heap, heapFlags, &nativeDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture->resource));
        if (FAILED(result)) return state->failure(result, "Create texture failed");
        status = texture->initViews(); if (!status) return std::unexpected(status.error());
        state->track(texture);
        return texture;
    }
    Result<ResourceMemoryRequirements> queryBufferMemoryRequirements(BufferDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!desc.size || desc.size > SIZE_MAX || desc.memory != Memory::Device) {
            return invalid("Placed buffer requires non-empty Device memory", ErrorCode::InvalidArgument);
        }
        auto nativeDesc = bufferDescription(desc.size);
        if (desc.storage) nativeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        const auto info = state->device->GetResourceAllocationInfo(0, 1, &nativeDesc);
        if (info.SizeInBytes == UINT64_MAX) {
            return invalid("Failed to query D3D12 buffer allocation info", ErrorCode::BackendFailure);
        }
        return ResourceMemoryRequirements{
            info.SizeInBytes,
            info.Alignment ? info.Alignment : static_cast<std::uint64_t>(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
            MemoryHeapType::Buffers
        };
    }
    Result<ResourceMemoryRequirements> queryTextureMemoryRequirements(TextureDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        status = validateTextureForDevice(desc); if (!status) return std::unexpected(status.error());
        const auto nativeDesc = textureNativeDesc(desc);
        const auto info = state->device->GetResourceAllocationInfo(0, 1, &nativeDesc);
        if (info.SizeInBytes == UINT64_MAX) {
            return invalid("Failed to query D3D12 texture allocation info", ErrorCode::BackendFailure);
        }
        const MemoryHeapType heapType = (desc.renderTarget || desc.depthStencil)
            ? MemoryHeapType::RenderTargets
            : MemoryHeapType::Textures;
        return ResourceMemoryRequirements{
            info.SizeInBytes,
            info.Alignment ? info.Alignment : static_cast<std::uint64_t>(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
            heapType
        };
    }
    Result<MemoryHeapPtr> createMemoryHeap(MemoryHeapDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        status = validateMemoryHeapDesc(desc); if (!status) return std::unexpected(status.error());
        D3D12_HEAP_DESC nativeDesc{};
        nativeDesc.SizeInBytes = desc.size;
        nativeDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        if (desc.type == MemoryHeapType::RenderTargets &&
            desc.size >= D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT &&
            (desc.size % D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT) == 0u) {
            nativeDesc.Alignment = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;
        } else {
            nativeDesc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        }
        nativeDesc.Flags = desc.type == MemoryHeapType::Buffers
            ? D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS
            : desc.type == MemoryHeapType::Textures
                ? D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES
                : D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
        auto heap = std::make_shared<DXMemoryHeap>();
        heap->owner = state;
        heap->desc = desc;
        heap->heapAlignment = nativeDesc.Alignment;
        const HRESULT hr = state->device->CreateHeap(&nativeDesc, IID_PPV_ARGS(&heap->heap));
        if (FAILED(hr)) return state->failure(hr, "CreateHeap failed");
        state->track(heap);
        return MemoryHeapPtr(std::move(heap));
    }
    Result<BufferPtr> createPlacedBuffer(const MemoryHeapPtr& heap, std::uint64_t heapOffset, BufferDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!heap) return invalid("Null MemoryHeap", ErrorCode::InvalidArgument);
        auto dxHeap = std::dynamic_pointer_cast<DXMemoryHeap>(heap);
        if (!dxHeap || dxHeap->owner != state) return invalid("MemoryHeap belongs to another device", ErrorCode::WrongDevice);
        if (!dxHeap->residentFlag->load(std::memory_order_acquire)) {
            return invalid("Cannot create placed buffer on an evicted MemoryHeap");
        }
        auto req = queryBufferMemoryRequirements(desc); if (!req) return std::unexpected(req.error());
        if (dxHeap->desc.type != req->heapType) {
            return invalid("MemoryHeapType mismatch for placed buffer", ErrorCode::InvalidArgument);
        }
        if (heapOffset % req->alignment != 0u) {
            return invalid("Placed buffer heapOffset is not aligned", ErrorCode::InvalidArgument);
        }
        if (heapOffset > dxHeap->desc.size || req->size > dxHeap->desc.size - heapOffset) {
            return invalid("Placed buffer exceeds MemoryHeap size", ErrorCode::InvalidArgument);
        }
        auto nativeDesc = bufferDescription(desc.size);
        if (desc.storage) nativeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        auto buffer = std::make_shared<DXBuffer>();
        buffer->owner = state;
        buffer->desc = desc;
        const HRESULT hr = state->device->CreatePlacedResource(
            dxHeap->heap.Get(), heapOffset, &nativeDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&buffer->resource));
        if (FAILED(hr)) return state->failure(hr, "CreatePlacedResource(buffer) failed");
        buffer->placedHeap = dxHeap;
        buffer->tracker->placedHeap = dxHeap->heap;
        buffer->tracker->heapResident = dxHeap->residentFlag;
        dxHeap->entries.push_back({buffer->tracker, heapOffset, req->size});
        state->track(buffer);
        return BufferPtr(std::move(buffer));
    }
    Result<TexturePtr> createPlacedTexture(const MemoryHeapPtr& heap, std::uint64_t heapOffset, TextureDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!heap) return invalid("Null MemoryHeap", ErrorCode::InvalidArgument);
        auto dxHeap = std::dynamic_pointer_cast<DXMemoryHeap>(heap);
        if (!dxHeap || dxHeap->owner != state) return invalid("MemoryHeap belongs to another device", ErrorCode::WrongDevice);
        if (!dxHeap->residentFlag->load(std::memory_order_acquire)) {
            return invalid("Cannot create placed texture on an evicted MemoryHeap");
        }
        auto req = queryTextureMemoryRequirements(desc); if (!req) return std::unexpected(req.error());
        if (dxHeap->desc.type != req->heapType) {
            return invalid("MemoryHeapType mismatch for placed texture", ErrorCode::InvalidArgument);
        }
        if (req->alignment > dxHeap->heapAlignment) {
            return invalid("MemoryHeap alignment is smaller than placed texture requirement", ErrorCode::InvalidArgument);
        }
        if (heapOffset % req->alignment != 0u) {
            return invalid("Placed texture heapOffset is not aligned", ErrorCode::InvalidArgument);
        }
        if (heapOffset > dxHeap->desc.size || req->size > dxHeap->desc.size - heapOffset) {
            return invalid("Placed texture exceeds MemoryHeap size", ErrorCode::InvalidArgument);
        }
        const auto nativeDesc = textureNativeDesc(desc);
        auto texture = std::make_shared<DXTexture>();
        texture->owner = state;
        texture->desc = desc;
        const HRESULT hr = state->device->CreatePlacedResource(
            dxHeap->heap.Get(), heapOffset, &nativeDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture->resource));
        if (FAILED(hr)) return state->failure(hr, "CreatePlacedResource(texture) failed");
        texture->placedHeap = dxHeap;
        texture->tracker->placedHeap = dxHeap->heap;
        texture->tracker->heapResident = dxHeap->residentFlag;
        status = texture->initViews(); if (!status) return std::unexpected(status.error());
        dxHeap->entries.push_back({texture->tracker, heapOffset, req->size});
        state->track(texture);
        return TexturePtr(std::move(texture));
    }
    Result<SparseTextureProperties> querySparseTextureProperties(TextureDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        status = validateTextureForDevice(desc); if (!status) return std::unexpected(status.error());
        SparseTextureProperties result{};
        if (!state->caps.sparseResidencyTexture2D || desc.sampleCount > 1u ||
            (desc.dimension != TextureDimension::Texture2D &&
             desc.dimension != TextureDimension::Texture2DArray &&
             desc.dimension != TextureDimension::TextureCube)) {
            return result;
        }
        D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{};
        formatSupport.Format = native(desc.format);
        HRESULT hr = state->device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &formatSupport, sizeof(formatSupport));
        if (FAILED(hr)) return state->failure(hr, "Query D3D12 tiled format support failed");
        if ((formatSupport.Support2 & D3D12_FORMAT_SUPPORT2_TILED) == 0) {
            return result;
        }
        auto nativeDesc = textureNativeDesc(desc);
        nativeDesc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
        ComPtr<ID3D12Resource> reserved;
        hr = state->device->CreateReservedResource(&nativeDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&reserved));
        if (FAILED(hr)) {
            if (FAILED(state->device->GetDeviceRemovedReason())) {
                return state->failure(hr, "CreateReservedResource failed");
            }
            return result;
        }
        UINT totalTiles{};
        D3D12_PACKED_MIP_INFO packedMip{};
        D3D12_TILE_SHAPE tileShape{};
        UINT numSubresourceTilings = 0;
        state->device->GetResourceTiling(reserved.Get(), &totalTiles, &packedMip, &tileShape, &numSubresourceTilings, 0, nullptr);
        result.supported = totalTiles > 0u && tileShape.WidthInTexels > 0u && tileShape.HeightInTexels > 0u;
        if (result.supported) {
            result.tileWidth = tileShape.WidthInTexels;
            result.tileHeight = tileShape.HeightInTexels;
            result.tileDepth = tileShape.DepthInTexels ? tileShape.DepthInTexels : 1u;
            result.standardTileBytes = 65536u;
            result.packedMipStart = packedMip.NumStandardMips;
            result.packedMipCount = packedMip.NumPackedMips;
            result.packedMipTiles = packedMip.NumTilesForPackedMips;
            result.totalTiles = totalTiles;
        }
        return result;
    }
    Result<TextureViewPtr> createTextureView(const TexturePtr& ptr, TextureViewDesc viewDesc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!ptr) return invalid("Null texture", ErrorCode::InvalidArgument);
        auto parent = std::dynamic_pointer_cast<DXTexture>(ptr);
        if (!parent || parent->owner != state) return invalid("Texture belongs to another device", ErrorCode::WrongDevice);
        if (!parent->tracker->available()) return invalid("Backbuffer is not current or its swapchain has been destroyed");
        auto resolved = validateTextureView(parent->desc, viewDesc);
        if (!resolved) return std::unexpected(resolved.error());
        if (resolved->usage == TextureViewUsage::ShaderRead && parent->backbuffer) {
            return invalid("Swapchain backbuffer cannot create a ShaderRead view", ErrorCode::InvalidArgument);
        }
        auto view = std::make_shared<DXTextureView>();
        view->owner = state;
        view->parent = parent;
        view->desc = *resolved;
        const DXGI_FORMAT viewFormat = native(*resolved->format);
        if (resolved->usage == TextureViewUsage::RenderTarget) {
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            heap.NumDescriptors = 1;
            HRESULT hr = state->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&view->rtv));
            if (FAILED(hr)) return state->failure(hr, "Create TextureView RTV heap failed");
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = viewFormat;
            if (parent->desc.sampleCount > 1u) {
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            } else if (parent->desc.dimension == TextureDimension::Texture2DArray ||
                       parent->desc.dimension == TextureDimension::TextureCube) {
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rtvDesc.Texture2DArray.MipSlice = resolved->baseMipLevel;
                rtvDesc.Texture2DArray.FirstArraySlice = resolved->baseArrayLayer;
                rtvDesc.Texture2DArray.ArraySize = 1;
            } else {
                rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtvDesc.Texture2D.MipSlice = resolved->baseMipLevel;
            }
            state->device->CreateRenderTargetView(parent->resource.Get(), &rtvDesc, view->rtv->GetCPUDescriptorHandleForHeapStart());
        } else if (resolved->usage == TextureViewUsage::DepthStencil) {
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            heap.NumDescriptors = 2;
            HRESULT hr = state->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&view->dsv));
            if (FAILED(hr)) return state->failure(hr, "Create TextureView DSV heap failed");
            auto handle = view->dsv->GetCPUDescriptorHandleForHeapStart();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = viewFormat;
            if (parent->desc.sampleCount > 1u) {
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
            } else if (parent->desc.dimension == TextureDimension::Texture2DArray) {
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                dsvDesc.Texture2DArray.MipSlice = resolved->baseMipLevel;
                dsvDesc.Texture2DArray.FirstArraySlice = resolved->baseArrayLayer;
                dsvDesc.Texture2DArray.ArraySize = 1;
            } else {
                dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                dsvDesc.Texture2D.MipSlice = resolved->baseMipLevel;
            }
            state->device->CreateDepthStencilView(parent->resource.Get(), &dsvDesc, handle);
            handle.ptr += state->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
            dsvDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
            state->device->CreateDepthStencilView(parent->resource.Get(), &dsvDesc, handle);
        } else if (resolved->usage == TextureViewUsage::Storage) {
            view->uavDesc.Format = viewFormat;
            view->uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            view->uavDesc.Texture2D.MipSlice = resolved->baseMipLevel;
        } else {
            view->srvDesc = fillD3D12SrvDesc(
                *resolved->format, *resolved->dimension,
                resolved->baseMipLevel, resolved->mipLevelCount,
                resolved->baseArrayLayer, resolved->arrayLayerCount);
        }
        state->track(view);
        return TextureViewPtr(view);
    }
    Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        const auto targets=desc.renderTargets.value_or(RenderTargetLayout{{desc.colorFormat},{}});
        status=validateRenderTargets(targets,desc.depthState);if(!status)return std::unexpected(status.error());
        if (targets.sampleCount > 1u) {
            for (const auto colorFormat : targets.colors) {
                auto support = queryFormatSupport(colorFormat);
                if (!support) return std::unexpected(support.error());
                if ((support->sampleCounts & targets.sampleCount) == 0u) {
                    return invalid("Adapter does not support pipeline color MSAA sample count", ErrorCode::Unsupported);
                }
            }
            if (targets.depth) {
                auto support = queryFormatSupport(*targets.depth);
                if (!support) return std::unexpected(support.error());
                if ((support->sampleCounts & targets.sampleCount) == 0u) {
                    return invalid("Adapter does not support pipeline depth MSAA sample count", ErrorCode::Unsupported);
                }
            }
        }
        const bool hasFragment=!desc.fragment.bytes.empty();
        if (desc.vertex.stage != ShaderStage::Vertex || desc.vertex.bytes.empty() || (hasFragment && desc.fragment.stage!=ShaderStage::Fragment) || (!hasFragment && !targets.colors.empty())) return invalid("Invalid graphics pipeline shaders", ErrorCode::InvalidArgument);
        if (desc.vertex.format == ShaderFormat::DXIL || (hasFragment && desc.fragment.format == ShaderFormat::DXIL)) {
            D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
            const HRESULT hr = state->device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model));
            if (hr == E_INVALIDARG || (SUCCEEDED(hr) && model.HighestShaderModel < D3D_SHADER_MODEL_6_0))
                return invalid("DXIL requires a D3D12 device with Shader Model 6.0", ErrorCode::Unsupported);
            if (FAILED(hr)) return state->failure(hr, "D3D12 Shader Model query failed");
        }
        status=validateVertexLayout(desc.vertexLayout);if(!status)return std::unexpected(status.error());
        auto pipeline = std::make_shared<DXPipeline>(); pipeline->owner = state; pipeline->renderTargets=targets;pipeline->depthState=desc.depthState;pipeline->sampled = desc.sampledTexture;
        if(desc.sampledTexture && (!desc.bindingLayout.entries.empty() || !desc.bindingLayout.samplers.empty()))return invalid("Legacy sampledTexture and explicit bindingLayout cannot be combined",ErrorCode::InvalidArgument);
        pipeline->bindings=desc.sampledTexture?BindingLayout{{{}},{{}}}:desc.bindingLayout;
        status=validateBindingLayout(pipeline->bindings);if(!status)return std::unexpected(status.error());
        for (const auto& entry : pipeline->bindings.entries) {
            if (entry.type == BindingType::StorageBuffer || entry.type == BindingType::StorageTexture2D ||
                entry.visibility == ShaderVisibility::Compute) {
                return invalid("Graphics pipeline layout does not support storage or compute-only bindings", ErrorCode::InvalidArgument);
            }
        }
        for (const auto& sampler : pipeline->bindings.samplers) {
            if (sampler.visibility == ShaderVisibility::Compute) {
                return invalid("Graphics pipeline layout does not support compute-only samplers", ErrorCode::InvalidArgument);
            }
        }
        pipeline->minimumBytes.resize(pipeline->bindings.entries.size());
        for(const auto& entry:pipeline->bindings.entries)pipeline->descriptorSize+=entry.count;
        auto validateBindings=[&](const D3D12ShaderReflection& reflection, ShaderVisibility visibility)->Result<void> {
            auto reflected=d3d12ShaderBindings(reflection);if(!reflected)return std::unexpected(reflected.error());
            for(const auto& resource:*reflected) {
                if(resource.type==ShaderResourceType::Unsupported || !resource.count || resource.count==UINT_MAX)
                    return invalid("Shader uses an unsupported resource type or unbounded array",ErrorCode::Unsupported);
                bool found=false;
                if(resource.type==ShaderResourceType::Sampler) {
                    if(resource.count!=1)return invalid("Sampler arrays require dynamic samplers and are not supported",ErrorCode::Unsupported);
                    for(const auto& sampler:pipeline->bindings.samplers)
                        if(sampler.space==resource.space && (sampler.visibility==visibility || sampler.visibility==ShaderVisibility::All) &&
                           resource.slot==sampler.slot && resource.count==1)found=true;
                } else {
                    const auto type = bindingTypeForResource(resource.type);
                    for(UINT i=0;i<pipeline->bindings.entries.size();++i) {
                        const auto& entry=pipeline->bindings.entries[i];
                        if(entry.type==type && entry.space==resource.space && (entry.visibility==visibility || entry.visibility==ShaderVisibility::All) &&
                           resource.slot>=entry.slot && static_cast<std::uint64_t>(resource.slot)+resource.count<=static_cast<std::uint64_t>(entry.slot)+entry.count) {
                            pipeline->minimumBytes[i]=std::max(pipeline->minimumBytes[i],resource.minimumBytes);found=true;
                        }
                    }
                }
                if(!found)return invalid("Shader resource is missing from the layout or has incompatible type/range/space/visibility: "+resource.name,ErrorCode::InvalidArgument);
            }
            return {};
        };
        auto vertex = reflectD3D12Shader(desc.vertex);
        if (!vertex) return std::unexpected(vertex.error());
        status=validateBindings(*vertex, ShaderVisibility::Vertex);if(!status)return std::unexpected(status.error());
        if(hasFragment){
            auto fragment = reflectD3D12Shader(desc.fragment);
            if (!fragment) return std::unexpected(fragment.error());
            status=validateBindings(*fragment, ShaderVisibility::Fragment);if(!status)return std::unexpected(status.error());
            UINT mask=0;
            for(UINT i=0;i<fragment->description.OutputParameters;++i){D3D12_SIGNATURE_PARAMETER_DESC output{};const HRESULT hr=fragment->native->GetOutputParameterDesc(i,&output);if(FAILED(hr))return state->failure(hr,"Pixel output parameter failed");
                if(output.SystemValueType!=D3D_NAME_TARGET)return invalid("Only floating-point SV_Target color outputs are supported",ErrorCode::Unsupported);
                if(output.SemanticIndex>=targets.colors.size() || output.ComponentType!=D3D_REGISTER_COMPONENT_FLOAT32)return invalid("Pixel output does not match color attachments",ErrorCode::InvalidArgument);
                mask|=1u<<output.SemanticIndex;
            }
            if(mask!=(1u<<targets.colors.size())-1)return invalid("Pixel shader must write every color attachment",ErrorCode::InvalidArgument);
        }
        std::vector<D3D12_INPUT_ELEMENT_DESC> inputs;
        for(const auto& binding:desc.vertexLayout.buffers) {
            VertexRequirement requirement{binding.slot,binding.stride,0,1,binding.rate};
            for(const auto& attribute:desc.vertexLayout.attributes)if(attribute.slot==binding.slot) {
                UINT bytes=attribute.format==VertexFormat::Float?4:attribute.format==VertexFormat::Float2?8:attribute.format==VertexFormat::Float3?12:attribute.format==VertexFormat::Float4?16:4;
                requirement.extent=std::max(requirement.extent,attribute.offset+bytes);
                if(attribute.format!=VertexFormat::UNorm8x4)requirement.alignment=4;
                inputs.push_back({attribute.semantic.c_str(),attribute.semanticIndex,native(attribute.format),binding.slot,attribute.offset,
                    binding.rate==VertexInputRate::Vertex?D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA:D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA,binding.rate==VertexInputRate::Vertex?0u:1u});
            }
            pipeline->vertexInputs.push_back(requirement);
        }
        std::vector<D3D12_DESCRIPTOR_RANGE> ranges(pipeline->bindings.entries.size());
        std::vector<D3D12_ROOT_PARAMETER> parameters(ranges.size());
        for(UINT i=0;i<ranges.size();++i) {
            const auto& entry=pipeline->bindings.entries[i];
            ranges[i]={isSampledTextureBinding(entry.type)?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:D3D12_DESCRIPTOR_RANGE_TYPE_CBV,entry.count,entry.slot,entry.space,0};
            parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameters[i].DescriptorTable={1,&ranges[i]};parameters[i].ShaderVisibility=native(entry.visibility);
        }
        std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
        for(const auto& entry:pipeline->bindings.samplers) {
            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter=entry.filter==SamplerFilter::Point?D3D12_FILTER_MIN_MAG_MIP_POINT:D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU=sampler.AddressV=sampler.AddressW=entry.address==SamplerAddress::Clamp?D3D12_TEXTURE_ADDRESS_MODE_CLAMP:D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.MaxAnisotropy=1;
            sampler.ShaderRegister=entry.slot;sampler.RegisterSpace=entry.space;sampler.ShaderVisibility=native(entry.visibility);samplers.push_back(sampler);
        }
        D3D12_ROOT_SIGNATURE_DESC root{};
        root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        root.NumParameters=static_cast<UINT>(parameters.size());root.pParameters=parameters.data();
        root.NumStaticSamplers=static_cast<UINT>(samplers.size());root.pStaticSamplers=samplers.data();
        ComPtr<ID3DBlob> binary, errors;
        HRESULT result = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &binary, &errors);
        if (FAILED(result)) return state->failure(result, "Root signature serialization failed");
        result = state->device->CreateRootSignature(0, binary->GetBufferPointer(), binary->GetBufferSize(), IID_PPV_ARGS(&pipeline->root));
        if (FAILED(result)) return state->failure(result, "Root signature creation failed");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = pipeline->root.Get(); pso.VS = {desc.vertex.bytes.data(), desc.vertex.bytes.size()}; pso.PS = {desc.fragment.bytes.data(), desc.fragment.bytes.size()};
        pso.InputLayout={inputs.data(),static_cast<UINT>(inputs.size())};
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pso.RasterizerState.DepthClipEnable = TRUE;
        pso.RasterizerState.MultisampleEnable = targets.sampleCount > 1u ? TRUE : FALSE;
        auto& blend = pso.BlendState.RenderTarget[0]; blend.BlendEnable = desc.alphaBlend;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA; blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE; blend.DestBlendAlpha = D3D12_BLEND_ZERO; blend.BlendOpAlpha = D3D12_BLEND_OP_ADD; blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.DepthStencilState.DepthEnable=desc.depthState.testEnabled;
        pso.DepthStencilState.DepthWriteMask=desc.depthState.writeEnabled?D3D12_DEPTH_WRITE_MASK_ALL:D3D12_DEPTH_WRITE_MASK_ZERO;
        pso.DepthStencilState.DepthFunc=static_cast<D3D12_COMPARISON_FUNC>(static_cast<UINT>(desc.depthState.compare)+1);
        pso.DSVFormat=targets.depth?native(*targets.depth):DXGI_FORMAT_UNKNOWN;
        pso.SampleMask = UINT_MAX; pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets=static_cast<UINT>(targets.colors.size());for(UINT i=0;i<pso.NumRenderTargets;++i)pso.RTVFormats[i]=native(targets.colors[i]);
        pso.SampleDesc.Count=targets.sampleCount;pso.SampleDesc.Quality=0;
        result = state->device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline->pso));
        if (FAILED(result)) return state->failure(result, "Graphics pipeline creation failed");
        state->track(pipeline);
        return pipeline;
    }
    Result<PipelinePtr> createComputePipeline(const ComputePipelineDesc& desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (desc.compute.stage != ShaderStage::Compute || desc.compute.bytes.empty() ||
            !desc.threadgroupSizeX || !desc.threadgroupSizeY || !desc.threadgroupSizeZ) {
            return invalid("Invalid compute pipeline shader or threadgroup dimensions", ErrorCode::InvalidArgument);
        }
        if (desc.compute.format == ShaderFormat::DXIL) {
            D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
            const HRESULT hr = state->device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model));
            if (hr == E_INVALIDARG || (SUCCEEDED(hr) && model.HighestShaderModel < D3D_SHADER_MODEL_6_0))
                return invalid("DXIL requires a D3D12 device with Shader Model 6.0", ErrorCode::Unsupported);
            if (FAILED(hr)) return state->failure(hr, "D3D12 Shader Model query failed");
        }
        status = validateBindingLayout(desc.bindingLayout); if (!status) return std::unexpected(status.error());
        for (const auto& entry : desc.bindingLayout.entries) {
            if (entry.visibility != ShaderVisibility::Compute && entry.visibility != ShaderVisibility::All) {
                return invalid("Compute binding entry must have Compute or All visibility", ErrorCode::InvalidArgument);
            }
        }
        for (const auto& sampler : desc.bindingLayout.samplers) {
            if (sampler.visibility != ShaderVisibility::Compute && sampler.visibility != ShaderVisibility::All) {
                return invalid("Compute static sampler must have Compute or All visibility", ErrorCode::InvalidArgument);
            }
        }
        auto pipeline = std::make_shared<DXPipeline>();
        pipeline->owner = state;
        pipeline->isCompute = true;
        pipeline->bindings = desc.bindingLayout;
        pipeline->minimumBytes.resize(pipeline->bindings.entries.size());
        for (const auto& entry : pipeline->bindings.entries) pipeline->descriptorSize += entry.count;

        auto reflectedShader = reflectD3D12Shader(desc.compute);
        if (!reflectedShader) return std::unexpected(reflectedShader.error());
        auto reflectedBindings = d3d12ShaderBindings(*reflectedShader);
        if (!reflectedBindings) return std::unexpected(reflectedBindings.error());
        for (const auto& resource : *reflectedBindings) {
            if (resource.type == ShaderResourceType::Unsupported || !resource.count || resource.count == UINT_MAX) {
                return invalid("Compute shader uses an unsupported resource type or unbounded array", ErrorCode::Unsupported);
            }
            bool found = false;
            if (resource.type == ShaderResourceType::Sampler) {
                if (resource.count != 1) return invalid("Sampler arrays are not supported", ErrorCode::Unsupported);
                for (const auto& sampler : pipeline->bindings.samplers) {
                    if (sampler.space == resource.space && sampler.slot == resource.slot) found = true;
                }
            } else {
                const auto expectedType = bindingTypeForResource(resource.type);
                for (UINT i = 0; i < pipeline->bindings.entries.size(); ++i) {
                    const auto& entry = pipeline->bindings.entries[i];
                    if (entry.type == expectedType && entry.space == resource.space &&
                        resource.slot >= entry.slot &&
                        static_cast<std::uint64_t>(resource.slot) + resource.count <= static_cast<std::uint64_t>(entry.slot) + entry.count) {
                        pipeline->minimumBytes[i] = std::max(pipeline->minimumBytes[i], resource.minimumBytes);
                        found = true;
                    }
                }
            }
            if (!found) {
                return invalid("Compute shader resource missing or mismatched in binding layout: " + resource.name, ErrorCode::InvalidArgument);
            }
        }

        std::vector<D3D12_DESCRIPTOR_RANGE> ranges(pipeline->bindings.entries.size());
        std::vector<D3D12_ROOT_PARAMETER> parameters(ranges.size());
        for (UINT i = 0; i < ranges.size(); ++i) {
            const auto& entry = pipeline->bindings.entries[i];
            const auto rangeType =
                isSampledTextureBinding(entry.type) ? D3D12_DESCRIPTOR_RANGE_TYPE_SRV :
                entry.type == BindingType::ConstantBuffer ? D3D12_DESCRIPTOR_RANGE_TYPE_CBV :
                D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            ranges[i] = {rangeType, entry.count, entry.slot, entry.space, 0};
            parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[i].DescriptorTable = {1, &ranges[i]};
            parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;
        for (const auto& entry : pipeline->bindings.samplers) {
            D3D12_STATIC_SAMPLER_DESC sampler{};
            sampler.Filter = entry.filter == SamplerFilter::Point ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = entry.address == SamplerAddress::Clamp ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            sampler.MaxAnisotropy = 1;
            sampler.ShaderRegister = entry.slot;
            sampler.RegisterSpace = entry.space;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            samplers.push_back(sampler);
        }
        D3D12_ROOT_SIGNATURE_DESC root{};
        root.NumParameters = static_cast<UINT>(parameters.size());
        root.pParameters = parameters.data();
        root.NumStaticSamplers = static_cast<UINT>(samplers.size());
        root.pStaticSamplers = samplers.data();
        ComPtr<ID3DBlob> binary, errors;
        HRESULT result = D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1, &binary, &errors);
        if (FAILED(result)) return state->failure(result, "Compute root signature serialization failed");
        result = state->device->CreateRootSignature(0, binary->GetBufferPointer(), binary->GetBufferSize(), IID_PPV_ARGS(&pipeline->root));
        if (FAILED(result)) return state->failure(result, "Compute root signature creation failed");

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = pipeline->root.Get();
        pso.CS = {desc.compute.bytes.data(), desc.compute.bytes.size()};
        result = state->device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&pipeline->pso));
        if (FAILED(result)) return state->failure(result, "Compute pipeline creation failed");
        state->track(pipeline);
        return pipeline;
    }
    Result<PipelinePtr> createMeshPipeline(const MeshPipelineDesc& desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        const auto targets = desc.renderTargets.value_or(RenderTargetLayout{{desc.colorFormat}, {}});
        status = validateRenderTargets(targets, desc.depthState); if (!status) return std::unexpected(status.error());
        status = validateBindingLayout(desc.bindingLayout); if (!status) return std::unexpected(status.error());
        if (desc.mesh.stage != ShaderStage::Mesh || desc.mesh.bytes.empty()) {
            return invalid("MeshPipelineDesc requires a non-empty Mesh shader", ErrorCode::InvalidArgument);
        }
        if (desc.task && (desc.task->stage != ShaderStage::Task || desc.task->bytes.empty())) {
            return invalid("MeshPipelineDesc task shader must have Task stage and non-empty bytes", ErrorCode::InvalidArgument);
        }
        if (!desc.fragment.bytes.empty() && desc.fragment.stage != ShaderStage::Fragment) {
            return invalid("MeshPipelineDesc fragment shader must have Fragment stage", ErrorCode::InvalidArgument);
        }
        if (!state->caps.meshShaders) {
            return invalid("Adapter does not support Mesh/Task shaders", ErrorCode::Unsupported);
        }
        return invalid("Mesh/Task shader pipeline execution is not yet implemented in this SDK snapshot", ErrorCode::Unsupported);
    }
    Result<std::unique_ptr<CommandList>> createCommandList() override {
        return createCommandList(QueueType::Graphics);
    }
    Result<std::unique_ptr<CommandList>> createCommandList(QueueType type) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!validQueueType(type)) return invalid("Unknown queue type", ErrorCode::InvalidArgument);
        return makeDXCommands(state, {}, type);
    }
    Result<std::unique_ptr<CommandPool>> createCommandPool() override {
        return createCommandPool(QueueType::Graphics);
    }
    Result<std::unique_ptr<CommandPool>> createCommandPool(QueueType type) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!validQueueType(type)) return invalid("Unknown queue type", ErrorCode::InvalidArgument);
        auto poolState = std::make_shared<DXCommandPoolState>();
        poolState->qType = type;
        poolState->recordingThread.store(state->thread, std::memory_order_release);
        HRESULT result = state->device->CreateCommandAllocator(
            d3dQueueType(type), IID_PPV_ARGS(&poolState->allocator));
        if (FAILED(result)) return state->failure(result, "Create command pool allocator failed");
        state->track(poolState);
        auto pool = std::make_unique<DXCommandPool>();
        pool->owner = state;
        pool->pool = std::move(poolState);
        return std::unique_ptr<CommandPool>(std::move(pool));
    }
    Result<TimelineFencePtr> createTimelineFence(std::uint64_t initialValue) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (initialValue >= UINT64_MAX - 1) return invalid("Invalid initial timeline fence value", ErrorCode::InvalidArgument);
        auto timeline = std::make_shared<DXTimelineFence>();
        timeline->owner = state;
        timeline->lastSignaled = initialValue;
        HRESULT result = state->device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&timeline->fence));
        if (FAILED(result)) return state->failure(result, "Create timeline fence failed");
        state->track(timeline);
        return TimelineFencePtr(std::move(timeline));
    }
    Result<NativeHandle> nativeDeviceHandle() override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        return NativeHandle{Backend::D3D12, NativeObjectType::D3D12Device,
                            reinterpret_cast<std::uintptr_t>(state->device.Get())};
    }
    Result<NativeHandle> nativeBufferHandle(const BufferPtr& buffer) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        auto* dx = dynamic_cast<DXBuffer*>(buffer.get());
        if (!dx) return invalid("Invalid D3D12 buffer", buffer ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument);
        if (dx->owner != state) return invalid("Buffer belongs to a different device", ErrorCode::WrongDevice);
        return NativeHandle{Backend::D3D12, NativeObjectType::D3D12Resource,
                            reinterpret_cast<std::uintptr_t>(dx->resource.Get())};
    }
    Result<NativeHandle> nativeTextureHandle(const TexturePtr& texture) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        auto* dx = dynamic_cast<DXTexture*>(texture.get());
        if (!dx) return invalid("Invalid D3D12 texture", texture ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument);
        if (dx->owner != state) return invalid("Texture belongs to a different device", ErrorCode::WrongDevice);
        return NativeHandle{Backend::D3D12, NativeObjectType::D3D12Resource,
                            reinterpret_cast<std::uintptr_t>(dx->resource.Get())};
    }
    Result<NativeHandle> nativeTimelineFenceHandle(const TimelineFencePtr& fence) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        auto* dx = dynamic_cast<DXTimelineFence*>(fence.get());
        if (!dx) return invalid("Invalid D3D12 timeline fence", fence ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument);
        if (dx->owner != state) return invalid("TimelineFence belongs to a different device", ErrorCode::WrongDevice);
        return NativeHandle{Backend::D3D12, NativeObjectType::D3D12Fence,
                            reinterpret_cast<std::uintptr_t>(dx->fence.Get())};
    }
    Result<ExternalHandle> exportBufferHandle(const BufferPtr& buffer) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        auto* dx = dynamic_cast<DXBuffer*>(buffer.get());
        if (!dx) return invalid("Invalid D3D12 buffer", buffer ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument);
        if (dx->owner != state) return invalid("Buffer belongs to a different device", ErrorCode::WrongDevice);
        if (!dx->desc.shared) return invalid("Buffer was not created with shared=true", ErrorCode::InvalidState);
        HANDLE ntHandle{};
        const HRESULT hr = state->device->CreateSharedHandle(dx->resource.Get(), nullptr, GENERIC_ALL, nullptr, &ntHandle);
        if (FAILED(hr) || !ntHandle) return state->failure(hr, "CreateSharedHandle(buffer) failed");
        return ExternalHandle{ExternalHandleType::D3D12Resource, reinterpret_cast<std::uintptr_t>(ntHandle)};
    }
    Result<BufferPtr> importBufferHandle(BufferDesc desc, const ExternalHandle& handle) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (handle.type() != ExternalHandleType::D3D12Resource && handle.type() != ExternalHandleType::OpaqueWin32Nt)
            return invalid("D3D12 interop requires D3D12Resource or OpaqueWin32Nt handle", ErrorCode::Unsupported);
        if (!handle) return invalid("ExternalHandle is empty", ErrorCode::InvalidArgument);
        if (!desc.size || desc.size > (1ull << 40) || desc.memory != Memory::Device) {
            return invalid("Imported D3D12 buffer requires non-zero size and Device memory", ErrorCode::InvalidArgument);
        }
        ComPtr<ID3D12Resource> resource;
        const HRESULT hr = state->device->OpenSharedHandle(reinterpret_cast<HANDLE>(handle.raw()), IID_PPV_ARGS(&resource));
        if (FAILED(hr)) return state->failure(hr, "OpenSharedHandle(buffer) failed");
        const auto nativeDesc = resource->GetDesc();
        if (nativeDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || nativeDesc.Width < desc.size) {
            return invalid("Imported D3D12 shared handle does not match requested buffer description", ErrorCode::InvalidArgument);
        }
        desc.shared = true;
        auto buffer = std::make_shared<DXBuffer>();
        buffer->owner = state;
        buffer->resource = std::move(resource);
        buffer->desc = desc;
        state->track(buffer);
        return BufferPtr(std::move(buffer));
    }
    Result<ExternalHandle> exportTextureHandle(const TexturePtr& texture) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        auto* dx = dynamic_cast<DXTexture*>(texture.get());
        if (!dx) return invalid("Invalid D3D12 texture", texture ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument);
        if (dx->owner != state) return invalid("Texture belongs to a different device", ErrorCode::WrongDevice);
        if (!dx->desc.shared) return invalid("Texture was not created with shared=true", ErrorCode::InvalidState);
        HANDLE ntHandle{};
        const HRESULT hr = state->device->CreateSharedHandle(dx->resource.Get(), nullptr, GENERIC_ALL, nullptr, &ntHandle);
        if (FAILED(hr) || !ntHandle) return state->failure(hr, "CreateSharedHandle(texture) failed");
        return ExternalHandle{ExternalHandleType::D3D12Resource, reinterpret_cast<std::uintptr_t>(ntHandle)};
    }
    Result<TexturePtr> importTextureHandle(TextureDesc desc, const ExternalHandle& handle) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (handle.type() != ExternalHandleType::D3D12Resource && handle.type() != ExternalHandleType::OpaqueWin32Nt)
            return invalid("D3D12 interop requires D3D12Resource or OpaqueWin32Nt handle", ErrorCode::Unsupported);
        if (!handle) return invalid("ExternalHandle is empty", ErrorCode::InvalidArgument);
        status = validateTextureForDevice(desc); if (!status) return std::unexpected(status.error());
        ComPtr<ID3D12Resource> resource;
        const HRESULT hr = state->device->OpenSharedHandle(reinterpret_cast<HANDLE>(handle.raw()), IID_PPV_ARGS(&resource));
        if (FAILED(hr)) return state->failure(hr, "OpenSharedHandle(texture) failed");
        const auto nativeDesc = resource->GetDesc();
        if (nativeDesc.Width != desc.width || nativeDesc.Height != desc.height || nativeDesc.Format != native(desc.format)) {
            return invalid("Imported D3D12 shared handle does not match requested texture extent or format", ErrorCode::InvalidArgument);
        }
        desc.shared = true;
        auto texture = std::make_shared<DXTexture>();
        texture->owner = state;
        texture->resource = std::move(resource);
        texture->desc = desc;
        status = texture->initViews(); if (!status) return std::unexpected(status.error());
        state->track(texture);
        return TexturePtr(std::move(texture));
    }
    Result<TimelineFencePtr> createSharedTimelineFence(std::uint64_t initialValue) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (initialValue >= UINT64_MAX - 1) return invalid("Invalid initial timeline fence value", ErrorCode::InvalidArgument);
        auto timeline = std::make_shared<DXTimelineFence>();
        timeline->owner = state;
        timeline->lastSignaled = initialValue;
        timeline->shared = true;
        HRESULT result = state->device->CreateFence(initialValue, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&timeline->fence));
        if (FAILED(result)) return state->failure(result, "Create shared timeline fence failed");
        state->track(timeline);
        return TimelineFencePtr(std::move(timeline));
    }
    Result<ExternalHandle> exportTimelineFenceHandle(const TimelineFencePtr& fence) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        auto* dx = dynamic_cast<DXTimelineFence*>(fence.get());
        if (!dx) return invalid("Invalid D3D12 timeline fence", fence ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument);
        if (dx->owner != state) return invalid("TimelineFence belongs to a different device", ErrorCode::WrongDevice);
        if (!dx->shared) return invalid("TimelineFence was not created via createSharedTimelineFence", ErrorCode::InvalidState);
        HANDLE ntHandle{};
        const HRESULT hr = state->device->CreateSharedHandle(dx->fence.Get(), nullptr, GENERIC_ALL, nullptr, &ntHandle);
        if (FAILED(hr) || !ntHandle) return state->failure(hr, "CreateSharedHandle(fence) failed");
        return ExternalHandle{ExternalHandleType::D3D12Fence, reinterpret_cast<std::uintptr_t>(ntHandle)};
    }
    Result<TimelineFencePtr> importTimelineFenceHandle(const ExternalHandle& handle, std::uint64_t initialValue) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (handle.type() != ExternalHandleType::D3D12Fence && handle.type() != ExternalHandleType::OpaqueWin32Nt)
            return invalid("D3D12 fence interop requires D3D12Fence or OpaqueWin32Nt handle", ErrorCode::Unsupported);
        if (!handle) return invalid("ExternalHandle is empty", ErrorCode::InvalidArgument);
        if (initialValue >= UINT64_MAX - 1) return invalid("Invalid initial timeline fence value", ErrorCode::InvalidArgument);
        ComPtr<ID3D12Fence> fence;
        const HRESULT hr = state->device->OpenSharedHandle(reinterpret_cast<HANDLE>(handle.raw()), IID_PPV_ARGS(&fence));
        if (FAILED(hr)) return state->failure(hr, "OpenSharedHandle(fence) failed");
        auto timeline = std::make_shared<DXTimelineFence>();
        timeline->owner = state;
        timeline->fence = std::move(fence);
        timeline->lastSignaled = initialValue;
        timeline->shared = true;
        state->track(timeline);
        return TimelineFencePtr(std::move(timeline));
    }
    Result<std::uint64_t> validationErrorCount() override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!state->info) return invalid("Validation was not requested", ErrorCode::Unsupported);
        // Storage/retrieval filters and the bounded native text queue cannot hide errors.
        return state->validationErrors.load(std::memory_order_relaxed);
    }
    Result<void> checkSurfaceSupport(Surface surface) override {
        auto status = state->check(); if (!status) return status;
        if (surface.kind == SurfaceKind::Xcb) return invalid("XCB surfaces require Linux", ErrorCode::Unsupported);
        if (surface.kind == SurfaceKind::MetalLayer) return invalid("CAMetalLayer surfaces require Metal", ErrorCode::Unsupported);
        DWORD process{}; auto thread = GetWindowThreadProcessId(static_cast<HWND>(surface.window), &process);
        if (surface.kind != SurfaceKind::Win32 || surface.connection || surface.windowId || !IsWindow(static_cast<HWND>(surface.window)) ||
            thread != GetCurrentThreadId() || process != GetCurrentProcessId())
            return invalid("Surface requires a live Win32 window on the creating thread", ErrorCode::InvalidArgument);
        return {};
    }
    Result<SurfacePresentCapabilities> querySurfacePresentCapabilities(Surface surface) override {
        auto status = checkSurfaceSupport(surface);
        if (!status) return std::unexpected(status.error());
        BOOL allowTearing = FALSE;
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(state->factory.As(&factory5))) {
            if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)))) {
                allowTearing = FALSE;
            }
        }
        SurfacePresentCapabilities caps{};
        caps.fifo = true;
        caps.immediate = true;
        caps.mailbox = false;
        caps.tearing = allowTearing == TRUE;
        caps.frameLatencyWaitable = true;
        caps.minImageCount = 2u;
        caps.maxImageCount = 4u;
        caps.partialPresent = true;
        caps.maxDirtyRects = 64u;
        return caps;
    }
    Result<SurfaceColorCapabilities> querySurfaceColorCapabilities(Surface surface) override {
        auto status = checkSurfaceSupport(surface);
        if (!status) return std::unexpected(status.error());
        SurfaceColorCapabilities out{};
        out.supportedFormats = {
            {Format::RGBA8UNorm, ColorSpace::SrgbNonlinear},
            {Format::BGRA8UNorm, ColorSpace::SrgbNonlinear},
            {Format::RGBA8SRGB, ColorSpace::SrgbNonlinear},
            {Format::BGRA8SRGB, ColorSpace::SrgbNonlinear},
            {Format::RGB10A2UNorm, ColorSpace::SrgbNonlinear},
            {Format::RGB10A2UNorm, ColorSpace::Hdr10St2084},
            {Format::RGBA16Float, ColorSpace::LinearSrgb},
            {Format::RGBA16Float, ColorSpace::ExtendedLinearScRgb},
        };
        out.srgbNonlinear = true;
        out.linearSrgb = true;
        out.displayP3 = false;
        out.rec2020Linear = false;
        out.hdr10St2084 = true;
        out.extendedLinearScRgb = true;
        out.tenBitOutput = true;
        out.hdrMetadata = true;
        out.activeDisplayColorSpace = ColorSpace::SrgbNonlinear;
        out.displayHdrEnabled = false;
        const HMONITOR targetMonitor = MonitorFromWindow(static_cast<HWND>(surface.window), MONITOR_DEFAULTTOPRIMARY);
        ComPtr<IDXGIAdapter1> adapter;
        if (SUCCEEDED(state->factory->EnumAdapterByLuid(state->device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)))) {
            for (UINT idx = 0;; ++idx) {
                ComPtr<IDXGIOutput> output;
                if (adapter->EnumOutputs(idx, &output) == DXGI_ERROR_NOT_FOUND) break;
                ComPtr<IDXGIOutput6> output6;
                if (SUCCEEDED(output.As(&output6))) {
                    DXGI_OUTPUT_DESC1 desc1{};
                    if (SUCCEEDED(output6->GetDesc1(&desc1)) &&
                        (targetMonitor == nullptr || desc1.Monitor == targetMonitor)) {
                        out.displayMaxLuminanceNits = desc1.MaxLuminance;
                        out.displayMinLuminanceNits = desc1.MinLuminance;
                        if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) {
                            out.activeDisplayColorSpace = ColorSpace::Hdr10St2084;
                            out.displayHdrEnabled = true;
                        } else if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) {
                            out.activeDisplayColorSpace = ColorSpace::ExtendedLinearScRgb;
                            out.displayHdrEnabled = true;
                        } else {
                            out.activeDisplayColorSpace = ColorSpace::SrgbNonlinear;
                            out.displayHdrEnabled = false;
                        }
                        break;
                    }
                }
            }
        }
        return out;
    }
    Result<SurfaceMetrics> querySurfaceMetrics(Surface surface) override {
        auto status = checkSurfaceSupport(surface);
        if (!status) return std::unexpected(status.error());
        return queryWin32Metrics(static_cast<HWND>(surface.window), false);
    }
    Result<std::unique_ptr<SwapChain>> createSwapChain(Surface surface, SwapChainDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (surface.kind == SurfaceKind::Xcb) return invalid("XCB surfaces require Linux", ErrorCode::Unsupported);
        if (surface.kind == SurfaceKind::MetalLayer) return invalid("CAMetalLayer surfaces require Metal", ErrorCode::Unsupported);
        if (surface.kind != SurfaceKind::Win32 || !IsWindow(static_cast<HWND>(surface.window)) || !desc.width || !desc.height || desc.width > 16384 || desc.height > 16384 || desc.buffers < 2 || desc.buffers > 4)
            return invalid("Invalid Win32 surface or swapchain description", ErrorCode::InvalidArgument);
        auto latency = detail::resolveFrameLatency(desc);
        if (!latency) return std::unexpected(latency.error());
        const PresentMode mode = desc.presentMode.value_or(desc.vsync ? PresentMode::Fifo : PresentMode::Immediate);
        if (mode == PresentMode::Mailbox) {
            return invalid("D3D12 DXGI flip-discard does not expose Vulkan-style Mailbox present mode", ErrorCode::Unsupported);
        }
        const auto dxgiCs = dxgiColorSpaceFor(desc.colorSpace);
        if (!dxgiCs) {
            return invalid("Requested swapchain color space is unsupported on D3D12 DXGI", ErrorCode::Unsupported);
        }
        const Format targetFormat = desc.format.value_or(
            desc.colorSpace == ColorSpace::Hdr10St2084 ? Format::RGB10A2UNorm
            : (desc.colorSpace == ColorSpace::ExtendedLinearScRgb || desc.colorSpace == ColorSpace::LinearSrgb) ? Format::RGBA16Float
            : Format::RGBA8UNorm);
        if (!validD3D12SwapPair(targetFormat, desc.colorSpace)) {
            return invalid("Requested swapchain format and color space combination is unsupported on D3D12 DXGI", ErrorCode::Unsupported);
        }
        const DXGI_FORMAT storageFormat = dxgiSwapChainStorageFormat(targetFormat);
        if (storageFormat == DXGI_FORMAT_UNKNOWN) {
            return invalid("Requested swapchain backbuffer format is unsupported on D3D12 DXGI", ErrorCode::Unsupported);
        }
        BOOL allowTearing = FALSE;
        if (mode == PresentMode::Tearing) {
            ComPtr<IDXGIFactory5> factory5;
            if (FAILED(state->factory.As(&factory5)) ||
                FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))) ||
                allowTearing != TRUE) {
                return invalid("Adapter or OS does not support DXGI_FEATURE_PRESENT_ALLOW_TEARING", ErrorCode::Unsupported);
            }
        }
        const UINT swapFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
            (mode == PresentMode::Tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u);
        auto swap = std::make_unique<DXSwapChain>();
        swap->owner = state;
        swap->window = static_cast<HWND>(surface.window);
        swap->desc = desc;
        swap->activeMode = mode;
        swap->activeFormat = targetFormat;
        swap->nativeStorageFormat = storageFormat;
        swap->activeColorSpace = desc.colorSpace;
        swap->dxgiColorSpace = *dxgiCs;
        swap->swapFlags = swapFlags;
        swap->effectiveLatency = *latency;
        swap->pacing.configure(mode, desc.pacingMode, *latency, desc.stallThresholdMilliseconds);
        DXGI_SWAP_CHAIN_DESC1 nativeDesc{}; nativeDesc.Width = desc.width; nativeDesc.Height = desc.height;
        nativeDesc.Format = storageFormat; nativeDesc.SampleDesc.Count = 1;
        nativeDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; nativeDesc.BufferCount = desc.buffers;
        nativeDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        nativeDesc.Flags = swapFlags;
        ComPtr<IDXGISwapChain1> chain;
        HRESULT result = state->factory->CreateSwapChainForHwnd(state->queues[0].queue.Get(), static_cast<HWND>(surface.window), &nativeDesc, nullptr, nullptr, &chain);
        if (FAILED(result)) return state->failure(result, "Create swapchain failed");
        result = chain.As(&swap->chain); if (FAILED(result)) return state->failure(result, "Swapchain3 interface unavailable");
        UINT csSupport = 0;
        result = swap->chain->CheckColorSpaceSupport(*dxgiCs, &csSupport);
        if (FAILED(result) || (csSupport & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) == 0u) {
            return invalid("Requested DXGI swapchain color space is not supported for this format", ErrorCode::Unsupported);
        }
        result = swap->chain->SetColorSpace1(*dxgiCs);
        if (FAILED(result)) return state->failure(result, "SetColorSpace1 failed");
        if (desc.hdrMetadata) {
            auto metaOk = swap->applyMetadataNative(*desc.hdrMetadata);
            if (!metaOk) return std::unexpected(metaOk.error());
        }
        result = swap->chain->SetMaximumFrameLatency(*latency);
        if (FAILED(result)) return state->failure(result, "SetMaximumFrameLatency failed");
        swap->objects->waitableObject = swap->chain->GetFrameLatencyWaitableObject();
        swap->objects->scope->chain=swap->chain.Get();
        result = state->factory->MakeWindowAssociation(static_cast<HWND>(surface.window), DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(result)) return state->failure(result, "Window association failed");
        status = swap->rebuild(); if (!status) return std::unexpected(status.error());
        state->track(swap->objects);
        return std::unique_ptr<SwapChain>(std::move(swap));
    }
};
}
Result<std::vector<AdapterInfo>> enumerateD3D12Adapters() {
    // Metadata discovery never creates/probes devices or changes debug/GBV configuration.
    ComPtr<IDXGIFactory6> factory;
    auto hr=CreateDXGIFactory2(0,IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return failed(hr,"Create adapter enumeration factory failed");
    std::vector<AdapterInfo> result;
    for (UINT index=0;;++index) {
        ComPtr<IDXGIAdapter1> adapter;
        hr=factory->EnumAdapterByGpuPreference(index,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter));
        if (hr==DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr)) return failed(hr,"Enumerating hardware adapter failed");
        auto info=adapterInfo(*adapter.Get()); if (!info) return std::unexpected(info.error());
        if (!info->softwareAdapter) result.push_back(std::move(*info));
    }
    ComPtr<IDXGIAdapter1> warp;
    hr=factory->EnumWarpAdapter(IID_PPV_ARGS(&warp));
    if (SUCCEEDED(hr)) {
        auto info=adapterInfo(*warp.Get()); if (!info) return std::unexpected(info.error());
        if (std::none_of(result.begin(),result.end(),[&](const auto& entry){return entry.id==info->id;}))
            result.push_back(std::move(*info));
    } else if (hr!=DXGI_ERROR_NOT_FOUND && hr!=DXGI_ERROR_UNSUPPORTED)
        return failed(hr,"Enumerating WARP adapter failed");
    return result;
}
Result<std::unique_ptr<Device>> createD3D12Device(DeviceDesc desc) {
    if (desc.validation) {
        ComPtr<ID3D12Debug> debug; HRESULT result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
        if (FAILED(result)) return invalid("D3D12 debug layer unavailable; install Windows Graphics Tools", ErrorCode::Unsupported);
        debug->EnableDebugLayer(); ComPtr<ID3D12Debug1> debug1;
        if (SUCCEEDED(debug.As(&debug1))) debug1->SetEnableGPUBasedValidation(TRUE);
    }
    auto state = std::make_shared<State>();
    state->description=desc;
    state->id = detail::allocateDeviceGeneration();
    if (!state->id) return invalid("Device generation sequence exhausted", ErrorCode::InvalidState);
    HRESULT result = CreateDXGIFactory2(desc.validation ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&state->factory));
    if (FAILED(result)) return failed(result, "Create DXGI factory failed");
    ComPtr<IDXGIAdapter1> selected;
    if (desc.adapter) {
        result=state->factory->EnumAdapterByLuid(adapterLuid(*desc.adapter),IID_PPV_ARGS(&selected));
        if (result==DXGI_ERROR_NOT_FOUND)
            return std::unexpected(Error{ErrorCode::Unsupported,"Requested D3D12 adapter is unavailable",result});
        if (FAILED(result)) return failed(result,"Select requested D3D12 adapter failed");
        auto info=adapterInfo(*selected.Get()); if (!info) return std::unexpected(info.error());
        if (info->softwareAdapter!=desc.softwareAdapter)
            return invalid("softwareAdapter must match the explicit adapter",ErrorCode::InvalidArgument);
        result=D3D12CreateDevice(selected.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&state->device));
        if (FAILED(result) && result!=DXGI_ERROR_UNSUPPORTED && result!=E_INVALIDARG)
            return failed(result,"Create requested adapter device failed");
    } else if (desc.softwareAdapter) {
        result = state->factory->EnumWarpAdapter(IID_PPV_ARGS(&selected));
        if (FAILED(result)) return failed(result, "WARP adapter unavailable");
        result = D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&state->device));
    } else {
        for (UINT i = 0; ; ++i) {
            result = state->factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&selected));
            if (result == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(result)) return failed(result, "Enumerating hardware adapter failed");
            DXGI_ADAPTER_DESC1 info{};
            result=selected->GetDesc1(&info); if (FAILED(result)) return failed(result,"Read adapter description failed");
            if (!(info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                result = D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&state->device));
                if (SUCCEEDED(result)) break;
                if(result==DXGI_ERROR_DEVICE_REMOVED || result==DXGI_ERROR_DEVICE_RESET || result==DXGI_ERROR_DEVICE_HUNG || result==DXGI_ERROR_DRIVER_INTERNAL_ERROR)return failed(result,"Adapter device is still removed; release other native device owners before retrying recovery");
            }
            selected.Reset();
        }
    }
    if(desc.softwareAdapter && FAILED(result) && (result==DXGI_ERROR_DEVICE_REMOVED || result==DXGI_ERROR_DEVICE_RESET || result==DXGI_ERROR_DEVICE_HUNG || result==DXGI_ERROR_DRIVER_INTERNAL_ERROR))return failed(result,"WARP device is still removed; release other native device owners before retrying recovery");
    if (!state->device) {
        if (desc.adapter) return std::unexpected(Error{ErrorCode::Unsupported,"Requested D3D12 adapter does not support feature level 11.0",result});
        return invalid("No D3D12 adapter supports feature level 11.0", ErrorCode::Unsupported);
    }
    auto info=adapterInfo(*selected.Get()); if (!info) return std::unexpected(info.error());
    const auto actual=adapterId(state->device->GetAdapterLuid());
    if (actual!=info->id || (desc.adapter && actual!=*desc.adapter))
        return invalid("Native device adapter does not match selection",ErrorCode::BackendFailure);
    state->caps = {Backend::D3D12, std::move(info->name), info->softwareAdapter, true, true, true, true, true, true, true, true, true};
    state->caps.adapterId=actual;
    state->caps.maxTextureDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    state->caps.maxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
    state->caps.constantBufferAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    state->caps.textureCopyRowAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    state->caps.textureCopyOffsetAlignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    state->caps.maxVertexStreams = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
    state->caps.maxConstantBufferSize = 65536u;
    state->caps.dedicatedComputeQueue = true;
    state->caps.dedicatedCopyQueue = true;
    state->caps.timelineFences = true;
    state->caps.resourceAliasing = true;
    state->caps.residencyControl = true;
    state->caps.indirectDraw = true;
    state->caps.multiDrawIndirect = true;
    state->caps.maxDrawIndirectCount = 65536u;
    D3D12_FEATURE_DATA_D3D12_OPTIONS d3d12Options{};
    if (SUCCEEDED(state->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &d3d12Options, sizeof(d3d12Options)))) {
        const bool tiledSupported = d3d12Options.TiledResourcesTier >= D3D12_TILED_RESOURCES_TIER_1;
        state->caps.sparseBinding = tiledSupported;
        state->caps.sparseResidencyTexture2D = tiledSupported;
        D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_0};
        const bool sm6 = SUCCEEDED(state->device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))) &&
                         sm.HighestShaderModel >= D3D_SHADER_MODEL_6_0;
        if (d3d12Options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3 && sm6) {
            state->caps.descriptorIndexing = true;
            state->caps.maxBindlessDescriptors = 1000000u;
        }
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 d3d12Options7{};
    if (SUCCEEDED(state->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &d3d12Options7, sizeof(d3d12Options7)))) {
        state->caps.meshShaders = d3d12Options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1;
    }
    state->caps.externalMemoryInterop = true;
    state->caps.externalFenceInterop = true;
    ComPtr<ID3D12Device5> device5;state->caps.deviceRecovery=true;state->caps.deviceLossInjection=SUCCEEDED(state->device.As(&device5));
    if (desc.validation) {
        result = state->device.As(&state->info); if (FAILED(result)) return state->failure(result, "D3D12 InfoQueue unavailable");
        if(FAILED(state->info.As(&state->messageSource)))return invalid("Reliable validation accounting requires ID3D12InfoQueue1",ErrorCode::Unsupported);
        result=state->messageSource->RegisterMessageCallback(State::onMessage,D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,state.get(),&state->messageCookie);
        if(FAILED(result))return state->failure(result,"Validation message callback registration failed");
        state->messageCallbackRegistered=true;
    }
    const std::array<QueueType, 3> queueTypes{QueueType::Graphics, QueueType::Compute, QueueType::Copy};
    for (std::size_t i = 0; i < 3; ++i) {
        D3D12_COMMAND_QUEUE_DESC qDesc{};
        qDesc.Type = d3dQueueType(queueTypes[i]);
        result = state->device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&state->queues[i].queue));
        if (FAILED(result)) return state->failure(result, "Create command queue failed");
        result = state->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&state->queues[i].fence));
        if (FAILED(result)) return state->failure(result, "Create fence failed");
        state->queues[i].id = (i == 0) ? state->id : detail::allocateDeviceGeneration();
        if (!state->queues[i].id) return invalid("Queue generation sequence exhausted", ErrorCode::InvalidState);
    }
    return std::unique_ptr<Device>(std::make_unique<DXDevice>(std::move(state)));
}
}
