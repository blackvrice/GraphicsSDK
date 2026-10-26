// Native scope observation only: no SDK Linux presentation implementation.
#define VK_USE_PLATFORM_XCB_KHR
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <graphics/Graphics.hpp>
#include <dlfcn.h>
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void vk(VkResult r){require(r==VK_SUCCESS,"Native Vulkan query failed");}
template<class T> T load(PFN_vkGetInstanceProcAddr get,VkInstance instance,const char* name){
    auto result=reinterpret_cast<T>(get(instance,name));require(result!=nullptr,name);return result;
}
struct Session {
    void* library{};xcb_connection_t* connection{};xcb_window_t window{};
    VkInstance instance{};VkSurfaceKHR surface{};PFN_vkGetInstanceProcAddr get{};
    ~Session(){
        if(surface)load<PFN_vkDestroySurfaceKHR>(get,instance,"vkDestroySurfaceKHR")(instance,surface,nullptr);
        if(instance)load<PFN_vkDestroyInstance>(get,instance,"vkDestroyInstance")(instance,nullptr);
        if(window)xcb_destroy_window(connection,window);
        if(connection)xcb_disconnect(connection);
        if(library)dlclose(library);
    }
};
}
int main()try{
    Session s;int screenIndex{};s.connection=xcb_connect(nullptr,&screenIndex);
    if(!s.connection||xcb_connection_has_error(s.connection)){
        std::cout<<"{\"event\":\"display_unavailable\",\"xcb_error\":"<<(s.connection?xcb_connection_has_error(s.connection):-1)<<"}\n";return 2;
    }
    auto screens=xcb_setup_roots_iterator(xcb_get_setup(s.connection));for(int i=0;i<screenIndex&&screens.rem;++i)xcb_screen_next(&screens);
    require(screens.rem&&screens.data,"XCB screen missing");auto screen=screens.data;s.window=xcb_generate_id(s.connection);
    auto cookie=xcb_create_window_checked(s.connection,XCB_COPY_FROM_PARENT,s.window,screen->root,0,0,64,48,0,XCB_WINDOW_CLASS_INPUT_OUTPUT,screen->root_visual,0,nullptr);
    auto error=xcb_request_check(s.connection,cookie);bool windowOk=error==nullptr;std::free(error);require(windowOk,"XCB window creation failed");
    error=xcb_request_check(s.connection,xcb_map_window_checked(s.connection,s.window));bool mapped=error==nullptr;std::free(error);require(mapped,"XCB map request failed");xcb_flush(s.connection);
    auto geometry=xcb_get_geometry_reply(s.connection,xcb_get_geometry(s.connection,s.window),nullptr);require(geometry&&geometry->width==64&&geometry->height==48,"XCB geometry mismatch");std::free(geometry);
    s.library=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);require(s.library,"Vulkan loader unavailable");s.get=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(s.library,"vkGetInstanceProcAddr"));require(s.get,"Vulkan entry missing");
    auto enumerate=load<PFN_vkEnumerateInstanceExtensionProperties>(s.get,{},"vkEnumerateInstanceExtensionProperties");uint32_t count{};vk(enumerate(nullptr,&count,nullptr));std::vector<VkExtensionProperties> extensions(count);vk(enumerate(nullptr,&count,extensions.data()));extensions.resize(count);
    auto has=[&](const char* name){return std::any_of(extensions.begin(),extensions.end(),[&](auto& e){return std::string(e.extensionName)==name;});};
    require(has(VK_KHR_SURFACE_EXTENSION_NAME)&&has(VK_KHR_XCB_SURFACE_EXTENSION_NAME),"XCB Vulkan extensions unavailable");
    std::vector<const char*> enabled{VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_XCB_SURFACE_EXTENSION_NAME};
    if(has(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME))enabled.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    if(has(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME))enabled.push_back(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="GraphicsSDK WSI scope";app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;ci.enabledExtensionCount=enabled.size();ci.ppEnabledExtensionNames=enabled.data();vk(load<PFN_vkCreateInstance>(s.get,{},"vkCreateInstance")(&ci,nullptr,&s.instance));
    VkXcbSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};sci.connection=s.connection;sci.window=s.window;vk(load<PFN_vkCreateXcbSurfaceKHR>(s.get,s.instance,"vkCreateXcbSurfaceKHR")(s.instance,&sci,nullptr,&s.surface));
    std::cout<<"{\"event\":\"session\",\"system\":\"xcb\",\"width\":64,\"height\":48,\"native_surface\":true,\"wayland_extension\":"<<std::boolalpha<<has("VK_KHR_wayland_surface")<<",\"wayland_session_tested\":false,\"surface_maintenance1\":"<<has(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME)<<",\"surface_capabilities2\":"<<has(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)<<"}\n";
    auto physicals=load<PFN_vkEnumeratePhysicalDevices>(s.get,s.instance,"vkEnumeratePhysicalDevices");vk(physicals(s.instance,&count,nullptr));std::vector<VkPhysicalDevice> devices(count);vk(physicals(s.instance,&count,devices.data()));devices.resize(count);require(!devices.empty(),"No native Vulkan candidates");
    unsigned ready{},software{};for(auto physical:devices){
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&id;load<PFN_vkGetPhysicalDeviceProperties2>(s.get,s.instance,"vkGetPhysicalDeviceProperties2")(physical,&props);
        std::string uuid;const char* hex="0123456789abcdef";for(auto b:id.deviceUUID){uuid+=hex[b>>4];uuid+=hex[b&15];}require(uuid!=std::string(32,'0'),"Zero native UUID");
        auto deviceExtensions=load<PFN_vkEnumerateDeviceExtensionProperties>(s.get,s.instance,"vkEnumerateDeviceExtensionProperties");vk(deviceExtensions(physical,nullptr,&count,nullptr));std::vector<VkExtensionProperties> ext(count);vk(deviceExtensions(physical,nullptr,&count,ext.data()));ext.resize(count);
        auto deviceHas=[&](const char* name){return std::any_of(ext.begin(),ext.end(),[&](auto& e){return std::string(e.extensionName)==name;});};
        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT};VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&maintenance;
        load<PFN_vkGetPhysicalDeviceFeatures2>(s.get,s.instance,"vkGetPhysicalDeviceFeatures2")(physical,&features);
        auto queues=load<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(s.get,s.instance,"vkGetPhysicalDeviceQueueFamilyProperties");queues(physical,&count,nullptr);std::vector<VkQueueFamilyProperties> families(count);queues(physical,&count,families.data());families.resize(count);int selected=-1;bool present{};
        for(unsigned i=0;i<count;++i){
            VkBool32 support{};vk(load<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(s.get,s.instance,"vkGetPhysicalDeviceSurfaceSupportKHR")(physical,i,s.surface,&support));
            const bool platform=load<PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR>(s.get,s.instance,"vkGetPhysicalDeviceXcbPresentationSupportKHR")(physical,i,s.connection,screen->root_visual);
            const bool graphics=(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)&&families[i].queueCount;
            if(graphics&&selected<0){selected=static_cast<int>(i);present=support&&platform;}
            std::cout<<"{\"event\":\"queue\",\"uuid\":"<<std::quoted(uuid)<<",\"family\":"<<i<<",\"graphics\":"<<graphics<<",\"surface_present\":"<<bool(support)<<",\"visual_present\":"<<platform<<"}\n";
        }
        require(selected>=0,"No graphics family");VkSurfaceCapabilitiesKHR caps{};vk(load<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(s.get,s.instance,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR")(physical,s.surface,&caps));
        auto formats=load<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(s.get,s.instance,"vkGetPhysicalDeviceSurfaceFormatsKHR");vk(formats(physical,s.surface,&count,nullptr));std::vector<VkSurfaceFormatKHR> fs(count);vk(formats(physical,s.surface,&count,fs.data()));fs.resize(count);
        const bool rgba=std::any_of(fs.begin(),fs.end(),[](auto f){return (f.format==VK_FORMAT_R8G8B8A8_UNORM||f.format==VK_FORMAT_UNDEFINED)&&f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;});
        auto modes=load<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(s.get,s.instance,"vkGetPhysicalDeviceSurfacePresentModesKHR");vk(modes(physical,s.surface,&count,nullptr));std::vector<VkPresentModeKHR> ms(count);vk(modes(physical,s.surface,&count,ms.data()));ms.resize(count);
        const bool fifo=std::find(ms.begin(),ms.end(),VK_PRESENT_MODE_FIFO_KHR)!=ms.end();const bool immediate=std::find(ms.begin(),ms.end(),VK_PRESENT_MODE_IMMEDIATE_KHR)!=ms.end();const bool mailbox=std::find(ms.begin(),ms.end(),VK_PRESENT_MODE_MAILBOX_KHR)!=ms.end();
        constexpr auto usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const bool swap=deviceHas(VK_KHR_SWAPCHAIN_EXTENSION_NAME),maintenanceExt=deviceHas(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME),policy=present&&swap&&maintenanceExt&&maintenance.swapchainMaintenance1&&rgba&&fifo&&((caps.supportedUsageFlags&usage)==usage)&&has(VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME)&&has(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
        bool deviceCreated{};if(policy){
            float priority=1;VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=selected;q.queueCount=1;q.pQueuePriorities=&priority;
            const char* required[]{VK_KHR_SWAPCHAIN_EXTENSION_NAME,VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME};VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&q;dc.pNext=&maintenance;dc.enabledExtensionCount=2;dc.ppEnabledExtensionNames=required;VkDevice device{};vk(load<PFN_vkCreateDevice>(s.get,s.instance,"vkCreateDevice")(physical,&dc,nullptr,&device));deviceCreated=true;
            load<PFN_vkDestroyDevice>(s.get,s.instance,"vkDestroyDevice")(device,nullptr);++ready;
        }
        software+=props.properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU;
        std::cout<<"{\"event\":\"adapter\",\"uuid\":"<<std::quoted(uuid)<<",\"name\":"<<std::quoted(props.properties.deviceName)<<",\"device_type\":"<<props.properties.deviceType<<",\"driver_version\":"<<props.properties.driverVersion<<",\"selected_family\":"<<selected<<",\"selected_present\":"<<present<<",\"swapchain_extension\":"<<swap<<",\"maintenance_extension\":"<<maintenanceExt<<",\"maintenance_feature\":"<<bool(maintenance.swapchainMaintenance1)<<",\"rgba8\":"<<rgba<<",\"fifo\":"<<fifo<<",\"immediate\":"<<immediate<<",\"mailbox\":"<<mailbox<<",\"usage\":"<<caps.supportedUsageFlags<<",\"min_images\":"<<caps.minImageCount<<",\"max_images\":"<<caps.maxImageCount<<",\"current_width\":"<<caps.currentExtent.width<<",\"current_height\":"<<caps.currentExtent.height<<",\"policy_ready\":"<<policy<<",\"native_device_created\":"<<deviceCreated<<"}\n";
    }
    auto adapters=gfx::enumerateAdapters(gfx::Backend::Vulkan);require(bool(adapters)&&adapters->size()==devices.size(),"SDK adapter count mismatch");
    for(auto& a:*adapters){gfx::DeviceDesc desc;desc.backend=gfx::Backend::Vulkan;desc.adapter=a.id;desc.softwareAdapter=a.softwareAdapter;auto device=gfx::createDevice(desc);require(bool(device),"Installed SDK creation failed");require(!(*device)->caps().swapChain,"SDK Linux WSI unexpectedly implemented");auto rejected=(*device)->createSwapChain({},{});require(!rejected&&rejected.error().code==gfx::ErrorCode::Unsupported,"SDK Linux WSI must remain Unsupported at scope stage");}
    std::cout<<"{\"event\":\"result\",\"source\":"<<std::quoted(gfx::buildFingerprint())<<",\"adapters\":"<<devices.size()<<",\"software_adapters\":"<<software<<",\"policy_ready_adapters\":"<<ready<<",\"sdk_linux_wsi\":false,\"swapchain_created\":false,\"presented\":false,\"human_display_accepted\":false}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
