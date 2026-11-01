"""Generate Linux LLGL resource consumer without changing allocation/copy/release bodies."""
import argparse
import hashlib
import pathlib
import port_linux_llgl_pso as pso
from linux_comparison import ROOT, read, save, sha, require

OWN=tuple(sorted(set(pso.OWN+(
    'benchmarks/llgl_vulkan_allocation/main.cpp','benchmarks/vulkan_allocation/Payload.hpp',
    'benchmarks/phase3_comparison/allocation.json','benchmarks/linux_llgl_allocation/CMakeLists.txt',
    'tools/port_linux_llgl_allocation.py'))))


def translate(main,pso_main,support,context,layer,upload_main,draw_main):
    _,adapted,platform,layer,changes=pso.translate(pso_main,support,context,layer,upload_main,draw_main)
    changes=['shared PSO support: '+x for x in changes]
    def replace(text,old,new,label):
        require(text.count(old)==1,'Frozen allocation source changed at '+label);changes.append(label)
        return text.replace(old,new)
    main=replace(main,'#include "../llgl_vulkan_pso/Support.hpp"','#include "SupportLinux.hpp"','Linux support include')
    main=replace(main,'#include "../vulkan_allocation/Payload.hpp"','#include "Payload.hpp"','original resource payload include')
    main=replace(main,'llgl_vulkan_allocation EXPECTED_GPU','linux_llgl_allocation EXPECTED_UUID','Linux arguments')
    main=replace(main,'P3-CMP-06','P3-LBEN-04-LLGL-readiness','Linux diagnostic task')
    main=replace(main,'<<quote(GFX_LLGL_ALLOCATION_FINGERPRINT)',
        '<<quote(GFX_LLGL_ALLOCATION_FINGERPRINT)<<",\\\"LLGL_commit\\\":"<<quote(GFX_LLGL_PIN)<<",\\\"dependency_profile\\\":"<<quote(GFX_LLGL_PROFILE)','dependency identity')
    platform=replace(platform,'GFX_LLGL_PSO_KIND','GFX_LLGL_ALLOCATION_KIND','resource kind guard')
    platform=replace(platform,'P3_LBEN_03_completed','P3_LBEN_04_completed','resource scope')
    return main,adapted,platform,layer,changes


def generate(output):
    require(output.resolve().is_relative_to(ROOT/'out'),'Generated files must remain under out/')
    source=lambda n:(ROOT/n).read_text(encoding='utf-8')
    original=source('benchmarks/llgl_vulkan_allocation/main.cpp');support=source('benchmarks/llgl_vulkan_pso/Support.hpp')
    main,adapted,platform,layer,changes=translate(original,source('benchmarks/llgl_vulkan_pso/main.cpp'),support,
        source('benchmarks/llgl_vulkan_upload/Context.hpp'),source('benchmarks/llgl_vulkan_draw/Layer.cpp'),
        source('benchmarks/llgl_vulkan_upload/main.cpp'),source('benchmarks/llgl_vulkan_draw/main.cpp'))
    require(original[original.index('struct Totals'):original.index('int main(')]==main[main.index('struct Totals'):main.index('int main(')],'Resource lifecycle bodies changed')
    require(support[support.index('std::string validationControl('):]==adapted[adapted.index('std::string validationControl('):],'Validation control body changed')
    output.mkdir(parents=True,exist_ok=True)
    outputs={'main.cpp':main,'SupportLinux.hpp':adapted,'Platform.hpp':platform,'Layer.cpp':layer}
    for n,text in outputs.items():(output/n).write_text(text,encoding='utf-8',newline='\n')
    inputs=[dict(path=n,sha256=sha(ROOT/n)) for n in OWN]
    identity='linux-llgl-allocation|'+read(ROOT/'benchmarks/competitors/sources.lock.json')['llgl']['commit']+'|llgl-fixed-pin-transfer-src-v1\n'
    identity+=''.join(x['path']+'|'+x['sha256']+'\n' for x in inputs)
    manifest=dict(schema_version=1,inputs=inputs,adaptations=changes,workload_bodies_changed=False,
        source_fingerprint=hashlib.sha256(identity.encode()).hexdigest(),generated={n:sha(output/n) for n in outputs})
    save(output/'adaptation.json',manifest);return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=pathlib.Path,required=True)
    generate(parser.parse_args().output)
