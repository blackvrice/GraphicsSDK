"""Assemble/validate own depth fixtures with the unchanged pinned local tool."""
import hashlib
import json
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[2]
out = root/'out/vulkan-depth-shaders'
evidence = root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-depth/shader-tools'
fixtures = root/'tests/fixtures/spirv_depth'
for p in (out, evidence, fixtures): p.mkdir(parents=True, exist_ok=True)
def rec(p): return dict(path=p.relative_to(root).as_posix(), bytes=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest())
prior = json.loads((root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-draw/shader-tools/receipt.json').read_text())
tool = root/prior['tool']['path']; library = root/prior['prerequisite_library']['path']
assert rec(tool) == prior['tool'] and rec(library) == prior['prerequisite_library']
commands = []
def run(name, source, expected=0):
    target = out/(name+'.spv')
    args = [str(tool), str(source), str(target)]
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=root)
    (evidence/(name+'.log')).write_bytes(result.stdout)
    commands.append(dict(name=name, argv=args, expected=expected, returncode=result.returncode))
    (evidence/'commands.json').write_text(json.dumps(commands, indent=2)+'\n')
    print(name, 'exit=', result.returncode, flush=True)
    assert result.returncode == expected, result.stdout.decode(errors='replace')
    return target

def block():
    return 'OpDecorate %block Block\nOpMemberDecorate %block 0 Offset 0\nOpMemberDecorate %block 1 Offset 16\nOpDecorate %params DescriptorSet 0\nOpDecorate %params Binding 0\n'
def vertex():
    text=(root/'tests/fixtures/spirv/fullscreen.spvasm').read_text()
    text=text.replace('%void =', block()+'%void =',1)
    text=text.replace('%main = OpFunction','%v2 = OpTypeVector %float 2\n%block = OpTypeStruct %vec %vec\n%ub = OpTypePointer Uniform %block\n%uv4 = OpTypePointer Uniform %vec\n%params = OpVariable %ub Uniform\n%u1 = OpConstant %uint 1\n%main = OpFunction')
    return text.replace('OpStore %position %p','%gptr = OpAccessChain %uv4 %params %u1\n%g = OpLoad %vec %gptr\n%z = OpCompositeExtract %float %g 0\n%xy = OpVectorShuffle %v2 %p %p 0 1\n%pos = OpCompositeConstruct %vec %xy %z %one\nOpStore %position %pos')
def fragment(count):
    text='OpCapability Shader\nOpMemoryModel Logical GLSL450\nOpEntryPoint Fragment %main "main" '+' '.join('%out'+str(i) for i in range(count))+'\nOpExecutionMode %main OriginUpperLeft\n'+block()
    for i in range(count):text+=f'OpDecorate %out{i} Location {i}\n'
    text+='%void = OpTypeVoid\n%fn = OpTypeFunction %void\n%float = OpTypeFloat 32\n%v4 = OpTypeVector %float 4\n%uint = OpTypeInt 32 0\n%u0 = OpConstant %uint 0\n%one = OpConstant %float 1\n%ones = OpConstantComposite %v4 %one %one %one %one\n%block = OpTypeStruct %v4 %v4\n%ub = OpTypePointer Uniform %block\n%uv4 = OpTypePointer Uniform %v4\n%params = OpVariable %ub Uniform\n%output = OpTypePointer Output %v4\n'
    for i in range(count):text+=f'%out{i} = OpVariable %output Output\n'
    text+='%main = OpFunction %void None %fn\n%label = OpLabel\n%ptr = OpAccessChain %uv4 %params %u0\n%color = OpLoad %v4 %ptr\n%inverse = OpFSub %v4 %ones %color\n'
    for i in range(count):text+=f'OpStore %out{i} '+('%inverse' if i%2 else '%color')+'\n'
    return text+'OpReturn\nOpFunctionEnd\n'
modules={'vertex':vertex(),'color':fragment(1),'mrt2':fragment(2),'mrt8':fragment(8)}
header = '#pragma once\n#include <array>\n#include <cstdint>\n// Own SPIR-V 1.3 modules, assembled and validated for Vulkan 1.1.\nnamespace depthFixtures {\n'
records = []
for name, text in modules.items():
    assembly = fixtures/(name+'.spvasm'); assembly.write_bytes(text.encode()); binary = run(name, assembly)
    data = binary.read_bytes(); words = [int.from_bytes(data[i:i+4], 'little') for i in range(0, len(data), 4)]
    header += f'inline constexpr std::array<std::uint32_t, {len(words)}> {name}{{\n'
    for i in range(0, len(words), 8): header += '    '+', '.join(f'0x{w:08x}u' for w in words[i:i+8])+',\n'
    header += '};\n'; records.append(dict(name=name, assembly=rec(assembly), binary=rec(binary)))
header += '}\n'; (root/'tests/fixtures/DepthFixtures.hpp').write_bytes(header.replace('\n', '\r\n').encode())
bad = out/'invalid.spvasm'; bad.write_text(modules['color'].replace('OpMemberDecorate %block 1 Offset 16', 'OpMemberDecorate %block 1 Offset 4')); run('invalid-block-offset', bad, 1)
assert rec(tool) == prior['tool'] and rec(library) == prior['prerequisite_library']
(evidence/'receipt.json').write_text(json.dumps(dict(component=prior['component'], scope='Reused local-only pinned assembler/validator; not SDK linked/distributed dependency audit',
    prerequisite_library=rec(library), tool=rec(tool), fixtures=records, header=rec(root/'tests/fixtures/DepthFixtures.hpp'), license_files=prior['license_files']), indent=2)+'\n')
