"""Assemble the same own geometry/colors for both SDKs under Vulkan 1.0 semantics."""
import hashlib
import json
import pathlib
import struct
import subprocess

root=pathlib.Path(__file__).resolve().parents[2]
out=root/'out/vulkan-draw-benchmark-shader-tools'
evidence=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-draw-benchmark/shader-tools'
assert not out.exists() and not evidence.exists()
evidence.mkdir();commands=[]
lock=json.loads((root/'tools/vulkan-validation.lock.json').read_bytes());component=next(c for c in lock['components'] if c['name']=='SPIRV-Tools')
source=root/'.deps/vvl-validation/SPIRV-Tools'
git=['git','-c','gc.auto=0','-c','maintenance.auto=false','-C',str(source)]
assert subprocess.check_output(git+['rev-parse','HEAD'],text=True).strip()==component['commit']
assert not subprocess.check_output(git+['status','--porcelain','--untracked-files=no'],text=True).strip()
def rec(p):return dict(path=p.relative_to(root).as_posix(),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
library=root/'out/vulkan-validation-runtime/prerequisites/lib/SPIRV-Tools.lib';before=rec(library)
def run(name,args,expected=0):
    p=subprocess.run([str(a) for a in args],cwd=root,capture_output=True,timeout=300)
    (evidence/(name+'.stdout')).write_bytes(p.stdout);(evidence/(name+'.stderr')).write_bytes(p.stderr)
    commands.append(dict(name=name,argv=[str(a) for a in args],returncode=p.returncode,expected=expected));(evidence/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    assert p.returncode==expected,(name,p.stderr);print(name,'PASS',flush=True)
run('configure',['cmake','-S','tools/spirv_fixtures','-B',out,'-G','Visual Studio 17 2022','-A','x64'])
run('build',['cmake','--build',out,'--config','Release','--parallel','1'])
tool=out/'Release/spirv_fixture_tool.exe';fixtures=[]
header='#pragma once\n#include <array>\n#include <cstdint>\n// Own assembly validated under Vulkan 1.0; identical bytes supplied to both SDKs.\nnamespace drawshaders {\n'
for name in ('fullscreen','red','green'):
    assembly=root/'tests/fixtures/spirv'/(name+'.spvasm');binary=out/(name+'.spv')
    run(name,[tool,assembly,binary,'--vulkan1.0']);data=binary.read_bytes();assert data[:8]==bytes.fromhex('0302230700000100')
    words=struct.unpack('<'+'I'*(len(data)//4),data)
    header+=f'inline constexpr std::array<std::uint32_t,{len(words)}> {name}{{\n'
    for i in range(0,len(words),8):header+='    '+', '.join(f'0x{w:08x}u' for w in words[i:i+8])+',\n'
    header+='};\n';fixtures.append(dict(name=name,assembly=rec(assembly),binary=rec(binary)))
header+='}\n';destination=root/'benchmarks/vulkan_draw/DrawShaders.hpp';assert not destination.exists();destination.write_text(header,encoding='utf-8',newline='\r\n')
invalid=out/'invalid.spvasm';invalid.write_text('not valid assembly\n');run('malformed',[tool,invalid,out/'invalid.spv','--vulkan1.0'],1)
invalid.write_text((root/'tests/fixtures/spirv/red.spvasm').read_text().replace('OpExecutionMode %main OriginUpperLeft\n',''));run('invalid-semantics',[tool,invalid,out/'invalid.spv','--vulkan1.0'],1)
run('unknown-target',[tool,invalid,out/'invalid.spv','--unknown'],1)
assert rec(library)==before
(evidence/'receipt.json').write_text(json.dumps(dict(component=component,target_environment='Vulkan1.0',SPIRV_version='1.0',tool=rec(tool),prerequisite_library=before,fixtures=fixtures,header=rec(destination),source=rec(root/'tools/spirv_fixtures/main.cpp'),license_files=[rec(p) for p in sorted(source.glob('LICENSE*')) if p.is_file()],external_tool_bundled=False),indent=2)+'\n')
