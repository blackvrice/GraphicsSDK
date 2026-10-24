#pragma once
#include <Windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace traceclient {
using Phase=int(__cdecl*)(unsigned,unsigned);
using Json=const char*(__cdecl*)();
struct Client {
    using Arm=int(__cdecl*)(int,int,int,unsigned);using Status=unsigned(__cdecl*)();
    HMODULE module{};Phase phase{};Json json{};Arm arm{};Status status{};
    Client(){
        if(!GetModuleHandleExW(0,L"VkLayer_gfx_buffer_fault.dll",&module))throw std::runtime_error("Required allocation trace layer not loaded");
        phase=reinterpret_cast<Phase>(GetProcAddress(module,"gfxTracePhase"));json=reinterpret_cast<Json>(GetProcAddress(module,"gfxTraceJson"));
        arm=reinterpret_cast<Arm>(GetProcAddress(module,"gfxFaultArm"));status=reinterpret_cast<Status>(GetProcAddress(module,"gfxFaultStatus"));
        if(!phase||!json||!arm||!status)throw std::runtime_error("Allocation trace layer interface missing");
        wchar_t actual[32768]{},expected[32768]{};
        auto n=GetModuleFileNameW(module,actual,32768),m=GetEnvironmentVariableW(L"GFX_EXPECT_FAULT_LAYER",expected,32768);
        if(!n||n>=32768||!m||m>=32768||!std::filesystem::equivalent(actual,expected))throw std::runtime_error("Allocation trace DLL path differs");
    }
    ~Client(){if(module)FreeLibrary(module);}
    void set(unsigned p,unsigned i){if(!phase(p,i))throw std::runtime_error("Allocation trace phase rejected");}
    std::string result(){auto s=json();if(!s)throw std::runtime_error("Incomplete allocation trace");return s;}
};
inline Client* active{};
inline void phase(unsigned p,unsigned i){if(!active)throw std::runtime_error("Missing trace client");active->set(p,i);}
}
