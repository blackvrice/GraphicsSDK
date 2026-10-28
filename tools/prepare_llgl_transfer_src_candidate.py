"""Build an explicitly modified LLGL candidate without changing the pinned checkout."""
import argparse
import difflib
import pathlib
import os
import subprocess
import sys
import time

from prepare_vulkan_comparison import ROOT, pin, read, record, require, save
from llgl_vulkan_upload import PIN

RELATIVE='sources/Renderer/Vulkan/Command/VKCommandContext.cpp'


def patch(source):
    old='''        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;'''
    new='''        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;

'''+old
    require(source.count(old)==1,'Unexpected pinned source-access switch')
    source=source.replace(old,new)
    old='''        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;'''
    new='''        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;

'''+old
    require(source.count(old)==1,'Unexpected pinned destination-access switch')
    return source.replace(old,new)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--cmake',type=pathlib.Path,required=True);p.add_argument('--dependency-build',type=pathlib.Path,required=True);a=p.parse_args()
    out=a.output.resolve();dep=a.dependency_build.resolve();require(out.is_relative_to(ROOT/'out') and not out.exists(),'Fresh candidate output under out/ required');out.mkdir(parents=True)
    pin(ROOT/'.deps/LLGL',PIN)
    original=subprocess.check_output(['git','--no-optional-locks','-C',str(ROOT/'.deps/LLGL'),'show',PIN+':'+RELATIVE])
    changed=patch(original.decode());source=out/'VKCommandContext.cpp';source.write_bytes(changed.encode())
    (out/'transfer-src.patch').write_bytes(''.join(difflib.unified_diff(original.decode().splitlines(True),changed.splitlines(True),fromfile='a/'+RELATIVE,tofile='b/'+RELATIVE)).encode())
    (out/'VKCommandContext.original.cpp').write_bytes(original)
    save(out/'profile.json',dict(schema_version=1,profile='transfer-src-candidate',upstream_commit=PIN,unmodified_upstream=False,adopted_as_comparison_baseline=False,
        original_path=RELATIVE,original=record(out/'VKCommandContext.original.cpp'),overlay=record(source),patch=record(out/'transfer-src.patch'),reason='Missing TRANSFER_SRC layout source/destination transfer-read access and transfer pipeline stages',performance_accepted=False))
    cmake=a.cmake.resolve();commands=[]
    for name,args in [('configure',[cmake,'-S',ROOT/'benchmarks/llgl_vulkan_upload/candidate','-B',out/'build','-G','Visual Studio 17 2022','-A','x64',
        '-DCMAKE_CONFIGURATION_TYPES=Release','-DPython3_EXECUTABLE='+sys.executable,'-DGFX_LLGL_OVERLAY='+str(source),'-DVulkan_INCLUDE_DIR='+str(ROOT/'.deps/Vulkan-Headers/include'),'-DVulkan_LIBRARY='+str(dep/'vulkan-1.lib')]),
        ('build',[cmake,'--build',out/'build','--config','Release','--target','llgl_transfer_src_candidate','--parallel','1'])]:
        start=time.monotonic()
        env=os.environ.copy();env['_CL_']=(env.get('_CL_','')+' /MP1').strip()
        with (out/(name+'.log')).open('wb') as log:r=subprocess.run([str(x) for x in args],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        commands.append(dict(name=name,argv=[str(x) for x in args],returncode=r.returncode,seconds=time.monotonic()-start));save(out/'commands.json',commands);print('candidate',name,r.returncode,flush=True);require(r.returncode==0,'Candidate build failed')
    pin(ROOT/'.deps/LLGL',PIN)


if __name__=='__main__':main()
