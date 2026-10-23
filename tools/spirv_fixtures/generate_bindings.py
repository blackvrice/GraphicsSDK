"""Assemble/validate own descriptor fixtures with the unchanged pinned local tool."""
import hashlib
import json
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[2]
out = root/'out/vulkan-bindings-shaders'
evidence = root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-bindings/shader-tools'
fixtures = root/'tests/fixtures/spirv_bindings'
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

def vertex(uniform=False):
    text = (root/'tests/fixtures/spirv/fullscreen.spvasm').read_text()
    text = text.replace('%position %vertex', '%position %vertex %uv')
    text = text.replace('OpDecorate %vertex BuiltIn VertexIndex', 'OpDecorate %vertex BuiltIn VertexIndex\nOpDecorate %uv Location 0')
    before = '%v2 = OpTypeVector %float 2\n%out2 = OpTypePointer Output %v2\n%uv = OpVariable %out2 Output\n%h2 = OpConstantComposite %v2 %half %half\n'
    instructions = '%xy = OpVectorShuffle %v2 %p %p 0 1\n%scaled = OpFMul %v2 %xy %h2\n%baseUV = OpFAdd %v2 %scaled %h2\n'
    if uniform:
        text = text.replace('%void =', 'OpDecorate %block Block\nOpMemberDecorate %block 0 Offset 0\nOpMemberDecorate %block 1 Offset 16\nOpDecorate %transform DescriptorSet 0\nOpDecorate %transform Binding 2\n%void =', 1)
        before += '%block = OpTypeStruct %vec %vec\n%ub = OpTypePointer Uniform %block\n%uvec = OpTypePointer Uniform %vec\n%transform = OpVariable %ub Uniform\n%u0 = OpConstant %uint 0\n'
        instructions += '%tp = OpAccessChain %uvec %transform %u0\n%tr = OpLoad %vec %tp\n%scale = OpVectorShuffle %v2 %tr %tr 0 1\n%offset = OpVectorShuffle %v2 %tr %tr 2 3\n%uvScaled = OpFMul %v2 %baseUV %scale\n%texUV = OpFAdd %v2 %uvScaled %offset\n'
    else: instructions += '%texUV = OpCopyObject %v2 %baseUV\n'
    text = text.replace('%main = OpFunction', before+'%main = OpFunction')
    return text.replace('OpStore %position %p', 'OpStore %position %p\n'+instructions+'OpStore %uv %texUV')

def fragment(array=False, large=False):
    text = 'OpCapability Shader\nOpMemoryModel Logical GLSL450\nOpEntryPoint Fragment %main "main" %uv %output\nOpExecutionMode %main OriginUpperLeft\nOpDecorate %uv Location 0\nOpDecorate %output Location 0\nOpDecorate %images DescriptorSet 0\nOpDecorate %images Binding 0\nOpDecorate %sampler DescriptorSet 0\nOpDecorate %sampler Binding 32\n'
    if array:
        text += 'OpDecorate %sampler2 DescriptorSet 0\nOpDecorate %sampler2 Binding 33\nOpDecorate %buffers DescriptorSet 0\nOpDecorate %buffers Binding 1\nOpDecorate %block Block\n'
        for i in range(17 if large else 2): text += f'OpMemberDecorate %block {i} Offset {16*i}\n'
    text += '%void = OpTypeVoid\n%fn = OpTypeFunction %void\n%float = OpTypeFloat 32\n%uint = OpTypeInt 32 0\n%v2 = OpTypeVector %float 2\n%v4 = OpTypeVector %float 4\n%in2 = OpTypePointer Input %v2\n%out4 = OpTypePointer Output %v4\n%uv = OpVariable %in2 Input\n%output = OpVariable %out4 Output\n%image = OpTypeImage %float 2D 0 0 0 1 Unknown\n%samp = OpTypeSampler\n%sampled = OpTypeSampledImage %image\n%psamp = OpTypePointer UniformConstant %samp\n%sampler = OpVariable %psamp UniformConstant\n%pimage = OpTypePointer UniformConstant %image\n%u0 = OpConstant %uint 0\n%u1 = OpConstant %uint 1\n%u2 = OpConstant %uint 2\n%uLast = OpConstant %uint '+str(16 if large else 1)+'\n'
    if array:
        text += '%images2 = OpTypeArray %image %u2\n%pimages = OpTypePointer UniformConstant %images2\n%images = OpVariable %pimages UniformConstant\n%sampler2 = OpVariable %psamp UniformConstant\n%block = OpTypeStruct '+' '.join(['%v4']*(17 if large else 2))+'\n%blocks2 = OpTypeArray %block %u2\n%pblocks = OpTypePointer Uniform %blocks2\n%pvec = OpTypePointer Uniform %v4\n%buffers = OpVariable %pblocks Uniform\n'
    else: text += '%images = OpVariable %pimage UniformConstant\n'
    text += '%main = OpFunction %void None %fn\n%label = OpLabel\n%coord = OpLoad %v2 %uv\n%s = OpLoad %samp %sampler\n'
    if array: text += '%i0p = OpAccessChain %pimage %images %u0\n%i = OpLoad %image %i0p\n'
    else: text += '%i = OpLoad %image %images\n'
    text += '%si = OpSampledImage %sampled %i %s\n%a = OpImageSampleImplicitLod %v4 %si %coord\n'
    if array:
        text += '%i1p = OpAccessChain %pimage %images %u1\n%i1 = OpLoad %image %i1p\n%s1 = OpLoad %samp %sampler2\n%si1 = OpSampledImage %sampled %i1 %s1\n%b = OpImageSampleImplicitLod %v4 %si1 %coord\n%w0p = OpAccessChain %pvec %buffers %u0 %u0\n%w1p = OpAccessChain %pvec %buffers %u1 %u0\n%z0p = OpAccessChain %pvec %buffers %u0 %uLast\n%z1p = OpAccessChain %pvec %buffers %u1 %uLast\n%w0 = OpLoad %v4 %w0p\n%w1 = OpLoad %v4 %w1p\n%z0 = OpLoad %v4 %z0p\n%z1 = OpLoad %v4 %z1p\n%wa = OpFMul %v4 %a %w0\n%wb = OpFMul %v4 %b %w1\n%sum = OpFAdd %v4 %wa %wb\n%bias = OpFAdd %v4 %z0 %z1\n%value = OpFAdd %v4 %sum %bias\n'
    else: text += '%value = OpCopyObject %v4 %a\n'
    return text+'OpStore %output %value\nOpReturn\nOpFunctionEnd\n'

modules = {'uv': vertex(), 'transform': vertex(True), 'sample': fragment(), 'arrays': fragment(True), 'large_block': fragment(True, True)}
header = '#pragma once\n#include <array>\n#include <cstdint>\n// Own SPIR-V 1.3 modules, assembled and validated for Vulkan 1.1.\nnamespace bindingFixtures {\n'
records = []
for name, text in modules.items():
    assembly = fixtures/(name+'.spvasm'); assembly.write_bytes(text.encode()); binary = run(name, assembly)
    data = binary.read_bytes(); words = [int.from_bytes(data[i:i+4], 'little') for i in range(0, len(data), 4)]
    header += f'inline constexpr std::array<std::uint32_t, {len(words)}> {name}{{\n'
    for i in range(0, len(words), 8): header += '    '+', '.join(f'0x{w:08x}u' for w in words[i:i+8])+',\n'
    header += '};\n'; records.append(dict(name=name, assembly=rec(assembly), binary=rec(binary)))
header += '}\n'; (root/'tests/fixtures/BindingFixtures.hpp').write_bytes(header.replace('\n', '\r\n').encode())
bad = out/'invalid.spvasm'; bad.write_text(modules['arrays'].replace('OpMemberDecorate %block 1 Offset 16', 'OpMemberDecorate %block 1 Offset 4')); run('invalid-block-offset', bad, 1)
assert rec(tool) == prior['tool'] and rec(library) == prior['prerequisite_library']
(evidence/'receipt.json').write_text(json.dumps(dict(component=prior['component'], scope='Reused local-only pinned assembler/validator; not SDK linked/distributed dependency audit',
    prerequisite_library=rec(library), tool=rec(tool), fixtures=records, header=rec(root/'tests/fixtures/BindingFixtures.hpp'), license_files=prior['license_files']), indent=2)+'\n')
