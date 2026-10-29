#pragma once
#include "../llgl_vulkan_pso/Support.hpp"
#include "../llgl_vulkan_upload/Payload.hpp"
namespace llglupload {
struct Upload {
    Context& c;LLGL::Buffer *staging{},*readback{};LLGL::Texture* texture{};LLGL::CommandBuffer* command{};LLGL::Fence* fence{};LLGL::TextureRegion region{};
    Upload(Context& context,Shape s):c(context){
        LLGL::TextureDescriptor desc{};desc.type=LLGL::TextureType::Texture2D;desc.format=s.bpp==8?LLGL::Format::RGBA16Float:LLGL::Format::RGBA8UNorm;desc.extent={s.w,s.h,1};desc.mipLevels=1;desc.arrayLayers=1;desc.samples=1;desc.bindFlags=LLGL::BindFlags::Sampled|LLGL::BindFlags::CopySrc|LLGL::BindFlags::CopyDst;
        texture=c.renderer->CreateTexture(desc);LLGL::BufferDescriptor buffer{};buffer.size=std::uint64_t(s.w)*s.h*s.bpp;buffer.bindFlags=LLGL::BindFlags::CopySrc;buffer.cpuAccessFlags=LLGL::CPUAccessFlags::Write;staging=c.renderer->CreateBuffer(buffer);
        buffer.bindFlags=LLGL::BindFlags::CopyDst;buffer.cpuAccessFlags=LLGL::CPUAccessFlags::Read;readback=c.renderer->CreateBuffer(buffer);command=c.renderer->CreateCommandBuffer({});fence=c.renderer->CreateFence();region.extent={s.w,s.h,1};
        require(texture&&staging&&readback&&command&&fence,"Upload resources unavailable");
    }
    void complete(){auto& q=*c.renderer->GetCommandQueue();q.Submit(*command);q.Submit(*fence);require(q.WaitFence(*fence,5'000'000'000ull),"Completed upload/readback fence timeout");}
    void write(std::span<const std::byte> pixels){
        auto mapped=c.renderer->MapBuffer(*staging,LLGL::CPUAccess::WriteOnly);require(mapped!=nullptr,"Upload map failed");std::memcpy(mapped,pixels.data(),pixels.size());c.renderer->UnmapBuffer(*staging);
        command->Begin();command->CopyTextureFromBuffer(*texture,region,*staging,0,0,0);command->End();complete();
    }
    std::vector<std::byte> read(std::size_t size){
        command->Begin();command->CopyBufferFromTexture(*readback,0,*texture,region,0,0);command->End();complete();std::vector<std::byte> result(size);c.renderer->ReadBuffer(*readback,0,result.data(),result.size());return result;
    }
    void close(){if(!texture)return;c.renderer->GetCommandQueue()->WaitIdle();c.renderer->Release(*command);c.renderer->Release(*fence);c.renderer->Release(*readback);c.renderer->Release(*staging);c.renderer->Release(*texture);texture=nullptr;}
    ~Upload(){close();}
};
}
