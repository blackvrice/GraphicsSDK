"""Translate frozen LLGL draw and pass-through observer; never change render/control bodies."""
import argparse
import hashlib
import pathlib
import linux_llgl_workload_mode as policy
import port_linux_llgl_upload as upload
from linux_comparison import ROOT, read, save, sha, require

OWN=tuple(sorted(set(upload.OWN+(
    'benchmarks/llgl_vulkan_draw/main.cpp','benchmarks/llgl_vulkan_draw/Layer.cpp',
    'benchmarks/llgl_vulkan_draw/Observer.hpp','benchmarks/vulkan_draw/Frame.hpp',
    'tests/fixtures/SpirvFixtures.hpp','benchmarks/linux_llgl_draw/CMakeLists.txt',
    'benchmarks/phase3_comparison/draw.json','tools/port_linux_llgl_draw.py','tools/port_linux_llgl_upload.py'))))


def translate(main,context,layer,upload_main, mode='software-diagnostic'):
    _,platform,changes=upload.translate(upload_main,context)
    changes=['shared platform: '+x for x in changes]
    def replace(text,old,new,label):
        require(text.count(old)==1,'Frozen draw source changed at '+label);changes.append(label)
        return text.replace(old,new)
    main=replace(main,'#include "../llgl_vulkan_upload/Context.hpp"','#include "Platform.hpp"','Linux platform')
    main=replace(main,'#include "../vulkan_draw/Frame.hpp"','#include "Frame.hpp"','original frame include')
    main=replace(main,'#include "../../tests/fixtures/SpirvFixtures.hpp"','#include "SpirvFixtures.hpp"','original shader include')
    main=replace(main,'#include <bcrypt.h>','#include <openssl/sha.h>','system crypto')
    start=main.index('std::string sha256(');end=main.index('std::string identity(',start)
    main=replace(main,main[start:end],'''std::string sha256(std::span<const std::byte> data){
    std::array<unsigned char,32> digest{};
    require(SHA256(reinterpret_cast<const unsigned char*>(data.data()),data.size(),digest.data())!=nullptr,"SHA256 failed");
    return hex(digest);
}
''','system SHA256')
    start=main.index('std::string identity(');end=main.index('struct Observer {',start)
    main=replace(main,main[start:end],'std::string identity(Context& c){return c.identity();}\n','shared native identity')
    start=main.index('struct Observer {');end=main.index('std::string validationControl(',start)
    main=replace(main,main[start:end],'''struct Observer {
    void* library{};drawobserver::Read read{};drawobserver::Reset reset{};std::string path;
    Observer(){path=module("libVkLayer_gfx_llgl_draw.so");library=dlopen(path.c_str(),RTLD_NOW|RTLD_NOLOAD);
        require(library!=nullptr,"Native draw observer not active");
        read=reinterpret_cast<drawobserver::Read>(dlsym(library,"gfxDrawSnapshot"));reset=reinterpret_cast<drawobserver::Reset>(dlsym(library,"gfxDrawReset"));
        require(read&&reset,"Observer snapshot entry missing");auto s=snapshot();
        require(s.createdInstances==2&&s.createdDevices==1&&s.liveInstances==2&&s.liveDevices==1&&s.instanceApi!=0,"Native draw observer not active");}
    drawobserver::Snapshot snapshot(){drawobserver::Snapshot s{};require(read(&s,sizeof(s))!=0,"Native snapshot failed");return s;}
    ~Observer(){if(library)dlclose(library);}
};
''','Linux observer loading and enumeration lifetime')
    main=replace(main,'llgl_vulkan_draw EXPECTED_GPU','linux_llgl_draw EXPECTED_UUID','Linux arguments')
    main=replace(main,'P3-CMP-04','P3-LBEN-02-LLGL-readiness','Linux diagnostic task')
    main=replace(main,'<<quote(GFX_DRAW_SHADER_SHA)',
        '<<quote(GFX_DRAW_SHADER_SHA)<<",\\\"LLGL_commit\\\":"<<quote(GFX_LLGL_PIN)<<",\\\"dependency_profile\\\":"<<quote(GFX_LLGL_PROFILE)','dependency identity')
    platform=replace(platform,'GFX_LLGL_UPLOAD_KIND','GFX_LLGL_DRAW_KIND','draw kind guard')
    platform=replace(platform,'P3_LBEN_01_completed','P3_LBEN_02_completed','draw scope')
    platform=replace(platform,'instance_API_request_observed\\\":false','instance_API_request_observed\\\":true','observer records actual instance request')
    platform=replace(platform,'renderer.reset();','renderer.reset();query.close();','close both observed instances')
    layer=replace(layer,'#define EXPORT extern "C" __declspec(dllexport)',
        '#define EXPORT extern "C" __attribute__((visibility("default")))','Linux observer exports')
    # Identity enumeration creates one extra native instance, but no native device or draws.
    require(not any(x in main+platform+layer for x in ('GetModuleHandle','BCrypt','__declspec','<Windows.h>')),'Windows API remains')
    platform,changes=policy.adapt(platform,changes,'draw',mode)
    return main,platform,layer,changes


def generate(output, mode='software-diagnostic'):
    require(output.resolve().is_relative_to(ROOT/'out'),'Generated files must remain under out/')
    source=lambda n:(ROOT/n).read_text(encoding='utf-8')
    original=source('benchmarks/llgl_vulkan_draw/main.cpp')
    main,platform,layer,changes=translate(original,source('benchmarks/llgl_vulkan_upload/Context.hpp'),
        source('benchmarks/llgl_vulkan_draw/Layer.cpp'),source('benchmarks/llgl_vulkan_upload/main.cpp'),mode=mode)
    begin=original.index('std::string validationControl(');end=original.index('int main(')
    require(original[begin:end]==main[main.index('std::string validationControl('):main.index('int main(')],'Draw/control bodies changed')
    output.mkdir(parents=True,exist_ok=True)
    for n,text in [('main.cpp',main),('Platform.hpp',platform),('Layer.cpp',layer)]:
        (output/n).write_text(text,encoding='utf-8',newline='\n')
    inputs=[dict(path=n,sha256=sha(ROOT/n)) for n in OWN]
    identity=policy.prefix('draw',mode)+'|'+read(ROOT/'benchmarks/competitors/sources.lock.json')['llgl']['commit']+'|llgl-fixed-pin-transfer-src-v1\n'
    identity+=''.join(x['path']+'|'+x['sha256']+'\n' for x in inputs)
    manifest=dict(schema_version=1,inputs=inputs,adaptations=changes,workload_bodies_changed=False,
        source_fingerprint=hashlib.sha256(identity.encode()).hexdigest(),
        generated={n:sha(output/n) for n in ('main.cpp','Platform.hpp','Layer.cpp')})
    if mode=='physical':manifest['mode']=mode
    save(output/'adaptation.json',manifest);return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--mode',choices=('software-diagnostic','physical'),default='software-diagnostic')
    args=parser.parse_args();generate(args.output,args.mode)
