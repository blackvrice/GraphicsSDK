"""Build a fresh pinned Windows Vulkan LLGL probe; save all commands and raw logs."""
import argparse
import json
import os
import pathlib
import re
import subprocess
import sys
import time

from prepare_vulkan_comparison import ROOT, pin, read, record, require, save


def build(output, cmake):
    require(os.name == 'nt', 'This build profile requires Windows')
    output = output.resolve()
    require(output.is_relative_to(ROOT/'out') and not output.exists(), 'Fresh output under out/ required')
    output.mkdir(parents=True)
    locks = read(ROOT/'benchmarks/competitors/sources.lock.json')
    pin(ROOT/'.deps/LLGL', locks['llgl']['commit'])
    headers_pin = next(c['commit'] for c in read(ROOT/'tools/vulkan-validation.lock.json')['components'] if c['name']=='Vulkan-Headers')
    pin(ROOT/'.deps/Vulkan-Headers', headers_pin)
    versions = list(pathlib.Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC').glob('*'))
    require(versions, 'MSVC toolset unavailable')
    version = max(versions, key=lambda p: tuple(map(int,p.name.split('.'))))
    compiler_tools = version/'bin/Hostx64/x64'
    loader = pathlib.Path(os.environ['WINDIR'])/'System32/vulkan-1.dll'
    tools = dict(cmake=record(cmake), lib=record(compiler_tools/'lib.exe'), dumpbin=record(compiler_tools/'dumpbin.exe'), loader=record(loader))
    save(output/'tools.json', tools)
    commands = []
    env = os.environ.copy(); env['_CL_']=(env.get('_CL_','')+' /MP1').strip()
    def call(name, args):
        start=time.monotonic()
        with (output/(name+'.log')).open('wb') as log:
            result=subprocess.run([str(a) for a in args],cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
        commands.append(dict(name=name,argv=[str(a) for a in args],returncode=result.returncode,seconds=time.monotonic()-start))
        save(output/'commands.json',commands)
        print(name, 'exit=', result.returncode, flush=True)
        require(result.returncode==0, name+' failed; inspect saved log')
    call('loader-exports',[compiler_tools/'dumpbin.exe','/nologo','/exports',loader])
    exports = re.findall(r'^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(vk\w+)\s*$',(output/'loader-exports.log').read_text(),re.MULTILINE)
    require(len(exports)>100 and 'vkGetInstanceProcAddr' in exports and len(exports)==len(set(exports)), 'Invalid system loader export list')
    definition=output/'vulkan-1.def'
    definition.write_text('LIBRARY vulkan-1.dll\nEXPORTS\n'+'\n'.join(exports)+'\n',encoding='ascii',newline='\n')
    library=output/'vulkan-1.lib'
    call('loader-import',[compiler_tools/'lib.exe','/nologo','/machine:x64','/def:'+str(definition),'/out:'+str(library)])
    save(output/'loader-import.json',dict(kind='generated_import_only_no_loader_implementation',system_loader=record(loader),exports=len(exports),definition=record(definition),library=record(library),redistributed=False))
    call('configure',[cmake,'-S',ROOT/'benchmarks/llgl_vulkan_probe','-B',output/'build','-G','Visual Studio 17 2022','-A','x64',
        '-DCMAKE_CONFIGURATION_TYPES=Release','-DPython3_EXECUTABLE='+sys.executable,
        '-DVulkan_INCLUDE_DIR='+str(ROOT/'.deps/Vulkan-Headers/include'),'-DVulkan_LIBRARY='+str(library)])
    call('build',[cmake,'--build',output/'build','--config','Release','--target','llgl_vulkan_probe','--parallel','1'])
    return output


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--cmake',type=pathlib.Path,required=True)
    args=parser.parse_args()
    build(args.output,args.cmake.resolve())


if __name__=='__main__':main()
