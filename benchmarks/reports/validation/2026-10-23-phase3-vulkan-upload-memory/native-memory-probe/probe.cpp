#define VK_NO_PROTOTYPES
#include <Windows.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
#include <cstdio>
#include <vector>
int main() {
auto dll=LoadLibraryW(L"vulkan-1.dll");
auto get=(PFN_vkGetInstanceProcAddr)GetProcAddress(dll,"vkGetInstanceProcAddr");
auto create=(PFN_vkCreateInstance)get(0,"vkCreateInstance");
const char* exts[]={"VK_KHR_surface","VK_KHR_win32_surface"};
VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_1;
VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;ci.enabledExtensionCount=2;ci.ppEnabledExtensionNames=exts;
VkInstance in{}; if(create(&ci,0,&in))return 1;
#define LOAD(n) auto n=(PFN_vk##n)get(in,"vk" #n)
LOAD(GetPhysicalDeviceMemoryProperties); LOAD(EnumerateDeviceExtensionProperties); LOAD(GetPhysicalDeviceFeatures2); LOAD(CreateWin32SurfaceKHR); LOAD(DestroySurfaceKHR); LOAD(DestroyInstance); LOAD(EnumeratePhysicalDevices); LOAD(GetPhysicalDeviceProperties); LOAD(GetPhysicalDeviceSurfaceFormatsKHR); LOAD(GetPhysicalDeviceSurfaceCapabilitiesKHR); LOAD(GetPhysicalDeviceSurfacePresentModesKHR);
auto w=CreateWindowExW(0,L"STATIC",L"probe",WS_OVERLAPPEDWINDOW,0,0,128,128,0,0,GetModuleHandleW(0),0);
VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};sci.hinstance=GetModuleHandleW(0);sci.hwnd=w;
VkSurfaceKHR s{}; if(CreateWin32SurfaceKHR(in,&sci,0,&s))return 2;
unsigned n{}; EnumeratePhysicalDevices(in,&n,0);std::vector<VkPhysicalDevice> ds(n);EnumeratePhysicalDevices(in,&n,ds.data());
for(auto d:ds){VkPhysicalDeviceProperties p{};GetPhysicalDeviceProperties(d,&p);printf("%s\n",p.deviceName); EnumerateDeviceExtensionProperties(d,0,&n,0); std::vector<VkExtensionProperties> es(n); EnumerateDeviceExtensionProperties(d,0,&n,es.data()); for(auto e:es) if(strstr(e.extensionName,"maintenance1")) printf("extension %s\n",e.extensionName); VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT mf{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT}; VkPhysicalDeviceFeatures2 feat{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};feat.pNext=&mf;GetPhysicalDeviceFeatures2(d,&feat);printf("swapchainMaintenance1 %u\n",mf.swapchainMaintenance1); VkPhysicalDeviceMemoryProperties mem{};GetPhysicalDeviceMemoryProperties(d,&mem);for(unsigned j=0;j<mem.memoryTypeCount;++j)printf("memoryType %u flags %x heap %u\n",j,mem.memoryTypes[j].propertyFlags,mem.memoryTypes[j].heapIndex);VkSurfaceCapabilitiesKHR caps{};GetPhysicalDeviceSurfaceCapabilitiesKHR(d,s,&caps);printf("extent %u %u min %u max %u usage %x\n",caps.currentExtent.width,caps.currentExtent.height,caps.minImageCount,caps.maxImageCount,caps.supportedUsageFlags);
GetPhysicalDeviceSurfaceFormatsKHR(d,s,&n,0);std::vector<VkSurfaceFormatKHR> fs(n);GetPhysicalDeviceSurfaceFormatsKHR(d,s,&n,fs.data());for(auto f:fs)printf("format %u color %u\n",f.format,f.colorSpace);
GetPhysicalDeviceSurfacePresentModesKHR(d,s,&n,0);std::vector<VkPresentModeKHR> ms(n);GetPhysicalDeviceSurfacePresentModesKHR(d,s,&n,ms.data());for(auto m:ms)printf("mode %u\n",m);
}DestroySurfaceKHR(in,s,0);DestroyWindow(w);DestroyInstance(in,0);FreeLibrary(dll);
}
