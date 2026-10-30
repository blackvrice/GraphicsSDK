"""Generate the Linux platform/crypto adapter; preserve frozen LLGL upload/control bodies."""
import argparse
import hashlib
import pathlib
import re
from linux_comparison import ROOT, read, save, sha, require

OWN = ('benchmarks/llgl_vulkan_upload/main.cpp','benchmarks/llgl_vulkan_upload/Context.hpp',
       'benchmarks/llgl_vulkan_upload/Payload.hpp','benchmarks/linux_llgl_upload/CMakeLists.txt',
       'benchmarks/common/LinuxEnvironment.hpp','benchmarks/competitors/sources.lock.json',
       'benchmarks/phase3_comparison/upload.json','tools/port_linux_llgl_upload.py')


def translate(main, context):
    changes=[]
    def replace(text, old, new, label):
        require(text.count(old)==1,'Frozen source changed at '+label); changes.append(label)
        return text.replace(old,new)
    main=replace(main,'#include "Context.hpp"','#include "Platform.hpp"','Linux platform include')
    main=replace(main,'#include <bcrypt.h>','#include <openssl/sha.h>','system crypto include')
    start=main.index('std::string sha256(');end=main.index('std::string validationControl(',start)
    main=replace(main,main[start:end],'''std::string sha256(std::span<const std::byte> data){
    std::array<unsigned char,32> digest{};
    require(SHA256(reinterpret_cast<const unsigned char*>(data.data()),data.size(),digest.data())!=nullptr,"SHA256 failed");
    return hex(digest);
}
''','system SHA256 implementation')
    main=replace(main,'llgl_vulkan_upload EXPECTED_GPU','linux_llgl_upload EXPECTED_UUID','Linux argument description')
    main=replace(main,'P3-CMP-03','P3-LBEN-01-LLGL-readiness','Linux diagnostic task')
    context=replace(context,'#include <Windows.h>\n#include <TlHelp32.h>',
        '#include <vulkan/vulkan.h>\n#include "LinuxEnvironment.hpp"\n#include <cstdlib>','Linux native helper')
    start=context.index('inline std::string module(');end=context.index('inline std::atomic<unsigned> errors',start)
    context=replace(context,context[start:end],'''inline std::string module(const char* name){
    std::vector<std::string> found;for(const auto& p:linuxbench::modules())if(std::filesystem::path(p).filename()==name)found.push_back(p);
    require(found.size()==1,"Required runtime module absent or ambiguous");return found.front();
}
inline std::string modules(){std::string out="[";for(const auto& p:linuxbench::modules()){if(out.size()>1)out+=',';out+=quote(p);}return out+"]";}
''','Linux module inventory')
    context=replace(context,'struct Context {\n    LLGL::RenderSystemPtr renderer;',
        'struct Context {\n    linuxbench::NativeQuery query;\n    linuxbench::Identity selected;\n    LLGL::RenderSystemPtr renderer;','native selection lifetime')
    context=replace(context,'Context(bool validation,const std::string& expected){',
        '''Context(bool validation,const std::string& expected){
        const char* kind=std::getenv("GFX_LLGL_UPLOAD_KIND");require(!kind||std::string_view(kind)=="software-diagnostic","Software diagnostics only");
        selected=linuxbench::select(query.enumerate(validation),expected,"software-diagnostic");''','exact software UUID guard')
    context=replace(context,'desc.flags=LLGL::RenderSystemFlags::PreferNVIDIA;',
        'desc.flags=0;','remove vendor preference')
    old='require(expected==properties.properties.deviceName,"Wrong target GPU; no fallback");require(properties.properties.vendorID==4318&&properties.properties.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU&&id.deviceLUIDValid,"Discrete NVIDIA and LUID required");'
    context=replace(context,old,'''require(hex(id.deviceUUID)==selected.uuid&&hex(id.driverUUID)==selected.driverUuid
            &&selected.name==properties.properties.deviceName&&selected.type==properties.properties.deviceType
            &&selected.vendor==properties.properties.vendorID&&selected.device==properties.properties.deviceID
            &&selected.driver==properties.properties.driverVersion&&selected.api==properties.properties.apiVersion,"Actual LLGL target differs from qualification");''','actual renderer identity')
    context=replace(context,'loader=module(GetModuleHandleW(L"vulkan-1.dll"));core=module(GetModuleHandleW(L"LLGL.dll"));backend=module(GetModuleHandleW(L"LLGL_Vulkan.dll"));',
        'loader=query.loaderPath;core=module("libLLGL.so");backend=module("libLLGL_Vulkan.so");','Linux renderer libraries')
    context=replace(context,'loadedLayer=module(GetModuleHandleW(L"VkLayer_khronos_validation.dll"));',
        'loadedLayer=module("libVkLayer_khronos_validation.so");','Linux validation library')
    context=replace(context,'else require(GetModuleHandleW(L"VkLayer_khronos_validation.dll")==nullptr,"Unexpected validation module");',
        'else for(const auto& path:linuxbench::modules())require(path.find("VkLayer_khronos_validation")==std::string::npos,"Unexpected validation module");','plain validation absence')
    context=replace(context,'<<quote(hex(id.deviceLUID))','<<"null"','Linux null LUID')
    context=replace(context,'<<quote(loadedLayer);return o.str();',
        '''<<quote(loadedLayer)<<",\\\"selected\\\":"<<selected.json()<<",\\\"environment\\\":"<<linuxbench::environment()
         <<",\\\"mode\\\":\\\"software-diagnostic\\\",\\\"unmodified_upstream\\\":false,\\\"physical_GPU_accepted\\\":false,\\\"P3_LBEN_01_completed\\\":false";return o.str();''','Linux scope metadata')
    require(not any(x in main+context for x in ('GetModuleHandle','<Windows.h>','<TlHelp32.h>','BCrypt','<bcrypt.h>')), 'Windows API remains')
    return main,context,changes


def generate(output):
    require(output.resolve().is_relative_to(ROOT/'out'),'Generated files must remain under out/')
    original=(ROOT/OWN[0]).read_text(encoding='utf-8'); context=(ROOT/OWN[1]).read_text(encoding='utf-8')
    main,platform,changes=translate(original,context)
    begin=original.index('std::string validationControl(');end=original.index('int main(')
    require(original[begin:end]==main[main.index('std::string validationControl('):main.index('int main(')],'Upload/control body changed')
    output.mkdir(parents=True,exist_ok=True)
    (output/'main.cpp').write_text(main,encoding='utf-8',newline='\n')
    (output/'Platform.hpp').write_text(platform,encoding='utf-8',newline='\n')
    inputs=[dict(path=n,sha256=sha(ROOT/n)) for n in sorted(OWN)]
    identity='linux-llgl-upload|'+read(ROOT/'benchmarks/competitors/sources.lock.json')['llgl']['commit']+'|llgl-fixed-pin-transfer-src-v1\n'
    identity+=''.join(x['path']+'|'+x['sha256']+'\n' for x in inputs)
    manifest=dict(schema_version=1,inputs=inputs,adaptations=changes,workload_bodies_changed=False,
        source_fingerprint=hashlib.sha256(identity.encode()).hexdigest(),
        generated={n:sha(output/n) for n in ('main.cpp','Platform.hpp')})
    save(output/'adaptation.json',manifest);return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=pathlib.Path,required=True)
    generate(parser.parse_args().output)
