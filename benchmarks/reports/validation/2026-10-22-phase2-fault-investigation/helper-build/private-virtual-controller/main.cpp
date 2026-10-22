#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <evntrace.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
int wmain(int argc,wchar_t** argv){
    if(argc!=5||!std::wstring_view(argv[1]).starts_with(L"GraphicsSDKPrivateVirtual-"))return 125;
    HANDLE token{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token))return 125;
    TOKEN_PRIVILEGES privileges{};privileges.PrivilegeCount=1;
    if(!LookupPrivilegeValueW(nullptr,L"SeSystemProfilePrivilege",&privileges.Privileges[0].Luid)){CloseHandle(token);return 125;}
    privileges.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
    const bool allowed=AdjustTokenPrivileges(token,FALSE,&privileges,0,nullptr,nullptr)&&GetLastError()==ERROR_SUCCESS;CloseHandle(token);if(!allowed)return 125;
    const std::wstring name=argv[1],file=argv[2];
    if(std::filesystem::exists(file)||std::filesystem::exists(argv[3])||std::filesystem::exists(argv[4]))return 125;
    std::vector<std::byte> buffer(sizeof(EVENT_TRACE_PROPERTIES)+(name.size()+file.size()+2)*sizeof(wchar_t));
    auto* props=reinterpret_cast<EVENT_TRACE_PROPERTIES*>(buffer.data());
    props->Wnode.BufferSize=static_cast<ULONG>(buffer.size());props->Wnode.Flags=WNODE_FLAG_TRACED_GUID;props->Wnode.ClientContext=1;
    props->LogFileMode=EVENT_TRACE_FILE_MODE_SEQUENTIAL|EVENT_TRACE_SYSTEM_LOGGER_MODE;props->MaximumFileSize=128;props->BufferSize=1024;props->MinimumBuffers=16;props->MaximumBuffers=64;
    props->LoggerNameOffset=sizeof(EVENT_TRACE_PROPERTIES);props->LogFileNameOffset=static_cast<ULONG>(sizeof(EVENT_TRACE_PROPERTIES)+(name.size()+1)*sizeof(wchar_t));
    std::memcpy(buffer.data()+props->LogFileNameOffset,file.c_str(),(file.size()+1)*sizeof(wchar_t));
    TRACEHANDLE handle{};const ULONG started=StartTraceW(&handle,name.c_str(),props);ULONG stacks=ERROR_INVALID_FUNCTION,flags=ERROR_INVALID_FUNCTION,stopped=ERROR_INVALID_FUNCTION;
    if(started==ERROR_SUCCESS){
        constexpr GUID memory{0x3d6fa8d3,0xfe05,0x11d0,{0x9d,0xda,0x00,0xc0,0x4f,0xd7,0xba,0x7c}};
        CLASSIC_EVENT_ID ids[2]{};ids[0].EventGuid=ids[1].EventGuid=memory;ids[0].Type=98;ids[1].Type=99;
        stacks=TraceSetInformation(handle,TraceStackTracingInfo,ids,sizeof(ids));
        ULONG enable[8]{EVENT_TRACE_FLAG_PROCESS|EVENT_TRACE_FLAG_THREAD|EVENT_TRACE_FLAG_IMAGE_LOAD|EVENT_TRACE_FLAG_VIRTUAL_ALLOC|EVENT_TRACE_FLAG_NO_SYSCONFIG};
        if(stacks==ERROR_SUCCESS)flags=TraceSetInformation(handle,TraceSystemTraceEnableFlagsInfo,enable,sizeof(enable));
        if(flags==ERROR_SUCCESS){
            {std::ofstream ready{std::filesystem::path(argv[3])};ready<<"ready";}
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
            while(!std::filesystem::exists(argv[4])&&std::chrono::steady_clock::now()<deadline)Sleep(50);
        }
        // Stop only the handle created by this invocation. Never use a global logger GUID/name.
        stopped=ControlTraceW(handle,nullptr,props,EVENT_TRACE_CONTROL_STOP);
    }
    std::cout<<"{\"started\":"<<started<<",\"stack_status\":"<<stacks<<",\"flags_status\":"<<flags<<",\"stop_status\":"<<stopped<<",\"events_lost\":"<<props->EventsLost<<",\"buffers_written\":"<<props->BuffersWritten<<",\"number_of_buffers\":"<<props->NumberOfBuffers<<",\"own_handle_only\":true,\"nt_kernel_logger_touched\":false,\"registry_changed\":false}\n";
    return started||stacks||flags||stopped||props->EventsLost?125:0;
}
