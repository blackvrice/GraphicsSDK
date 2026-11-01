#pragma once
#include "Platform.hpp"
#include "Observer.hpp"
#include <openssl/sha.h>
#include <span>
namespace llglpso {
using namespace llglupload;
std::string sha256(std::span<const std::byte> data){
    std::array<unsigned char,32> digest{};
    require(SHA256(reinterpret_cast<const unsigned char*>(data.data()),data.size(),digest.data())!=nullptr,"SHA256 failed");
    return hex(digest);
}
std::string identity(Context& c){return c.identity();}
struct Observer {
    void* library{};drawobserver::Read read{};drawobserver::Reset reset{};std::string path;
    Observer(){path=module("libVkLayer_gfx_llgl_draw.so");library=dlopen(path.c_str(),RTLD_NOW|RTLD_NOLOAD);
        require(library!=nullptr,"Native draw observer not active");
        read=reinterpret_cast<drawobserver::Read>(dlsym(library,"gfxDrawSnapshot"));reset=reinterpret_cast<drawobserver::Reset>(dlsym(library,"gfxDrawReset"));
        require(read&&reset,"Observer snapshot entry missing");auto s=snapshot();
        require(s.createdInstances==2&&s.createdDevices==1&&s.liveInstances==2&&s.liveDevices==1&&s.instanceApi!=0,"Native draw observer not active");}
    drawobserver::Snapshot snapshot(){drawobserver::Snapshot s{};require(read(&s,sizeof(s))!=0,"Native snapshot failed");return s;}
    ~Observer(){if(library)dlclose(library);}
};
std::string validationControl(Context& c,const std::string& name){
    require(!c.loadedLayer.empty(),"Controls require validation");
    if(name=="core-control"){VkPhysicalDeviceProperties2 invalid{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};c.getProperties(c.native.physicalDevice,&invalid);}
    else{
        LLGL::BufferDescriptor a{};a.size=4096;a.bindFlags=LLGL::BindFlags::CopySrc;auto b=a;b.bindFlags=LLGL::BindFlags::CopyDst;auto src=c.renderer->CreateBuffer(a);auto dst=c.renderer->CreateBuffer(b);auto command=c.renderer->CreateCommandBuffer({});require(src&&dst&&command,"Control resources unavailable");
        LLGL::Vulkan::ResourceNativeHandle sn{},dn{};require(src->GetNativeHandle(&sn,sizeof(sn))&&dst->GetNativeHandle(&dn,sizeof(dn)),"Native control buffers unavailable");command->Begin();LLGL::Vulkan::CommandBufferNativeHandle cn{};require(command->GetNativeHandle(&cn,sizeof(cn)),"Native control command unavailable");
        // Validation-only negative control, never submitted to the GPU.
        VkBufferCopy copy{0,0,4096};vkCmdCopyBuffer(cn.commandBuffer,sn.buffer.buffer,dn.buffer.buffer,1,&copy);vkCmdCopyBuffer(cn.commandBuffer,sn.buffer.buffer,dn.buffer.buffer,1,&copy);command->End();c.renderer->Release(*command);c.renderer->Release(*dst);c.renderer->Release(*src);
    }
    std::string token=name=="core-control"?"VUID-VkPhysicalDeviceProperties2-sType-sType":"SYNC-HAZARD-WRITE-AFTER-WRITE";
    require(std::any_of(errorIds.begin(),errorIds.end(),[&](auto& s){return s.find(token)!=std::string::npos;}),"Expected validation control diagnostic absent");return token;
}
}
