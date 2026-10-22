#include <graphics/Graphics.hpp>
#include <array>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>
#if defined(_WIN32) && defined(GFX_TEST_D3D12)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <bit>
#endif

namespace {
unsigned assertions{},observations{},controls{},recoveries{};
std::uint64_t verifiedBytes{};
void check(bool value,const char* text){++assertions;if(!value)throw std::runtime_error(text);}
template<class T>T take(gfx::Result<T> value){if(!value)throw std::runtime_error(value.error().message+" native="+std::to_string(value.error().nativeCode));return std::move(*value);}
void take(gfx::Result<void> value){if(!value)throw std::runtime_error(value.error().message);}
template<class T>void reject(gfx::Result<T> value,gfx::ErrorCode code,const char* text){check(!value&&value.error().code==code,text);}
void cpu(){
    static_assert(std::is_same_v<decltype(std::declval<gfx::Device&>().queryMemoryBudget(gfx::MemorySegment::Local)),gfx::Result<gfx::MemoryBudget>>);
    gfx::MemoryBudget snapshot{{gfx::Backend::D3D12,0},gfx::MemorySegment::NonLocal,1,2,3,4};
    check(snapshot.adapterId.value==0&&snapshot.segment==gfx::MemorySegment::NonLocal&&snapshot.usageBytes>snapshot.budgetBytes,
          "A snapshot can contain explicit zero identity and usage over budget");
    check(snapshot.availableForReservationBytes==3&&snapshot.reservationBytes==4,"Reservation fields are distinct byte amounts");
    if(!gfx::backendCompiled(gfx::Backend::D3D12))reject(gfx::createDevice(),gfx::ErrorCode::Unsupported,"No backend must not synthesize a device/budget");
}
#if defined(_WIN32) && defined(GFX_TEST_D3D12)
using Microsoft::WRL::ComPtr;
constexpr std::uint64_t MiB=1024*1024;
LUID luid(gfx::AdapterId id){return {static_cast<DWORD>(id.value),std::bit_cast<LONG>(static_cast<DWORD>(id.value>>32))};}
bool equal(const DXGI_QUERY_VIDEO_MEMORY_INFO& a,const DXGI_QUERY_VIDEO_MEMORY_INFO& b){return a.Budget==b.Budget&&a.CurrentUsage==b.CurrentUsage&&a.AvailableForReservation==b.AvailableForReservation&&a.CurrentReservation==b.CurrentReservation;}
gfx::Result<gfx::MemoryBudget> observe(gfx::Device& device,IDXGIAdapter3& adapter,gfx::MemorySegment segment,const char* phase){
    // Global OS budgets may change between calls. Require a stable independent
    // bracket before testing exact field mapping, rather than a guessed tolerance.
    for(unsigned attempt=0;attempt<20;++attempt){
        DXGI_QUERY_VIDEO_MEMORY_INFO before{},after{};
        const auto group=segment==gfx::MemorySegment::Local?DXGI_MEMORY_SEGMENT_GROUP_LOCAL:DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL;
        const auto first=adapter.QueryVideoMemoryInfo(0,group,&before);
        auto sdk=device.queryMemoryBudget(segment);
        const auto last=adapter.QueryVideoMemoryInfo(0,group,&after);
        if(first!=last||(SUCCEEDED(first)&&!equal(before,after))){std::this_thread::sleep_for(std::chrono::milliseconds(5));continue;}
        if(FAILED(first)){
            check(!sdk&&sdk.error().nativeCode==first,"Native query failure must preserve HRESULT without fake zero values");
            check(device.caps().softwareAdapter,"Hardware budget observation is required on this test host");
            std::cout<<"{\"event\":\"budget_unavailable\",\"adapter_id\":"<<device.caps().adapterId.value<<",\"segment\":"<<static_cast<int>(segment)<<",\"native_code\":"<<first<<"}\n";
            return sdk;
        }
        const auto value=take(sdk);
        check(value.adapterId==device.caps().adapterId&&value.segment==segment,"Snapshot identifies actual adapter and requested segment");
        check(value.budgetBytes==before.Budget&&value.usageBytes==before.CurrentUsage&&value.availableForReservationBytes==before.AvailableForReservation&&value.reservationBytes==before.CurrentReservation,"Every field matches independent native DXGI bracket");
        ++observations;
        std::cout<<"{\"event\":\"budget\",\"phase\":\""<<phase<<"\",\"adapter_id\":"<<value.adapterId.value<<",\"segment\":"<<static_cast<int>(segment)
                 <<",\"budget_bytes\":"<<value.budgetBytes<<",\"usage_bytes\":"<<value.usageBytes<<",\"available_for_reservation_bytes\":"<<value.availableForReservationBytes<<",\"reservation_bytes\":"<<value.reservationBytes<<",\"native_matched\":true}\n";
        return value;
    }
    throw std::runtime_error("Could not obtain stable independent DXGI bracket");
}
void exercise(bool validation){
    const auto candidates=take(gfx::enumerateAdapters());check(!candidates.empty(),"Real adapters required");
    ComPtr<IDXGIFactory4> factory;check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"Independent DXGI factory");
    std::cout<<"{\"event\":\"budget_test\",\"sdk_sha256\":\""<<gfx::buildFingerprint()<<"\",\"validation\":"<<(validation?"true":"false")<<"}\n";
    for(const auto& candidate:candidates){
        auto d=take(gfx::createDevice({gfx::Backend::D3D12,validation,candidate.softwareAdapter,true,candidate.id}));
        ComPtr<IDXGIAdapter3> adapter;check(SUCCEEDED(factory->EnumAdapterByLuid(luid(candidate.id),IID_PPV_ARGS(&adapter))),"Independent same-LUID adapter");
        auto baseline=observe(*d,*adapter.Get(),gfx::MemorySegment::Local,"baseline");
        (void)observe(*d,*adapter.Get(),gfx::MemorySegment::NonLocal,"baseline");
        bool wrongThread=false;std::thread worker([&]{auto r=d->queryMemoryBudget(gfx::MemorySegment::Local);wrongThread=!r&&r.error().code==gfx::ErrorCode::InvalidState;});worker.join();check(wrongThread,"Budget query enforces creating-thread affinity");
        reject(d->queryMemoryBudget(static_cast<gfx::MemorySegment>(-1)),gfx::ErrorCode::InvalidArgument,"Reject invalid negative segment");
        reject(d->queryMemoryBudget(static_cast<gfx::MemorySegment>(2)),gfx::ErrorCode::InvalidArgument,"Reject unknown positive segment");
        auto held=take(d->createBuffer({64*MiB}));
        {
            auto upload=take(d->createBuffer({64*MiB,gfx::Memory::Upload}));const std::vector<std::byte> pattern(64*MiB,std::byte{0xa5});take(upload->write(0,pattern));
            auto readback=take(d->createBuffer({65536,gfx::Memory::Readback}));auto c=take(d->createCommandList());
            take(c->begin());take(c->transition(held,gfx::ResourceState::CopyDestination));take(c->copyBuffer(upload,held,64*MiB));take(c->transition(held,gfx::ResourceState::CopySource));take(c->copyBuffer(held,readback,65536));take(c->end());take(d->queue().wait(take(d->queue().submit(*c))));
            const auto data=take(readback->read(0,65536));for(auto value:data)check(value==std::byte{0xa5},"Full positive-control buffer reference");verifiedBytes+=data.size();
        }
        take(d->queue().waitIdle());const auto owned=take(d->resourceStatistics());check(owned.trackedObjects==1&&owned.uniqueResources==1&&owned.pendingSubmissions==0,"Retain one SDK resource at the observation checkpoint");
        const auto retained=observe(*d,*adapter.Get(),gfx::MemorySegment::Local,"held");(void)observe(*d,*adapter.Get(),gfx::MemorySegment::NonLocal,"held");
        const auto afterQuery=take(d->resourceStatistics());check(afterQuery.trackedObjects==owned.trackedObjects&&afterQuery.uniqueResources==owned.uniqueResources&&afterQuery.resourceAllocationBytes==owned.resourceAllocationBytes&&afterQuery.pendingSubmissions==owned.pendingSubmissions&&afterQuery.pendingObjectReferences==owned.pendingObjectReferences,"Budget queries leave SDK resource ownership unchanged");
        held.reset();take(d->queue().waitIdle());
        auto released=observe(*d,*adapter.Get(),gfx::MemorySegment::Local,"released");(void)observe(*d,*adapter.Get(),gfx::MemorySegment::NonLocal,"released");
        if(!candidate.softwareAdapter){
            check(baseline&&retained&&released,"Hardware requires successful local budget snapshots");
            check(retained->usageBytes>=baseline->usageBytes+32*MiB&&retained->usageBytes>=released->usageBytes+32*MiB,"64MiB GPU usage retain/release positive control");++controls;
        }
        if(validation)check(take(d->validationErrorCount())==0,"Healthy budget/control validation errors");
        take(d->removeForTesting());const auto lost=d->checkStatus();check(!lost&&lost.error().code==gfx::ErrorCode::DeviceLost,"Controlled native loss");const auto reason=lost.error().nativeCode;
        auto lostBudget=[&](gfx::Device& old){for(auto segment:{gfx::MemorySegment::Local,gfx::MemorySegment::NonLocal,static_cast<gfx::MemorySegment>(2)}){auto r=old.queryMemoryBudget(segment);check(!r&&r.error().code==gfx::ErrorCode::DeviceLost&&r.error().nativeCode==reason,"Lost or retired query preserves native reason before inspecting segment");}};
        lostBudget(*d);auto recovered=take(d->recover());lostBudget(*d);check(recovered->caps().adapterId==candidate.id,"Recovered budget uses exact selected adapter");
        (void)observe(*recovered,*adapter.Get(),gfx::MemorySegment::Local,"recovered");(void)observe(*recovered,*adapter.Get(),gfx::MemorySegment::NonLocal,"recovered");++recoveries;
        if(validation)check(take(recovered->validationErrorCount())==0,"Recovered budget validation errors");
    }
    std::cout<<"{\"event\":\"budget_result\",\"adapters\":"<<candidates.size()<<",\"observations\":"<<observations<<",\"positive_controls\":"<<controls<<",\"recoveries\":"<<recoveries<<",\"verified_buffer_bytes\":"<<verifiedBytes<<",\"passed\":true}\n";
}
#endif
}
int main(int argc,char** argv)try{
    const std::string_view mode=argc>1?argv[1]:"--cpu";
    if(mode=="--cpu")cpu();
#if defined(_WIN32) && defined(GFX_TEST_D3D12)
    else if(mode=="--gpu"||mode=="--validation")exercise(mode=="--validation");
#endif
    else throw std::runtime_error("Unknown/unavailable budget test mode");
    std::cout<<"PASS: "<<assertions<<" memory budget assertions\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
