// Private Objective-C++ implementation; CMake explicitly selects OBJCXX for this unit.
#include <graphics/Graphics.hpp>
#include "../../foundation/DeviceGeneration.hpp"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <deque>
#include <cstring>
#include <limits>
#include <thread>
#include <array>

namespace gfx {
namespace {
auto fail(ErrorCode code, std::string text, std::int64_t native = 0) {
    return std::unexpected(Error{code, std::move(text), native});
}
struct Pending {
    std::uint64_t serial;
    id<MTLCommandBuffer> buffer;
    std::vector<std::shared_ptr<struct Allocation>> allocations;
    std::vector<std::shared_ptr<struct NativePipeline>> pipelines;
};
struct Allocation {
    id<MTLBuffer> buffer;
    id<MTLTexture> texture;
    BufferDesc desc;
    TextureDesc textureDesc;
    TextureFootprint footprint;
    bool initialized{};
    ResourceState state{ResourceState::Common};
    std::uint64_t version{}, lastUse{}, bytes{};
};
struct Use {
    std::shared_ptr<Allocation> allocation;
    std::uint64_t version;
    ResourceState shadow;
    bool initialized;
};
struct NativePipeline {
    id<MTLRenderPipelineState> pipeline;
    id<MTLDepthStencilState> depth;
    std::vector<id<MTLSamplerState>> samplers;
    VertexLayout vertices;
    BindingLayout bindings;
    RenderTargetLayout targets;
    DepthState depthState;
    std::vector<std::uint64_t> minimumBytes;
    bool sampled{};
    void release() { pipeline = nil; depth = nil; samplers.clear(); }
};
struct BufferView {
    std::shared_ptr<Allocation> allocation;
    std::uint64_t offset{},bytes{};
    std::uint32_t stride{};
};
struct ResourceView { std::shared_ptr<Allocation> allocation; std::uint64_t offset{},size{}; };
struct Draw {
    std::shared_ptr<NativePipeline> pipeline;
    std::shared_ptr<const std::vector<ResourceView>> bindings;
    std::array<BufferView,15> vertices;
    BufferView indices;
    std::uint32_t count{},instances{},first{},firstInstance{};
    std::int32_t baseVertex{};
    bool indexed{};
    MTLScissorRect scissor{};
};
struct Attachment {
    std::shared_ptr<Allocation> allocation;
    LoadOp load;
    Color clear;
    float clearDepth{};
    bool readOnly{};
};
enum class OperationKind { Buffer, UploadTexture, ReadTexture, Pass };
struct Operation {
    std::shared_ptr<Allocation> source, destination;
    std::uint64_t bytes{};
    std::string marker;
    OperationKind kind{OperationKind::Buffer};
    std::vector<Attachment> attachments;
    std::vector<std::string> markers;
    std::vector<Draw> draws;
    std::vector<std::pair<bool,std::size_t>> order;
};
struct NativeRecording { id<MTLCommandBuffer> buffer; };
struct State {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    const std::thread::id thread = std::this_thread::get_id();
    std::uint64_t generation{}, last{}, completed{};
    bool retired{};
    std::optional<Error> failure;
    std::deque<Pending> pending;
    std::vector<std::weak_ptr<Allocation>> allocations;
    std::vector<std::weak_ptr<int>> objects;
    id<MTLComputePipelineState> tailPipeline;
    std::vector<std::weak_ptr<NativeRecording>> recordings;
    std::vector<std::weak_ptr<NativePipeline>> pipelines;
    void retire() {
        if (retired) return;
        @autoreleasepool {
            for (auto& p : pending) [p.buffer waitUntilCompleted];
            pending.clear();
            for (auto& w : recordings) if (auto r = w.lock()) r->buffer = nil;
            for (auto& w : allocations) if (auto a = w.lock()) { a->buffer = nil; a->texture = nil; }
            for (auto& w : pipelines) if (auto p = w.lock()) p->release();
            tailPipeline = nil; queue = nil; device = nil;
        }
        retired = true;
    }
    ~State() { retire(); }
    Result<void> check() const {
        if (std::this_thread::get_id() != thread) return fail(ErrorCode::InvalidState, "Metal access requires the creating thread");
        if (retired) return fail(ErrorCode::DeviceLost, "Metal device generation retired");
        if (failure) return std::unexpected(*failure);
        return {};
    }
    Result<void> poll() {
        if (auto ok = check(); !ok) return ok;
        @autoreleasepool {
            while (!pending.empty()) {
                auto& p = pending.front();
                const auto status = p.buffer.status;
                if (status == MTLCommandBufferStatusError) {
                    NSError* error = p.buffer.error;
                    const bool metal = [error.domain isEqualToString:MTLCommandBufferErrorDomain];
                    const auto code = error ? error.code : 0;
                    const auto category = metal && (code == MTLCommandBufferErrorDeviceRemoved || code == MTLCommandBufferErrorAccessRevoked)
                        ? ErrorCode::DeviceLost : ErrorCode::BackendFailure;
                    const char* message = error.localizedDescription.UTF8String;
                    failure = Error{category, message ? message : "Metal command buffer failed", code};
                    return std::unexpected(*failure);
                }
                if (status != MTLCommandBufferStatusCompleted) break;
                completed = p.serial;
                pending.pop_front();
            }
        }
        return {};
    }
    Result<void> prepareTail() {
        if (tailPipeline) return {};
        @autoreleasepool {
            // macOS blits require a four-byte size. One thread copies only the logical tail,
            // preserving all destination bytes beyond the requested copy (including partial copies).
            NSString* source = @"#include <metal_stdlib>\nusing namespace metal;\n"
                "kernel void gfx_copy_tail(const device uchar* src [[buffer(0)]], device uchar* dst [[buffer(1)]],"
                "constant ulong& offset [[buffer(2)]], constant uint& count [[buffer(3)]]) {"
                "for (uint i=0; i<count; ++i) dst[offset+i]=src[offset+i]; }";
            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
            if (!library) {
                const char* message = error.localizedDescription.UTF8String;
                return fail(ErrorCode::BackendFailure, message ? message : "Metal tail library failed", error.code);
            }
            id<MTLFunction> function = [library newFunctionWithName:@"gfx_copy_tail"];
            if (!function) return fail(ErrorCode::BackendFailure, "Metal tail function unavailable");
            tailPipeline = [device newComputePipelineStateWithFunction:function error:&error];
            if (!tailPipeline) {
                const char* message = error.localizedDescription.UTF8String;
                return fail(ErrorCode::BackendFailure, message ? message : "Metal tail pipeline failed", error.code);
            }
        }
        return {};
    }
    // Presentation, timestamps and native validation remain unsupported.
    auto unsupported() const {
        if (auto ok = check(); !ok) return std::unexpected(ok.error());
        return fail(ErrorCode::Unsupported, "Not implemented by the Metal submission backend");
    }
};
struct MetalBuffer final : Buffer {
    std::shared_ptr<State> owner;
    std::shared_ptr<Allocation> allocation;
    std::shared_ptr<int> token;
    BufferDesc description() const override { return allocation->desc; }
    Result<void> write(std::uint64_t offset, std::span<const std::byte> data) override {
        if (auto ok = owner->poll(); !ok) return ok;
        auto& a = *allocation;
        if (a.desc.memory != Memory::Upload || offset > a.desc.size || data.size() > a.desc.size-offset)
            return fail(ErrorCode::InvalidArgument, "Metal upload write memory or range invalid");
        if (a.lastUse > owner->completed || a.version == UINT64_MAX)
            return fail(ErrorCode::InvalidState, "Metal upload busy or version exhausted");
        @autoreleasepool {
            if (!data.empty()) {
                std::memcpy(static_cast<std::byte*>(a.buffer.contents)+offset, data.data(), data.size());
                if (a.buffer.storageMode == MTLStorageModeManaged) [a.buffer didModifyRange:NSMakeRange(offset, data.size())];
            }
        }
        ++a.version; // Even a successful empty write invalidates an older recording.
        return {};
    }
    Result<std::vector<std::byte>> read(std::uint64_t offset, std::uint64_t bytes) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        const auto& a = *allocation;
        if (a.desc.memory != Memory::Readback || offset > a.desc.size || bytes > a.desc.size-offset || bytes > SIZE_MAX)
            return fail(ErrorCode::InvalidArgument, "Metal readback read memory or range invalid");
        if (a.lastUse > owner->completed) return fail(ErrorCode::InvalidState, "Metal readback busy");
        std::vector<std::byte> result(static_cast<std::size_t>(bytes));
        @autoreleasepool {
            if (bytes) std::memcpy(result.data(), static_cast<const std::byte*>(a.buffer.contents)+offset, result.size());
        }
        return result;
    }
};
struct MetalTexture final : Texture {
    std::shared_ptr<State> owner;
    std::shared_ptr<Allocation> allocation;
    std::shared_ptr<int> token;
    TextureDesc description() const override { return allocation->textureDesc; }
    TextureFootprint footprint() const override { return allocation->footprint; }
};
struct MetalPipeline final : Pipeline {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativePipeline> native;
    std::shared_ptr<int> token;
};
bool vertexState(ResourceState s) { return s == ResourceState::VertexBuffer || s == ResourceState::VertexAndIndexBuffer; }
bool indexState(ResourceState s) { return s == ResourceState::IndexBuffer || s == ResourceState::VertexAndIndexBuffer; }
bool visible(ShaderVisibility v,unsigned stage) { return v == ShaderVisibility::All || unsigned(v) == stage; }
MTLPixelFormat pixelFormat(Format f) {
    switch(f) { case Format::RGBA8UNorm:return MTLPixelFormatRGBA8Unorm;case Format::BGRA8UNorm:return MTLPixelFormatBGRA8Unorm;
    case Format::RGBA16Float:return MTLPixelFormatRGBA16Float;case Format::D32Float:return MTLPixelFormatDepth32Float; }
    return MTLPixelFormatInvalid;
}
#include "MetalGraphics.hpp"
struct MetalCommands final : CommandList {
    std::shared_ptr<State> owner;
    std::shared_ptr<NativeRecording> native;
    bool open{}, ended{};
    bool inPass{};
    std::size_t passOperation{};
    std::uint64_t submitted{};
    std::vector<Use> uses;
    std::vector<Operation> operations;
    std::shared_ptr<NativePipeline> selected;
    std::shared_ptr<const std::vector<ResourceView>> bindings;
    std::array<BufferView,15> vertices;
    BufferView indices;
    MTLScissorRect scissor{};
    explicit MetalCommands(std::shared_ptr<State> state) : owner(std::move(state)) {}
    Result<void> begin() override {
        if (auto ok = owner->poll(); !ok) return ok;
        if (open || submitted > owner->completed) return fail(ErrorCode::InvalidState, "Metal command list is open or in flight");
        auto next = std::make_shared<NativeRecording>();
        std::erase_if(owner->recordings, [](auto& w) { return w.expired(); });
        owner->recordings.push_back(next); native = std::move(next);
        open = true; ended = false; submitted = 0; inPass = false;
        operations.clear(); uses.clear();
        selected.reset(); bindings.reset(); vertices = {}; indices = {};
        return {};
    }
    Result<void> end() override {
        if (auto ok = owner->check(); !ok) return ok;
        if (!open) return fail(ErrorCode::InvalidState, "Metal command list is not recording");
        if (inPass) return fail(ErrorCode::InvalidState, "Metal render pass must be ended first");
        // Finalize into a fresh native buffer. An encoder/compiler failure discards that
        // uncommitted buffer and leaves the accepted CPU recording open and retryable.
        @autoreleasepool {
            if (std::any_of(operations.begin(), operations.end(), [](const auto& op) { return op.kind == OperationKind::Buffer && (op.bytes & 3) != 0; }))
                if (auto ok = owner->prepareTail(); !ok) return ok;
            id<MTLCommandBuffer> next = [owner->queue commandBuffer];
            if (!next) return fail(ErrorCode::OutOfMemory, "Metal command buffer creation failed");
            for (const auto& op : operations) {
                if (op.kind == OperationKind::Pass) {
                    MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
                    if (!descriptor) return fail(ErrorCode::OutOfMemory, "Metal pass descriptor allocation failed");
                    unsigned index = 0;
                    for (const auto& a : op.attachments) {
                        if (a.allocation->textureDesc.depthStencil) {
                            descriptor.depthAttachment.texture = a.allocation->texture;
                            descriptor.depthAttachment.loadAction = a.load == LoadOp::Clear ? MTLLoadActionClear : MTLLoadActionLoad;
                            descriptor.depthAttachment.storeAction = MTLStoreActionStore;
                            if (a.load == LoadOp::Clear) descriptor.depthAttachment.clearDepth = a.clearDepth;
                        } else {
                            auto color = descriptor.colorAttachments[index++];
                            color.texture = a.allocation->texture;
                            color.loadAction = a.load == LoadOp::Clear ? MTLLoadActionClear : MTLLoadActionLoad;
                            color.storeAction = MTLStoreActionStore;
                            if (a.load == LoadOp::Clear) color.clearColor = MTLClearColorMake(a.clear.r,a.clear.g,a.clear.b,a.clear.a);
                        }
                    }
                    std::vector<NSString*> labels;
                    for (const auto& text : op.markers) {
                        NSString* label = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
                        if (!label) return fail(ErrorCode::OutOfMemory, "Metal pass label allocation failed");
                        labels.push_back(label);
                    }
                    id<MTLRenderCommandEncoder> encoder = [next renderCommandEncoderWithDescriptor:descriptor];
                    if (!encoder) return fail(ErrorCode::BackendFailure, "Metal render encoder creation failed");
                    const auto& extent = op.attachments.front().allocation->textureDesc;
                    [encoder setViewport:MTLViewport{0,0,double(extent.width),double(extent.height),0,1}];
                    [encoder setCullMode:MTLCullModeNone];
                    for (const auto& item : op.order) {
                        if (!item.first) { [encoder insertDebugSignpost:labels[item.second]]; continue; }
                        const auto& draw = op.draws[item.second]; const auto& p = *draw.pipeline;
                        [encoder setRenderPipelineState:p.pipeline]; [encoder setDepthStencilState:p.depth]; [encoder setScissorRect:draw.scissor];
                        for (const auto& slot : p.vertices.buffers) {
                            const auto& view = draw.vertices[slot.slot];
                            [encoder setVertexBuffer:view.allocation->buffer offset:view.offset atIndex:16+slot.slot];
                        }
                        std::size_t offset = 0;
                        for (const auto& entry : p.bindings.entries) for (unsigned element=0;element<entry.count;++element) {
                            const auto& view = (*draw.bindings)[offset++];
                            for (unsigned stage=0;stage<2;++stage) if (visible(entry.visibility,stage)) {
                                if (entry.type == BindingType::Texture2D) {
                                    if (stage==0) [encoder setVertexTexture:view.allocation->texture atIndex:entry.slot+element];
                                    else [encoder setFragmentTexture:view.allocation->texture atIndex:entry.slot+element];
                                } else {
                                    if (stage==0) [encoder setVertexBuffer:view.allocation->buffer offset:view.offset atIndex:entry.slot+element];
                                    else [encoder setFragmentBuffer:view.allocation->buffer offset:view.offset atIndex:entry.slot+element];
                                }
                            }
                        }
                        for (std::size_t i=0;i<p.bindings.samplers.size();++i) {
                            const auto& sampler = p.bindings.samplers[i];
                            if (visible(sampler.visibility,0)) [encoder setVertexSamplerState:p.samplers[i] atIndex:sampler.slot];
                            if (visible(sampler.visibility,1)) [encoder setFragmentSamplerState:p.samplers[i] atIndex:sampler.slot];
                        }
                        if (draw.indexed) [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:draw.count
                            indexType:draw.indices.stride==2?MTLIndexTypeUInt16:MTLIndexTypeUInt32 indexBuffer:draw.indices.allocation->buffer
                            indexBufferOffset:draw.indices.offset+std::uint64_t(draw.first)*draw.indices.stride instanceCount:draw.instances
                            baseVertex:draw.baseVertex baseInstance:draw.firstInstance];
                        else [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:draw.first vertexCount:draw.count instanceCount:draw.instances baseInstance:draw.firstInstance];
                    }
                    [encoder endEncoding];
                    continue;
                }
                if (op.kind == OperationKind::UploadTexture || op.kind == OperationKind::ReadTexture) {
                    const bool upload = op.kind == OperationKind::UploadTexture;
                    auto image = upload ? op.destination : op.source;
                    auto host = upload ? op.source : op.destination;
                    const auto f = image->footprint;
                    id<MTLBlitCommandEncoder> encoder = [next blitCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal texture blit encoder creation failed");
                    if (upload) {
                        [encoder copyFromBuffer:host->buffer sourceOffset:0 sourceBytesPerRow:f.rowPitch sourceBytesPerImage:f.size
                            sourceSize:MTLSizeMake(image->textureDesc.width,image->textureDesc.height,1) toTexture:image->texture
                            destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
                    } else {
                        [encoder copyFromTexture:image->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                            sourceSize:MTLSizeMake(image->textureDesc.width,image->textureDesc.height,1) toBuffer:host->buffer
                            destinationOffset:0 destinationBytesPerRow:f.rowPitch destinationBytesPerImage:f.size];
                        if (host->buffer.storageMode == MTLStorageModeManaged) [encoder synchronizeResource:host->buffer];
                    }
                    [encoder endEncoding];
                    continue;
                }
                if (!op.bytes) {
                    NSString* label = [[NSString alloc] initWithBytes:op.marker.data() length:op.marker.size() encoding:NSUTF8StringEncoding];
                    if (!label) return fail(ErrorCode::OutOfMemory, "Metal marker label allocation failed");
                    id<MTLBlitCommandEncoder> encoder = [next blitCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal marker encoder creation failed");
                    [encoder insertDebugSignpost:label]; [encoder endEncoding];
                    continue;
                }
                const auto aligned = op.bytes & ~std::uint64_t{3};
                if (aligned) {
                    id<MTLBlitCommandEncoder> encoder = [next blitCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal blit encoder creation failed");
                    [encoder copyFromBuffer:op.source->buffer sourceOffset:0 toBuffer:op.destination->buffer destinationOffset:0 size:aligned];
                    [encoder endEncoding];
                }
                if (op.bytes != aligned) {
                    id<MTLComputeCommandEncoder> encoder = [next computeCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal tail encoder creation failed");
                    [encoder setComputePipelineState:owner->tailPipeline];
                    [encoder setBuffer:op.source->buffer offset:0 atIndex:0];
                    [encoder setBuffer:op.destination->buffer offset:0 atIndex:1];
                    [encoder setBytes:&aligned length:sizeof(aligned) atIndex:2];
                    const auto count = static_cast<std::uint32_t>(op.bytes-aligned);
                    [encoder setBytes:&count length:sizeof(count) atIndex:3];
                    [encoder dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
                    [encoder endEncoding];
                }
                if (op.destination->buffer.storageMode == MTLStorageModeManaged) {
                    id<MTLBlitCommandEncoder> encoder = [next blitCommandEncoder];
                    if (!encoder) return fail(ErrorCode::OutOfMemory, "Metal synchronization encoder creation failed");
                    [encoder synchronizeResource:op.destination->buffer]; [encoder endEncoding];
                }
            }
            native->buffer = next;
        }
        open = false; ended = true;
        return {};
    }
    Result<void> debugMarker(std::string_view text) override {
        if (auto ok = owner->check(); !ok) return ok;
        if (!open) return fail(ErrorCode::InvalidState, "Metal marker requires recording");
        @autoreleasepool {
            NSString* label = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
            if (!label || text.empty() || text.find('\0') != std::string_view::npos)
                return fail(ErrorCode::InvalidArgument, "Metal marker requires nonempty UTF-8 without NUL");
            if (inPass) {
                auto& op = operations[passOperation]; op.markers.reserve(op.markers.size()+1);op.order.reserve(op.order.size()+1);
                op.markers.emplace_back(text);op.order.emplace_back(false,op.markers.size()-1);
            }
            else operations.push_back(Operation{{},{},0,std::string(text)});
        }
        return {};
    }
    Result<void> ready() const {
        if (auto ok = owner->check(); !ok) return ok;
        if (!open) return fail(ErrorCode::InvalidState, "Metal command list is not recording");
        return {};
    }
    Result<void> outsidePass() const {
        if (auto ok = ready(); !ok) return ok;
        if (inPass) return fail(ErrorCode::InvalidState, "Metal operation requires no active render pass");
        return {};
    }
    Result<std::shared_ptr<Allocation>> allocation(const BufferPtr& value) const {
        auto buffer = std::dynamic_pointer_cast<MetalBuffer>(value);
        if (!buffer) return fail(value ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected a Metal buffer");
        if (buffer->owner != owner) return fail(ErrorCode::WrongDevice, "Metal buffer belongs to another generation");
        return buffer->allocation;
    }
    Result<std::shared_ptr<Allocation>> allocation(const TexturePtr& value) const;
    Use* find(const std::shared_ptr<Allocation>& a) {
        auto it = std::find_if(uses.begin(), uses.end(), [&](auto& u) { return u.allocation == a; });
        return it == uses.end() ? nullptr : &*it;
    }
    ResourceState shadow(const std::shared_ptr<Allocation>& a) { auto u = find(a); return u ? u->shadow : a->state; }
    Use& retain(const std::shared_ptr<Allocation>& a) {
        if (auto u = find(a)) return *u;
        return uses.emplace_back(Use{a,a->version,a->state,a->initialized});
    }
    Result<void> transition(const BufferPtr& value, ResourceState target) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto a = allocation(value); if (!a) return std::unexpected(a.error());
        if (target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination &&
            !vertexState(target) && !indexState(target) && target != ResourceState::ConstantBuffer)
            return fail(ErrorCode::Unsupported, "Metal buffer state is outside the transfer slice");
        if (((*a)->desc.memory == Memory::Upload && target != ResourceState::CopySource && !vertexState(target) && !indexState(target) && target != ResourceState::ConstantBuffer) ||
            ((*a)->desc.memory == Memory::Readback && target != ResourceState::CopyDestination))
            return fail(ErrorCode::InvalidArgument, "Metal host buffer state is fixed");
        uses.reserve(uses.size()+1); retain(*a).shadow = target;
        // Explicitly tracked native resources synchronize accesses between encoders on this queue.
        return {};
    }
    Result<void> transition(const TexturePtr& value, ResourceState target) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto a = allocation(value); if (!a) return std::unexpected(a.error());
        if (target != ResourceState::Common && target != ResourceState::CopySource && target != ResourceState::CopyDestination &&
            target != ResourceState::RenderTarget && target != ResourceState::DepthWrite && target != ResourceState::DepthRead && target != ResourceState::ShaderRead)
            return fail(ErrorCode::Unsupported, "Metal texture state is outside the transfer/pass slice");
        if (target == ResourceState::RenderTarget && !(*a)->textureDesc.renderTarget)
            return fail(ErrorCode::InvalidArgument, "Metal color attachment usage required");
        if ((target == ResourceState::DepthWrite || target == ResourceState::DepthRead) && !(*a)->textureDesc.depthStencil)
            return fail(ErrorCode::InvalidArgument, "Metal depth attachment usage required");
        if (target == ResourceState::ShaderRead && (*a)->textureDesc.depthStencil) return fail(ErrorCode::Unsupported,"Metal depth sampling unsupported");
        uses.reserve(uses.size()+1); retain(*a).shadow = target;
        return {};
    }
    Result<void> copyBuffer(const BufferPtr& source, const BufferPtr& destination, std::uint64_t bytes) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = allocation(source); if (!src) return std::unexpected(src.error());
        auto dst = allocation(destination); if (!dst) return std::unexpected(dst.error());
        if (!bytes || bytes > (*src)->desc.size || bytes > (*dst)->desc.size || *src == *dst)
            return fail(ErrorCode::InvalidArgument, "Metal copy range or alias invalid");
        if (shadow(*src) != ResourceState::CopySource || shadow(*dst) != ResourceState::CopyDestination)
            return fail(ErrorCode::InvalidState, "Metal copy requires source and destination states");
        uses.reserve(uses.size()+2); operations.reserve(operations.size()+1);
        retain(*src); retain(*dst); operations.push_back(Operation{*src,*dst,bytes,{}});
        return {};
    }
    bool initialized(const std::shared_ptr<Allocation>& a) { auto u = find(a); return u ? u->initialized : a->initialized; }
    Result<void> textureCopy(const std::shared_ptr<Allocation>& image, const std::shared_ptr<Allocation>& host, bool upload) {
        if (host->desc.size < image->footprint.size || (upload ? host->desc.memory == Memory::Readback : host->desc.memory == Memory::Upload))
            return fail(ErrorCode::InvalidArgument, "Metal texture copy footprint/memory invalid");
        if (shadow(image) != (upload ? ResourceState::CopyDestination : ResourceState::CopySource) ||
            shadow(host) != (upload ? ResourceState::CopySource : ResourceState::CopyDestination))
            return fail(ErrorCode::InvalidState, "Metal texture copy requires explicit transfer states");
        if (!upload && !initialized(image)) return fail(ErrorCode::InvalidState, "Metal texture contents uninitialized");
        uses.reserve(uses.size()+2); operations.reserve(operations.size()+1);
        retain(image); retain(host);
        Operation op; op.source = upload ? host : image; op.destination = upload ? image : host;
        op.kind = upload ? OperationKind::UploadTexture : OperationKind::ReadTexture;
        operations.push_back(std::move(op));
        if (upload) find(image)->initialized = true;
        return {};
    }
    Result<void> copyBufferToTexture(const BufferPtr& source, const TexturePtr& destination) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = allocation(source); if (!src) return std::unexpected(src.error());
        auto dst = allocation(destination); if (!dst) return std::unexpected(dst.error());
        return textureCopy(*dst,*src,true);
    }
    Result<void> copyTextureToBuffer(const TexturePtr& source, const BufferPtr& destination) override {
        if (auto ok = outsidePass(); !ok) return ok;
        auto src = allocation(source); if (!src) return std::unexpected(src.error());
        auto dst = allocation(destination); if (!dst) return std::unexpected(dst.error());
        return textureCopy(*src,*dst,false);
    }
    Result<void> beginRenderPass(const TexturePtr& value, Color clear) override { return beginRenderPass(RenderPassDesc{{{value,LoadOp::Clear,clear}},{}}); }
    Result<void> beginRenderPass(const RenderPassDesc& desc) override {
        if (auto ok = outsidePass(); !ok) return ok;
        if (auto ok = validateRenderPass(desc); !ok) return ok;
        Operation op; op.kind = OperationKind::Pass;
        for (const auto& color : desc.colors) {
            auto a = allocation(color.texture); if (!a) return std::unexpected(a.error());
            if (shadow(*a) != ResourceState::RenderTarget || (color.load == LoadOp::Load && !initialized(*a)))
                return fail(ErrorCode::InvalidState, "Metal color pass requires state and initialized Load contents");
            op.attachments.push_back(Attachment{*a,color.load,color.clear,0});
        }
        if (desc.depth.texture) {
            auto a = allocation(desc.depth.texture); if (!a) return std::unexpected(a.error());
            if (shadow(*a) != (desc.depth.readOnly ? ResourceState::DepthRead : ResourceState::DepthWrite) ||
                (desc.depth.load == LoadOp::Load && !initialized(*a)))
                return fail(ErrorCode::InvalidState, "Metal depth pass requires state and initialized Load contents");
            op.attachments.push_back(Attachment{*a,desc.depth.load,{},desc.depth.clear,desc.depth.readOnly});
        }
        uses.reserve(uses.size()+op.attachments.size()); operations.reserve(operations.size()+1);
        for (const auto& a : op.attachments) retain(a.allocation).initialized = true;
        operations.push_back(std::move(op)); inPass = true; passOperation = operations.size()-1;
        selected.reset();bindings.reset();const auto& extent=operations.back().attachments.front().allocation->textureDesc;
        scissor={0,0,extent.width,extent.height};
        return {};
    }
    Result<void> endRenderPass() override {
        if (auto ok = ready(); !ok) return ok;
        if (!inPass) return fail(ErrorCode::InvalidState, "Metal render pass is not open");
        inPass = false; return {};
    }
    Result<void> setPipeline(const PipelinePtr& value) override {
        if(auto ok=ready();!ok)return ok;
        if(!inPass)return fail(ErrorCode::InvalidState,"Metal pipeline requires active pass");
        auto p=std::dynamic_pointer_cast<MetalPipeline>(value);if(!p)return fail(value?ErrorCode::WrongDevice:ErrorCode::InvalidArgument,"Expected Metal pipeline");
        if(p->owner!=owner)return fail(ErrorCode::WrongDevice,"Metal pipeline belongs to another generation");
        std::vector<Format> colors;std::optional<Format> depth;
        for(const auto& a:operations[passOperation].attachments){if(a.allocation->textureDesc.depthStencil)depth=a.allocation->textureDesc.format;else colors.push_back(a.allocation->textureDesc.format);}
        if(p->native->targets.colors!=colors||p->native->targets.depth!=depth)return fail(ErrorCode::InvalidArgument,"Metal pipeline/pass attachment formats differ");
        selected=p->native;bindings.reset();return {};
    }
    Result<void> bindTexture(const TexturePtr& value) override {
        if(auto ok=ready();!ok)return ok;
        if(!inPass||!selected||!selected->sampled)return fail(ErrorCode::InvalidState,"Metal bindTexture requires sampledTexture pipeline");
        ResourceBindings data;data.textures={{0,0,value}};return bindResources(data);
    }
    Result<void> bindResources(const ResourceBindings& data) override {
        if(auto ok=ready();!ok)return ok;
        if(!inPass||!selected)return fail(ErrorCode::InvalidState,"Metal bindings require pass/pipeline");
        std::size_t count=0;for(const auto& e:selected->bindings.entries)count+=e.count;
        auto next=std::make_shared<std::vector<ResourceView>>(count);
        auto slot=[&](unsigned index,unsigned element,BindingType type)->Result<std::size_t>{
            if(index>=selected->bindings.entries.size())return fail(ErrorCode::InvalidArgument,"Unknown Metal binding entry");
            const auto& e=selected->bindings.entries[index];if(e.type!=type||element>=e.count)return fail(ErrorCode::InvalidArgument,"Metal binding type/array mismatch");
            std::size_t at=element;for(unsigned i=0;i<index;++i)at+=selected->bindings.entries[i].count;
            if((*next)[at].allocation)return fail(ErrorCode::InvalidArgument,"Duplicate Metal binding element");return at;};
        for(const auto& view:data.textures){auto at=slot(view.binding,view.element,BindingType::Texture2D);if(!at)return std::unexpected(at.error());auto a=allocation(view.texture);if(!a)return std::unexpected(a.error());
            if((*a)->textureDesc.depthStencil)return fail(ErrorCode::Unsupported,"Metal depth sampling unsupported");
            if(shadow(*a)!=ResourceState::ShaderRead||!initialized(*a))return fail(ErrorCode::InvalidState,"Metal sampling requires initialized ShaderRead texture");(*next)[*at].allocation=*a;}
        for(const auto& view:data.constants){auto at=slot(view.binding,view.element,BindingType::ConstantBuffer);if(!at)return std::unexpected(at.error());auto a=allocation(view.buffer);if(!a)return std::unexpected(a.error());
            if((*a)->desc.memory==Memory::Readback||view.offset%256||!view.size||view.size%256||view.size>65536||view.offset>(*a)->desc.size||view.size>(*a)->desc.size-view.offset||view.size<selected->minimumBytes[view.binding])return fail(ErrorCode::InvalidArgument,"Metal constant view range/alignment/size invalid");
            if(shadow(*a)!=ResourceState::ConstantBuffer)return fail(ErrorCode::InvalidState,"Metal constants require ConstantBuffer state");(*next)[*at]={*a,view.offset,view.size};}
        for(const auto& view:*next)if(!view.allocation)return fail(ErrorCode::InvalidArgument,"Incomplete Metal resource snapshot");
        uses.reserve(uses.size()+next->size());for(const auto& view:*next)retain(view.allocation);bindings=std::move(next);return {};
    }
    Result<void> bindVertexBuffer(std::uint32_t slot,const BufferPtr& value,std::uint32_t stride,std::uint64_t offset,std::uint64_t size) override {
        if(auto ok=ready();!ok)return ok;auto a=allocation(value);if(!a)return std::unexpected(a.error());
        if(slot>=15)return fail(ErrorCode::Unsupported,"Metal vertex stream slots are 0..14");
        if(!stride||stride>2048||(*a)->desc.memory==Memory::Readback||offset>(*a)->desc.size||size>(*a)->desc.size-offset)return fail(ErrorCode::InvalidArgument,"Invalid Metal vertex view");
        const auto bytes=size?size:(*a)->desc.size-offset;if(!bytes||bytes>UINT32_MAX)return fail(ErrorCode::InvalidArgument,"Invalid Metal vertex view size");
        if(!vertexState(shadow(*a)))return fail(ErrorCode::InvalidState,"Metal vertex buffer state required");
        uses.reserve(uses.size()+1);retain(*a);vertices[slot]={*a,offset,bytes,stride};return {};
    }
    Result<void> bindIndexBuffer(const BufferPtr& value,IndexFormat format,std::uint64_t offset,std::uint64_t size) override {
        if(auto ok=ready();!ok)return ok;auto a=allocation(value);if(!a)return std::unexpected(a.error());
        if(format!=IndexFormat::UInt16&&format!=IndexFormat::UInt32)return fail(ErrorCode::InvalidArgument,"Invalid Metal index format");
        const auto element=format==IndexFormat::UInt16?2u:4u;
        if((*a)->desc.memory==Memory::Readback||offset%element||offset>(*a)->desc.size||size>(*a)->desc.size-offset)return fail(ErrorCode::InvalidArgument,"Invalid Metal index view");
        const auto bytes=size?size:(*a)->desc.size-offset;if(!bytes||bytes>UINT32_MAX||bytes%element)return fail(ErrorCode::InvalidArgument,"Invalid Metal index view size");
        if(!indexState(shadow(*a)))return fail(ErrorCode::InvalidState,"Metal index buffer state required");
        uses.reserve(uses.size()+1);retain(*a);indices={*a,offset,bytes,element};return {};
    }
    Result<void> setScissor(std::uint32_t x,std::uint32_t y,std::uint32_t width,std::uint32_t height) override {
        if(auto ok=ready();!ok)return ok;if(!inPass)return fail(ErrorCode::InvalidState,"Metal scissor requires pass");
        const auto d=operations[passOperation].attachments.front().allocation->textureDesc;
        if(!width||!height||x>d.width||width>d.width-x||y>d.height||height>d.height-y)return fail(ErrorCode::InvalidArgument,"Metal scissor outside attachment");
        scissor={x,y,width,height};return {};
    }
    Result<void> recordDraw(std::uint32_t count,std::uint32_t instances,std::uint32_t first,std::int32_t base,std::uint32_t firstInstance,bool indexed) {
        if(auto ok=ready();!ok)return ok;if(!inPass||!selected)return fail(ErrorCode::InvalidState,"Metal draw requires pass and pipeline");
        if(!count||!instances||instances>UINT32_MAX-firstInstance||(!indexed&&count>UINT32_MAX-first))return fail(ErrorCode::InvalidArgument,"Invalid Metal draw range");
        if(indexed&&(!indices.allocation||!indexState(shadow(indices.allocation))))return fail(ErrorCode::InvalidState,"Metal index binding missing/stale");
        if(indexed&&std::uint64_t(first)+count>indices.bytes/indices.stride)return fail(ErrorCode::InvalidArgument,"Metal index range exceeds view");
        for(const auto& input:selected->vertices.buffers){const auto& view=vertices[input.slot];unsigned extent=0,alignment=1;
            for(const auto& a:selected->vertices.attributes)if(a.slot==input.slot){extent=std::max(extent,a.offset+vertexBytes(a.format));alignment=std::max(alignment,a.format==VertexFormat::UNorm8x4?1u:4u);}
            if(!view.allocation||view.stride!=input.stride||!vertexState(shadow(view.allocation)))return fail(ErrorCode::InvalidState,"Metal vertex binding missing/mismatched");
            if(view.offset%alignment||view.bytes<extent)return fail(ErrorCode::InvalidArgument,"Metal vertex alignment/extent mismatch");
            auto last=(view.bytes-extent)/view.stride;
            if((input.rate==VertexInputRate::Instance&&std::uint64_t(firstInstance)+instances-1>last)||
                (input.rate==VertexInputRate::Vertex&&((!indexed&&std::uint64_t(first)+count-1>last)||(indexed&&base>0&&std::uint32_t(base)>last))))return fail(ErrorCode::InvalidArgument,"Metal input exceeds vertex/instance view");}
        if(!selected->bindings.entries.empty()&&!bindings)return fail(ErrorCode::InvalidState,"Metal draw requires complete bindings");
        if(bindings)for(const auto& view:*bindings)if(shadow(view.allocation)!=(view.allocation->texture?ResourceState::ShaderRead:ResourceState::ConstantBuffer))return fail(ErrorCode::InvalidState,"Metal binding read state changed");
        for(const auto& a:operations[passOperation].attachments)if(a.readOnly&&selected->depthState.writeEnabled)return fail(ErrorCode::InvalidState,"Read-only Metal depth rejects writing pipeline");
        auto& op=operations[passOperation];op.draws.reserve(op.draws.size()+1);op.order.reserve(op.order.size()+1);
        op.draws.push_back(Draw{selected,bindings,vertices,indices,count,instances,first,firstInstance,base,indexed,scissor});op.order.emplace_back(true,op.draws.size()-1);return {};
    }
    Result<void> draw(std::uint32_t n,std::uint32_t instances,std::uint32_t first,std::uint32_t firstInstance) override { return recordDraw(n,instances,first,0,firstInstance,false); }
    Result<void> drawIndexed(std::uint32_t n,std::uint32_t instances,std::uint32_t first,std::int32_t base,std::uint32_t firstInstance) override { return recordDraw(n,instances,first,base,firstInstance,true); }
    Result<void> writeTimestamp(std::uint32_t) override { return owner->unsupported(); }
    Result<void> resolveTimestamps() override { return owner->unsupported(); }
    Result<double> elapsedGpuMilliseconds() override { return owner->unsupported(); }
};
Result<std::shared_ptr<Allocation>> MetalCommands::allocation(const TexturePtr& value) const {
    auto texture = std::dynamic_pointer_cast<MetalTexture>(value);
    if (!texture) return fail(value ? ErrorCode::WrongDevice : ErrorCode::InvalidArgument, "Expected a Metal texture");
    if (texture->owner != owner) return fail(ErrorCode::WrongDevice, "Metal texture belongs to another generation");
    return texture->allocation;
}
struct MetalQueue final : CommandQueue {
    std::shared_ptr<State> owner;
    explicit MetalQueue(std::shared_ptr<State> state) : owner(std::move(state)) {}
    Result<Fence> submit(CommandList& value) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        auto* commands = dynamic_cast<MetalCommands*>(&value);
        if (!commands || commands->owner != owner) return fail(ErrorCode::WrongDevice, "Metal command list belongs to another generation");
        if (!commands->ended || commands->open || commands->submitted) return fail(ErrorCode::InvalidState, "Metal submission requires an ended, unsubmitted command list");
        if (owner->last == UINT64_MAX) return fail(ErrorCode::InvalidState, "Metal fence sequence exhausted");
        for (const auto& use : commands->uses)
            if (use.version != use.allocation->version || use.allocation->version == UINT64_MAX)
                return fail(ErrorCode::InvalidState, "Stale Metal buffer observation");
        // Allocate the retention node before commit; retain even if the caller destroys its list.
        std::vector<std::shared_ptr<Allocation>> allocations;
        allocations.reserve(commands->uses.size());
        for (auto& use : commands->uses) allocations.push_back(use.allocation);
        std::vector<std::shared_ptr<NativePipeline>> pipelines;
        for(const auto& op:commands->operations)for(const auto& draw:op.draws)
            if(std::find(pipelines.begin(),pipelines.end(),draw.pipeline)==pipelines.end())pipelines.push_back(draw.pipeline);
        owner->pending.push_back({owner->last + 1, commands->native->buffer, std::move(allocations),std::move(pipelines)});
        @autoreleasepool { [commands->native->buffer commit]; }
        commands->submitted = ++owner->last;
        for (auto& use : commands->uses) {
            use.allocation->state = use.shadow; use.allocation->initialized = use.initialized;
            use.allocation->lastUse = owner->last; ++use.allocation->version;
        }
        return Fence{owner->last, owner->generation};
    }
    Result<void> wait(Fence fence, std::chrono::milliseconds timeout) override {
        if (auto ok = owner->check(); !ok) return ok;
        if (timeout.count() < 0 || timeout.count() > 4294967294ll) return fail(ErrorCode::InvalidArgument, "Metal fence timeout outside finite range");
        if (fence.device != owner->generation) return fail(ErrorCode::WrongDevice, "Metal fence belongs to another generation");
        if (!fence.value || fence.value > owner->last) return fail(ErrorCode::InvalidArgument, "Metal fence value was not submitted");
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            if (auto ok = owner->poll(); !ok) return ok;
            if (owner->completed >= fence.value) return {};
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return fail(ErrorCode::Timeout, "Metal fence wait timed out");
            std::this_thread::sleep_for(std::min(deadline - now, std::chrono::steady_clock::duration(std::chrono::milliseconds(1))));
        }
    }
    Result<void> waitIdle() override {
        if (auto ok = owner->check(); !ok) return ok;
        @autoreleasepool {
            if (!owner->pending.empty()) [owner->pending.back().buffer waitUntilCompleted];
        }
        return owner->poll();
    }
    Result<std::uint64_t> completedValue() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        return owner->completed;
    }
};
struct MetalDevice final : Device {
    std::shared_ptr<State> owner;
    DeviceCaps capabilities;
    MetalQueue commands;
    MetalDevice(std::shared_ptr<State> state, DeviceCaps caps) : owner(std::move(state)), capabilities(std::move(caps)), commands(owner) {}
    ~MetalDevice() override { owner->retire(); }
    const DeviceCaps& caps() const override { return capabilities; }
    Result<void> checkStatus() override { return owner->poll(); }
    Result<ResourceStatistics> resourceStatistics() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        ResourceStatistics result;
        result.pendingSubmissions = owner->pending.size();
        std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
        std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
        result.trackedObjects = owner->objects.size();
        for (const auto& weak : owner->allocations) if (auto a = weak.lock()) { ++result.uniqueResources; result.resourceAllocationBytes += a->bytes; }
        for (const auto& p : owner->pending) result.pendingObjectReferences += p.allocations.size();
        return result;
    }
    Result<MemoryBudget> queryMemoryBudget(MemorySegment) override { return owner->unsupported(); }
    Result<std::unique_ptr<Device>> recover() override { return owner->unsupported(); }
    Result<void> removeForTesting() override { return owner->unsupported(); }
    Result<BufferPtr> createBuffer(BufferDesc desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (!desc.size || desc.size > NSUIntegerMax || (desc.memory != Memory::Upload && desc.memory != Memory::Readback && desc.memory != Memory::Device))
            return fail(ErrorCode::InvalidArgument, "Invalid Metal buffer description");
        if (desc.size > owner->device.maxBufferLength) return fail(ErrorCode::Unsupported, "Metal buffer exceeds device limit");
        @autoreleasepool {
            auto a = std::make_shared<Allocation>(); a->desc = desc;
            MTLResourceOptions options = MTLResourceHazardTrackingModeTracked;
            if (desc.memory == Memory::Device) options |= MTLResourceStorageModePrivate;
            else options |= owner->device.hasUnifiedMemory ? MTLResourceStorageModeShared : MTLResourceStorageModeManaged;
            if (desc.memory == Memory::Upload) options |= MTLResourceCPUCacheModeWriteCombined;
            a->buffer = [owner->device newBufferWithLength:desc.size options:options];
            if (!a->buffer) return fail(ErrorCode::OutOfMemory, "Metal buffer allocation failed");
            const auto mode = desc.memory == Memory::Device ? MTLStorageModePrivate : owner->device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
            a->bytes = a->buffer.allocatedSize;
            if (a->bytes < desc.size || a->buffer.storageMode != mode || a->buffer.hazardTrackingMode != MTLHazardTrackingModeTracked ||
                (desc.memory != Memory::Device && !a->buffer.contents))
                return fail(ErrorCode::BackendFailure, "Metal buffer allocation lacks required size or mapping");
            a->state = desc.memory == Memory::Upload ? ResourceState::CopySource : desc.memory == Memory::Readback ? ResourceState::CopyDestination : ResourceState::Common;
            auto value = std::make_shared<MetalBuffer>(); value->owner = owner; value->allocation = a; value->token = std::make_shared<int>();
            std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
            std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
            owner->allocations.push_back(a); owner->objects.push_back(value->token);
            return BufferPtr(value);
        }
    }
    Result<TexturePtr> createTexture(TextureDesc desc) override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        if (auto ok = validateTextureDescription(desc); !ok) return std::unexpected(ok.error());
        @autoreleasepool {
            const auto format = desc.format == Format::RGBA8UNorm ? MTLPixelFormatRGBA8Unorm :
                desc.format == Format::BGRA8UNorm ? MTLPixelFormatBGRA8Unorm :
                desc.format == Format::RGBA16Float ? MTLPixelFormatRGBA16Float : MTLPixelFormatDepth32Float;
            MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:desc.width height:desc.height mipmapped:NO];
            if (!descriptor) return fail(ErrorCode::OutOfMemory, "Metal texture descriptor creation failed");
            descriptor.resourceOptions = MTLResourceStorageModePrivate | MTLResourceHazardTrackingModeTracked;
            descriptor.usage = MTLTextureUsageShaderRead;
            if (desc.renderTarget || desc.depthStencil) descriptor.usage |= MTLTextureUsageRenderTarget;
            auto a = std::make_shared<Allocation>(); a->textureDesc = desc;
            a->texture = [owner->device newTextureWithDescriptor:descriptor];
            if (!a->texture) return fail(ErrorCode::OutOfMemory, "Metal texture allocation failed");
            a->bytes = a->texture.allocatedSize;
            if (!a->bytes || a->texture.storageMode != MTLStorageModePrivate || a->texture.hazardTrackingMode != MTLHazardTrackingModeTracked ||
                a->texture.pixelFormat != format || a->texture.width != desc.width || a->texture.height != desc.height)
                return fail(ErrorCode::BackendFailure, "Metal texture descriptor/storage mismatch");
            const auto rowBytes = desc.width * (desc.format == Format::RGBA16Float ? 8u : 4u);
            const auto rowPitch = (rowBytes+255u)&~255u;
            a->footprint = {std::uint64_t(rowPitch)*desc.height,rowPitch,rowBytes,desc.height};
            auto value = std::make_shared<MetalTexture>(); value->owner = owner; value->allocation = a; value->token = std::make_shared<int>();
            std::erase_if(owner->allocations, [](auto& w) { return w.expired(); });
            std::erase_if(owner->objects, [](auto& w) { return w.expired(); });
            owner->allocations.push_back(a); owner->objects.push_back(value->token);
            return TexturePtr(value);
        }
    }
    Result<PipelinePtr> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override {
        auto p=graphics(*owner,desc);if(!p)return std::unexpected(p.error());
        auto wrapper=std::make_shared<MetalPipeline>();wrapper->owner=owner;wrapper->native=*p;wrapper->token=std::make_shared<int>();
        std::erase_if(owner->pipelines,[](auto& w){return w.expired();});std::erase_if(owner->objects,[](auto& w){return w.expired();});
        owner->pipelines.push_back(*p);owner->objects.push_back(wrapper->token);return PipelinePtr(wrapper);
    }
    Result<std::unique_ptr<CommandList>> createCommandList() override {
        if (auto ok = owner->poll(); !ok) return std::unexpected(ok.error());
        return std::make_unique<MetalCommands>(owner);
    }
    Result<void> checkSurfaceSupport(Surface) override { return owner->unsupported(); }
    Result<std::unique_ptr<SwapChain>> createSwapChain(Surface, SwapChainDesc) override { return owner->unsupported(); }
    CommandQueue& queue() override { return commands; }
    Result<std::uint64_t> validationErrorCount() override { return owner->unsupported(); }
};
}
Result<std::vector<AdapterInfo>> enumerateMetalAdapters() {
    @autoreleasepool {
        std::vector<AdapterInfo> result;
        for (id<MTLDevice> device in MTLCopyAllDevices()) {
            const char* name = device.name.UTF8String;
            if (!device.registryID || !name || !*name) return fail(ErrorCode::BackendFailure, "Metal adapter has no registry identity or name");
            AdapterInfo info;
            info.id = {Backend::Metal, device.registryID}; info.name = name;
            // Metal exposes neither a portable software/virtual classification nor PCI memory totals.
            // false is the only supported selection policy; it does not establish physical hardware.
            result.push_back(std::move(info));
        }
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id.value < b.id.value; });
        if (std::adjacent_find(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id == b.id; }) != result.end())
            return fail(ErrorCode::BackendFailure, "Metal registry identities are ambiguous");
        return result;
    }
}
Result<std::unique_ptr<Device>> createMetalDevice(DeviceDesc desc) {
    if (desc.validation || desc.softwareAdapter || desc.enableDeviceLossTesting)
        return fail(ErrorCode::Unsupported, "Metal submission backend does not support requested validation, software or device-loss policy");
    @autoreleasepool {
        id<MTLDevice> selected = nil;
        if (desc.adapter) {
            for (id<MTLDevice> candidate in MTLCopyAllDevices()) {
                if (candidate.registryID != desc.adapter->value) continue;
                if (selected) return fail(ErrorCode::BackendFailure, "Metal registry identity is ambiguous");
                selected = candidate;
            }
        } else selected = MTLCreateSystemDefaultDevice();
        if (!selected) return fail(ErrorCode::Unsupported, "Requested Metal device is unavailable; no fallback");
        const char* name = selected.name.UTF8String;
        if (!selected.registryID || !name || !*name) return fail(ErrorCode::BackendFailure, "Selected Metal device has no identity or name");
        id<MTLCommandQueue> queue = [selected newCommandQueue];
        if (!queue) return fail(ErrorCode::OutOfMemory, "Metal command queue creation failed");
        auto state = std::make_shared<State>();
        const auto generation = detail::allocateDeviceGeneration();
        if (!generation) return fail(ErrorCode::InvalidState, "Device generation sequence exhausted");
        state->device = selected; state->queue = queue; state->generation = generation;
        DeviceCaps caps;
        caps.backend = Backend::Metal; caps.adapterName = name; caps.adapterId = {Backend::Metal, selected.registryID};
        caps.depthAttachment = true; caps.multipleRenderTargets = true;
        caps.vertexInput = true; caps.indexedDrawing = true; caps.resourceBinding = true; caps.bindingLayouts = true;
        return std::make_unique<MetalDevice>(std::move(state), std::move(caps));
    }
}
}
