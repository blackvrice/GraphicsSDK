// Own-process positive/negative controls for debugger exception semantics.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <array>

__declspec(noinline) void nullCall() {
    volatile std::uintptr_t target=0;
    reinterpret_cast<void(*)()>(target)();
}
__declspec(noinline) void handled() {
    __try { RaiseException(0xe0421001,0,0,nullptr); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}
struct Waiter {HANDLE ready,release;};
__declspec(noinline) DWORD WINAPI fixtureWaiter(void* value){
    const auto& events=*static_cast<Waiter*>(value);
    SetEvent(events.ready);return WaitForSingleObject(events.release,INFINITE)==WAIT_OBJECT_0?0:1;
}
__declspec(noinline) DWORD WINAPI fixtureCrashWorker(void*){nullCall();return 1;}
int workerCrash(unsigned count){
    HANDLE release=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!release)return 69;
    std::array<Waiter,24> events{};std::array<HANDLE,24> workers{};
    for(unsigned i=0;i<count;++i){
        events[i]={CreateEventW(nullptr,TRUE,FALSE,nullptr),release};
        if(!events[i].ready)return 69;
        workers[i]=CreateThread(nullptr,0,fixtureWaiter,&events[i],0,nullptr);
        if(!workers[i]||WaitForSingleObject(events[i].ready,5000)!=WAIT_OBJECT_0)return 69;
    }
    // All waiters are alive before the faulting worker starts. The main thread
    // waits too, so the collector must distinguish worker-PC and fault-PC rows.
    HANDLE crash=CreateThread(nullptr,0,fixtureCrashWorker,nullptr,0,nullptr);
    if(!crash)return 69;
    WaitForSingleObject(crash,INFINITE);return 70;
}
int wmain(int argc,wchar_t** argv) {
    if(argc<2)return 64;
    const std::wstring_view mode=argv[1];
    if(mode==L"handled"){handled();return 0;}
    if(mode==L"null-call"){nullCall();return 65;}
    if(mode==L"worker-crash")return workerCrash(3);
    if(mode==L"thread-limit")return workerCrash(24);
    if(mode==L"breakpoint"){DebugBreak();return 66;}
    if(mode==L"exit-seven")return 7;
    if(mode==L"sleep"){Sleep(10000);return 0;}
    if(mode==L"debug-spam"){const std::wstring text(128,L'x');for(unsigned i=0;i<1000;++i)OutputDebugStringW(text.c_str());return 0;}
    if(mode==L"arguments"){
        if(argc!=6||std::wstring_view(argv[2])!=L""||std::wstring_view(argv[3])!=L"space and 한글"
           ||std::wstring_view(argv[4])!=L"quote\"here"||std::wstring_view(argv[5])!=L"trailing slash\\")return 67;
        OutputDebugStringW(L"Unicode diagnostic 한글\n");return 0;
    }
    return 68;
}
