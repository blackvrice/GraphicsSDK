// Debug only a newly launched child. No attach, global policy, symbol server or SDK dependency.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dbghelp.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(std::string(text)+"; win32="+std::to_string(GetLastError()));}
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h=nullptr):value(h){}
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
std::string utf8(std::wstring_view text){
    if(text.empty())return {};
    const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    check(n>0,"UTF8 size");std::string out(n,'\0');check(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),out.data(),n,nullptr,nullptr)==n,"UTF8 text");return out;
}
std::string json(std::string_view value){
    std::ostringstream out;out<<'"';constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:value){if(c=='"'||c=='\\')out<<'\\'<<char(c);else if(c<32)out<<"\\u00"<<hex[c>>4]<<hex[c&15];else out<<char(c);}out<<'"';return out.str();
}
std::wstring quote(std::wstring_view value){
    std::wstring out=L"\"";unsigned slashes=0;
    for(wchar_t c:value){if(c==L'\\'){++slashes;continue;}out.append(c==L'"'?slashes*2+1:slashes,L'\\');slashes=0;out+=c;}
    out.append(slashes*2,L'\\');return out+L'"';
}
std::uint64_t integer(std::wstring_view value){
    check(!value.empty(),"Missing decimal value");std::uint64_t result=0;
    for(auto c:value){check(c>=L'0'&&c<=L'9'&&result<=(UINT64_MAX-(c-L'0'))/10,"Invalid decimal value");result=result*10+c-L'0';}return result;
}
struct Log {
    Handle file;
    explicit Log(const std::filesystem::path& path):file(CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)){check(file.value!=INVALID_HANDLE_VALUE,"Refuse existing/invalid capture output");}
    void row(const std::string& text){const auto line=text+'\n';DWORD written{};check(WriteFile(file.value,line.data(),static_cast<DWORD>(line.size()),&written,nullptr)&&written==line.size(),"Capture write");check(FlushFileBuffers(file.value)!=FALSE,"Capture flush");}
};
std::wstring filePath(HANDLE file){
    if(!file||file==INVALID_HANDLE_VALUE)return {};
    std::vector<wchar_t> name(32768);const auto n=GetFinalPathNameByHandleW(file,name.data(),static_cast<DWORD>(name.size()),FILE_NAME_NORMALIZED);
    return n&&n<name.size()?std::wstring(name.data(),n):std::wstring{};
}
template<class T>bool read(HANDLE process,DWORD64 address,T& value){SIZE_T bytes{};return ReadProcessMemory(process,reinterpret_cast<const void*>(address),&value,sizeof(value),&bytes)&&bytes==sizeof(value);}
struct Module {DWORD64 base{},size{};std::wstring path;};
Module module(HANDLE process,void* base,HANDLE file){
    Module result{reinterpret_cast<DWORD64>(base),0,filePath(file)};IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(read(process,result.base,dos)&&dos.e_magic==IMAGE_DOS_SIGNATURE&&dos.e_lfanew>0&&dos.e_lfanew<1024*1024
       &&read(process,result.base+dos.e_lfanew,nt)&&nt.Signature==IMAGE_NT_SIGNATURE&&nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC)result.size=nt.OptionalHeader.SizeOfImage;
    return result;
}
void address(std::ostream& out,DWORD64 pc,const std::map<DWORD64,Module>& modules){
    out<<"\"address\":"<<pc;
    auto it=modules.upper_bound(pc);
    if(it!=modules.begin()){--it;const auto& m=it->second;if(pc-m.base<m.size){out<<",\"module\":"<<json(utf8(m.path))<<",\"module_base\":"<<m.base<<",\"module_offset\":"<<pc-m.base;return;}}
    out<<",\"module\":null,\"module_base\":null,\"module_offset\":null";
}
constexpr unsigned threadLimit=16,frameLimit=64;
struct Symbols {
    HANDLE process;bool available;
    Symbols(HANDLE p,const std::map<DWORD64,Module>& modules,const std::filesystem::path& search):process(p){
        SymSetOptions(SYMOPT_DEFERRED_LOADS|SYMOPT_UNDNAME|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS|SYMOPT_IGNORE_NT_SYMPATH);
        available=SymInitializeW(process,search.c_str(),FALSE)!=FALSE;
        if(available)for(const auto& [base,m]:modules)if(!m.path.empty())(void)SymLoadModuleExW(process,nullptr,m.path.c_str(),nullptr,base,static_cast<DWORD>(m.size),nullptr,0);
    }
    ~Symbols(){if(available)(void)SymCleanup(process);}
};
unsigned stack(Log& log,HANDLE process,HANDLE thread,DWORD tid,CONTEXT context,const std::map<DWORD64,Module>& modules,bool symbols,unsigned snapshot,bool fault){
    auto frame=[&](DWORD64 pc,unsigned index,const char* kind){
        std::ostringstream row;row<<"{\"event\":\"stack\",\"snapshot_id\":"<<snapshot<<",\"thread_id\":"<<tid<<",\"index\":"<<index<<",\"kind\":"<<json(kind)<<',';address(row,pc,modules);
        alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};auto* symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=MAX_SYM_NAME;DWORD64 displacement{};
        if(symbols&&SymFromAddr(process,pc,&displacement,symbol))row<<",\"symbol\":"<<json(std::string_view(symbol->Name,symbol->NameLen))<<",\"symbol_displacement\":"<<displacement;
        else row<<",\"symbol\":null,\"symbol_displacement\":null";
        row<<'}';log.row(row.str());
    };
    frame(context.Rip,0,fault?"exception_pc":"thread_pc");unsigned index=1;
    // A null function call has no unwind metadata. The stack top is a candidate
    // return PC, explicitly labelled, rather than an invented resolved fault module.
    if(fault&&context.Rip==0){DWORD64 candidate{};if(read(process,context.Rsp,candidate)&&candidate){frame(candidate,index++,"null_pc_stack_top_candidate");context.Rip=candidate;context.Rsp+=8;}}
    STACKFRAME64 f{};f.AddrPC={context.Rip,0,AddrModeFlat};f.AddrStack={context.Rsp,0,AddrModeFlat};f.AddrFrame={context.Rbp,0,AddrModeFlat};
    DWORD64 previousPC=UINT64_MAX,previousSP=UINT64_MAX;
    if(symbols)while(index<frameLimit&&StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&f,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)){
        if(!f.AddrPC.Offset||(f.AddrPC.Offset==previousPC&&f.AddrStack.Offset==previousSP))break;
        frame(f.AddrPC.Offset,index++,"unwound");previousPC=f.AddrPC.Offset;previousSP=f.AddrStack.Offset;
    }
    return index;
}
CONTEXT snapshot(Log& log,HANDLE process,DWORD fault,const std::map<DWORD,HANDLE>& threads,const std::map<DWORD64,Module>& modules,const std::filesystem::path& search,unsigned id,bool& faultAvailable){
    // Windows suspends all child threads for this debug event. Do not add suspend
    // counts or resume threads; ContinueDebugEvent owns that operation.
    Symbols symbols(process,modules,search);CONTEXT faultContext{};
    std::vector<DWORD> selected{fault};
    for(const auto& [tid,handle]:threads)if(tid!=fault&&selected.size()<threadLimit)selected.push_back(tid);
    unsigned available=0,frames=0;
    for(auto tid:selected){
        CONTEXT context{};context.ContextFlags=CONTEXT_FULL;const auto it=threads.find(tid);
        const bool have=it!=threads.end()&&GetThreadContext(it->second,&context);
        const DWORD error=have?0:it==threads.end()?ERROR_INVALID_HANDLE:GetLastError();
        if(tid==fault){faultContext=context;faultAvailable=have;}
        std::ostringstream row;row<<"{\"event\":"<<json(tid==fault?"context":"thread_context")<<",\"snapshot_id\":"<<id<<",\"thread_id\":"<<tid<<",\"available\":"<<(have?"true":"false")<<",\"win32_error\":"<<error<<",\"rip\":"<<context.Rip<<",\"rsp\":"<<context.Rsp<<",\"rbp\":"<<context.Rbp<<'}';log.row(row.str());
        if(have){++available;frames+=stack(log,process,it->second,tid,context,modules,symbols.available,id,tid==fault);}
    }
    const auto observed=threads.size()+(threads.contains(fault)?0:1),omitted=observed-selected.size();
    std::ostringstream row;row<<"{\"event\":\"thread_snapshot\",\"snapshot_id\":"<<id<<",\"fault_thread_id\":"<<fault<<",\"observed_threads\":"<<observed<<",\"captured_threads\":"<<selected.size()<<",\"omitted_threads\":"<<omitted<<",\"contexts_available\":"<<available<<",\"stack_frames\":"<<frames<<",\"symbols_available\":"<<(symbols.available?"true":"false")<<",\"all_threads_captured\":"<<(!omitted&&available==observed?"true":"false")<<'}';log.row(row.str());
    return faultContext;
}
}
int wmain(int argc,wchar_t** argv)try {
    std::filesystem::path output,dump;std::uint64_t timeout=0;int child=0;
    for(int i=1;i<argc;++i){std::wstring_view arg=argv[i];if(arg==L"--"){child=i+1;break;}check(i+1<argc,"Missing capture option value");const std::wstring value=argv[++i];
        if(arg==L"--output"){check(output.empty(),"Duplicate output");output=value;}
        else if(arg==L"--dump"){check(dump.empty(),"Duplicate dump");dump=value;}
        else if(arg==L"--timeout-ms"){check(timeout==0,"Duplicate timeout");timeout=integer(value);}
        else throw std::runtime_error("Unknown capture option");
    }
    check(!output.empty()&&timeout>=1&&timeout<=900000&&child>0&&child<argc,"Usage: --output fresh.jsonl --timeout-ms 1..900000 [--dump fresh.dmp] -- child.exe args");
    if(!dump.empty())check(!std::filesystem::exists(dump)&&std::filesystem::exists(dump.parent_path())&&std::filesystem::absolute(dump)!=std::filesystem::absolute(output),"Invalid/existing dump path");
    const auto exe=std::filesystem::absolute(argv[child]);std::wstring command=quote(exe.native());for(int i=child+1;i<argc;++i)command+=L" "+quote(argv[i]);
    Log log(output);const auto started=Clock::now();STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION pi{};
    check(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,DEBUG_ONLY_THIS_PROCESS|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&pi)!=FALSE,"Create diagnostic child");
    Handle process(pi.hProcess),initialThread(pi.hThread);
    check(DebugSetProcessKillOnExit(TRUE)!=FALSE,"Keep own-child kill-on-debugger-exit enabled");
    BOOL wow64{};check(IsWow64Process(process.value,&wow64)&&!wow64,"Only x64 child processes are supported");
    log.row("{\"event\":\"launch\",\"schema\":2,\"thread_limit\":"+std::to_string(threadLimit)+",\"frame_limit\":"+std::to_string(frameLimit)+",\"pid\":"+std::to_string(pi.dwProcessId)+",\"child\":"+json(utf8(exe.native()))+",\"timeout_ms\":"+std::to_string(timeout)+",\"debug_only_this_process\":true,\"diagnostic_only\":true}");
    std::map<DWORD,HANDLE> threads;std::map<DWORD64,Module> modules;bool loaderBreakpoint=false,timedOut=false,complete=false;DWORD exitCode=1;unsigned firstChance=0,secondChance=0,debugMessages=0;std::size_t debugBytes=0;bool dumpFailed=false;
    auto elapsed=[&]{return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-started).count();};
    while(!complete){
        if(!timedOut&&elapsed()>=static_cast<long long>(timeout)){timedOut=true;check(TerminateProcess(process.value,124)!=FALSE,"Terminate timed-out child");log.row("{\"event\":\"timeout\",\"elapsed_ms\":"+std::to_string(elapsed())+'}');}
        DEBUG_EVENT event{};
        if(!WaitForDebugEventEx(&event,100)){check(GetLastError()==ERROR_SEM_TIMEOUT,"WaitForDebugEventEx");check(!timedOut||elapsed()<static_cast<long long>(timeout)+10000,"Timed-out child did not exit");continue;}
        DWORD status=DBG_CONTINUE;
        switch(event.dwDebugEventCode){
        case CREATE_PROCESS_DEBUG_EVENT:{auto& e=event.u.CreateProcessInfo;threads[event.dwThreadId]=e.hThread;auto m=module(process.value,e.lpBaseOfImage,e.hFile);modules[m.base]=m;if(e.hFile)CloseHandle(e.hFile);break;}
        case CREATE_THREAD_DEBUG_EVENT:threads[event.dwThreadId]=event.u.CreateThread.hThread;break;
        case EXIT_THREAD_DEBUG_EVENT:threads.erase(event.dwThreadId);break;
        case LOAD_DLL_DEBUG_EVENT:{auto& e=event.u.LoadDll;auto m=module(process.value,e.lpBaseOfDll,e.hFile);modules[m.base]=m;if(e.hFile)CloseHandle(e.hFile);break;}
        case UNLOAD_DLL_DEBUG_EVENT:modules.erase(reinterpret_cast<DWORD64>(event.u.UnloadDll.lpBaseOfDll));break;
        case OUTPUT_DEBUG_STRING_EVENT:{
            // The advertised length is bytes (low 16 bits), including for Unicode.
            // Never read beyond it or let malformed optional text stop crash capture.
            const auto& e=event.u.DebugString;++debugMessages;const auto bytes=std::size_t(e.nDebugStringLength);
            if(bytes&&debugBytes+bytes<=65536){std::vector<char> text(bytes+2,0);SIZE_T got{};
                if(ReadProcessMemory(process.value,e.lpDebugStringData,text.data(),bytes,&got)&&got==bytes){
                    std::string value;bool malformed=false;
                    if(e.fUnicode){
                        std::wstring wide(bytes/2,L'\0');std::memcpy(wide.data(),text.data(),wide.size()*2);wide.resize(wide.find(L'\0')==std::wstring::npos?wide.size():wide.find(L'\0'));
                        const auto count=static_cast<int>(wide.size());
                        const int n=count?WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),count,nullptr,0,nullptr,nullptr):0;
                        malformed=(bytes%2)!=0||(count&&n==0);
                        const int safe=count?WideCharToMultiByte(CP_UTF8,0,wide.data(),count,nullptr,0,nullptr,nullptr):0;
                        if(safe>0){value.resize(safe);(void)WideCharToMultiByte(CP_UTF8,0,wide.data(),count,value.data(),safe,nullptr,nullptr);}
                    }else{
                        const auto end=std::find(text.begin(),text.begin()+bytes,'\0');const int count=static_cast<int>(end-text.begin());
                        const int size=count?MultiByteToWideChar(CP_ACP,0,text.data(),count,nullptr,0):0;
                        if(size>0){std::wstring wide(size,L'\0');(void)MultiByteToWideChar(CP_ACP,0,text.data(),count,wide.data(),size);value=utf8(wide);}
                    }
                    debugBytes+=bytes;log.row("{\"event\":\"debug_text\",\"malformed\":"+std::string(malformed?"true":"false")+",\"text\":"+json(value)+'}');
                }
            }break;
        }
        case EXCEPTION_DEBUG_EVENT:{
            auto& e=event.u.Exception;const bool first=e.dwFirstChance!=0;first?++firstChance:++secondChance;
            const bool initial=first&&!loaderBreakpoint&&e.ExceptionRecord.ExceptionCode==EXCEPTION_BREAKPOINT;
            if(initial)loaderBreakpoint=true;else status=DBG_EXCEPTION_NOT_HANDLED;
            if(!first||firstChance<=64){std::ostringstream row;row<<"{\"event\":\"exception\",\"thread_id\":"<<event.dwThreadId<<",\"code\":"<<e.ExceptionRecord.ExceptionCode<<",\"first_chance\":"<<(first?"true":"false")<<",\"loader_breakpoint\":"<<(initial?"true":"false")<<',';address(row,reinterpret_cast<DWORD64>(e.ExceptionRecord.ExceptionAddress),modules);
                row<<",\"parameters\":[";for(DWORD n=0;n<std::min<DWORD>(e.ExceptionRecord.NumberParameters,EXCEPTION_MAXIMUM_PARAMETERS);++n){if(n)row<<',';row<<e.ExceptionRecord.ExceptionInformation[n];}row<<"]}";log.row(row.str());}
            if(!first){
                for(const auto& [base,m]:modules)log.row("{\"event\":\"module\",\"base\":"+std::to_string(base)+",\"size\":"+std::to_string(m.size)+",\"path\":"+json(utf8(m.path))+'}');
                bool have=false;auto context=snapshot(log,process.value,event.dwThreadId,threads,modules,exe.parent_path(),secondChance,have);
                if(!dump.empty()){
                    Handle file(CreateFileW(dump.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));EXCEPTION_POINTERS pointers{&e.ExceptionRecord,&context};MINIDUMP_EXCEPTION_INFORMATION info{event.dwThreadId,&pointers,FALSE};
                    const bool ok=have&&file.value!=INVALID_HANDLE_VALUE&&MiniDumpWriteDump(process.value,pi.dwProcessId,file.value,static_cast<MINIDUMP_TYPE>(MiniDumpNormal|MiniDumpWithThreadInfo|MiniDumpWithUnloadedModules),&info,nullptr,nullptr);
                    const DWORD error=ok?0:GetLastError();dumpFailed=!ok;log.row("{\"event\":\"dump\",\"written\":"+std::string(ok?"true":"false")+",\"win32_error\":"+std::to_string(error)+'}');
                }
            }break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:exitCode=event.u.ExitProcess.dwExitCode;complete=true;break;
        default:break;
        }
        check(ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status)!=FALSE,"ContinueDebugEvent");
    }
    check(WaitForSingleObject(process.value,10000)==WAIT_OBJECT_0,"Reap child");
    log.row("{\"event\":\"exit\",\"child_exit_code\":"+std::to_string(exitCode)+",\"first_chance_count\":"+std::to_string(firstChance)+",\"second_chance_count\":"+std::to_string(secondChance)+",\"debug_message_count\":"+std::to_string(debugMessages)+",\"debug_text_bytes\":"+std::to_string(debugBytes)+",\"timed_out\":"+(timedOut?"true":"false")+",\"elapsed_ms\":"+std::to_string(elapsed())+",\"capture_complete\":true,\"dump_failed\":"+(dumpFailed?"true":"false")+",\"diagnostic_only\":true}");
    return dumpFailed?125:static_cast<int>(exitCode);
}catch(const std::exception& error){std::cerr<<"FAIL capture: "<<error.what()<<'\n';return 125;}
