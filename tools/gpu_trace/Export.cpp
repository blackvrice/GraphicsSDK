#include <Windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Decode with the installed OS's TDH schema, never guessed offsets from a different OS version.
namespace {
constexpr GUID dxg{0x802ec45a,0x1e99,0x4b83,{0x99,0x20,0x87,0xc9,0x82,0x77,0xba,0x9d}};
struct Reader{std::ofstream events;std::string error;unsigned long long count=0,first=0,last=0;};
unsigned long long field(EVENT_RECORD* event,const wchar_t* name){
    PROPERTY_DATA_DESCRIPTOR d{};d.PropertyName=reinterpret_cast<ULONGLONG>(name);d.ArrayIndex=ULONG_MAX;
    ULONG size=0;auto status=TdhGetPropertySize(event,0,nullptr,1,&d,&size);
    if(status!=ERROR_SUCCESS||!(size==1||size==2||size==4||size==8))throw std::runtime_error("Missing or non-scalar TDH field");
    unsigned long long value=0;status=TdhGetProperty(event,0,nullptr,1,&d,size,reinterpret_cast<PBYTE>(&value));
    if(status!=ERROR_SUCCESS)throw std::runtime_error("TDH decoding failed");return value;
}
void WINAPI eventRecord(EVENT_RECORD* event){
    auto& reader=*static_cast<Reader*>(event->UserContext);if(!reader.error.empty())return;
    try{
        auto time=static_cast<unsigned long long>(event->EventHeader.TimeStamp.QuadPart);if(!reader.first)reader.first=time;reader.last=std::max(reader.last,time);
        if(!IsEqualGUID(event->EventHeader.ProviderId,dxg))return;
        auto id=event->EventHeader.EventDescriptor.Id;
        std::vector<std::pair<const char*,const wchar_t*>> fields;
        if(id==33||id==34||id==35)fields={{"pid",L"hProcessId"},{"adapter",L"pDxgAdapter"},{"handle",L"hVidMmGlobalAlloc"},{"bytes",L"allocSize"}};
        else if(id==225||id==226)fields={{"pid",L"ProcessId"},{"handle",L"Handle"},{"bytes",L"Size"},{"mapping",L"Mapping"},{"heap_block",L"IsHeapBlock"}};
        else if(id==367)fields={{"pid",L"ProcessId"},{"adapter",L"pDxgAdapter"},{"physical_adapter",L"PhysicalAdapterIndex"},{"segment_group",L"MemorySegmentGroup"},{"new_bytes",L"NewUsage"},{"old_bytes",L"OldUsage"}};
        else if(id==371)fields={{"pid",L"ProcessId"},{"adapter",L"pDxgAdapter"},{"physical_adapter",L"PhysicalAdapterIndex"},{"segment_group",L"MemorySegmentGroup"},{"new_bytes",L"Commitment"},{"old_bytes",L"OldCommitment"}};
        else return;
        std::vector<unsigned long long> values;for(auto [key,name]:fields){(void)key;values.push_back(field(event,name));}
        reader.events<<"{\"qpc\":"<<time<<",\"id\":"<<id<<",\"version\":"<<unsigned(event->EventHeader.EventDescriptor.Version)<<",\"opcode\":"<<unsigned(event->EventHeader.EventDescriptor.Opcode)<<",\"header_pid\":"<<event->EventHeader.ProcessId;
        for(size_t i=0;i<fields.size();++i)reader.events<<",\""<<fields[i].first<<"\":"<<values[i];reader.events<<"}\n";++reader.count;
    }catch(const std::exception& e){reader.error=e.what();}
}
}
int wmain(int argc,wchar_t** argv)try{
    if(argc!=4)throw std::runtime_error("Usage: gpu_etl_export <own-session.etl> <events.jsonl> <trace.json>");
    if(GetFileAttributesW(argv[2])!=INVALID_FILE_ATTRIBUTES||GetFileAttributesW(argv[3])!=INVALID_FILE_ATTRIBUTES)throw std::runtime_error("Preserve existing export files");
    Reader reader;reader.events.open(argv[2],std::ios::binary);if(!reader.events)throw std::runtime_error("Cannot create event export");
    EVENT_TRACE_LOGFILEW log{};log.LogFileName=argv[1];log.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD|PROCESS_TRACE_MODE_RAW_TIMESTAMP;log.EventRecordCallback=eventRecord;log.Context=&reader;
    auto trace=OpenTraceW(&log);if(trace==INVALID_PROCESSTRACE_HANDLE)throw std::runtime_error("OpenTrace failed: "+std::to_string(GetLastError()));
    auto status=ProcessTrace(&trace,1,nullptr,nullptr);CloseTrace(trace);reader.events.close();
    std::ofstream metadata(argv[3],std::ios::binary);metadata<<"{\"revision\":1,\"clock\":\"raw_QPC\",\"qpc_frequency\":"<<log.LogfileHeader.PerfFreq.QuadPart<<",\"clock_type\":"<<log.LogfileHeader.ReservedFlags
        <<",\"header_events_lost\":"<<log.LogfileHeader.EventsLost<<",\"consumer_events_lost\":"<<log.EventsLost<<",\"buffers_lost\":"<<log.LogfileHeader.BuffersLost<<",\"decode_failed\":"<<(!reader.error.empty()?"true":"false")
        <<",\"process_trace_status\":"<<status<<",\"exported_events\":"<<reader.count<<",\"first_qpc\":"<<reader.first<<",\"last_qpc\":"<<reader.last<<"}\n";metadata.close();
    if(!reader.error.empty())throw std::runtime_error(reader.error);
    if(status!=ERROR_SUCCESS||log.LogfileHeader.EventsLost||log.EventsLost||log.LogfileHeader.BuffersLost||!reader.count)throw std::runtime_error("Incomplete, empty or lossy ETL");
    std::cout<<"Decoded "<<reader.count<<" selected DxgKrnl events\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
