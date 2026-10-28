#pragma once
#include "../common/LinuxEnvironment.hpp"
#include <graphics/Graphics.hpp>
#include <cstdlib>

namespace linuxwork {
inline linuxbench::Identity selected(std::string_view expected={}){
    const auto* uuid=std::getenv("GFX_COMPARISON_UUID");const auto* mode=std::getenv("GFX_COMPARISON_MODE");
    linuxbench::require(uuid&&mode,"Explicit Linux comparison UUID/mode required");
    linuxbench::NativeQuery query;auto rows=query.enumerate(std::getenv("GFX_EXPECT_VULKAN_LAYER")!=nullptr);
    auto found=linuxbench::select(rows,uuid,mode);linuxbench::require(expected.empty()||expected==found.name,"Selected public name differs from exact UUID");return found;
}
inline void guard(){
    auto native=selected();auto result=gfx::enumerateAdapters(gfx::Backend::Vulkan);if(!result)throw std::runtime_error(result.error().message);
    const auto& adapters=*result;linuxbench::require(std::count_if(adapters.begin(),adapters.end(),[&](auto& a){return a.id.value==native.id;})==1,"Exact SDK UUID-derived adapter missing or ambiguous");
    auto a=std::find_if(adapters.begin(),adapters.end(),[&](auto& item){return item.id.value==native.id;});
    linuxbench::require(a->name==native.name&&a->vendorId==native.vendor&&a->deviceId==native.device&&a->softwareAdapter==(native.type==4),"SDK/native target mismatch");
}
inline std::string layer(bool validation){
    std::string found;for(auto& path:linuxbench::modules())if(std::filesystem::path(path).filename()=="libVkLayer_khronos_validation.so"){
        linuxbench::require(found.empty(),"Ambiguous validation library");found=path;
    }
    if(validation){const auto* expected=std::getenv("GFX_EXPECT_VULKAN_LAYER");linuxbench::require(expected&&found==std::filesystem::canonical(expected).string(),"Pinned validation layer not loaded");}
    else linuxbench::require(found.empty(),"Validation active in plain probe");return found;
}
}
namespace upload {
struct Identity {std::string uuid,driverUuid,luid;std::uint32_t vendor{},device{},driver{},api{};};
inline Identity identify(const std::string& name){auto d=linuxwork::selected(name);return{d.uuid,d.driverUuid,{},d.vendor,d.device,d.driver,d.api};}
}
