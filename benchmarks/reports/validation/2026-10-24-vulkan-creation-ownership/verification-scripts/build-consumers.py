import json
import pathlib
import subprocess
import time
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent
commands=[]
def run(name,args,expected=0):
    start=time.monotonic();p=subprocess.run(args,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (v/(name+'.log')).write_bytes(p.stdout);commands.append(dict(name=name,argv=list(map(str,args)),returncode=p.returncode,expected=expected,seconds=time.monotonic()-start));(v/'consumer-build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    print(name,p.returncode,p.stdout.decode('utf-8',errors='replace')[-1400:],flush=True)
    if p.returncode!=expected:raise RuntimeError(name+' failed')
for variant,source,install in (('before','out/vulkan-creation-before-source','out/vulkan-presentation-install'),('after','.','out/vulkan-creation-install')):
    folder='out/vulkan-creation-cpu-'+variant+'-v2'
    run('cpu-'+variant+'-configure',['cmake','-S','tests/vulkan_creation_fault','-B',folder,'-G','Visual Studio 17 2022','-A','x64','-DGFX_IMPLEMENTATION_ROOT='+str((root/source).resolve()),'-DGFX_INSTALLED_SDK='+str(root/install)])
    run('cpu-'+variant+'-build',['cmake','--build',folder,'--config','Release','--parallel','1'])
    run('cpu-'+variant+'-run',[str(root/folder/'Release/vulkan_creation_fault.exe')],2 if variant=='before' else 0)
    folder='out/vulkan-creation-native-'+variant
    run('native-'+variant+'-configure',['cmake','-S','experiments/vulkan_buffer_pool_fault','-B',folder,'-G','Visual Studio 17 2022','-A','x64','-DGFX_INSTALLED_SDK='+str(root/install)])
    run('native-'+variant+'-build',['cmake','--build',folder,'--config','Release','--parallel','1'])
