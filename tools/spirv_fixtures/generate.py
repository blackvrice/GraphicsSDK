"""Assemble and validate own fixtures with the existing pinned, local-only SPIRV-Tools."""
import hashlib
import json
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[2]
out = root/'out/vulkan-draw-shader-tools'
evidence = root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-draw/shader-tools'
fixtures = root/'tests/fixtures/spirv'
for p in (out, evidence, fixtures): p.mkdir(parents=True, exist_ok=True)
lock = json.loads((root/'tools/vulkan-validation.lock.json').read_text())
component = next(c for c in lock['components'] if c['name'] == 'SPIRV-Tools')
source = root/'.deps/vvl-validation/SPIRV-Tools'
assert subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() == component['commit']
assert not subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no'], text=True).strip()
commands = []
def record(p): return dict(path=p.relative_to(root).as_posix(), bytes=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest())
library = root/'out/vulkan-validation-runtime/prerequisites/lib/SPIRV-Tools.lib'
library_before = record(library)
def run(name, args, expected=0):
    result = subprocess.run([str(a) for a in args], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (evidence/(name+'.log')).write_bytes(result.stdout)
    commands.append(dict(name=name, argv=[str(a) for a in args], expected=expected, returncode=result.returncode))
    (evidence/'commands.json').write_text(json.dumps(commands, indent=2)+'\n')
    print(name, 'exit=', result.returncode, flush=True)
    assert result.returncode == expected, result.stdout.decode(errors='replace')
run('configure', ['cmake', '-S', 'tools/spirv_fixtures', '-B', out, '-G', 'Visual Studio 17 2022', '-A', 'x64'])
run('build', ['cmake', '--build', out, '--config', 'Release', '--parallel', '1'])
def vertex(name, fullscreen=False, instanced=False, point=False):
    positions = ('-1 -1', '3 -1', '-1 3') if fullscreen else ('-0.75 -0.75', '-0.75 0.75', '0 -0.75')
    text = 'OpCapability Shader\nOpMemoryModel Logical GLSL450\n'
    text += 'OpEntryPoint Vertex %main "main" %position %vertex' + (' %instance' if instanced else '') + (' %point' if point else '') + '\n'
    text += 'OpDecorate %position BuiltIn Position\nOpDecorate %vertex BuiltIn VertexIndex\n'
    if instanced: text += 'OpDecorate %instance BuiltIn InstanceIndex\n'
    if point: text += 'OpDecorate %point BuiltIn PointSize\n'
    text += '%void = OpTypeVoid\n%fn = OpTypeFunction %void\n%float = OpTypeFloat 32\n%vec = OpTypeVector %float 4\n%int = OpTypeInt 32 1\n%uint = OpTypeInt 32 0\n'
    text += '%in = OpTypePointer Input %int\n%output = OpTypePointer Output %vec\n%position = OpVariable %output Output\n%vertex = OpVariable %in Input\n'
    if instanced: text += '%instance = OpVariable %in Input\n'
    if point: text += '%pointType = OpTypePointer Output %float\n%point = OpVariable %pointType Output\n'
    text += '%u3 = OpConstant %uint 3\n%u2 = OpConstant %uint 2\n%zero = OpConstant %float 0\n%half = OpConstant %float 0.5\n%one = OpConstant %float 1\n'
    for i, pair in enumerate(positions):
        x, y = pair.split()
        text += f'%x{i} = OpConstant %float {x}\n%y{i} = OpConstant %float {y}\n%p{i} = OpConstantComposite %vec %x{i} %y{i} %half %one\n'
    text += '%array = OpTypeArray %vec %u3\n%table = OpConstantComposite %array %p0 %p1 %p2\n%localArray = OpTypePointer Function %array\n%localVec = OpTypePointer Function %vec\n'
    text += '%main = OpFunction %void None %fn\n%label = OpLabel\n%local = OpVariable %localArray Function\nOpStore %local %table\n%id = OpLoad %int %vertex\n%idU = OpBitcast %uint %id\n%index = OpUMod %uint %idU %u3\n%element = OpAccessChain %localVec %local %index\n%p = OpLoad %vec %element\n'
    if instanced:
        text += '%i = OpLoad %int %instance\n%iu = OpBitcast %uint %i\n%parity = OpUMod %uint %iu %u2\n%offset = OpConvertUToF %float %parity\n%delta = OpCompositeConstruct %vec %offset %zero %zero %zero\n%shifted = OpFAdd %vec %p %delta\nOpStore %position %shifted\n'
    else: text += 'OpStore %position %p\n'
    if point: text += 'OpStore %point %one\n'
    return text + 'OpReturn\nOpFunctionEnd\n'
def fragment(colors):
    text = 'OpCapability Shader\nOpMemoryModel Logical GLSL450\nOpEntryPoint Fragment %main "main" ' + ' '.join('%out'+str(i) for i in range(len(colors))) + '\nOpExecutionMode %main OriginUpperLeft\n'
    for i in range(len(colors)): text += f'OpDecorate %out{i} Location {i}\n'
    text += '%void = OpTypeVoid\n%fn = OpTypeFunction %void\n%float = OpTypeFloat 32\n%vec = OpTypeVector %float 4\n%ptr = OpTypePointer Output %vec\n'
    for i, c in enumerate(colors):
        text += f'%out{i} = OpVariable %ptr Output\n'
        for j, v in enumerate(c): text += f'%c{i}_{j} = OpConstant %float {v}\n'
        text += f'%value{i} = OpConstantComposite %vec ' + ' '.join(f'%c{i}_{j}' for j in range(4)) + '\n'
    text += '%main = OpFunction %void None %fn\n%label = OpLabel\n'
    for i in range(len(colors)): text += f'OpStore %out{i} %value{i}\n'
    return text + 'OpReturn\nOpFunctionEnd\n'
modules = dict(triangle=vertex('triangle'), fullscreen=vertex('fullscreen', fullscreen=True), instanced=vertex('instanced', instanced=True),
    unsupported_point=vertex('unsupported_point', point=True), red=fragment([(1, 0, 0, 1)]), green=fragment([(0, 1, 0, 1)]),
    half_red=fragment([(1, 0, 0, .5)]), mrt2=fragment([(1, 0, 0, 1), (0, 1, 0, 1)]),
    mrt8=fragment([(i & 1, (i >> 1) & 1, (i >> 2) & 1, 1) for i in range(8)]))
header = '#pragma once\n#include <array>\n#include <cstdint>\n// Own assembly, assembled and validated for Vulkan 1.1 by pinned local SPIRV-Tools.\nnamespace fixtures {\n'
records = []
for name, text in modules.items():
    assembly = fixtures/(name+'.spvasm'); assembly.write_bytes(text.encode())
    binary = out/(name+'.spv'); run(name, [out/'Release/spirv_fixture_tool.exe', assembly, binary])
    words = [int.from_bytes(binary.read_bytes()[i:i+4], 'little') for i in range(0, binary.stat().st_size, 4)]
    header += f'inline constexpr std::array<std::uint32_t, {len(words)}> {name}{{\n'
    for i in range(0, len(words), 8): header += '    ' + ', '.join(f'0x{w:08x}u' for w in words[i:i+8]) + ',\n'
    header += '};\n'; records.append(dict(name=name, assembly=record(assembly), binary=record(binary)))
header += '}\n'; (root/'tests/fixtures/SpirvFixtures.hpp').write_bytes(header.replace('\n', '\r\n').encode())
# Actual validator rejection controls never reach a GPU driver.
bad = out/'invalid.spvasm'; bad.write_text('this is not SPIR-V assembly\n')
run('malformed-assembly', [out/'Release/spirv_fixture_tool.exe', bad, out/'invalid.spv'], 1)
bad.write_text(modules['red'].replace('OpExecutionMode %main OriginUpperLeft\n', ''))
run('invalid-semantics', [out/'Release/spirv_fixture_tool.exe', bad, out/'invalid.spv'], 1)
assert record(library) == library_before
(evidence/'receipt.json').write_text(json.dumps(dict(component=component, scope='Local-only fixture assembler/validator; not SDK linked/distributed dependency audit',
    prerequisite_library=library_before, tool=record(out/'Release/spirv_fixture_tool.exe'), fixtures=records,
    header=record(root/'tests/fixtures/SpirvFixtures.hpp'), license_files=[record(p) for p in sorted(source.glob('LICENSE*')) if p.is_file()]), indent=2)+'\n')
