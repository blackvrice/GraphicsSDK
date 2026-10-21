#include "Adapter.hpp"
#include "Telemetry.hpp"
#include <Windows.h>
#include <psapi.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace bench {
namespace {
void require(bool success, const char* operation) {
    if (!success) throw std::runtime_error(std::string(operation)+" failed; native code="+std::to_string(GetLastError()));
}
void requireHr(HRESULT status, const char* operation) {
    if (FAILED(status)) throw std::runtime_error(std::string(operation)+" failed; HRESULT="+std::to_string(status));
}
std::uint64_t ticks(FILETIME t) { return (static_cast<std::uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime; }
struct CpuTimes { std::uint64_t kernel, user; };
CpuTimes cpuTimes() {
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0,"GetProcessTimes");
    return {ticks(kernel),ticks(user)};
}
}
struct Telemetry::State {
    struct Snapshot {
        bool live; unsigned sample;
        PROCESS_MEMORY_COUNTERS_EX cpu{};
        DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
    };
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
    std::string luid;
    std::vector<Snapshot> snapshots;
    CpuTimes startCpu{};
    Clock::time_point startWall;
    bool started=false, finished=false;
    unsigned expectedSamples; bool allocation;
    Snapshot read(bool live, unsigned sample) {
        Snapshot s{}; s.live=live; s.sample=sample; s.cpu.cb=sizeof(s.cpu);
        require(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&s.cpu),sizeof(s.cpu))!=0,"GetProcessMemoryInfo");
        requireHr(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&s.local),"QueryVideoMemoryInfo(local)");
        requireHr(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&s.nonlocal),"QueryVideoMemoryInfo(nonlocal)");
        return s;
    }
};
Telemetry::Telemetry(std::string_view selectedDevice, unsigned samples, bool allocation):state(std::make_unique<State>()) {
    Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
    requireHr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"CreateDXGIFactory1(telemetry)");
    unsigned matches=0;
    for (UINT i=0;;++i) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;
        auto status=factory->EnumAdapters1(i,&candidate);
        if (status==DXGI_ERROR_NOT_FOUND) break;
        requireHr(status,"EnumAdapters1(telemetry)");
        DXGI_ADAPTER_DESC1 desc{}; requireHr(candidate->GetDesc1(&desc),"GetDesc1(telemetry)");
        char name[512]{};
        require(WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,name,sizeof(name),nullptr,nullptr)>0,"Adapter UTF-8 conversion");
        if (selectedDevice!=name) continue;
        ++matches; requireHr(candidate.As(&state->adapter),"IDXGIAdapter3(telemetry)");
        state->luid=std::to_string(desc.AdapterLuid.HighPart)+":"+std::to_string(desc.AdapterLuid.LowPart);
    }
    if (matches!=1) throw std::runtime_error("Telemetry GPU must uniquely match the selected adapter");
    state->expectedSamples=samples; state->allocation=allocation;
    state->snapshots.reserve(1+static_cast<size_t>(samples)*(allocation?2:1));
}
Telemetry::~Telemetry()=default;
void Telemetry::begin() {
    if (state->started) throw std::runtime_error("Telemetry already started");
    state->snapshots.push_back(state->read(false,0));
    state->startWall=Clock::now(); state->startCpu=cpuTimes(); state->started=true;
}
void Telemetry::checkpoint(bool allocationLive, unsigned sample) {
    if (!state->started || state->finished) throw std::runtime_error("Telemetry is outside measurement");
    state->snapshots.push_back(state->read(allocationLive,sample));
}
std::string Telemetry::finish() {
    if (!state->started || state->finished) throw std::runtime_error("Telemetry is outside measurement");
    const auto endCpu=cpuTimes(); const auto wall=ms(state->startWall,Clock::now()); state->finished=true;
    if (endCpu.kernel<state->startCpu.kernel || endCpu.user<state->startCpu.user)
        throw std::runtime_error("CPU process counters reversed");
    if (state->snapshots.size()!=1+static_cast<size_t>(state->expectedSamples)*(state->allocation?2:1))
        throw std::runtime_error("Telemetry checkpoint count mismatch");
    const auto kernelMs=static_cast<double>(endCpu.kernel-state->startCpu.kernel)/10000.0;
    const auto userMs=static_cast<double>(endCpu.user-state->startCpu.user)/10000.0;
    const auto percent=cpuOneCorePercent(state->startCpu.kernel+state->startCpu.user,endCpu.kernel+endCpu.user,wall);
    std::ostringstream out; out<<std::setprecision(17);
    out<<"{\"revision\":1,\"scope\":\"measured_batch_after_warmup_before_post_readback_v1\",\"gpu_luid\":"<<quote(state->luid)
       <<",\"gpu_node\":0,\"cpu_normalization\":\"one_logical_cpu_100_percent_all_process_threads\",\"wall_ms\":"<<wall
       <<",\"kernel_ms\":"<<kernelMs<<",\"user_ms\":"<<userMs<<",\"cpu_ms\":"<<kernelMs+userMs<<",\"cpu_one_core_percent\":"<<percent
       <<",\"checkpoints\":[";
    for (size_t i=0;i<state->snapshots.size();++i) {
        const auto& s=state->snapshots[i]; if(i) out<<',';
        out<<"{\"phase\":"<<quote(i==0?"baseline":s.live?"allocation_live":"sample_end")<<",\"sample\":"<<s.sample
           <<",\"working_set_bytes\":"<<s.cpu.WorkingSetSize<<",\"private_commit_bytes\":"<<s.cpu.PrivateUsage
           <<",\"lifetime_peak_working_set_bytes\":"<<s.cpu.PeakWorkingSetSize<<",\"lifetime_peak_commit_bytes\":"<<s.cpu.PeakPagefileUsage
           <<",\"gpu_local_current_bytes\":"<<s.local.CurrentUsage<<",\"gpu_nonlocal_current_bytes\":"<<s.nonlocal.CurrentUsage
           <<",\"gpu_local_budget_bytes\":"<<s.local.Budget<<",\"gpu_nonlocal_budget_bytes\":"<<s.nonlocal.Budget<<'}';
    }
    out<<"]}"; return out.str();
}
}
