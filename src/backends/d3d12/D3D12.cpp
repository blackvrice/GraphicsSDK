#include <graphics/Graphics.hpp>
#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <deque>
#include <limits>
#include <thread>
#include <unordered_set>

namespace gfx {
namespace {
using Microsoft::WRL::ComPtr;
auto invalid(std::string text, ErrorCode code = ErrorCode::InvalidState) { return std::unexpected(Error{code, std::move(text)}); }
auto failed(HRESULT code, std::string operation) {
    auto kind = code == E_OUTOFMEMORY ? ErrorCode::OutOfMemory :
        code == DXGI_ERROR_DEVICE_REMOVED || code == DXGI_ERROR_DEVICE_RESET || code == DXGI_ERROR_DEVICE_HUNG ? ErrorCode::DeviceLost : ErrorCode::BackendFailure;
    return std::unexpected(Error{kind, std::move(operation), code});
}
DXGI_FORMAT native(Format format) { return format == Format::RGBA8UNorm ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R16G16B16A16_FLOAT; }
bool validFormat(Format format) { return format == Format::RGBA8UNorm || format == Format::RGBA16Float; }
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
    }
    return D3D12_RESOURCE_STATE_COMMON;
}
bool validState(ResourceState state) { return state >= ResourceState::Common && state <= ResourceState::ConstantBuffer; }
D3D12_SHADER_VISIBILITY native(ShaderVisibility visibility) {
    return visibility==ShaderVisibility::Vertex?D3D12_SHADER_VISIBILITY_VERTEX:visibility==ShaderVisibility::Fragment?D3D12_SHADER_VISIBILITY_PIXEL:D3D12_SHADER_VISIBILITY_ALL;
}
struct Tracker { ResourceState state{ResourceState::Common}; std::uint64_t version{}, lastUse{}; };
struct Batch { std::uint64_t fence{}; std::vector<ComPtr<IUnknown>> objects; };
struct State {
    ComPtr<IDXGIFactory6> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12InfoQueue> info;
    DeviceCaps caps;
    std::thread::id thread{std::this_thread::get_id()};
    std::uint64_t id{}, last{};
    std::deque<Batch> pending;
    Result<void> check() const {
        if (thread != std::this_thread::get_id()) return invalid("Device operations must run on the creating thread");
        HRESULT status = device->GetDeviceRemovedReason();
        if (FAILED(status)) return failed(status, "D3D12 device removed");
        return {};
    }
    Result<std::uint64_t> completed() {
        auto status = check(); if (!status) return std::unexpected(status.error());
        auto value = fence->GetCompletedValue();
        if (value == UINT64_MAX) return failed(DXGI_ERROR_DEVICE_REMOVED, "Fence reports device removal");
        while (!pending.empty() && pending.front().fence <= value) pending.pop_front();
        return value;
    }
    Result<void> wait(std::uint64_t value, std::chrono::milliseconds timeout) {
        auto done = completed(); if (!done) return std::unexpected(done.error());
        if (value > last) return invalid("Cannot wait for a fence that has not been submitted", ErrorCode::InvalidArgument);
        if (*done >= value) return {};
        if (timeout.count() < 0 || timeout.count() >= INFINITE) return invalid("Invalid fence timeout", ErrorCode::InvalidArgument);
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) return failed(HRESULT_FROM_WIN32(GetLastError()), "CreateEvent failed");
        HRESULT result = fence->SetEventOnCompletion(value, event);
        if (FAILED(result)) { CloseHandle(event); return failed(result, "SetEventOnCompletion failed"); }
        DWORD waited = WaitForSingleObject(event, static_cast<DWORD>(timeout.count()));
        DWORD error = waited == WAIT_FAILED ? GetLastError() : 0;
        CloseHandle(event);
        if (waited == WAIT_TIMEOUT) return invalid("GPU fence timeout", ErrorCode::Timeout);
        if (waited != WAIT_OBJECT_0) return failed(HRESULT_FROM_WIN32(error), "Fence wait failed");
        done = completed(); if (!done) return std::unexpected(done.error());
        if (*done < value) return invalid("Fence woke before completion");
        return {};
    }
    Result<void> idle() {
        auto status = check(); if (!status) return status;
        if (last >= UINT64_MAX-1) return invalid("Fence timeline exhausted");
        // Present may enqueue DXGI work after the last render submission.
        HRESULT result = queue->Signal(fence.Get(), last+1);
        if (FAILED(result)) return failed(result, "Idle queue signal failed");
        ++last;
        return wait(last, std::chrono::seconds(30));
    }
    ~State() {
        // Resource batches contain native COM objects only; no shared_ptr cycle back to State.
        if (queue && fence && last && fence->GetCompletedValue() < last && SUCCEEDED(device->GetDeviceRemovedReason())) {
            HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (event) {
                if (SUCCEEDED(fence->SetEventOnCompletion(last, event))) WaitForSingleObject(event, INFINITE);
                CloseHandle(event);
            } else {
                while (fence->GetCompletedValue() < last && SUCCEEDED(device->GetDeviceRemovedReason())) SwitchToThread();
            }
        }
    }
};
D3D12_RESOURCE_DESC bufferDescription(std::uint64_t size) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = size; desc.Height = 1;
    desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}
Result<ComPtr<ID3D12Resource>> makeBuffer(State& state, std::uint64_t size, Memory memory) {
    auto desc = bufferDescription(size);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = memory == Memory::Upload ? D3D12_HEAP_TYPE_UPLOAD : memory == Memory::Readback ? D3D12_HEAP_TYPE_READBACK : D3D12_HEAP_TYPE_DEFAULT;
    auto initial = memory == Memory::Upload ? D3D12_RESOURCE_STATE_GENERIC_READ : memory == Memory::Readback ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON;
    ComPtr<ID3D12Resource> resource;
    HRESULT result = state.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initial, nullptr, IID_PPV_ARGS(&resource));
    if (FAILED(result)) return failed(result, "CreateCommittedResource(buffer) failed");
    return resource;
}
struct DXBuffer final : Buffer {
    std::shared_ptr<State> owner;
    std::shared_ptr<Tracker> tracker{std::make_shared<Tracker>()};
    ComPtr<ID3D12Resource> resource;
    BufferDesc desc;
    BufferDesc description() const override { return desc; }
    Result<void> write(std::uint64_t offset, std::span<const std::byte> bytes) override {
        auto status = owner->completed(); if (!status) return std::unexpected(status.error());
        if (desc.memory != Memory::Upload || offset > desc.size || bytes.size() > desc.size - offset) return invalid("Invalid upload buffer write", ErrorCode::InvalidArgument);
        if (*status < tracker->lastUse) return invalid("Cannot overwrite a buffer in flight");
        if (tracker->version == UINT64_MAX) return invalid("Resource version exhausted");
        D3D12_RANGE range{0, 0}; void* data{};
        HRESULT result = resource->Map(0, &range, &data);
        if (FAILED(result)) return failed(result, "Upload Map failed");
        if (!bytes.empty()) std::memcpy(static_cast<std::byte*>(data) + offset, bytes.data(), bytes.size());
        D3D12_RANGE written{static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + bytes.size())};
        resource->Unmap(0, &written); ++tracker->version;
        return {};
    }
    Result<std::vector<std::byte>> read(std::uint64_t offset, std::uint64_t size) override {
        auto status = owner->completed(); if (!status) return std::unexpected(status.error());
        if (desc.memory != Memory::Readback || offset > desc.size || size > desc.size - offset || size > SIZE_MAX) return invalid("Invalid readback buffer range", ErrorCode::InvalidArgument);
        if (*status < tracker->lastUse) return invalid("Readback fence has not completed");
        std::vector<std::byte> output(static_cast<size_t>(size));
        D3D12_RANGE range{static_cast<SIZE_T>(offset), static_cast<SIZE_T>(offset + size)}; void* data{};
        HRESULT result = resource->Map(0, &range, &data);
        if (FAILED(result)) return failed(result, "Readback Map failed");
        if (size) std::memcpy(output.data(), static_cast<std::byte*>(data) + offset, static_cast<size_t>(size));
        D3D12_RANGE written{0, 0}; resource->Unmap(0, &written);
        return output;
    }
};
struct DXTexture final : Texture {
    std::shared_ptr<State> owner;
    std::shared_ptr<Tracker> tracker{std::make_shared<Tracker>()};
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12DescriptorHeap> rtv;
    TextureDesc desc;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed{};
    TextureFootprint layout;
    bool backbuffer{};
    TextureDesc description() const override { return desc; }
    TextureFootprint footprint() const override { return layout; }
    Result<void> initViews() {
        auto nativeDesc = resource->GetDesc();
        UINT rows{}; UINT64 rowSize{}, total{};
        owner->device->GetCopyableFootprints(&nativeDesc, 0, 1, 0, &placed, &rows, &rowSize, &total);
        layout = {total, placed.Footprint.RowPitch, static_cast<UINT>(rowSize), rows};
        if (desc.renderTarget) {
            D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.NumDescriptors = 1;
            HRESULT result = owner->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtv));
            if (FAILED(result)) return failed(result, "Create RTV heap failed");
            owner->device->CreateRenderTargetView(resource.Get(), nullptr, rtv->GetCPUDescriptorHandleForHeapStart());
        }
        return {};
    }
};
struct VertexRequirement { UINT slot{},stride{},extent{},alignment{}; VertexInputRate rate{}; };
struct DXPipeline final : Pipeline {
    std::shared_ptr<State> owner;
    ComPtr<ID3D12PipelineState> pso;
    ComPtr<ID3D12RootSignature> root;
    Format format{};
    bool sampled{};
    BindingLayout bindings;
    std::vector<UINT> minimumBytes;
    UINT descriptorSize{};
    std::vector<VertexRequirement> vertexInputs;
};
struct Use { std::shared_ptr<Tracker> tracker; ComPtr<ID3D12Resource> resource; std::uint64_t observedVersion{}; ResourceState state{}; Memory memory{Memory::Device}; };
struct DXCommands final : CommandList {
    std::shared_ptr<State> owner;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12DescriptorHeap> descriptors;
    ComPtr<ID3D12QueryHeap> query;
    ComPtr<ID3D12Resource> queryReadback;
    std::vector<ComPtr<IUnknown>> references;
    std::unordered_set<IUnknown*> retained;
    std::vector<Use> uses;
    std::shared_ptr<DXPipeline> pipeline;
    std::shared_ptr<DXTexture> target;
    struct VertexBinding { std::shared_ptr<DXBuffer> buffer; UINT stride{},size{}; std::uint64_t offset{}; };
    std::array<VertexBinding,32> vertexBuffers{};
    std::shared_ptr<DXBuffer> indexBuffer;
    UINT indexSize{},indexElementSize{};
    std::uint64_t submitted{};
    UINT descriptorCount{};
    struct BoundResource { std::shared_ptr<DXTexture> texture; std::shared_ptr<DXBuffer> buffer; std::uint64_t offset{},size{}; };
    std::vector<BoundResource> boundResources;
    bool recording{}, ended{}, renderPass{}, textureBound{}, timestampsResolved{};
    std::array<bool, 2> timestampWritten{};
    Result<void> ready(bool outsidePass = false) {
        // Device removal is checked at begin/submit/wait, rather than polling the
        // driver for every recorded draw. Recording itself does not execute GPU work.
        if (owner->thread != std::this_thread::get_id()) return invalid("Command recording must run on the creating thread");
        if (!recording || (outsidePass && renderPass)) return invalid("Command operation is outside its recording/pass scope");
        return {};
    }
    template<class T> void retain(const ComPtr<T>& object) {
        IUnknown* ptr = static_cast<IUnknown*>(object.Get());
        if (retained.insert(ptr).second) { ComPtr<IUnknown> ref; object.As(&ref); references.push_back(std::move(ref)); }
    }
    Use& use(const std::shared_ptr<Tracker>& tracker, const ComPtr<ID3D12Resource>& resource, Memory memory = Memory::Device) {
        auto found = std::find_if(uses.begin(), uses.end(), [&](const Use& value) { return value.tracker == tracker; });
        if (found != uses.end()) return *found;
        uses.push_back({tracker, resource, tracker->version, tracker->state, memory}); retain(resource); return uses.back();
    }
    template<class T> Result<std::shared_ptr<T>> owned(const std::shared_ptr<typename std::conditional_t<std::is_same_v<T, DXBuffer>, Buffer, Texture>>& ptr) {
        auto object = std::dynamic_pointer_cast<T>(ptr);
        if (!object || object->owner != owner) return invalid("Null or foreign resource", ErrorCode::WrongDevice);
        return object;
    }
    template<class T> ResourceState current(const T& buffer) const {
        auto entry=std::find_if(uses.begin(),uses.end(),[&](const Use& u){return u.tracker==buffer.tracker;});
        return entry==uses.end()?buffer.tracker->state:entry->state;
    }
    Result<void> change(Use& usage, ResourceState next) {
        if (usage.memory == Memory::Upload) {
            if (next != ResourceState::CopySource && !vertexState(next) && !indexState(next) && next!=ResourceState::ConstantBuffer) return invalid("Upload memory stays in GENERIC_READ", ErrorCode::InvalidArgument);
            usage.state = next; return {};
        }
        if (usage.memory == Memory::Readback) {
            if (next != ResourceState::CopyDestination) return invalid("Readback memory stays in COPY_DEST", ErrorCode::InvalidArgument);
            usage.state = next; return {};
        }
        if (usage.state != next && native(usage.state) != native(next)) {
            D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = usage.resource.Get(); barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = native(usage.state); barrier.Transition.StateAfter = native(next);
            list->ResourceBarrier(1, &barrier);
        }
        usage.state = next; return {};
    }
    Result<void> begin() override {
        auto done = owner->completed(); if (!done) return std::unexpected(done.error());
        if (recording || (submitted && *done < submitted)) return invalid("Command allocator is still recording or in flight");
        HRESULT result = allocator->Reset(); if (FAILED(result)) return failed(result, "Allocator reset failed");
        result = list->Reset(allocator.Get(), nullptr); if (FAILED(result)) return failed(result, "Command list reset failed");
        references.clear(); retained.clear(); uses.clear(); pipeline.reset(); target.reset();
        boundResources.clear();
        vertexBuffers={};indexBuffer.reset();indexSize=indexElementSize=0;
        descriptorCount = 0; submitted = 0; renderPass = false; recording = true; ended = false;
        textureBound = false; timestampsResolved = false; timestampWritten = {};
        return {};
    }
    Result<void> end() override {
        auto status = ready(true); if (!status) return status;
        HRESULT result = list->Close(); if (FAILED(result)) return failed(result, "Command list close failed");
        recording = false; ended = true; return {};
    }
    Result<void> transition(const BufferPtr& ptr, ResourceState next) override {
        auto status = ready(true); if (!status) return status;
        if (next != ResourceState::Common && next != ResourceState::CopySource && next != ResourceState::CopyDestination && !vertexState(next) && !indexState(next) && next!=ResourceState::ConstantBuffer) return invalid("Unsupported buffer state", ErrorCode::InvalidArgument);
        auto object = owned<DXBuffer>(ptr); if (!object) return std::unexpected(object.error());
        return change(use((*object)->tracker, (*object)->resource, (*object)->desc.memory), next);
    }
    Result<void> transition(const TexturePtr& ptr, ResourceState next) override {
        auto status = ready(true); if (!status) return status;
        auto object = owned<DXTexture>(ptr); if (!object) return std::unexpected(object.error());
        if (!validState(next) || vertexState(next) || indexState(next) || next==ResourceState::ConstantBuffer || (next == ResourceState::RenderTarget && !(*object)->desc.renderTarget) ||
            (next == ResourceState::Present && !(*object)->backbuffer) || (next == ResourceState::ShaderRead && (*object)->backbuffer))
            return invalid("Invalid texture state", ErrorCode::InvalidArgument);
        return change(use((*object)->tracker, (*object)->resource), next);
    }
    Result<void> copyBuffer(const BufferPtr& src, const BufferPtr& dst, std::uint64_t bytes) override {
        auto status = ready(true); if (!status) return status;
        auto source = owned<DXBuffer>(src); auto destination = owned<DXBuffer>(dst);
        if (!source) return std::unexpected(source.error()); if (!destination) return std::unexpected(destination.error());
        if (src == dst || !bytes || bytes > (*source)->desc.size || bytes > (*destination)->desc.size || (*source)->desc.memory == Memory::Readback || (*destination)->desc.memory == Memory::Upload)
            return invalid("Invalid buffer copy", ErrorCode::InvalidArgument);
        auto& a = use((*source)->tracker, (*source)->resource, (*source)->desc.memory); if (a.state != ResourceState::CopySource) return invalid("Source is not CopySource");
        auto& b = use((*destination)->tracker, (*destination)->resource, (*destination)->desc.memory); if (b.state != ResourceState::CopyDestination) return invalid("Destination is not CopyDestination");
        list->CopyBufferRegion((*destination)->resource.Get(), 0, (*source)->resource.Get(), 0, bytes); return {};
    }
    Result<void> copyBufferToTexture(const BufferPtr& src, const TexturePtr& dst) override {
        auto status = ready(true); if (!status) return status;
        auto source = owned<DXBuffer>(src); auto destination = owned<DXTexture>(dst);
        if (!source) return std::unexpected(source.error()); if (!destination) return std::unexpected(destination.error());
        if ((*source)->desc.memory == Memory::Readback || (*source)->desc.size < (*destination)->layout.size) return invalid("Upload buffer is too small or not copyable", ErrorCode::InvalidArgument);
        if (use((*source)->tracker, (*source)->resource, (*source)->desc.memory).state != ResourceState::CopySource ||
            use((*destination)->tracker, (*destination)->resource).state != ResourceState::CopyDestination) return invalid("Texture upload requires CopySource/CopyDestination");
        D3D12_TEXTURE_COPY_LOCATION a{}, b{};
        a.pResource = (*source)->resource.Get(); a.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; a.PlacedFootprint = (*destination)->placed;
        b.pResource = (*destination)->resource.Get(); b.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        list->CopyTextureRegion(&b, 0, 0, 0, &a, nullptr); return {};
    }
    Result<void> copyTextureToBuffer(const TexturePtr& src, const BufferPtr& dst) override {
        auto status = ready(true); if (!status) return status;
        auto source = owned<DXTexture>(src); auto destination = owned<DXBuffer>(dst);
        if (!source) return std::unexpected(source.error()); if (!destination) return std::unexpected(destination.error());
        if ((*destination)->desc.memory == Memory::Upload || (*destination)->desc.size < (*source)->layout.size) return invalid("Readback buffer is too small or not copyable", ErrorCode::InvalidArgument);
        if (use((*source)->tracker, (*source)->resource).state != ResourceState::CopySource ||
            use((*destination)->tracker, (*destination)->resource, (*destination)->desc.memory).state != ResourceState::CopyDestination) return invalid("Texture download requires CopySource/CopyDestination");
        D3D12_TEXTURE_COPY_LOCATION a{}, b{};
        a.pResource = (*source)->resource.Get(); a.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        b.pResource = (*destination)->resource.Get(); b.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; b.PlacedFootprint = (*source)->placed;
        list->CopyTextureRegion(&b, 0, 0, 0, &a, nullptr); return {};
    }
    Result<void> beginRenderPass(const TexturePtr& ptr, Color clear) override {
        auto status = ready(true); if (!status) return status;
        auto object = owned<DXTexture>(ptr); if (!object) return std::unexpected(object.error());
        if (!(*object)->desc.renderTarget || use((*object)->tracker, (*object)->resource).state != ResourceState::RenderTarget) return invalid("Render target must be in RenderTarget state");
        target = *object; retain(target->rtv); renderPass = true;
        auto handle = target->rtv->GetCPUDescriptorHandleForHeapStart();
        float color[]{clear.r, clear.g, clear.b, clear.a};
        list->OMSetRenderTargets(1, &handle, FALSE, nullptr); list->ClearRenderTargetView(handle, color, 0, nullptr);
        D3D12_VIEWPORT viewport{0, 0, static_cast<float>(target->desc.width), static_cast<float>(target->desc.height), 0, 1};
        D3D12_RECT scissor{0, 0, static_cast<LONG>(target->desc.width), static_cast<LONG>(target->desc.height)};
        list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &scissor); return {};
    }
    Result<void> endRenderPass() override {
        auto status = ready(); if (!status) return status;
        if (!renderPass) return invalid("No active render pass"); renderPass = false; target.reset(); return {};
    }
    Result<void> setPipeline(const PipelinePtr& ptr) override {
        auto status = ready(); if (!status) return status;
        auto object = std::dynamic_pointer_cast<DXPipeline>(ptr);
        if (!object || object->owner != owner) return invalid("Null or foreign pipeline", ErrorCode::WrongDevice);
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
    Result<void> bindResources(const ResourceBindings& bindings) override {
        auto status=ready();if(!status)return status;
        if(!pipeline)return invalid("Resource binding requires a pipeline");
        if(pipeline->descriptorSize>256-descriptorCount)return invalid("Command descriptor capacity exhausted; split the command list");
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
            auto at=slot(binding.binding,binding.element,BindingType::Texture2D);if(!at)return std::unexpected(at.error());
            auto object=owned<DXTexture>(binding.texture);if(!object)return std::unexpected(object.error());
            if((*object)->backbuffer || current(**object)!=ResourceState::ShaderRead)return invalid("Texture binding requires ShaderRead");
            snapshot[*at].texture=*object;
        }
        for(const auto& binding:bindings.constants) {
            auto at=slot(binding.binding,binding.element,BindingType::ConstantBuffer);if(!at)return std::unexpected(at.error());
            auto object=owned<DXBuffer>(binding.buffer);if(!object)return std::unexpected(object.error());
            const auto& buffer=**object;
            if(buffer.desc.memory==Memory::Readback || binding.offset%256 || !binding.size || binding.size%256 || binding.size>65536 ||
               binding.offset>buffer.desc.size || binding.size>buffer.desc.size-binding.offset || binding.size<pipeline->minimumBytes[binding.binding])
                return invalid("Invalid constant buffer view: alignment, bounds or shader size",ErrorCode::InvalidArgument);
            if(current(buffer)!=ResourceState::ConstantBuffer)return invalid("Constant buffer binding requires ConstantBuffer state");
            snapshot[*at]={nullptr,*object,binding.offset,binding.size};
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
                    D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=native(resource.texture->desc.format);view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
                    view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
                    owner->device->CreateShaderResourceView(resource.texture->resource.Get(),&view,cpu);
                } else {
                    D3D12_CONSTANT_BUFFER_VIEW_DESC view{resource.buffer->resource->GetGPUVirtualAddress()+resource.offset,static_cast<UINT>(resource.size)};
                    owner->device->CreateConstantBufferView(&view,cpu);
                }
                cpu.ptr+=increment;gpu.ptr+=increment;
            }
        }
        descriptorCount+=pipeline->descriptorSize;boundResources=std::move(snapshot);textureBound=true;return {};
    }
    Result<void> setScissor(UINT x, UINT y, UINT width, UINT height) override {
        auto status = ready(); if (!status) return status;
        if (!renderPass || !width || !height || x > target->desc.width || y > target->desc.height || width > target->desc.width-x || height > target->desc.height-y)
            return invalid("Scissor outside target", ErrorCode::InvalidArgument);
        D3D12_RECT rect{static_cast<LONG>(x), static_cast<LONG>(y), static_cast<LONG>(x+width), static_cast<LONG>(y+height)};
        list->RSSetScissorRects(1, &rect); return {};
    }
    Result<void> bindVertexBuffer(UINT slot,const BufferPtr& ptr,UINT stride,std::uint64_t offset,std::uint64_t size) override {
        auto status=ready();if(!status)return status;
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
        if (!renderPass || !pipeline || pipeline->format != target->desc.format || (pipeline->descriptorSize && !textureBound)) return invalid("Draw requires compatible target, pipeline and bindings");
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
        list->DrawInstanced(vertices, instances, firstVertex, firstInstance); return {};
    }
    Result<void> drawIndexed(UINT indices,UINT instances,UINT firstIndex,INT baseVertex,UINT firstInstance) override {
        auto status=drawReady();if(!status)return status;
        if(!indices || !instances)return invalid("Empty indexed draw",ErrorCode::InvalidArgument);
        if(!indexBuffer || !indexState(current(*indexBuffer)))return invalid("Missing or stale index buffer binding");
        if(static_cast<std::uint64_t>(firstIndex)+indices>indexSize/indexElementSize || static_cast<std::uint64_t>(firstInstance)+instances>std::uint64_t{UINT_MAX}+1)
            return invalid("Indexed draw range exceeds buffer view",ErrorCode::InvalidArgument);
        status=inputReady(0,instances,0,firstInstance,true,baseVertex);if(!status)return status;
        list->DrawIndexedInstanced(indices,instances,firstIndex,baseVertex,firstInstance);return {};
    }
    Result<void> writeTimestamp(UINT index) override {
        auto status = ready(); if (!status) return status;
        if (index > 1 || timestampWritten[index] || timestampsResolved) return invalid("Timestamp slot invalid or already written", ErrorCode::InvalidArgument);
        list->EndQuery(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index); timestampWritten[index] = true; return {};
    }
    Result<void> resolveTimestamps() override {
        auto status = ready(true); if (!status) return status;
        if (!timestampWritten[0] || !timestampWritten[1] || timestampsResolved) return invalid("Two timestamps must be written once before resolving");
        list->ResolveQueryData(query.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, queryReadback.Get(), 0); timestampsResolved = true; return {};
    }
    Result<double> elapsedGpuMilliseconds() override {
        auto done = owner->completed(); if (!done) return std::unexpected(done.error());
        if (!submitted || *done < submitted || !timestampsResolved) return invalid("GPU timestamps are not ready");
        UINT64 frequency{}; HRESULT result = owner->queue->GetTimestampFrequency(&frequency);
        if (FAILED(result) || !frequency) return failed(FAILED(result) ? result : E_FAIL, "Timestamp frequency unavailable");
        void* data{}; D3D12_RANGE range{0, 16}; result = queryReadback->Map(0, &range, &data);
        if (FAILED(result)) return failed(result, "Timestamp Map failed");
        UINT64 values[2]{}; std::memcpy(values, data, 16); D3D12_RANGE written{0, 0}; queryReadback->Unmap(0, &written);
        if (values[1] < values[0]) return invalid("Timestamps written in reverse order");
        return 1000.0 * static_cast<double>(values[1] - values[0]) / static_cast<double>(frequency);
    }
    Result<void> debugMarker(std::string_view text) override {
        auto status = ready(); if (!status) return status;
        if (text.size() > UINT_MAX) return invalid("Marker too long", ErrorCode::InvalidArgument);
        list->SetMarker(0, text.data(), static_cast<UINT>(text.size())); return {};
    }
};
struct DXSwapChain final : SwapChain {
    std::shared_ptr<State> owner;
    ComPtr<IDXGISwapChain3> chain;
    SwapChainDesc desc;
    std::vector<std::shared_ptr<DXTexture>> buffers;
    Result<void> rebuild() {
        for (UINT i = 0; i < desc.buffers; ++i) {
            auto texture = std::make_shared<DXTexture>(); texture->owner = owner;
            texture->desc = {desc.width, desc.height, Format::RGBA8UNorm, true}; texture->backbuffer = true;
            texture->tracker->state = ResourceState::Present;
            HRESULT result = chain->GetBuffer(i, IID_PPV_ARGS(&texture->resource));
            if (FAILED(result)) return failed(result, "Get swapchain buffer failed");
            auto status = texture->initViews(); if (!status) return status;
            buffers.push_back(std::move(texture));
        }
        return {};
    }
    Result<TexturePtr> acquire() override {
        auto status = owner->check(); if (!status) return std::unexpected(status.error());
        if (buffers.size() != desc.buffers) return invalid("Swapchain buffers unavailable");
        return buffers[chain->GetCurrentBackBufferIndex()];
    }
    Result<void> present() override {
        auto status = owner->check(); if (!status) return status;
        if (buffers.size() != desc.buffers) return invalid("Swapchain buffers unavailable");
        auto texture = buffers[chain->GetCurrentBackBufferIndex()];
        if (texture->tracker->state != ResourceState::Present) return invalid("Backbuffer must be submitted in Present state");
        HRESULT result = chain->Present(desc.vsync ? 1 : 0, 0);
        if (FAILED(result)) return failed(result, "Present failed"); return {};
    }
    Result<void> resize(UINT width, UINT height) override {
        auto status = owner->check(); if (!status) return status;
        if (!width || !height || width > 16384 || height > 16384) return invalid("Invalid swapchain extent", ErrorCode::InvalidArgument);
        for (const auto& buffer : buffers) if (buffer.use_count() != 1 || buffer->tracker.use_count() != 1) return invalid("Release acquired backbuffers and command references before resizing");
        status = owner->idle(); if (!status) return status;
        buffers.clear();
        HRESULT result = chain->ResizeBuffers(desc.buffers, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        if (FAILED(result)) { auto restored = rebuild(); (void)restored; return failed(result, "ResizeBuffers failed"); }
        desc.width = width; desc.height = height; return rebuild();
    }
    ~DXSwapChain() override {
        if (owner) { auto waited = owner->idle(); (void)waited; }
    }
};
class DXDevice final : public Device, public CommandQueue {
    std::shared_ptr<State> state;
public:
    explicit DXDevice(std::shared_ptr<State> value) : state(std::move(value)) {}
    const DeviceCaps& caps() const override { return state->caps; }
    CommandQueue& queue() override { return *this; }
    Result<BufferPtr> createBuffer(BufferDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!desc.size || desc.size > SIZE_MAX || (desc.memory != Memory::Device && desc.memory != Memory::Upload && desc.memory != Memory::Readback)) return invalid("Invalid buffer description", ErrorCode::InvalidArgument);
        auto resource = makeBuffer(*state, desc.size, desc.memory); if (!resource) return std::unexpected(resource.error());
        auto buffer = std::make_shared<DXBuffer>(); buffer->owner = state; buffer->resource = std::move(*resource); buffer->desc = desc;
        if (desc.memory == Memory::Upload) buffer->tracker->state = ResourceState::CopySource;
        if (desc.memory == Memory::Readback) buffer->tracker->state = ResourceState::CopyDestination;
        return buffer;
    }
    Result<TexturePtr> createTexture(TextureDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!desc.width || !desc.height || desc.width > 16384 || desc.height > 16384 || !validFormat(desc.format)) return invalid("Invalid texture description", ErrorCode::InvalidArgument);
        auto texture = std::make_shared<DXTexture>(); texture->owner = state; texture->desc = desc;
        D3D12_RESOURCE_DESC nativeDesc{};
        nativeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; nativeDesc.Width = desc.width; nativeDesc.Height = desc.height;
        nativeDesc.DepthOrArraySize = 1; nativeDesc.MipLevels = 1; nativeDesc.Format = native(desc.format); nativeDesc.SampleDesc.Count = 1;
        if (desc.renderTarget) nativeDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        HRESULT result = state->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &nativeDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&texture->resource));
        if (FAILED(result)) return failed(result, "Create texture failed");
        status = texture->initViews(); if (!status) return std::unexpected(status.error());
        return texture;
    }
    Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (desc.vertex.stage != ShaderStage::Vertex || desc.fragment.stage != ShaderStage::Fragment || desc.vertex.bytes.empty() || desc.fragment.bytes.empty() || !validFormat(desc.colorFormat)) return invalid("Invalid graphics pipeline shaders/format", ErrorCode::InvalidArgument);
        status=validateVertexLayout(desc.vertexLayout);if(!status)return std::unexpected(status.error());
        auto pipeline = std::make_shared<DXPipeline>(); pipeline->owner = state; pipeline->format = desc.colorFormat; pipeline->sampled = desc.sampledTexture;
        if(desc.sampledTexture && (!desc.bindingLayout.entries.empty() || !desc.bindingLayout.samplers.empty()))return invalid("Legacy sampledTexture and explicit bindingLayout cannot be combined",ErrorCode::InvalidArgument);
        pipeline->bindings=desc.sampledTexture?BindingLayout{{{}},{{}}}:desc.bindingLayout;
        status=validateBindingLayout(pipeline->bindings);if(!status)return std::unexpected(status.error());
        pipeline->minimumBytes.resize(pipeline->bindings.entries.size());
        for(const auto& entry:pipeline->bindings.entries)pipeline->descriptorSize+=entry.count;
        auto validateShader=[&](const ShaderBinary& shader)->Result<void> {
            auto reflected=reflectShader(shader);if(!reflected)return std::unexpected(reflected.error());
            const auto visibility=shader.stage==ShaderStage::Vertex?ShaderVisibility::Vertex:ShaderVisibility::Fragment;
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
                    const auto type=resource.type==ShaderResourceType::Texture2D?BindingType::Texture2D:BindingType::ConstantBuffer;
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
        status=validateShader(desc.vertex);if(!status)return std::unexpected(status.error());
        status=validateShader(desc.fragment);if(!status)return std::unexpected(status.error());
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
            ranges[i]={entry.type==BindingType::Texture2D?D3D12_DESCRIPTOR_RANGE_TYPE_SRV:D3D12_DESCRIPTOR_RANGE_TYPE_CBV,entry.count,entry.slot,entry.space,0};
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
        if (FAILED(result)) return failed(result, "Root signature serialization failed");
        result = state->device->CreateRootSignature(0, binary->GetBufferPointer(), binary->GetBufferSize(), IID_PPV_ARGS(&pipeline->root));
        if (FAILED(result)) return failed(result, "Root signature creation failed");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = pipeline->root.Get(); pso.VS = {desc.vertex.bytes.data(), desc.vertex.bytes.size()}; pso.PS = {desc.fragment.bytes.data(), desc.fragment.bytes.size()};
        pso.InputLayout={inputs.data(),static_cast<UINT>(inputs.size())};
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pso.RasterizerState.DepthClipEnable = TRUE;
        auto& blend = pso.BlendState.RenderTarget[0]; blend.BlendEnable = desc.alphaBlend;
        blend.SrcBlend = D3D12_BLEND_SRC_ALPHA; blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE; blend.DestBlendAlpha = D3D12_BLEND_ZERO; blend.BlendOpAlpha = D3D12_BLEND_OP_ADD; blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pso.SampleMask = UINT_MAX; pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1; pso.RTVFormats[0] = native(desc.colorFormat); pso.SampleDesc.Count = 1;
        result = state->device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline->pso));
        if (FAILED(result)) return failed(result, "Graphics pipeline creation failed");
        return pipeline;
    }
    Result<std::unique_ptr<CommandList>> createCommandList() override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        auto commands = std::make_unique<DXCommands>(); commands->owner = state;
        HRESULT result = state->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commands->allocator));
        if (FAILED(result)) return failed(result, "Create allocator failed");
        result = state->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commands->allocator.Get(), nullptr, IID_PPV_ARGS(&commands->list));
        if (FAILED(result)) return failed(result, "Create command list failed");
        result = commands->list->Close(); if (FAILED(result)) return failed(result, "Initial command list close failed");
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.NumDescriptors = 256; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        result = state->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&commands->descriptors));
        if (FAILED(result)) return failed(result, "Create command descriptor heap failed");
        D3D12_QUERY_HEAP_DESC queries{}; queries.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; queries.Count = 2;
        result = state->device->CreateQueryHeap(&queries, IID_PPV_ARGS(&commands->query));
        if (FAILED(result)) return failed(result, "Create timestamp query heap failed");
        auto readback = makeBuffer(*state, 16, Memory::Readback); if (!readback) return std::unexpected(readback.error());
        commands->queryReadback = *readback;
        return std::unique_ptr<CommandList>(std::move(commands));
    }
    Result<Fence> submit(CommandList& commands) override {
        auto status = state->completed(); if (!status) return std::unexpected(status.error());
        auto* cmd = dynamic_cast<DXCommands*>(&commands);
        if (!cmd || cmd->owner != state) return invalid("Command list belongs to another device", ErrorCode::WrongDevice);
        if (!cmd->ended || cmd->recording || cmd->submitted) return invalid("Command list is not executable or was already submitted");
        if (state->last >= UINT64_MAX-1) return invalid("Fence timeline exhausted");
        for (const auto& usage : cmd->uses) if (usage.observedVersion != usage.tracker->version || usage.tracker->version == UINT64_MAX)
            return invalid("Resource changed after recording; re-record the command list");
        Batch batch; batch.fence = state->last+1; batch.objects = cmd->references;
        auto add = [&](const auto& ptr) { ComPtr<IUnknown> ref; ptr.As(&ref); batch.objects.push_back(std::move(ref)); };
        add(cmd->allocator); add(cmd->list); add(cmd->descriptors); add(cmd->query); add(cmd->queryReadback);
        // Allocate retention before ExecuteCommandLists; failure cannot orphan submitted GPU work.
        state->pending.push_back(std::move(batch));
        ID3D12CommandList* lists[]{cmd->list.Get()}; state->queue->ExecuteCommandLists(1, lists);
        ++state->last; cmd->submitted = state->last;
        for (auto& usage : cmd->uses) { usage.tracker->state = usage.state; usage.tracker->lastUse = state->last; ++usage.tracker->version; }
        HRESULT result = state->queue->Signal(state->fence.Get(), state->last);
        if (FAILED(result)) return failed(result, "Queue signal failed after execution");
        return Fence{state->last, state->id};
    }
    Result<void> wait(Fence fence, std::chrono::milliseconds timeout) override {
        if (fence.device != state->id) return invalid("Fence belongs to another device", ErrorCode::WrongDevice);
        return state->wait(fence.value, timeout);
    }
    Result<void> waitIdle() override { return state->idle(); }
    Result<std::uint64_t> completedValue() override { return state->completed(); }
    Result<std::uint64_t> validationErrorCount() override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (!state->info) return invalid("Validation was not requested", ErrorCode::Unsupported);
        std::uint64_t errors = 0;
        for (UINT64 i = 0; i < state->info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
            SIZE_T bytes{}; HRESULT result = state->info->GetMessage(i, nullptr, &bytes);
            if (FAILED(result)) return failed(result, "Validation message query failed");
            std::vector<std::byte> storage(bytes); auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            result = state->info->GetMessage(i, message, &bytes); if (FAILED(result)) return failed(result, "Validation message read failed");
            if (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION || message->Severity == D3D12_MESSAGE_SEVERITY_ERROR) ++errors;
        }
        return errors;
    }
    Result<std::unique_ptr<SwapChain>> createSwapChain(Surface surface, SwapChainDesc desc) override {
        auto status = state->check(); if (!status) return std::unexpected(status.error());
        if (surface.kind != SurfaceKind::Win32 || !IsWindow(static_cast<HWND>(surface.window)) || !desc.width || !desc.height || desc.width > 16384 || desc.height > 16384 || desc.buffers < 2 || desc.buffers > 4)
            return invalid("Invalid Win32 surface or swapchain description", ErrorCode::InvalidArgument);
        auto swap = std::make_unique<DXSwapChain>(); swap->owner = state; swap->desc = desc;
        DXGI_SWAP_CHAIN_DESC1 nativeDesc{}; nativeDesc.Width = desc.width; nativeDesc.Height = desc.height;
        nativeDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; nativeDesc.SampleDesc.Count = 1;
        nativeDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; nativeDesc.BufferCount = desc.buffers;
        nativeDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain;
        HRESULT result = state->factory->CreateSwapChainForHwnd(state->queue.Get(), static_cast<HWND>(surface.window), &nativeDesc, nullptr, nullptr, &chain);
        if (FAILED(result)) return failed(result, "Create swapchain failed");
        result = chain.As(&swap->chain); if (FAILED(result)) return failed(result, "Swapchain3 interface unavailable");
        result = state->factory->MakeWindowAssociation(static_cast<HWND>(surface.window), DXGI_MWA_NO_ALT_ENTER);
        if (FAILED(result)) return failed(result, "Window association failed");
        status = swap->rebuild(); if (!status) return std::unexpected(status.error());
        return std::unique_ptr<SwapChain>(std::move(swap));
    }
};
}
Result<std::unique_ptr<Device>> createD3D12Device(DeviceDesc desc) {
    if (desc.validation) {
        ComPtr<ID3D12Debug> debug; HRESULT result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
        if (FAILED(result)) return invalid("D3D12 debug layer unavailable; install Windows Graphics Tools", ErrorCode::Unsupported);
        debug->EnableDebugLayer(); ComPtr<ID3D12Debug1> debug1;
        if (SUCCEEDED(debug.As(&debug1))) debug1->SetEnableGPUBasedValidation(TRUE);
    }
    auto state = std::make_shared<State>();
    static std::atomic<std::uint64_t> identifiers{1}; state->id = identifiers.fetch_add(1);
    HRESULT result = CreateDXGIFactory2(desc.validation ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&state->factory));
    if (FAILED(result)) return failed(result, "Create DXGI factory failed");
    ComPtr<IDXGIAdapter1> selected;
    if (desc.softwareAdapter) {
        result = state->factory->EnumWarpAdapter(IID_PPV_ARGS(&selected));
        if (FAILED(result)) return failed(result, "WARP adapter unavailable");
        result = D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&state->device));
    } else {
        for (UINT i = 0; ; ++i) {
            result = state->factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&selected));
            if (result == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(result)) return failed(result, "Enumerating hardware adapter failed");
            DXGI_ADAPTER_DESC1 info{}; selected->GetDesc1(&info);
            if (!(info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                result = D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&state->device));
                if (SUCCEEDED(result)) break;
            }
            selected.Reset();
        }
    }
    if (!state->device) return invalid("No D3D12 adapter supports feature level 11.0", ErrorCode::Unsupported);
    DXGI_ADAPTER_DESC1 info{}; selected->GetDesc1(&info);
    int count = WideCharToMultiByte(CP_UTF8, 0, info.Description, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return failed(E_FAIL, "Adapter name conversion failed");
    std::string name(static_cast<size_t>(count), '\0'); WideCharToMultiByte(CP_UTF8, 0, info.Description, -1, name.data(), count, nullptr, nullptr); name.pop_back();
    state->caps = {Backend::D3D12, std::move(name), (info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0, true, false, true, true, true, true, true};
    if (desc.validation) { result = state->device.As(&state->info); if (FAILED(result)) return failed(result, "D3D12 InfoQueue unavailable"); }
    D3D12_COMMAND_QUEUE_DESC queue{}; queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    result = state->device->CreateCommandQueue(&queue, IID_PPV_ARGS(&state->queue));
    if (FAILED(result)) return failed(result, "Create command queue failed");
    result = state->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&state->fence));
    if (FAILED(result)) return failed(result, "Create fence failed");
    return std::unique_ptr<Device>(std::make_unique<DXDevice>(std::move(state)));
}
}
