// Public RHI workload; native access only registers a device destruction observer.
#define main memoryStressEntryUnused
#include "../MemoryStress.cpp"
#undef main
#include <d3d12.h>
#include <d3dcommon.h>
#include <optional>

namespace {
void CALLBACK destroyed(void* context) { static_cast<std::atomic<unsigned>*>(context)->fetch_add(1); }
unsigned positive(const char* input,unsigned maximum) {
    std::size_t used{};const std::string value=input;const auto n=std::stoul(value,&used);
    check(used==value.size()&&n>0&&n<=maximum,"Invalid count");return static_cast<unsigned>(n);
}
void run(std::ostream& out,std::string_view mode,unsigned cycles,unsigned batches) {
    const auto start=Clock::now();std::atomic<unsigned> destructions{};unsigned frames=0;
    const bool warp=mode=="warp";std::unique_ptr<Observer> observer;
    const auto pd=pipelineDesc();
    out<<"{\"type\":\"header\",\"schema\":1,\"mode\":"<<quote(mode)<<",\"sdk_sha256\":"<<quote(gfx::buildFingerprint())<<",\"gpu_validation\":true,\"gpu_state_tracking\":true,\"cycles\":"<<cycles<<",\"batches_per_cycle\":"<<batches<<",\"ring\":"<<ring<<",\"side\":"<<side<<",\"copy_bytes\":"<<copyBytes<<"}\n";
    auto sample=[&](std::string_view phase,unsigned cycle,std::optional<gfx::ResourceStatistics> stats=std::nullopt) {
        out<<"{\"type\":\"sample\",\"phase\":"<<quote(phase)<<",\"cycle\":"<<cycle<<",\"elapsed\":"<<std::chrono::duration<double>(Clock::now()-start).count()<<",\"frames\":"<<frames<<",\"device_destructions\":"<<destructions.load()<<",\"sdk_statistics_available\":"<<(stats?"true":"false");if(stats)writeStats(out,*stats);observer->write(out);out<<"}\n";out.flush();
    };
    for(unsigned cycle=1;cycle<=cycles;++cycle) {
        auto d=take(gfx::createDevice({gfx::Backend::D3D12,true,warp}));
        if(!observer){observer=std::make_unique<Observer>(d->caps());out<<"{\"type\":\"adapter\",\"adapter\":"<<quote(d->caps().adapterName)<<",\"software\":"<<(warp?"true":"false")<<",\"luid_high\":"<<observer->desc.AdapterLuid.HighPart<<",\"luid_low\":"<<observer->desc.AdapterLuid.LowPart<<"}\n";}
        {
            ComPtr<ID3D12Device> native;hr(D3D12CreateDevice(observer->adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&native)));
            ComPtr<ID3DDestructionNotifier> notifier;hr(native.As(&notifier));UINT cookie{};hr(notifier->RegisterDestructionCallback(destroyed,&destructions,&cookie));
        }
        sample("device_ready",cycle,take(d->resourceStatistics()));
        for(unsigned batch=0;batch<batches;++batch) {
            auto p=take(d->createGraphicsPipeline(pd));std::vector<Frame> work;
            for(unsigned i=0;i<ring;++i)work.push_back(submit(*d,p,frames+i));
            take(d->queue().wait(work.back().fence));for(const auto& frame:work)verify(frame);frames+=ring;
            work.clear();p.reset();take(d->queue().waitIdle());const auto s=take(d->resourceStatistics());
            check(s.trackedObjects==0&&s.uniqueResources==0&&s.resourceAllocationBytes==0&&s.pendingSubmissions==0&&s.pendingObjectReferences==0,"SDK batch failed to reclaim resources");
        }
        sample("resources_released",cycle,take(d->resourceStatistics()));
        check(take(d->validationErrorCount())==0,"SDK lifetime full-GBV validation error");
        auto held=take(d->createBuffer({256,gfx::Memory::Upload}));auto alias=held;
        const auto heldStats=take(d->resourceStatistics());check(heldStats.trackedObjects==1&&heldStats.uniqueResources==1&&heldStats.pendingSubmissions==0&&heldStats.pendingObjectReferences==0,"Alias control accounting");
        sample("child_ready",cycle,heldStats);
        d.reset();check(destructions.load()==cycle-1,"Native device destroyed while child owns generation");sample("sdk_released_child_held",cycle);
        held.reset();check(destructions.load()==cycle-1,"Native device destroyed before last child alias");sample("last_alias_held",cycle);
        alias.reset();const auto deadline=Clock::now()+std::chrono::seconds(5);
        while(destructions.load()<cycle&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
        check(destructions.load()==cycle,"SDK generation still retains native device after last owner");
        // There is no Device to query here; SDK statistics are unavailable.
        sample("native_device_destroyed",cycle);std::this_thread::sleep_for(std::chrono::milliseconds(200));sample("settled",cycle);
    }
    out<<"{\"type\":\"footer\",\"lifetime_contracts_passed\":true,\"cycles\":"<<cycles<<",\"frames\":"<<frames<<",\"full_images\":"<<frames*3<<",\"verified_buffer_bytes\":"<<std::uint64_t(frames)*copyBytes<<",\"device_destructions\":"<<destructions.load()<<",\"validation_errors\":0,\"validation_counter\":\"unfiltered_callback_before_device_teardown\",\"memory_acceptance_claim\":false}\n";out.flush();
}
}
int main(int argc,char** argv)try {
    check(argc==5,"Usage: sdk_lifetime_probe hardware|warp cycles batches output.jsonl");const std::string mode=argv[1];check(mode=="hardware"||mode=="warp","Unknown SDK lifetime mode");
    const auto cycles=positive(argv[2],16),batches=positive(argv[3],16);check(GetFileAttributesA(argv[4])==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND,"Refuse overwrite or invalid output path");
    std::ofstream out(argv[4]);check(out.good(),"Open output");out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(12);run(out,mode,cycles,batches);
    std::cout<<"PASS public SDK readback and native generation destruction (not long-running acceptance)\n";return 0;
} catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
