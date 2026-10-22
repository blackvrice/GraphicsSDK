#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <graphics/Graphics.hpp>
#include "../src/backends/d3d12/FenceWait.hpp"
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;
unsigned assertions{},registrations{},zeroPolls{},timedRetries{};
void check(bool v,const char* message){++assertions;if(!v)throw std::runtime_error(message);}
template<class T>T take(gfx::Result<T> r){if(!r)throw std::runtime_error(r.error().message);return std::move(*r);}
void take(gfx::Result<void> r){if(!r)throw std::runtime_error(r.error().message);}
void hr(HRESULT v){check(SUCCEEDED(v),"Native D3D12 control failed");}
template<class T>void reject(const gfx::Result<T>& r,gfx::ErrorCode code,const char* text){check(!r&&r.error().code==code,text);}
DWORD handles(){DWORD n{};check(GetProcessHandleCount(GetCurrentProcess(),&n)!=0,"Read handle count");return n;}
struct ObservedFence {
    ID3D12Fence& fence;
    unsigned arms{};
    HANDLE event{};
    std::uint64_t threshold{};
    HRESULT SetEventOnCompletion(std::uint64_t value,HANDLE handle){
        ++arms;++registrations;event=handle;threshold=value;return fence.SetEventOnCompletion(value,handle);
    }
};
void nativeControls(gfx::Device& sdk){
    const auto id=sdk.caps().adapterId.value;
    ComPtr<IDXGIFactory4> factory;hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
    const LUID luid{static_cast<DWORD>(id),std::bit_cast<LONG>(static_cast<DWORD>(id>>32))};
    ComPtr<IDXGIAdapter> adapter;hr(factory->EnumAdapterByLuid(luid,IID_PPV_ARGS(&adapter)));
    ComPtr<ID3D12Device> device;hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    D3D12_COMMAND_QUEUE_DESC desc{};ComPtr<ID3D12CommandQueue> queue;hr(device->CreateCommandQueue(&desc,IID_PPV_ARGS(&queue)));
    ComPtr<ID3D12Fence> fence,gate;hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));hr(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)));
    // Always open both gates on every exit path, including a failed assertion.
    struct Release {ID3D12Fence& gate;~Release(){(void)gate.Signal(2);}} release{*gate.Get()};
    hr(queue->Wait(gate.Get(),1));hr(queue->Signal(fence.Get(),1));hr(queue->Wait(gate.Get(),2));hr(queue->Signal(fence.Get(),2));
    auto poll=[&]()->gfx::Result<std::uint64_t>{auto value=fence->GetCompletedValue();if(value==UINT64_MAX)return std::unexpected(gfx::Error{gfx::ErrorCode::DeviceLost,"Native control device lost",device->GetDeviceRemovedReason()});return value;};
    auto fail=[](HRESULT code,std::string operation)->gfx::Result<void>{return std::unexpected(gfx::Error{gfx::ErrorCode::BackendFailure,std::move(operation),code});};
    ObservedFence observed{*fence.Get()};
    const auto before=handles();DWORD active{};
    {
        gfx::detail::FenceWaiter waiter;
        for(unsigned i=0;i<4096;++i){reject(waiter.wait(observed,2,0ms,poll,fail),gfx::ErrorCode::Timeout,"Gated zero poll must timeout");++zeroPolls;}
        check(observed.arms==0,"Zero polls create no native registration");
        for(auto duration:{-1ms,std::chrono::milliseconds(4294967295LL),std::chrono::milliseconds::max()})
            reject(waiter.wait(observed,2,duration,poll,fail),gfx::ErrorCode::InvalidArgument,"Invalid pending timeout rejected");
        check(observed.arms==0,"Invalid timeouts do not register native events");
        for(unsigned i=0;i<64;++i){reject(waiter.wait(observed,2,1ms,poll,fail),gfx::ErrorCode::Timeout,"Gated timed wait must timeout");++timedRetries;}
        check(observed.arms==1&&observed.threshold==1,"Timed retries retain one next-incomplete registration");
        active=handles();const auto ownedEvent=observed.event;
        DWORD flags{};check(ownedEvent&&GetHandleInformation(ownedEvent,&flags),"Owned reusable event remains valid after timeout");
        // A stale wake must neither satisfy the caller nor return an early-wake error.
        check(SetEvent(observed.event)!=0,"Inject an event wake without fence progress");
        reject(waiter.wait(observed,1,2ms,poll,fail),gfx::ErrorCode::Timeout,"Stale wake rechecks fence until timeout");
        check(observed.arms==1,"Earlier target reuses pending threshold");
        hr(gate->Signal(1));take(waiter.wait(observed,1,2000ms,poll,fail));
        check(fence->GetCompletedValue()==1,"First gate releases only first fence point");
        reject(waiter.wait(observed,2,1ms,poll,fail),gfx::ErrorCode::Timeout,"Second gate remains blocked");
        check(observed.arms==2&&observed.threshold==2,"Advance registration only after old threshold completion");
        hr(gate->Signal(2));take(waiter.wait(observed,2,2000ms,poll,fail));take(waiter.wait(observed,2,0ms,poll,fail));
        for(auto duration:{-1ms,std::chrono::milliseconds(4294967295LL),std::chrono::milliseconds::max()})
            reject(waiter.wait(observed,2,duration,poll,fail),gfx::ErrorCode::InvalidArgument,"Invalid completed timeout rejected");
        check(observed.arms==2&&observed.event==ownedEvent,"Advancing/completed/invalid waits reuse the same event");
        // Recheck loss at a timeout boundary, rather than masking it as Timeout.
        unsigned polls{};
        auto lost=[&]()->gfx::Result<std::uint64_t>{if(++polls==1)return 2;return std::unexpected(gfx::Error{gfx::ErrorCode::DeviceLost,"Controlled poll loss",-17});};
        auto r=waiter.wait(observed,3,1ms,lost,fail);reject(r,gfx::ErrorCode::DeviceLost,"Poll loss preserved");check(r.error().nativeCode==-17,"Loss native reason preserved");
        check(polls==2&&observed.arms==3,"Timeout path rechecks generation status");hr(fence->Signal(3));
        waiter.reset();check(!GetHandleInformation(ownedEvent,&flags)&&GetLastError()==ERROR_INVALID_HANDLE,"Explicit retirement closes the owned event");
    }
    std::cout<<"{\"event\":\"native_wait_control\",\"adapter_id\":"<<id<<",\"zero_polls\":4096,\"timed_retries\":64,\"native_registrations\":"<<observed.arms<<",\"handles_before\":"<<before<<",\"handles_while_waiting\":"<<active<<",\"handles_after\":"<<handles()<<",\"owned_event_closed\":true,\"passed\":true}\n";
}
void publicControls(gfx::Device& d,bool validation){
    auto upload=take(d.createBuffer({4096,gfx::Memory::Upload})),readback=take(d.createBuffer({4096,gfx::Memory::Readback}));
    const std::array<std::byte,4096> data{};take(upload->write(0,data));
    auto c=take(d.createCommandList());take(c->begin());take(c->copyBuffer(upload,readback,data.size()));take(c->end());auto fence=take(d.queue().submit(*c));
    c.reset();upload.reset();take(d.queue().wait(fence));take(d.queue().wait(fence,0ms));
    for(auto duration:{-1ms,std::chrono::milliseconds(4294967295LL),std::chrono::milliseconds::max()})
        reject(d.queue().wait(fence,duration),gfx::ErrorCode::InvalidArgument,"Public completed timeout validation");
    take(d.queue().wait(fence,std::chrono::milliseconds(4294967294LL)));
    reject(d.queue().wait({fence.value+100,fence.device},0ms),gfx::ErrorCode::InvalidArgument,"Future fence cannot be polled");
    reject(d.queue().wait({fence.value,fence.device+1},0ms),gfx::ErrorCode::WrongDevice,"Foreign fence cannot be polled");
    bool rejected=false;std::thread other([&]{auto r=d.queue().wait(fence,0ms);rejected=!r&&r.error().code==gfx::ErrorCode::InvalidState;});other.join();check(rejected,"Public wait obeys thread affinity");
    check(take(readback->read(0,data.size()))==std::vector<std::byte>(data.begin(),data.end()),"Full copy readback after completed retries");readback.reset();
    take(d.queue().waitIdle());const auto stats=take(d.resourceStatistics());check(stats.trackedObjects==0&&stats.uniqueResources==0&&stats.pendingSubmissions==0,"Public resources reclaimed after waits");
    if(validation)check(take(d.validationErrorCount())==0,"Public wait/readback controls have no validation errors");
    take(d.removeForTesting());const auto status=d.checkStatus();reject(status,gfx::ErrorCode::DeviceLost,"Controlled removal");
    auto lost=d.queue().wait(fence,-1ms);reject(lost,gfx::ErrorCode::DeviceLost,"Loss precedes timeout validation");check(lost.error().nativeCode==status.error().nativeCode,"Wait preserves removal reason");
    auto fresh=take(d.recover());reject(fresh->queue().wait(fence,0ms),gfx::ErrorCode::WrongDevice,"Old generation fence rejected after recovery");take(fresh->queue().waitIdle());
    if(validation)check(take(fresh->validationErrorCount())==0,"Recovered wait has no validation errors");
}
}
int main(int argc,char** argv)try{
    check(argc==2,"Expected --gpu or --validation");const std::string_view mode=argv[1];check(mode=="--gpu"||mode=="--validation","Unknown test mode");const bool validation=mode=="--validation";
    const auto adapters=take(gfx::enumerateAdapters());check(!adapters.empty(),"Require real adapters");
    for(const auto& adapter:adapters){
        auto d=take(gfx::createDevice({gfx::Backend::D3D12,validation,adapter.softwareAdapter,true,adapter.id}));
        nativeControls(*d);
        if(validation)check(take(d->validationErrorCount())==0,"Native wait controls have no validation errors");
        publicControls(*d,validation);
    }
    std::cout<<"{\"event\":\"fence_wait_result\",\"sdk_sha256\":\""<<gfx::buildFingerprint()<<"\",\"adapters\":"<<adapters.size()<<",\"validation\":"<<(validation?"true":"false")<<",\"zero_polls\":"<<zeroPolls<<",\"timed_retries\":"<<timedRetries<<",\"native_registrations\":"<<registrations<<",\"assertions\":"<<assertions<<",\"passed\":true}\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
