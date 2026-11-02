"""Port the frozen draw timer and observer adapter; preserve all timed bodies."""
import argparse
import hashlib
import pathlib
import port_linux_llgl_pso as base
from linux_comparison import ROOT,require,read,save,sha

OWN=tuple(sorted(set(base.OWN+('benchmarks/llgl_vulkan_draw_compare/main.cpp',
    'benchmarks/llgl_vulkan_draw_compare/Draw.hpp','benchmarks/llgl_vulkan_draw_compare/Identity.hpp',
    'benchmarks/common/Adapter.hpp','benchmarks/linux_draw_campaign/CMakeLists.txt',
    'tools/port_linux_draw_campaign.py','benchmarks/phase3_comparison/linux-draw-campaign.json'))))
OUTPUTS=('main.cpp','Draw.hpp','Identity.hpp','SupportLinux.hpp','Platform.hpp','Layer.cpp')


def identity(inputs,mode,pin):
    base.policy.prefix('draw',mode)
    return 'linux-draw-campaign|'+mode+'|'+pin+'|llgl-fixed-pin-transfer-src-v1\n'+''.join(x['path']+'|'+x['sha256']+'\n' for x in inputs)


def translate(sources,mode):
    _,support,platform,layer,changes=base.translate(*[sources[n] for n in (
        'benchmarks/llgl_vulkan_pso/main.cpp','benchmarks/llgl_vulkan_pso/Support.hpp',
        'benchmarks/llgl_vulkan_upload/Context.hpp','benchmarks/llgl_vulkan_draw/Layer.cpp',
        'benchmarks/llgl_vulkan_upload/main.cpp','benchmarks/llgl_vulkan_draw/main.cpp')],mode=mode)
    changes=['Linux shared support: '+x for x in changes]
    def replace(text,old,new,label):
        require(text.count(old)==1,'Frozen draw comparison changed at '+label);changes.append(label);return text.replace(old,new)
    main=sources['benchmarks/llgl_vulkan_draw_compare/main.cpp'];draw=sources['benchmarks/llgl_vulkan_draw_compare/Draw.hpp'];ident=sources['benchmarks/llgl_vulkan_draw_compare/Identity.hpp']
    for old,new in (('#include "../llgl_vulkan_pso/Support.hpp"','#include "SupportLinux.hpp"'),
                    ('#include "../vulkan_draw/Frame.hpp"','#include "Frame.hpp"'),
                    ('#include "../../tests/fixtures/SpirvFixtures.hpp"','#include "SpirvFixtures.hpp"'),
                    ('#include "../common/Adapter.hpp"','#include "Adapter.hpp"')):
        draw=replace(draw,old,new,'Linux draw include '+old)
    ident=replace(ident,'#include "../llgl_vulkan_pso/Support.hpp"','#include "SupportLinux.hpp"','Linux identity support')
    ident=replace(ident,'<<quote(hex(c.id.deviceLUID))','<<"null"','Linux null LUID')
    ident=replace(ident,'unsigned number(','''bool observerPresent(){auto paths=linuxbench::modules();return std::any_of(paths.begin(),paths.end(),[](auto& p){return std::filesystem::path(p).filename()=="libVkLayer_gfx_llgl_draw.so";});}
unsigned number(''','Linux actual observer lookup')
    main=replace(main,'llgl_vulkan_draw_compare EXPECTED_GPU','linux_draw_compare EXPECTED_UUID','Linux UUID argument')
    main=replace(main,'auto env=bench::environment(gpu);','auto env=linuxbench::environment();','native Linux environment')
    main=replace(main,'P3-CMP-08','P3-LBEN-02-draw-campaign','Linux campaign task')
    main=replace(main,'actual_LLGL_native_handles_and_unique_DXGI','actual_LLGL_native_handles_and_native_UUID','actual native attribution')
    token='GetModuleHandleW(L"VkLayer_gfx_llgl_draw.dll")==nullptr'
    require(main.count(token)==2,'Frozen observer absence guards changed')
    main=main.replace(token,'!observerPresent()');changes.append('Linux observer absence guards')
    main=replace(main,'<<quote(GFX_LLGL_DRAW_COMPARE_FINGERPRINT)',
        '<<quote(GFX_LLGL_DRAW_COMPARE_FINGERPRINT)<<",\\\"LLGL_commit\\\":"<<quote(GFX_LLGL_PIN)<<",\\\"mode\\\":"<<quote("'+mode+'")','compiled dependency and mode')
    platform=replace(platform,'GFX_LLGL_PSO_KIND','GFX_LLGL_DRAW_COMPARE_KIND','campaign native kind guard')
    platform=replace(platform,'P3_LBEN_03_completed','P3_LBEN_02_completed','campaign scope')
    old=sources['benchmarks/llgl_vulkan_draw_compare/Draw.hpp'];require(old[old.index('struct Draw {'):]==draw[draw.index('struct Draw {'):],'Draw/timing/readback/teardown body changed')
    old=sources['benchmarks/llgl_vulkan_draw_compare/main.cpp'];a=old.index('        for(unsigned i=0;');b=old.index('adapter.close();}',a)+len('adapter.close();}')
    require(old[a:b] in main,'Timed loop/boundaries or native observations changed')
    require(not any(t in main+draw+ident+support+platform+layer for t in ('GetModuleHandle','BCrypt','__declspec','<Windows.h>')),'Windows API remains')
    return dict(zip(OUTPUTS,(main,draw,ident,support,platform,layer))),changes


def generate(output,mode):
    require(output.resolve().is_relative_to(ROOT/'out'),'Generated source must remain under out/')
    sources={n:(ROOT/n).read_text(encoding='utf-8') for n in OWN};outputs,changes=translate(sources,mode)
    output.mkdir(parents=True,exist_ok=True)
    for name,text in outputs.items():(output/name).write_text(text,encoding='utf-8',newline='\n')
    inputs=[dict(path=n,sha256=sha(ROOT/n)) for n in OWN]
    fp=hashlib.sha256(identity(inputs,mode,read(ROOT/'benchmarks/competitors/sources.lock.json')['llgl']['commit']).encode()).hexdigest()
    value=dict(schema_version=1,mode=mode,inputs=inputs,adaptations=changes,workload_bodies_changed=False,timed_loop_changed=False,
        source_fingerprint=fp,generated={n:sha(output/n) for n in OUTPUTS})
    save(output/'adaptation.json',value);return value


if __name__=='__main__':
    a=argparse.ArgumentParser(description=__doc__);a.add_argument('--output',type=pathlib.Path,required=True)
    a.add_argument('--mode',choices=('physical','software-diagnostic'),required=True);v=a.parse_args();generate(v.output,v.mode)
