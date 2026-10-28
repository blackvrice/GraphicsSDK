"""Deterministic Linux adapters for frozen Windows workload bodies; no source edits."""
import argparse
import hashlib
import pathlib
import re
from linux_comparison import ROOT,read,save,sha,require

PROFILES=('upload','draw','pso','allocation')

def paths(profile):
    require(profile in PROFILES,'Unknown workload profile')
    own=list((ROOT/('benchmarks/vulkan_'+profile)).glob('*.cpp'))+list((ROOT/('benchmarks/vulkan_'+profile)).glob('*.hpp'))
    own += [ROOT/n for n in ('benchmarks/common/Adapter.hpp','benchmarks/common/LinuxEnvironment.hpp','benchmarks/linux_workloads/Platform.hpp','benchmarks/linux_workloads/Environment.cpp','benchmarks/linux_workloads/CMakeLists.txt','tools/port_linux_benchmarks.py','benchmarks/competitors/sources.lock.json')]
    if profile in ('draw','pso'):own += [ROOT/n for n in ('tests/fixtures/SpirvFixtures.hpp','tests/fixtures/spirv/fullscreen.spvasm','tests/fixtures/spirv/red.spvasm','tests/fixtures/spirv/green.spvasm')]
    return sorted(set(own))

def fingerprint(profile):return hashlib.sha256(''.join(p.relative_to(ROOT).as_posix()+'|'+sha(p)+'\n' for p in paths(profile)).encode()).hexdigest()

def translate(profile,text):
    require(profile in PROFILES,'Unknown workload profile');changes=[]
    def replace(old,new,label):
        nonlocal text
        require(text.count(old)==1,'Source contract changed at '+label);text=text.replace(old,new);changes.append(label)
    include='#include "Identity.hpp"' if profile=='upload' else '#include "../vulkan_upload/Identity.hpp"'
    replace(include,'#include "../linux_workloads/Platform.hpp"','platform include')
    match=re.search(r'std::string layer\(bool validation\)\{.*?\n\}',text,re.S);require(match is not None,'Exactly one complete platform layer function required')
    replace(match.group(0),'std::string layer(bool validation){return linuxwork::layer(validation);}','platform layer lookup')
    replace('if(validation){SDK required(true,expected);required.close();}','linuxwork::guard();\n    if(validation){SDK required(true,expected);required.close();}','exact UUID/kind guard before logical devices')
    replace('unique_exact_name_to_native_vulkan_and_dxgi_not_SDL_native_handle','unique_exact_name_to_native_vulkan_not_SDL_native_handle','Linux identity attribution')
    replace('<<quote('+('identity' if profile=='upload' else 'id')+'.luid)','<<"null"','Linux null LUID')
    require(not any(word in text for word in ('GetModuleHandle','GetModuleFileName','GetEnvironmentVariable','<Windows.h>','_MSC_FULL_VER')),'Unadapted Windows API remains')
    return text,changes

def generate(profile,output):
    require(output.resolve().is_relative_to(ROOT/'out'),'Generated source must stay under out/')
    source=ROOT/('benchmarks/vulkan_'+profile)/'main.cpp';text,changes=translate(profile,source.read_text(encoding='utf-8'))
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(text,encoding='utf-8',newline='\n')
    manifest=dict(profile=profile,source_fingerprint=fingerprint(profile),original_source_sha256=sha(source),generated_sha256=sha(output),adaptations=changes,
                  workload_bodies_changed=False,inputs=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in paths(profile)])
    save(output.with_suffix('.json'),manifest);return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--profile',choices=PROFILES,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args();generate(a.profile,a.output)
if __name__=='__main__':main()
