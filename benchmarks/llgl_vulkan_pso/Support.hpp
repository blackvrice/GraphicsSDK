#pragma once
#include "../llgl_vulkan_upload/Context.hpp"
#include "../llgl_vulkan_draw/Observer.hpp"
#include <bcrypt.h>
#include <span>
namespace llglpso {
using namespace llglupload;
std::string sha256(std::span<const std::byte> data){BCRYPT_ALG_HANDLE algorithm{};require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider failed");std::array<unsigned char,32> out{};auto r=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data())),ULONG(data.size()),out.data(),ULONG(out.size()));BCryptCloseAlgorithmProvider(algorithm,0);require(r>=0,"SHA256 failed");return hex(out);}
std::string identity(Context& c){auto& p=c.properties.properties;std::ostringstream o;
    o<<"\"device_name\":"<<quote(p.deviceName)<<",\"device_uuid\":"<<quote(hex(c.id.deviceUUID))<<",\"driver_uuid\":"<<quote(hex(c.id.driverUUID))<<",\"device_luid\":"<<quote(hex(c.id.deviceLUID))
     <<",\"native_vendor_id\":"<<p.vendorID<<",\"native_device_id\":"<<p.deviceID<<",\"native_driver_version\":"<<p.driverVersion<<",\"native_api_version\":"<<p.apiVersion<<",\"native_handle_verified\":true"
     <<",\"queue_family\":"<<c.native.queueFamily<<",\"queue_index\":0,\"queue_flags\":"<<c.flags<<",\"loader_api_version\":"<<c.loaderVersion<<",\"instance_API_request_observed\":true"
     <<",\"loader_path\":"<<quote(c.loader)<<",\"LLGL_path\":"<<quote(c.core)<<",\"backend_path\":"<<quote(c.backend)<<",\"loaded_validation_layer\":"<<quote(c.loadedLayer);return o.str();}
struct Observer {
    HMODULE library{};drawobserver::Read read{};drawobserver::Reset reset{};std::string path;
    Observer(){auto loaded=GetModuleHandleW(L"VkLayer_gfx_llgl_draw.dll");require(loaded!=nullptr,"Native draw observer not active");path=module(loaded);library=LoadLibraryW(std::filesystem::path(path).c_str());require(library!=nullptr,"Observer retain failed");read=reinterpret_cast<drawobserver::Read>(GetProcAddress(library,"gfxDrawSnapshot"));reset=reinterpret_cast<drawobserver::Reset>(GetProcAddress(library,"gfxDrawReset"));require(read&&reset,"Observer snapshot entry missing");auto s=snapshot();require(s.createdInstances==1&&s.createdDevices==1&&s.liveInstances==1&&s.liveDevices==1&&s.instanceApi!=0,"Native draw observer not active");}
    drawobserver::Snapshot snapshot(){drawobserver::Snapshot s{};require(read(&s,sizeof(s))!=0,"Native snapshot failed");return s;}
    ~Observer(){if(library)FreeLibrary(library);}
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
