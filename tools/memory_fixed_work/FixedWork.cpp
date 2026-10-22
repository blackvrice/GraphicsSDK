// Reuse the exact public-workload submit/readback/statistics implementation.
// This standalone diagnostic has its own entry point and evidence schema.
#define main originalTimeBasedMemoryStressMain
#include "../MemoryStress.cpp"
#undef main
#include <filesystem>
#include <sstream>

struct FixedOutput {
    HANDLE handle;
    explicit FixedOutput(const std::string& path):handle(CreateFileW(std::filesystem::path(path).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)) {
        check(handle!=INVALID_HANDLE_VALUE,"Refuse existing/invalid fixed-work output");
    }
    ~FixedOutput(){CloseHandle(handle);}
    void flush(std::ostringstream& out){
        const auto text=out.str();DWORD written{};
        check(WriteFile(handle,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size(),"Write fixed-work output");
        check(FlushFileBuffers(handle)!=FALSE,"Flush fixed-work output");out.str("");out.clear();
    }
};

int main(int argc,char** argv)try {
    std::string mode="validation",output;
    std::uint64_t warm=8,limit=512,checkpoint=8,maxSeconds=120,guardMiB=512;
    std::optional<gfx::AdapterId> selected;std::set<std::string_view> seen;
    for(int i=1;i<argc;++i){
        const std::string_view arg=argv[i];check(seen.insert(arg).second&&i+1<argc,"Duplicate/missing fixed-work option");const std::string_view value=argv[++i];
        if(arg=="--mode")mode=value;else if(arg=="--output")output=value;
        else if(arg=="--adapter")selected=gfx::AdapterId{gfx::Backend::D3D12,decimal(value)};
        else if(arg=="--rounds")limit=decimal(value);else if(arg=="--warmup-rounds")warm=decimal(value);
        else if(arg=="--checkpoint-rounds")checkpoint=decimal(value);else if(arg=="--max-seconds")maxSeconds=decimal(value);
        else if(arg=="--max-private-growth-mib")guardMiB=decimal(value);else throw std::runtime_error("Unknown fixed-work option");
    }
    check((mode=="gpu"||mode=="validation"||mode=="warp")&&!output.empty()&&selected.has_value(),"Require mode, output and fresh explicit adapter");
    check(limit>=1&&limit<=100000&&warm<=10000&&checkpoint>=1&&checkpoint<=limit&&maxSeconds>=1&&maxSeconds<=3600&&guardMiB>=1&&guardMiB<=4096,"Invalid fixed-work bounds");
    // Exclusive creation before device work: never replace a previous result.
    FixedOutput file(output);
    auto d=take(gfx::createDevice({gfx::Backend::D3D12,mode=="validation",mode=="warp",false,selected}));
    Observer observer(d->caps());statisticsContracts(*d);
    std::ostringstream out;out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(12);
    const auto start=Clock::now();auto elapsed=[&]{return std::chrono::duration<double>(Clock::now()-start).count();};
    const auto baseline=processMemory().PrivateUsage,guardBytes=guardMiB*MiB;
    out<<"{\"type\":\"header\",\"schema\":\"fixed_work_v1\",\"reference_schema\":3,\"diagnostic_only\":true,\"memory_acceptance\":false,\"mode\":"<<quote(mode)
       <<",\"adapter\":"<<quote(d->caps().adapterName)<<",\"adapter_id\":"<<d->caps().adapterId.value<<",\"observer_adapter_id\":"<<identity(observer.desc.AdapterLuid)
       <<",\"requested_adapter_id\":"<<selected->value<<",\"software\":"<<(d->caps().softwareAdapter?"true":"false")<<",\"gpu_validation\":"<<(mode=="validation"?"true":"false")
       <<",\"gpu_state_tracking\":"<<(mode=="validation"?"true":"false")<<",\"private_growth_limit_bytes\":"<<guardBytes<<",\"guard_baseline_private_bytes\":"<<baseline
       <<",\"luid_high\":"<<observer.desc.AdapterLuid.HighPart<<",\"luid_low\":"<<observer.desc.AdapterLuid.LowPart<<",\"sdk_sha256\":"<<quote(gfx::buildFingerprint())
       <<",\"warmup_rounds\":"<<warm<<",\"measured_round_limit\":"<<limit<<",\"checkpoint_rounds\":"<<checkpoint<<",\"max_seconds\":"<<maxSeconds
       <<",\"ring\":"<<ring<<",\"side\":"<<side<<",\"copy_bytes\":"<<copyBytes<<"}\n";
    std::uint64_t frames=0,rounds=0;const char* reason="completed";int code=0;
    auto sample=[&](std::string_view phase){out<<"{\"type\":\"sample\",\"phase\":"<<quote(phase)<<",\"elapsed\":"<<elapsed()<<",\"frames\":"<<frames<<",\"rounds\":"<<rounds;writeStats(out,take(d->resourceStatistics()));observer.write(out,*d);out<<"}\n";file.flush(out);};
    sample("baseline");
    {void* pages=VirtualAlloc(nullptr,32*MiB,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);check(pages!=nullptr,"CPU fixed-work control");std::memset(pages,0x5a,32*MiB);sample("cpu_held");check(VirtualFree(pages,0,MEM_RELEASE)!=0,"Release CPU fixed-work control");}sample("cpu_released");
    {auto held=take(d->createBuffer({64*MiB}));{auto upload=take(d->createBuffer({64*MiB,gfx::Memory::Upload}));auto c=take(d->createCommandList());take(c->begin());take(c->transition(held,S::CopyDestination));take(c->copyBuffer(upload,held,64*MiB));take(c->end());take(d->queue().wait(take(d->queue().submit(*c))));}take(d->queue().waitIdle());sample("gpu_held");}take(d->queue().waitIdle());sample("gpu_released");
    const auto pd=pipelineDesc();
    while(rounds<warm+limit){
        if(rounds==warm)sample("measurement_start");
        if(elapsed()>=maxSeconds){code=3;reason="wall_time_limit";sample("time_aborted");break;}
        auto p=take(d->createGraphicsPipeline(pd));std::vector<Frame> batch;
        for(unsigned i=0;i<ring;++i)batch.push_back(submit(*d,p,static_cast<unsigned>(frames+i)));
        take(d->queue().wait(batch.back().fence));for(const auto& f:batch)verify(f);frames+=ring;++rounds;
        batch.clear();p.reset();take(d->queue().waitIdle());
        const auto stats=take(d->resourceStatistics());
        check(stats.trackedObjects==0&&stats.uniqueResources==0&&stats.resourceAllocationBytes==0&&stats.pendingSubmissions==0&&stats.pendingObjectReferences==0,"Fixed-work ownership did not drain");
        if(rounds>warm&&((rounds-warm)%checkpoint==0||rounds==warm+limit))sample("drained");
        const auto current=processMemory().PrivateUsage;
        if(current>baseline&&current-baseline>guardBytes){code=2;reason="private_growth_limit";sample("guard_aborted");break;}
        if(elapsed()>=maxSeconds){code=3;reason="wall_time_limit";sample("time_aborted");break;}
    }
    sample("final");const auto errors=mode=="validation"?take(d->validationErrorCount()):0;check(errors==0,"Unexpected fixed-work validation errors");
    out<<"{\"type\":\"footer\",\"adapter_id\":"<<d->caps().adapterId.value<<",\"elapsed\":"<<elapsed()<<",\"frames\":"<<frames<<",\"full_images\":"<<frames*3
       <<",\"verified_buffer_bytes\":"<<frames*copyBytes<<",\"warmup_rounds_verified\":"<<std::min(rounds,warm)<<",\"measured_rounds_verified\":"<<(rounds>warm?rounds-warm:0)
       <<",\"validation_errors\":"<<errors<<",\"statistics_contracts\":true,\"status\":"<<quote(code?"aborted":"passed")<<",\"stop_reason\":"<<quote(reason)<<",\"memory_acceptance\":false}\n";
    file.flush(out);std::cout<<"Fixed work: "<<frames<<" verified frames, reason="<<reason<<"; memory acceptance=false\n";return code;
}catch(const std::exception& error){std::cerr<<"FAIL fixed work: "<<error.what()<<'\n';return 1;}
