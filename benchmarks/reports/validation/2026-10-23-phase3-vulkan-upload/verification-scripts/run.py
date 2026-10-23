"""Build only the new benchmark; frozen SDK/SDL binaries are reused, not rebuilt."""
import json
import os
import pathlib
import subprocess
import sys
import time

root=pathlib.Path(__file__).resolve().parents[5]
v=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload'
v.mkdir(exist_ok=True)
env={k:x for k,x in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_VULKAN_'))}
env['VK_LOADER_LAYERS_DISABLE']='~implicit~'
commands=[]
def run(name,args):
    start=time.monotonic()
    with (v/(name+'.log')).open('wb') as f:
        p=subprocess.Popen(args,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        for line in p.stdout:f.write(line);f.flush();print(name+':',line.decode('utf-8',errors='replace').rstrip(),flush=True)
        status=p.wait()
    commands.append(dict(name=name,argv=list(map(str,args)),returncode=status,seconds=time.monotonic()-start))
    (v/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    if status:raise RuntimeError(name+' failed')
run('configure',['cmake','-S','benchmarks/vulkan_upload','-B','out/vulkan-upload-final','-G','Visual Studio 17 2022','-A','x64'])
run('build',['cmake','--build','out/vulkan-upload-final','--config','Release','--parallel','1'])
run('ctest',['ctest','--test-dir','out/vulkan-upload-final','-C','Release','--output-on-failure','-j','1','--output-junit',str(v/'ctest.xml')])
run('python-tests',[sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py'])
run('SDK-reuse-audit',[sys.executable,'tools/audit_vulkan.py','--variant','presentation','--output','licenses/generated/windows-sdk-vulkan-presentation-release','--package','out/GraphicsSDK-0.0.1-dev-windows-x64-vulkan-presentation.zip','--check'])
run('matrix',[sys.executable,'tools/run_vulkan_upload.py','--exe','out/vulkan-upload-final/Release/vulkan_upload.exe','--output','benchmarks/results/2026-10-23-vulkan-upload','--repetitions','3','--warmup','5','--samples','30'])
