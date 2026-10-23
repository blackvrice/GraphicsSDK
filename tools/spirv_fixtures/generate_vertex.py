"""Generate new own vertex-input fixtures without modifying the historical draw fixtures/tool."""
import hashlib
import json
import pathlib
import subprocess

root=pathlib.Path(__file__).resolve().parents[2]
out=root/'out/vulkan-vertex-shaders'
evidence=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-vertex/shader-tools'
fixtures=root/'tests/fixtures/spirv_vertex'
for p in (out,evidence,fixtures):p.mkdir(parents=True,exist_ok=True)
def rec(p):return dict(path=p.relative_to(root).as_posix(),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
prior=json.loads((root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-draw/shader-tools/receipt.json').read_text())
tool=root/prior['tool']['path'];library=root/prior['prerequisite_library']['path']
assert rec(tool)==prior['tool'] and rec(library)==prior['prerequisite_library']
commands=[]
def run(name,source,expected=0):
    target=out/(name+'.spv');result=subprocess.run([str(tool),str(source),str(target)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,cwd=root)
    (evidence/(name+'.log')).write_bytes(result.stdout)
    commands.append(dict(name=name,argv=[str(tool),str(source),str(target)],expected=expected,returncode=result.returncode))
    (evidence/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    print(name,'exit=',result.returncode,flush=True);assert result.returncode==expected,result.stdout.decode(errors='replace')
    return target
def vertex(components,instance=False):
    inputs=['%inputPosition','%inputColor'] if not instance else ['%inputPosition','%inputOffset','%inputColor']
    if components==1:inputs.insert(1,'%inputY')
    text='OpCapability Shader\nOpMemoryModel Logical GLSL450\nOpEntryPoint Vertex %main "main" %position %color '+' '.join(inputs)+'\n'
    text+='OpDecorate %position BuiltIn Position\nOpDecorate %color Location 0\n'
    for i,p in enumerate(inputs):text+=f'OpDecorate {p} Location {i}\n'
    text+='%void = OpTypeVoid\n%fn = OpTypeFunction %void\n%float = OpTypeFloat 32\n%v2 = OpTypeVector %float 2\n%v3 = OpTypeVector %float 3\n%v4 = OpTypeVector %float 4\n'
    text+='%in1 = OpTypePointer Input %float\n%in2 = OpTypePointer Input %v2\n%in3 = OpTypePointer Input %v3\n%in4 = OpTypePointer Input %v4\n%out4 = OpTypePointer Output %v4\n'
    text+=f'%inputPosition = OpVariable %in{components} Input\n%inputColor = OpVariable %in4 Input\n%position = OpVariable %out4 Output\n%color = OpVariable %out4 Output\n'
    if components==1:text+='%inputY = OpVariable %in1 Input\n'
    if instance:text+='%inputOffset = OpVariable %in2 Input\n'
    text+='%half = OpConstant %float 0.5\n%one = OpConstant %float 1\n%main = OpFunction %void None %fn\n%label = OpLabel\n'
    text+=f'%p = OpLoad {"%float" if components==1 else "%v"+str(components)} %inputPosition\n'
    if components==1:text+='%y = OpLoad %float %inputY\n%pos = OpCompositeConstruct %v4 %p %y %half %one\n'
    elif instance:text+='%offset = OpLoad %v2 %inputOffset\n%xy = OpFAdd %v2 %p %offset\n%pos = OpCompositeConstruct %v4 %xy %half %one\n'
    elif components==2:text+='%pos = OpCompositeConstruct %v4 %p %half %one\n'
    elif components==3:text+='%pos = OpCompositeConstruct %v4 %p %one\n'
    else:text+='%pos = OpCopyObject %v4 %p\n'
    return text+'%c = OpLoad %v4 %inputColor\nOpStore %position %pos\nOpStore %color %c\nOpReturn\nOpFunctionEnd\n'
def fragment(components=4):
    return f'''OpCapability Shader
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %color %output
OpExecutionMode %main OriginUpperLeft
OpDecorate %color Location 0
OpDecorate %output Location 0
%void = OpTypeVoid
%fn = OpTypeFunction %void
%float = OpTypeFloat 32
%v3 = OpTypeVector %float 3
%v4 = OpTypeVector %float 4
%in = OpTypePointer Input %v{components}
%out = OpTypePointer Output %v4
%color = OpVariable %in Input
%output = OpVariable %out Output
%one = OpConstant %float 1
%main = OpFunction %void None %fn
%label = OpLabel
%c = OpLoad %v{components} %color
'''+('%value = OpCompositeConstruct %v4 %c %one\n' if components==3 else '%value = OpCopyObject %v4 %c\n')+'OpStore %output %value\nOpReturn\nOpFunctionEnd\n'
modules={**{'position'+str(n):vertex(n) for n in range(1,5)},'instance2':vertex(2,True),'color':fragment(),'mismatch3':fragment(3)}
header='#pragma once\n#include <array>\n#include <cstdint>\n// Own SPIR-V 1.3 modules, assembled and validated for Vulkan 1.1.\nnamespace vertexFixtures {\n'
records=[]
for name,text in modules.items():
    assembly=fixtures/(name+'.spvasm');assembly.write_bytes(text.encode());binary=run(name,assembly)
    data=binary.read_bytes();words=[int.from_bytes(data[i:i+4],'little') for i in range(0,len(data),4)]
    header+=f'inline constexpr std::array<std::uint32_t, {len(words)}> {name}{{\n'
    for i in range(0,len(words),8):header+='    '+', '.join(f'0x{w:08x}u' for w in words[i:i+8])+',\n'
    header+='};\n';records.append(dict(name=name,assembly=rec(assembly),binary=rec(binary)))
header+='}\n';(root/'tests/fixtures/VertexFixtures.hpp').write_bytes(header.replace('\n','\r\n').encode())
bad=out/'invalid.spvasm';bad.write_text(modules['color'].replace('OpExecutionMode %main OriginUpperLeft\n',''));run('invalid-semantics',bad,1)
assert rec(tool)==prior['tool'] and rec(library)==prior['prerequisite_library']
(evidence/'receipt.json').write_text(json.dumps(dict(component=prior['component'],scope='Reused local-only pinned assembler/validator; not SDK linked/distributed dependency audit',
    prerequisite_library=rec(library),tool=rec(tool),fixtures=records,header=rec(root/'tests/fixtures/VertexFixtures.hpp'),license_files=prior['license_files']),indent=2)+'\n')
