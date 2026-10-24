#define VK_NO_PROTOTYPES
#include <vulkan/vk_layer.h>
#include <Windows.h>
#include <iostream>
#include <stdexcept>
int main(int argc,char**argv)try{
    if(argc!=2)throw std::runtime_error("Trace DLL path required");auto module=LoadLibraryA(argv[1]);if(!module)throw std::runtime_error("Trace DLL unavailable");
    unsigned checks{};auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("Layer interface contract");};
    auto negotiate=reinterpret_cast<PFN_vkNegotiateLoaderLayerInterfaceVersion>(GetProcAddress(module,"gfxNegotiateLayer"));
    auto phase=reinterpret_cast<int(__cdecl*)(unsigned,unsigned)>(GetProcAddress(module,"gfxTracePhase"));auto json=reinterpret_cast<const char*(__cdecl*)()>(GetProcAddress(module,"gfxTraceJson"));check(negotiate&&phase&&json);
    check(negotiate(nullptr)==VK_ERROR_INITIALIZATION_FAILED);VkNegotiateLayerInterface v{};v.sType=LAYER_NEGOTIATE_INTERFACE_STRUCT;v.loaderLayerInterfaceVersion=1;check(negotiate(&v)==VK_ERROR_INITIALIZATION_FAILED);
    v.loaderLayerInterfaceVersion=99;check(negotiate(&v)==VK_SUCCESS&&v.loaderLayerInterfaceVersion==2);check(v.pfnGetInstanceProcAddr&&v.pfnGetDeviceProcAddr&&v.pfnGetPhysicalDeviceProcAddr);
    check(v.pfnGetInstanceProcAddr(nullptr,"vkCreateInstance")!=nullptr);check(v.pfnGetInstanceProcAddr(nullptr,"vkUnknownGfxTraceControl")==nullptr);
    check(!phase(7,0));check(!phase(1,257));check(phase(1,0));check(json()==nullptr);check(phase(0,0));
    auto text=json();check(text&&std::string_view(text).find("\"errors\":2")!=std::string_view::npos);FreeLibrary(module);
    std::cout<<"PASS trace_interface_assertions="<<checks<<" GPU_calls=0\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
