"""Generate Linux LLGL PSO consumer; preserve creation/use/public-release and control bodies."""
import argparse
import hashlib
import pathlib
import linux_llgl_workload_mode as policy
import port_linux_llgl_draw as draw
from linux_comparison import ROOT, read, save, sha, require

OWN=tuple(sorted(set(draw.OWN+(
    'benchmarks/llgl_vulkan_pso/main.cpp','benchmarks/llgl_vulkan_pso/Support.hpp',
    'benchmarks/vulkan_pso/Frame.hpp','benchmarks/phase3_comparison/pso.json',
    'benchmarks/linux_llgl_pso/CMakeLists.txt','tools/port_linux_llgl_pso.py'))))


def translate(main,support,context,layer,upload_main,draw_main, mode='software-diagnostic'):
    adapted,platform,layer,changes=draw.translate(draw_main,context,layer,upload_main)
    changes=['shared draw platform: '+x for x in changes]
    def replace(text,old,new,label):
        require(text.count(old)==1,'Frozen PSO source changed at '+label);changes.append(label)
        return text.replace(old,new)
    main=replace(main,'#include "Support.hpp"','#include "SupportLinux.hpp"','Linux support include')
    main=replace(main,'#include "../vulkan_pso/Frame.hpp"','#include "Frame.hpp"','original PSO frame include')
    main=replace(main,'#include "../../tests/fixtures/SpirvFixtures.hpp"','#include "SpirvFixtures.hpp"','original shader include')
    main=replace(main,'llgl_vulkan_pso EXPECTED_GPU','linux_llgl_pso EXPECTED_UUID','Linux arguments')
    main=replace(main,'P3-CMP-05','P3-LBEN-03-LLGL-readiness','Linux diagnostic task')
    main=replace(main,'<<quote(GFX_PSO_SHADER_SHA)',
        '<<quote(GFX_PSO_SHADER_SHA)<<",\\\"LLGL_commit\\\":"<<quote(GFX_LLGL_PIN)<<",\\\"dependency_profile\\\":"<<quote(GFX_LLGL_PROFILE)','dependency identity')
    support=replace(support,'#include "../llgl_vulkan_upload/Context.hpp"','#include "Platform.hpp"','shared Linux platform')
    support=replace(support,'#include "../llgl_vulkan_draw/Observer.hpp"','#include "Observer.hpp"','original observer header')
    support=replace(support,'#include <bcrypt.h>','#include <openssl/sha.h>','system crypto include')
    start=support.index('std::string sha256(');end=support.index('std::string validationControl(',start)
    frozen=draw_main[draw_main.index('std::string sha256('):draw_main.index('std::string validationControl(')]
    require(support[start:end]==frozen,'Frozen PSO support differs from shared draw platform')
    native=adapted[adapted.index('std::string sha256('):adapted.index('std::string validationControl(')]
    support=replace(support,frozen,native,'shared Linux crypto identity and observer')
    platform=replace(platform,'GFX_LLGL_DRAW_KIND','GFX_LLGL_PSO_KIND','PSO kind guard')
    platform=replace(platform,'P3_LBEN_02_completed','P3_LBEN_03_completed','PSO scope')
    require(not any(x in main+support+platform+layer for x in ('GetModuleHandle','BCrypt','__declspec','<Windows.h>')),'Windows API remains')
    platform,changes=policy.adapt(platform,changes,'pso',mode)
    return main,support,platform,layer,changes


def generate(output, mode='software-diagnostic'):
    require(output.resolve().is_relative_to(ROOT/'out'),'Generated files must remain under out/')
    source=lambda n:(ROOT/n).read_text(encoding='utf-8')
    original=source('benchmarks/llgl_vulkan_pso/main.cpp');support=source('benchmarks/llgl_vulkan_pso/Support.hpp')
    main,adapted,platform,layer,changes=translate(original,support,source('benchmarks/llgl_vulkan_upload/Context.hpp'),
        source('benchmarks/llgl_vulkan_draw/Layer.cpp'),source('benchmarks/llgl_vulkan_upload/main.cpp'),source('benchmarks/llgl_vulkan_draw/main.cpp'),mode=mode)
    begin=original.index('struct Totals');end=original.index('int main(')
    require(original[begin:end]==main[main.index('struct Totals'):main.index('int main(')],'PSO lifecycle bodies changed')
    require(support[support.index('std::string validationControl('):]==adapted[adapted.index('std::string validationControl('):],'Validation control body changed')
    output.mkdir(parents=True,exist_ok=True)
    outputs={'main.cpp':main,'SupportLinux.hpp':adapted,'Platform.hpp':platform,'Layer.cpp':layer}
    for n,text in outputs.items():(output/n).write_text(text,encoding='utf-8',newline='\n')
    inputs=[dict(path=n,sha256=sha(ROOT/n)) for n in OWN]
    identity=policy.prefix('pso',mode)+'|'+read(ROOT/'benchmarks/competitors/sources.lock.json')['llgl']['commit']+'|llgl-fixed-pin-transfer-src-v1\n'
    identity+=''.join(x['path']+'|'+x['sha256']+'\n' for x in inputs)
    manifest=dict(schema_version=1,inputs=inputs,adaptations=changes,workload_bodies_changed=False,
        source_fingerprint=hashlib.sha256(identity.encode()).hexdigest(),generated={n:sha(output/n) for n in outputs})
    if mode=='physical':manifest['mode']=mode
    save(output/'adaptation.json',manifest);return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--mode',choices=('software-diagnostic','physical'),default='software-diagnostic')
    args=parser.parse_args();generate(args.output,args.mode)
