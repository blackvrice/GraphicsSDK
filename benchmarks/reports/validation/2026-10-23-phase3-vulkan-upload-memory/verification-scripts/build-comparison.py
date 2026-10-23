"""Build comparison programs serially against the validated installed SDK."""
import json
import pathlib
import subprocess
import time

root=pathlib.Path(__file__).resolve().parents[5]
out=root/'benchmarks/reports/validation/2026-10-23-phase3-vulkan-upload-memory'
commands=[]
for source,build in [('benchmarks/vulkan_upload','out/vulkan-upload-memory-bench'),('benchmarks/vulkan_upload_profile','out/vulkan-upload-profile-after')]:
    for phase,args in [('configure',['cmake','-S',source,'-B',build,'-G','Visual Studio 17 2022','-A','x64','-DGFX_INSTALLED_SDK='+str(root/'out/vulkan-upload-memory-install')]),('build',['cmake','--build',build,'--config','Release','--parallel','1'])]:
        name=pathlib.Path(build).name+'-'+phase
        start=time.monotonic();p=subprocess.run(args,cwd=root,capture_output=True,timeout=600)
        (out/(name+'.stdout')).write_bytes(p.stdout);(out/(name+'.stderr')).write_bytes(p.stderr)
        commands.append(dict(name=name,argv=args,returncode=p.returncode,seconds=time.monotonic()-start))
        (out/'comparison-build-commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        print(name,p.returncode,flush=True)
        assert p.returncode==0
