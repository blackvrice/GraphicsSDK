import json
import os
import pathlib
import subprocess
import time
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent
for name in ('CMakeLists.txt','src/backends/vulkan/Vulkan.cpp','src/backends/vulkan/Presentation.hpp','src/backends/vulkan/Creation.hpp','tests/VulkanCreationTests.cpp'):
    p=root/name;p.write_bytes(p.read_text(encoding='utf-8').replace('\n','\r\n').encode())
receipt=json.loads((root/'out/vulkan-validation-runtime/receipt.json').read_text(encoding='utf-8'));dll=next(root/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
settings=v/'settings';settings.mkdir();(settings/'vk_layer_settings.txt').write_text('# SDK pNext controls validation.\n')
env={k:x for k,x in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_','GFX_TRACE_'))}
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
run('product-configure',['cmake','-S','.', '-B','out/vulkan-creation-product','-G','Visual Studio 17 2022','-A','x64','-DGFX_ENABLE_D3D12=ON','-DGFX_ENABLE_VULKAN=ON','-DGFX_VULKAN_VALIDATION_TESTS=ON','-DGFX_BUILD_EXAMPLES=OFF','-DGFX_VULKAN_HEADERS='+str(root/'.deps/Vulkan-Headers')])
run('product-build',['cmake','--build','out/vulkan-creation-product','--config','Release','--parallel','1'])
run('product-tests',['ctest','--test-dir','out/vulkan-creation-product','-C','Release','--output-on-failure','-j','1','-E','^memory_reclamation_','--output-junit',str(v/'product-tests.xml')])
run('product-install',['cmake','--install','out/vulkan-creation-product','--config','Release','--prefix',str(root/'out/vulkan-creation-install')])
