import json
import pathlib
import subprocess
import time
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent;commands=[]
for p in (root/'experiments/vulkan_pool_rebase_fault').iterdir():
    if p.suffix in ('.cpp','.hpp') or p.name=='CMakeLists.txt':p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
def run(name,args):
    start=time.monotonic();p=subprocess.run(list(map(str,args)),cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (v/(name+'.log')).write_bytes(p.stdout);commands.append(dict(name=name,argv=list(map(str,args)),returncode=p.returncode,seconds=time.monotonic()-start));(v/'build-commands.json').write_text(json.dumps(commands,indent=2)+'\n');print(name,p.returncode,p.stdout.decode(errors='replace')[-1000:],flush=True)
    if p.returncode:raise RuntimeError(name+' failed')
for variant,source in (('regression','experiments/vulkan_buffer_pool_fault'),('mixed','experiments/vulkan_pool_rebase_fault')):
    folder='out/vulkan-pool-rebase-fault-'+variant;assert not (root/folder).exists()
    run(variant+'-configure',['cmake','-S',source,'-B',folder,'-G','Visual Studio 17 2022','-A','x64','-DGFX_INSTALLED_SDK='+str(root/'out/vulkan-pool-churn-install')])
    run(variant+'-build',['cmake','--build',folder,'--config','Release','--parallel','1'])
    if variant=='regression':run('fault-plan',['ctest','--test-dir',folder,'-C','Release','--output-on-failure'])
