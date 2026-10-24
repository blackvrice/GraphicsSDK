"""Build/test an isolated experimental SDK. Exclude the deferred memory tests at registration selection."""
import json
import os
import pathlib
import subprocess
import time
root=pathlib.Path(__file__).resolve().parents[5]
v=root/'benchmarks/reports/validation/2026-10-24-phase3-vulkan-buffer-pool';v.mkdir(exist_ok=True)
receipt=json.loads((root/'out/vulkan-validation-runtime/receipt.json').read_text(encoding='utf-8'));dll=next(root/a['path'] for a in receipt['artifacts'] if a['path'].endswith('.dll'))
env={k:x for k,x in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_','GFX_TRACE_'))}
settings=v/'settings';settings.mkdir(exist_ok=True);(settings/'vk_layer_settings.txt').write_text('# SDK VkLayerSettingsCreateInfoEXT controls synchronization validation.\n',encoding='utf-8')
env.update(VK_LAYER_PATH=str(dll.parent),VK_LAYER_SETTINGS_PATH=str(settings),VK_LOADER_LAYERS_DISABLE='~implicit~',GFX_EXPECT_VULKAN_LAYER=str(dll))
(v/'environment.json').write_text(json.dumps({k:env[k] for k in ('VK_LAYER_PATH','VK_LAYER_SETTINGS_PATH','VK_LOADER_LAYERS_DISABLE','GFX_EXPECT_VULKAN_LAYER')},indent=2)+'\n')
commands=[]
def run(name,args):
    start=time.monotonic()
    with (v/(name+'.log')).open('wb') as f:
        p=subprocess.Popen(args,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        for line in p.stdout:f.write(line);f.flush();print(name+':',line.decode('utf-8',errors='replace').rstrip(),flush=True)
        status=p.wait()
    commands.append(dict(name=name,argv=list(map(str,args)),returncode=status,seconds=time.monotonic()-start));(v/'build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    if status:raise RuntimeError(name+' failed')
run('candidate-configure',['cmake','-S','out/vulkan-buffer-pool-source-v2','-B','out/vulkan-buffer-pool-sdk','-G','Visual Studio 17 2022','-A','x64','-DGFX_ENABLE_D3D12=ON','-DGFX_ENABLE_VULKAN=ON','-DGFX_VULKAN_VALIDATION_TESTS=ON','-DGFX_BUILD_EXAMPLES=OFF','-DGFX_VULKAN_HEADERS='+str(root/'.deps/Vulkan-Headers')])
run('candidate-build',['cmake','--build','out/vulkan-buffer-pool-sdk','--config','Release','--parallel','1'])
run('candidate-tests',['ctest','--test-dir','out/vulkan-buffer-pool-sdk','-C','Release','--output-on-failure','-j','1','-E','^memory_reclamation_','--output-junit',str(v/'candidate-tests.xml')])
run('candidate-install',['cmake','--install','out/vulkan-buffer-pool-sdk','--config','Release','--prefix',str(root/'out/vulkan-buffer-pool-install')])
run('benchmark-configure',['cmake','-S','benchmarks/vulkan_allocation','-B','out/vulkan-buffer-pool-benchmark','-G','Visual Studio 17 2022','-A','x64','-DGFX_INSTALLED_SDK='+str(root/'out/vulkan-buffer-pool-install')])
run('benchmark-build',['cmake','--build','out/vulkan-buffer-pool-benchmark','--config','Release','--parallel','1'])
run('trace-configure',['cmake','-S','benchmarks/vulkan_allocation_trace','-B','out/vulkan-buffer-pool-trace','-G','Visual Studio 17 2022','-A','x64','-DGFX_INSTALLED_SDK='+str(root/'out/vulkan-buffer-pool-install')])
run('trace-build',['cmake','--build','out/vulkan-buffer-pool-trace','--config','Release','--parallel','1'])
