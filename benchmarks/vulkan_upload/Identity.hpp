#pragma once
#define VK_NO_PROTOTYPES
#include <Windows.h>
#include <vulkan/vulkan_core.h>
#include <array>
#include <string>
#include <vector>
#include <stdexcept>

namespace upload {
inline void vkCheck(VkResult r) { if(r!=VK_SUCCESS) throw std::runtime_error("Vulkan identity query failed: "+std::to_string(r)); }
template<class T> std::string hex(const T& a) { std::string s; for(auto c:a) { s+="0123456789abcdef"[(c>>4)&15];s+="0123456789abcdef"[c&15]; } return s; }
struct Identity {
    std::string uuid,driverUuid,luid;
    std::uint32_t vendor{},device{},driver{},api{};
};
inline Identity identify(const std::string& name) {
    struct Loader { HMODULE h{LoadLibraryExW(L"vulkan-1.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32)}; ~Loader(){if(h)FreeLibrary(h);} } lib;
    if(!lib.h) throw std::runtime_error("System Vulkan loader unavailable");
    auto get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(lib.h,"vkGetInstanceProcAddr"));
    if(!get) throw std::runtime_error("Vulkan identity entry unavailable");
    auto create=reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"));
    if(!create) throw std::runtime_error("Vulkan identity create unavailable");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo=&app;
    VkInstance instance{};vkCheck(create(&info,nullptr,&instance));
    auto destroy=reinterpret_cast<PFN_vkDestroyInstance>(get(instance,"vkDestroyInstance"));
    struct Scope { VkInstance h; PFN_vkDestroyInstance d; ~Scope(){if(d)d(h,nullptr);} } scope{instance,destroy};
    auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get(instance,"vkEnumeratePhysicalDevices"));
    auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get(instance,"vkGetPhysicalDeviceProperties2"));
    if(!destroy||!enumerate||!properties) throw std::runtime_error("Vulkan 1.1 identity functions unavailable");
    unsigned count{};vkCheck(enumerate(instance,&count,nullptr));std::vector<VkPhysicalDevice> devices(count);vkCheck(enumerate(instance,&count,devices.data()));devices.resize(count);
    Identity found;unsigned matches{};
    for(auto d:devices) {
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 p{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p.pNext=&id;properties(d,&p);
        if(name!=p.properties.deviceName)continue;
        ++matches;
        if(!id.deviceLUIDValid || p.properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU)throw std::runtime_error("Hardware Vulkan identity/LUID required");
        found={hex(id.deviceUUID),hex(id.driverUUID),hex(id.deviceLUID),p.properties.vendorID,p.properties.deviceID,p.properties.driverVersion,p.properties.apiVersion};
    }
    if(matches!=1)throw std::runtime_error("GPU name must match exactly one native Vulkan device; ambiguous identity rejected");
    return found;
}
}
