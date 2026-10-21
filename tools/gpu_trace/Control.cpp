#include "common/Adapter.hpp"
#include "common/Telemetry.hpp"
#include <LLGL/LLGL.h>
#include <LLGL/Backend/Direct3D12/NativeHandle.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <Windows.h>
#include <array>
#include <iostream>

namespace {
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
long long qpc(){LARGE_INTEGER n;require(QueryPerformanceCounter(&n)!=0,"QPC");return n.QuadPart;}
}
int main()try{
    LARGE_INTEGER frequency;require(QueryPerformanceFrequency(&frequency)!=0,"QPC frequency");
    LLGL::RenderSystemDescriptor desc{"Direct3D12"};desc.flags=LLGL::RenderSystemFlags::DebugDevice;
    LLGL::Report report;auto device=LLGL::RenderSystem::Load(desc,&report);require(bool(device),report.GetText());
    LLGL::Direct3D12::RenderSystemNativeHandle native{};require(device->GetNativeHandle(&native,sizeof(native)),"Validation handle");
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;auto hr=native.device->QueryInterface(IID_PPV_ARGS(&info));native.device->Release();if(native.factory)native.factory->Release();if(native.commandQueue)native.commandQueue->Release();require(SUCCEEDED(hr),"Validation queue");
    auto check=[&]{for(UINT64 i=0;i<info->GetNumStoredMessages();++i){SIZE_T size=0;require(SUCCEEDED(info->GetMessage(i,nullptr,&size)),"Validation size");std::vector<std::byte> data(size);auto m=reinterpret_cast<D3D12_MESSAGE*>(data.data());require(SUCCEEDED(info->GetMessage(i,m,&size)),"Validation message");require(m->Severity>D3D12_MESSAGE_SEVERITY_ERROR,m->pDescription);}info->ClearStoredMessages();};
    constexpr unsigned bytes=32*1024*1024;
    std::vector<unsigned> source(bytes/4),readback(bytes/4);for(unsigned i=0;i<source.size();++i)source[i]=i*19+3;
    auto command=device->CreateCommandBuffer();require(command!=nullptr,"Command buffer");
    const auto name=device->GetRendererInfo().deviceName;const auto env=bench::environment(name.c_str());bench::Telemetry observer(name.c_str(),3,false);
    std::array<std::array<long long,3>,3> intervals{};observer.begin();
    for(unsigned i=0;i<3;++i){
        auto& t=intervals[i];t[0]=qpc();
        LLGL::BufferDescriptor d;d.size=bytes;d.bindFlags=LLGL::BindFlags::CopySrc|LLGL::BindFlags::CopyDst;
        auto a=device->CreateBuffer(d,source.data()),b=device->CreateBuffer(d);require(a&&b,"Transient GPU buffers");
        command->Begin();command->CopyBuffer(*b,0,*a,0,bytes);command->End();device->GetCommandQueue()->Submit(*command);device->GetCommandQueue()->WaitIdle();
        device->ReadBuffer(*b,0,readback.data(),bytes);device->GetCommandQueue()->WaitIdle();require(readback==source,"Positive control full readback mismatch");t[1]=qpc();
        device->Release(*b);device->Release(*a);device->GetCommandQueue()->WaitIdle();Sleep(100);check();t[2]=qpc();observer.checkpoint(false,i);
    }
    auto telemetry=observer.finish();device->Release(*command);device.reset();check();
    std::cout<<"{\"revision\":1,\"kind\":\"transient_gpu_memory_positive_control\",\"pid\":"<<GetCurrentProcessId()<<",\"qpc_frequency\":"<<frequency.QuadPart
        <<",\"public_api\":\"LLGL_D3D12\",\"buffer_bytes\":"<<bytes<<",\"simultaneous_buffer_count\":2,\"reference_checks\":3,\"validation\":true,\"readback_verified\":true,\"intervals\":[";
    for(size_t i=0;i<intervals.size();++i){if(i)std::cout<<',';std::cout<<"{\"before_qpc\":"<<intervals[i][0]<<",\"live_qpc\":"<<intervals[i][1]<<",\"after_qpc\":"<<intervals[i][2]<<'}';}
    std::cout<<"],\"environment\":"<<env<<",\"telemetry\":"<<telemetry<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
