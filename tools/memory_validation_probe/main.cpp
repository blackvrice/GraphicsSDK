// Diagnostic only: share the exact render/readback helpers with the failed soak.
// Native device access below observes the debug layer; workloads still use the SDK.
#define main memoryStressEntryUnused
#include "../MemoryStress.cpp"
#undef main
#include <d3d12.h>
#include <d3d12sdklayers.h>

namespace {
Frame pooledSubmit(gfx::Device& d,const gfx::PipelinePtr& pipeline,unsigned index,gfx::CommandList& commands){
    auto a=take(d.createTexture({side,side,gfx::Format::RGBA8UNorm,true})),b=take(d.createTexture({side,side,gfx::Format::RGBA8UNorm,true})),z=take(d.createTexture({side,side,gfx::Format::D32Float,false,true}));
    auto image=take(d.createTexture({side,side}));auto f=a->footprint();
    auto upload=take(d.createBuffer({f.size,gfx::Memory::Upload}));std::vector<std::byte> white(f.size,std::byte{255});take(upload->write(0,white));
    auto cb=take(d.createBuffer({512,gfx::Memory::Upload}));const std::array<float,4> color{index%2?0.f:1.f,index%2?1.f:0.f,0,1};take(cb->write(0,std::as_bytes(std::span{color})));take(cb->write(256,std::as_bytes(std::span{color})));
    auto input=take(d.createBuffer({copyBytes,gfx::Memory::Upload})),device=take(d.createBuffer({copyBytes}));std::vector<std::byte> pattern(copyBytes,std::byte(index%251));take(input->write(0,pattern));
    Frame r{take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({f.size,gfx::Memory::Readback})),take(d.createBuffer({copyBytes,gfx::Memory::Readback})),f,{},index};
    auto* c=&commands;take(c->begin());take(c->transition(image,S::CopyDestination));take(c->copyBufferToTexture(upload,image));take(c->transition(image,S::ShaderRead));take(c->transition(cb,S::ConstantBuffer));
    take(c->transition(a,S::RenderTarget));take(c->transition(b,S::RenderTarget));take(c->transition(z,S::DepthWrite));take(c->beginRenderPass({{{a},{b}},{z}}));take(c->setPipeline(pipeline));
    take(c->bindResources({{{0,0,image}},{{1,0,cb,0,256}}}));take(c->setScissor(0,0,side/2,side));take(c->draw(3));
    take(c->bindResources({{{0,0,image}},{{1,0,cb,256,256}}}));take(c->setScissor(side/2,0,side/2,side));take(c->draw(3));take(c->endRenderPass());
    take(c->transition(a,S::CopySource));take(c->copyTextureToBuffer(a,r.a));take(c->transition(b,S::CopySource));take(c->copyTextureToBuffer(b,r.b));take(c->transition(z,S::CopySource));take(c->copyTextureToBuffer(z,r.z));
    take(c->transition(device,S::CopyDestination));take(c->copyBuffer(input,device,copyBytes));take(c->transition(device,S::CopySource));take(c->copyBuffer(device,r.copy,copyBytes));
    take(c->writeTimestamp(0));take(c->writeTimestamp(1));take(c->resolveTimestamps());take(c->end());r.fence=take(d.queue().submit(*c));return r;
}
}
int main(int argc,char** argv)try{
    check(argc==4,"Usage: memory_validation_probe case seconds output.jsonl");const std::string mode=argv[1],output=argv[3];const unsigned seconds=std::stoul(argv[2]);
    check((mode=="reuse-pipeline"||mode=="reuse-commands"||mode=="churn-pipeline"||mode=="counter-contract")&&seconds>0&&seconds<=120,"Invalid diagnostic case");std::ifstream exists(output);check(!exists.good(),"Refuse overwrite");exists.close();std::ofstream out(output);out.exceptions(std::ios::badbit|std::ios::failbit);out<<std::setprecision(12);
    auto d=take(gfx::createDevice({gfx::Backend::D3D12,true}));Observer observer(d->caps());ComPtr<ID3D12Device> native;hr(D3D12CreateDevice(observer.adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&native)));ComPtr<ID3D12InfoQueue> info;hr(native.As(&info));
    if(mode=="counter-contract"){
        hr(info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,FALSE));hr(info->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION,FALSE));hr(info->SetMessageCountLimit(8));
        for(unsigned i=0;i<64;++i)hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_WARNING,"Intentional queue saturation"));
        check(info->GetNumMessagesDiscardedByMessageCountLimit()>0&&take(d->validationErrorCount())==0,"Warning overflow does not invent errors");
        hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"Intentional overflowed error"));check(take(d->validationErrorCount())==1,"Overflowed error must be counted");
        info->ClearStoredMessages();check(take(d->validationErrorCount())==1,"Clearing native text cannot reset cumulative errors");
        D3D12_MESSAGE_SEVERITY denied[]{D3D12_MESSAGE_SEVERITY_ERROR,D3D12_MESSAGE_SEVERITY_CORRUPTION};D3D12_INFO_QUEUE_FILTER filter{};filter.DenyList.NumSeverities=2;filter.DenyList.pSeverityList=denied;hr(info->AddStorageFilterEntries(&filter));hr(info->AddRetrievalFilterEntries(&filter));
        hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"Intentional filtered error"));hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_CORRUPTION,"Intentional filtered corruption"));check(take(d->validationErrorCount())==3,"Filters cannot hide ERROR/CORRUPTION");
        HRESULT workerResult=E_FAIL;std::thread worker([&]{workerResult=info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"Intentional worker-thread error");});worker.join();hr(workerResult);check(take(d->validationErrorCount())==4,"Worker callback is thread safe");
        {auto second=take(gfx::createDevice({gfx::Backend::D3D12,true}));check(take(second->validationErrorCount())==0,"Independent registration starts at zero");hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"Intentional shared native device error"));check(take(d->validationErrorCount())==5&&take(second->validationErrorCount())==1,"Shared native owners keep independent callback lifetimes");}
        hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"Intentional error after other owner destruction"));check(take(d->validationErrorCount())==6,"Other owner teardown preserves callback");
        d.reset();hr(info->AddApplicationMessage(D3D12_MESSAGE_SEVERITY_ERROR,"No callback context after SDK destruction"));
        out<<"{\"counter_contract_passed\":true,\"expected_injected_errors\":6,\"overflow_filter_clear_worker_shared_owner_teardown\":true,\"sdk_sha256\":"<<quote(gfx::buildFingerprint())<<"}\n";out.flush();std::cout<<"PASS: 6 intentional errors counted across overflow/filter/clear/worker/shared-owner teardown\n";return 0;
    }
    const auto pd=pipelineDesc();auto p=take(d->createGraphicsPipeline(pd));std::vector<std::unique_ptr<gfx::CommandList>> pool;if(mode!="reuse-pipeline")for(unsigned i=0;i<ring;++i)pool.push_back(take(d->createCommandList()));
    auto start=Clock::now();std::uint64_t frames=0;double next=0;
    auto sample=[&](std::string_view phase){out<<"{\"phase\":"<<quote(phase)<<",\"elapsed\":"<<std::chrono::duration<double>(Clock::now()-start).count()<<",\"frames\":"<<frames<<",\"stored_messages\":"<<info->GetNumStoredMessages()<<",\"discarded_messages\":"<<info->GetNumMessagesDiscardedByMessageCountLimit();writeStats(out,take(d->resourceStatistics()));observer.write(out);out<<"}\n";out.flush();};
    out<<"{\"diagnostic_only\":true,\"case\":"<<quote(mode)<<",\"sdk_sha256\":"<<quote(gfx::buildFingerprint())<<",\"seconds\":"<<seconds<<"}\n";sample("baseline");
    while(std::chrono::duration<double>(Clock::now()-start).count()<seconds){
        if(mode=="churn-pipeline")p=take(d->createGraphicsPipeline(pd));std::vector<Frame> batch;
        for(unsigned i=0;i<ring;++i)batch.push_back(pool.empty()?submit(*d,p,static_cast<unsigned>(frames+i)):pooledSubmit(*d,p,static_cast<unsigned>(frames+i),*pool[i]));
        take(d->queue().wait(batch.back().fence));for(const auto& r:batch)verify(r);frames+=ring;batch.clear();
        for(auto& c:pool){take(c->begin());take(c->end());}take(d->queue().waitIdle());
        const auto t=std::chrono::duration<double>(Clock::now()-start).count();if(t>=next){sample("drained");next+=1;}
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    pool.clear();p.reset();take(d->queue().waitIdle());sample("released");auto errors=take(d->validationErrorCount());check(errors==0,"Diagnostic workload native validation errors");
    // Save the native external-reference view before clearing messages. Do not hide errors.
    ComPtr<ID3D12DebugDevice> debug;hr(native.As(&debug));hr(debug->ReportLiveDeviceObjects(static_cast<D3D12_RLDO_FLAGS>(D3D12_RLDO_DETAIL|D3D12_RLDO_IGNORE_INTERNAL)));
    std::ofstream messages(output+".native.txt");messages.exceptions(std::ios::badbit|std::ios::failbit);
    for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size{};hr(info->GetMessage(i,nullptr,&size));std::vector<std::byte> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());hr(info->GetMessage(i,message,&size));messages<<message->ID<<" severity="<<message->Severity<<" "<<message->pDescription<<'\n';}
    info->ClearStoredMessages();sample("after_message_clear");
    out<<"{\"functional_passed\":true,\"memory_acceptance_claim\":false,\"validation_errors\":"<<errors<<",\"frames\":"<<frames<<",\"full_images\":"<<frames*3<<"}\n";out.flush();std::cout<<"PASS diagnostic references: "<<mode<<" frames="<<frames<<" (not soak acceptance)\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
