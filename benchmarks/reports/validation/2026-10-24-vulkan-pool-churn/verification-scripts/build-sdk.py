import json
import os
import pathlib
import subprocess
import time
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent
receipt=json.loads((root/'out/vulkan-validation-runtime/receipt.json').read_text(encoding='utf-8'));dll=next(root/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
env={k:x for k,x in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_','GFX_TRACE_'))}
settings=v/'settings';settings.mkdir();(settings/'vk_layer_settings.txt').write_text('# SDK pNext validation controls\n')
env.update(VK_LAYER_PATH=str(dll.parent),VK_LAYER_SETTINGS_PATH=str(settings),VK_LOADER_LAYERS_DISABLE='~implicit~',GFX_EXPECT_VULKAN_LAYER=str(dll))
commands=[]
def run(name,args):
    start=time.monotonic();p=subprocess.run(list(map(str,args)),cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (v/(name+'.log')).write_bytes(p.stdout);commands.append(dict(name=name,argv=list(map(str,args)),returncode=p.returncode,seconds=time.monotonic()-start));(v/'sdk-build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    print(name,p.returncode,p.stdout.decode(errors='replace')[-1200:],flush=True)
    if p.returncode:raise RuntimeError(name+' failed')
run('candidate-configure',['cmake','-S','out/vulkan-pool-churn-source','-B','out/vulkan-pool-churn-sdk','-G','Visual Studio 17 2022','-A','x64','-DGFX_ENABLE_D3D12=ON','-DGFX_ENABLE_VULKAN=ON','-DGFX_VULKAN_VALIDATION_TESTS=ON','-DGFX_BUILD_EXAMPLES=OFF','-DGFX_VULKAN_HEADERS='+str(root/'.deps/Vulkan-Headers')])
run('candidate-build',['cmake','--build','out/vulkan-pool-churn-sdk','--config','Release','--parallel','1'])
run('candidate-tests',['ctest','--test-dir','out/vulkan-pool-churn-sdk','-C','Release','--output-on-failure','-j','1','-E','^memory_reclamation_','--output-junit',str(v/'candidate-tests.xml')])
run('candidate-install',['cmake','--install','out/vulkan-pool-churn-sdk','--config','Release','--prefix',str(root/'out/vulkan-pool-churn-install')])
