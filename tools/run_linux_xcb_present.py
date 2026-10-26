"""Validate actual XCB acquire/draw/present and independent BGRA references."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
from audit_licenses import ROOT,sdk_fingerprint

DIAGNOSTIC_WRAPPER="""import os, subprocess, sys
with open(sys.argv[1], 'wb') as stream:
    result = subprocess.run(sys.argv[2:], stderr=stream, env={**os.environ, 'LD_DEBUG': 'libs'}, timeout=160)
sys.exit(result.returncode)
"""


def require(ok,message):
    if not ok:raise ValueError(message)


def validate_rows(rows,fp,control,validation):
    require(rows and rows[-1].get('event')=='present_result','Missing native present result')
    r=rows[-1];adapters=[x for x in rows if x.get('event')=='adapter'];chains=[x for x in rows if x.get('event')=='chain'];n=len(adapters)
    require(n==r['adapters']>0 and len({a['id'] for a in adapters})==n and all(a['software'] is True for a in adapters),'Wrong native software candidate set')
    count=n*(6 if control==0 else 0 if control==6 else 1)
    require(r['source']==fp and r['control']==control and r['validation'] is validation and r['human_display_accepted'] is False,'Wrong source/configuration/origin')
    require(r['cases']==len(chains)==count and r['rejected']==(n if control in (6,7) else 0),'Wrong coverage')
    for c in chains:
        require(c['native_format']==44 and c['native_images']>=c['native_min_images']>=c['requested_buffers'],'False native format/image count')
        require(c['native_mode']==(2 if c['vsync'] else 1 if control==8 else 0),'Wrong native present mode')
        require((c['width'],c['height'])==((17,9) if c['vsync'] else (65,7)),'Wrong native extent')
    if control==0:
        require(sorted((c['requested_buffers'],c['vsync']) for c in chains)==sorted([(i,s) for i in (2,3,4) for s in (False,True)]*n),'Missing buffer/vsync matrix')
    require(r['chains']==r['destroyed_chains']==count and r['acquires']==r['presents']==r['fenced_presents']==r['waited_presents']==3*count,'Missing actual present/completion or closure')
    require(r['instances']==r['destroyed_instances']==n+1 and r['devices']==r['destroyed_devices']==n and r['surfaces']==r['destroyed_surfaces'],'Native closure failed')
    require(r['images']==6*n+3*count and r['pixel_bytes']==10920*n+sum(c['width']*c['height']*12 for c in chains)
            and r['transfers']==2*n and r['transfer_bytes']==3640*n and r['padding_rows']==42*n+sum(c['height']*3 for c in chains),'Missing independent full images or byte references')
    require(r['validation_errors']==0,'Native validation errors')
    return r


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('binary','proxy','sdk-library','icd','output'):p.add_argument('--'+n,type=pathlib.Path,required=True)
    p.add_argument('--validation-dir',type=pathlib.Path);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=False);empty=args.output/'empty-settings';empty.mkdir()
    fp=sdk_fingerprint(ROOT);require(fp.encode() in args.binary.read_bytes(),'Stale installed SDK executable')
    sources=[ROOT/'tests/BgraSupport.hpp',ROOT/'tests/fixtures/SpirvFixtures.hpp',*sorted((ROOT/'tests/linux_xcb_present').glob('*')),*sorted((ROOT/'tests/linux_xcb_surface').glob('*')),pathlib.Path(__file__)]
    files=[args.binary,args.proxy,args.sdk_library,args.icd,*sources]
    if args.validation_dir:files += [args.validation_dir/'libVkLayer_khronos_validation.so',args.validation_dir/'VkLayer_khronos_validation.json']
    frozen={str(x.resolve()):hashlib.sha256(x.read_bytes()).hexdigest() for x in files if x.is_file()}
    receipt=dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),SDK_source_fingerprint=fp,status='running',frozen_inputs=frozen,cases=[])
    def save():(args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    save()
    try:
        env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','LD_','GFX_SURFACE_'))}
        env.update(VK_DRIVER_FILES=str(args.icd.resolve()),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str((args.validation_dir or empty).resolve()),VK_LAYER_SETTINGS_PATH=str(empty.resolve()),LP_NUM_THREADS='2',GFX_SURFACE_REAL_LOADER=str(pathlib.Path('/usr/lib/x86_64-linux-gnu/libvulkan.so.1').resolve()),LD_LIBRARY_PATH=str(args.proxy.parent.resolve()))
        validation=bool(args.validation_dir)
        for mode in (0,6,7,8):
            name='control-'+str(mode);child_env={**env,'GFX_SURFACE_CONTROL':str(mode)}
            native=[str(args.binary.resolve()),'--proxy',str(args.proxy.resolve())]+(['--validation'] if validation else [])
            native_stderr=args.output/(name+'.stderr')
            if validation:native=[sys.executable,'-c',DIAGNOSTIC_WRAPPER,str(native_stderr.resolve()),*native]
            command=['xvfb-run','-a','-s','-screen 0 800x600x24',*native]
            child=subprocess.run(command,cwd=ROOT,env=child_env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=180)
            (args.output/(name+'.stdout')).write_bytes(child.stdout)
            if validation:(args.output/(name+'.wrapper.stderr')).write_bytes(child.stderr)
            else:native_stderr.write_bytes(child.stderr)
            row=dict(name=name,control=mode,command=command,returncode=child.returncode,environment={k:v for k,v in child_env.items() if k.startswith(('VK_','GFX_SURFACE_','LD_'))});receipt['cases'].append(row);save();require(child.returncode==0,'Native presentation failed: '+name)
            row['result']=validate_rows([json.loads(l) for l in child.stdout.decode().splitlines()],fp,mode,validation)
            if validation:
                loaded={pathlib.Path(s.strip()).resolve() for s in re.findall(r'calling init:\s+([^\n]*libVkLayer_khronos_validation\.so)\s*$',native_stderr.read_text(),re.M)}
                require(loaded=={(args.validation_dir/'libVkLayer_khronos_validation.so').resolve()},'Actual validation runtime was not observed');row['loaded_layers']=[str(x) for x in loaded]
            save()
        receipt['status']='passed'
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:receipt.update(status='failed',error=str(error))
    receipt['inputs_unchanged']=fp==sdk_fingerprint(ROOT) and all(pathlib.Path(n).exists() and hashlib.sha256(pathlib.Path(n).read_bytes()).hexdigest()==h for n,h in frozen.items());save();print(receipt['status'],flush=True)
    return 0 if receipt['status']=='passed' and receipt['inputs_unchanged'] else 1


if __name__=='__main__':sys.exit(main())
