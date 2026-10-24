import json
import pathlib
import subprocess
import time
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent;commands=[]
for p in (root/'experiments/vulkan_pool_churn').iterdir():
    if p.suffix in ('.cpp','.hpp') or p.name=='CMakeLists.txt':p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
def run(name,args):
    start=time.monotonic();p=subprocess.run(list(map(str,args)),cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (v/(name+'.log')).write_bytes(p.stdout);commands.append(dict(name=name,argv=list(map(str,args)),returncode=p.returncode,seconds=time.monotonic()-start));(v/'consumer-build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    print(name,p.returncode,p.stdout.decode(errors='replace')[-1500:],flush=True)
    if p.returncode:raise RuntimeError(name+' failed')
for variant,install in (('product','out/vulkan-creation-install'),('candidate','out/vulkan-pool-churn-install')):
    folder='out/vulkan-pool-churn-'+variant;assert not (root/folder).exists()
    run(variant+'-configure',['cmake','-S','experiments/vulkan_pool_churn','-B',folder,'-G','Visual Studio 17 2022','-A','x64','-DGFX_INSTALLED_SDK='+str(root/install)])
    run(variant+'-build',['cmake','--build',folder,'--config','Release','--parallel','1'])
    run(variant+'-model',['ctest','--test-dir',folder,'-C','Release','--output-on-failure'])
