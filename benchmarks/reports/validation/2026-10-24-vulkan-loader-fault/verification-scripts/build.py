import json
import pathlib
import subprocess
import time
root=pathlib.Path(__file__).resolve().parents[5];v=pathlib.Path(__file__).resolve().parent.parent
commands=[]
def run(name,args):
    start=time.monotonic();p=subprocess.run(list(map(str,args)),cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (v/(name+'.log')).write_bytes(p.stdout);commands.append(dict(name=name,argv=list(map(str,args)),returncode=p.returncode,seconds=time.monotonic()-start));(v/'build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    print(name,p.returncode,p.stdout.decode('utf-8',errors='replace')[-2200:],flush=True)
    if p.returncode:raise RuntimeError(name+' failed')
for p in (root/'tests/vulkan_loader_fault').iterdir():
    if p.suffix in ('.cpp','.hpp') or p.name=='CMakeLists.txt':p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
for variant,install in (('after','out/vulkan-creation-install'),('before','out/vulkan-presentation-install')):
    folder='out/vulkan-loader-fault-'+variant
    assert not (root/folder).exists(),'Exclusive build root'
    run(variant+'-configure',['cmake','-S','tests/vulkan_loader_fault','-B',folder,'-G','Visual Studio 17 2022','-A','x64','-DGFX_INSTALLED_SDK='+str(root/install)])
    run(variant+'-build',['cmake','--build',folder,'--config','Release','--parallel','1'])
    run(variant+'-plan',['ctest','--test-dir',folder,'-C','Release','--output-on-failure'])
