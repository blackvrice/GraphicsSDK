"""Serial, process-local Vulkan synchronization validation. No driver/registry changes."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import time

root = pathlib.Path(__file__).resolve().parents[5]
output = root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-presentation'
runtime = root/'out/vulkan-validation-runtime'
receipt = json.loads((runtime/'receipt.json').read_text())
artifacts = receipt['artifacts']
def frozen():
    assert all(hashlib.sha256((root/r['path']).read_bytes()).hexdigest() == r['sha256'] for r in artifacts)
frozen()
dll = next(root/r['path'] for r in artifacts if r['path'].endswith('.dll'))
manifest = next(root/r['path'] for r in artifacts if r['path'].endswith('.json'))
assert dll.parent == manifest.parent
env = {k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_', 'VVL_', 'GFX_EXPECT_VULKAN_'))}
settings = output/'controlled-settings'; settings.mkdir(exist_ok=True)
(settings/'vk_layer_settings.txt').write_bytes(b'# Empty controlled settings; use SDK VkLayerSettingsCreateInfoEXT values.\n')
env.update(VK_LAYER_PATH=str(manifest.parent), VK_LAYER_SETTINGS_PATH=str(settings),
           VK_LOADER_LAYERS_DISABLE='~implicit~', GFX_EXPECT_VULKAN_LAYER=str(dll))
(output/'environment.json').write_bytes((json.dumps(dict(removed_inherited_keys=sorted(set(os.environ)-set(env)),
    overrides={k:env[k] for k in ('VK_LAYER_PATH','VK_LAYER_SETTINGS_PATH','VK_LOADER_LAYERS_DISABLE','GFX_EXPECT_VULKAN_LAYER')}),indent=2)+'\n').encode())
commands=[]
def run(name, args, expected=0, overrides=None):
    frozen(); start=time.monotonic()
    result=subprocess.run(args,cwd=root,env={**env,**(overrides or {})},stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=600)
    (output/(name+'.log')).write_bytes(result.stdout)
    commands.append(dict(name=name,argv=args,expected=expected,returncode=result.returncode,seconds=time.monotonic()-start,environment_overrides=overrides or {}))
    (output/'commands.json').write_bytes((json.dumps(commands,indent=2)+'\n').encode())
    print(name,'exit=',result.returncode,'seconds=',round(time.monotonic()-start,1),flush=True)
    if result.returncode!=expected: raise RuntimeError(name+' failed; inspect log')
    frozen(); return result.stdout

run('configure',['cmake','-S','.','-B','out/vulkan-presentation','-G','Visual Studio 17 2022','-A','x64',
    '-DGFX_ENABLE_VULKAN=ON','-DGFX_ENABLE_D3D12=ON','-DGFX_VULKAN_VALIDATION_TESTS=ON','-DGFX_BUILD_EXAMPLES=OFF'])
run('build',['cmake','--build','out/vulkan-presentation','--config','Release','--parallel','1'])
run('ctest',['ctest','--test-dir','out/vulkan-presentation','-C','Release','--output-on-failure','-j','1','-E','^memory_reclamation_','--output-junit',str(output/'ctest.xml')])
shutil.copyfile(root/'out/vulkan-presentation/Testing/Temporary/LastTest.log',output/'native-tests.log')
run('controls-loader-trace',['out/vulkan-presentation/Release/gfx_vulkan_validation_tests.exe'],overrides={'VK_LOADER_DEBUG':'layer'})
run('unknown-option',['out/vulkan-presentation/Release/gfx_vulkan_tests.exe','--validaton'],1)
run('extra-option',['out/vulkan-presentation/Release/gfx_vulkan_tests.exe','--validation','extra'],1)
absent=output/'absent-layers'; absent.mkdir(exist_ok=True)
missing=run('missing-layer',['out/vulkan-presentation/Release/gfx_vulkan_tests.exe','--validation'],1,{'VK_LAYER_PATH':str(absent)})
assert b'VK_LAYER_KHRONOS_validation is not installed' in missing
run('install',['cmake','--install','out/vulkan-presentation','--config','Release','--prefix','out/vulkan-presentation-install'])
run('consumer-configure',['cmake','-S','tests/vulkan_consumer','-B','out/vulkan-presentation-consumer','-G','Visual Studio 17 2022','-A','x64',
    '-DCMAKE_PREFIX_PATH='+(root/'out/vulkan-presentation-install').as_posix()])
run('consumer-build',['cmake','--build','out/vulkan-presentation-consumer','--config','Release','--parallel','1'])
run('consumer-native',['out/vulkan-presentation-consumer/Release/vulkan_consumer.exe','--validation'])
run('consumer-texture',['out/vulkan-presentation-consumer/Release/vulkan_texture_consumer.exe','--validation'])
run('consumer-render',['out/vulkan-presentation-consumer/Release/vulkan_render_consumer.exe','--validation'])
run('consumer-draw',['out/vulkan-presentation-consumer/Release/vulkan_draw_consumer.exe','--validation'])
run('consumer-vertex',['out/vulkan-presentation-consumer/Release/vulkan_vertex_consumer.exe','--validation'])
run('consumer-binding',['out/vulkan-presentation-consumer/Release/vulkan_binding_consumer.exe','--validation'])
run('consumer-depth',['out/vulkan-presentation-consumer/Release/vulkan_depth_consumer.exe','--validation'])
run('consumer-timestamp',['out/vulkan-presentation-consumer/Release/vulkan_timestamp_consumer.exe','--validation'])
run('consumer-presentation',['out/vulkan-presentation-consumer/Release/vulkan_presentation_consumer.exe','--validation'])
run('only-configure',['cmake','-S','.','-B','out/vulkan-presentation-only','-G','Visual Studio 17 2022','-A','x64',
    '-DGFX_ENABLE_VULKAN=ON','-DGFX_ENABLE_D3D12=OFF','-DGFX_VULKAN_VALIDATION_TESTS=ON','-DGFX_BUILD_EXAMPLES=OFF'])
run('only-build',['cmake','--build','out/vulkan-presentation-only','--config','Release','--parallel','1'])
run('only-ctest',['ctest','--test-dir','out/vulkan-presentation-only','-C','Release','--output-on-failure','-j','1','--output-junit',str(output/'only-ctest.xml')])
shutil.copyfile(root/'out/vulkan-presentation-only/Testing/Temporary/LastTest.log',output/'only-native-tests.log')
run('portable-configure',['cmake','-S','.','-B','out/vulkan-presentation-portable','-G','Visual Studio 17 2022','-A','x64',
    '-DGFX_ENABLE_VULKAN=OFF','-DGFX_ENABLE_D3D12=OFF','-DGFX_BUILD_EXAMPLES=OFF'])
run('portable-build',['cmake','--build','out/vulkan-presentation-portable','--config','Release','--parallel','1'])
run('portable-ctest',['ctest','--test-dir','out/vulkan-presentation-portable','-C','Release','--output-on-failure','-j','1','--output-junit',str(output/'portable-ctest.xml')])
run('texture-unknown',['out/vulkan-presentation/Release/gfx_vulkan_texture_tests.exe','--validaton'],1)
run('texture-extra',['out/vulkan-presentation/Release/gfx_vulkan_texture_tests.exe','--validation','extra'],1)
run('texture-missing-layer',['out/vulkan-presentation/Release/gfx_vulkan_texture_tests.exe','--validation'],1,{'VK_LAYER_PATH':str(absent)})
run('render-unknown',['out/vulkan-presentation/Release/gfx_vulkan_render_tests.exe','--validaton'],1)
run('render-extra',['out/vulkan-presentation/Release/gfx_vulkan_render_tests.exe','--validation','extra'],1)
run('render-missing-layer',['out/vulkan-presentation/Release/gfx_vulkan_render_tests.exe','--validation'],1,{'VK_LAYER_PATH':str(absent)})
run('draw-unknown',['out/vulkan-presentation/Release/gfx_vulkan_draw_tests.exe','--validaton'],1)
run('draw-extra',['out/vulkan-presentation/Release/gfx_vulkan_draw_tests.exe','--validation','extra'],1)
run('draw-missing-layer',['out/vulkan-presentation/Release/gfx_vulkan_draw_tests.exe','--validation'],1,{'VK_LAYER_PATH':str(absent)})
run('vertex-unknown',['out/vulkan-presentation/Release/gfx_vulkan_vertex_tests.exe','--validaton'],1)
run('vertex-extra',['out/vulkan-presentation/Release/gfx_vulkan_vertex_tests.exe','--validation','extra'],1)
run('vertex-missing-layer',['out/vulkan-presentation/Release/gfx_vulkan_vertex_tests.exe','--validation'],1,{'VK_LAYER_PATH':str(absent)})
run('binding-unknown',['out/vulkan-presentation/Release/gfx_vulkan_binding_tests.exe','--validaton'],1)
run('binding-extra',['out/vulkan-presentation/Release/gfx_vulkan_binding_tests.exe','--validation','extra'],1)
run('binding-missing-layer',['out/vulkan-presentation/Release/gfx_vulkan_binding_tests.exe','--validation'],1,{'VK_LAYER_PATH':str(absent)})
run('depth-unknown',['out/vulkan-presentation/Release/gfx_vulkan_depth_tests.exe','--validaton'],1)
run('depth-extra',['out/vulkan-presentation/Release/gfx_vulkan_depth_tests.exe','--validation','extra'],1)
run('depth-missing-layer',['out/vulkan-presentation/Release/gfx_vulkan_depth_tests.exe','--validation'],1,{'VK_LAYER_PATH':str(absent)})
run('timestamp-unknown',['out/vulkan-presentation/Release/gfx_vulkan_timestamp_tests.exe','--validaton'],1)
run('timestamp-extra',['out/vulkan-presentation/Release/gfx_vulkan_timestamp_tests.exe','--validation','extra'],1)
run('timestamp-missing-layer',['out/vulkan-presentation/Release/gfx_vulkan_timestamp_tests.exe','--validation'],1,{'VK_LAYER_PATH':str(absent)})
run('presentation-unknown',['out/vulkan-presentation/Release/gfx_vulkan_presentation_tests.exe','--validaton'],1)
run('presentation-extra',['out/vulkan-presentation/Release/gfx_vulkan_presentation_tests.exe','--validation','extra'],1)
run('presentation-missing-layer',['out/vulkan-presentation/Release/gfx_vulkan_presentation_tests.exe','--validation'],1,{'VK_LAYER_PATH':str(absent)})
shutil.copyfile(runtime/'receipt.json',output/'runtime-receipt.json')
shutil.copyfile(runtime/'commands.json',output/'runtime-build-commands.json')
print('PASS: controlled required layer tests, counter controls, installed consumer and backend configurations',flush=True)
