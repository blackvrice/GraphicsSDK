"""Run installed-SDK XCB lifetime controls against an actual software ICD."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
from audit_licenses import ROOT,sdk_fingerprint
from run_linux_xcb_present import DIAGNOSTIC_WRAPPER

CASES=['retained-and-stale','pending-chain-destruction','present-completion','partial-and-pre-present-failures','foreign-device','device-retirement']


def require(ok,message):
    if not ok:raise ValueError(message)


def validate_rows(rows,fp,validation):
    require(rows and rows[-1].get('event')=='lifetime_result','Missing lifetime result')
    r=rows[-1];cases=[x for x in rows if x.get('event')=='case']
    require([x['name'] for x in cases]==CASES and r['cases']==6,'Missing ordered lifetime cases')
    require(r['source']==fp and r['validation'] is validation and r['physical_gpu_accepted'] is False and r['human_display_accepted'] is False,'Wrong source/configuration/scope')
    for key,n in [('rejections',16),('references',3),('verified_bytes',10752),('gates',3),('blocked',2),('released',3),('cleaned',3),('faults',3),('order_errors',0),('validation_errors',0),('chains',8),('presents',11),('instances',4),('devices',3),('surfaces',7)]:
        require(r[key]==n,'Wrong lifetime coverage: '+key)
    require(r['presents']==r['fenced_presents']==r['waited_presents'],'Missing native presentation completion')
    for key in ('views','semaphores','fences','chains','instances','devices','surfaces'):
        require(r[key]>0 and r[key]==r['destroyed_'+key],'Unclosed native objects: '+key)
    require(cases[-1]['rejections']==r['rejections'] and cases[-1]['references']==r['references'],'Wrong cumulative result')
    return r


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','proxy','sdk-library','icd','output'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--validation-dir',type=pathlib.Path);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=False);empty=args.output/'empty-settings';empty.mkdir()
    fp=sdk_fingerprint(ROOT);require(fp.encode() in args.binary.read_bytes(),'Stale installed SDK executable')
    files=[args.binary,args.proxy,args.sdk_library,args.icd,ROOT/'tests/BgraSupport.hpp',ROOT/'tests/fixtures/SpirvFixtures.hpp',ROOT/'tools/run_linux_xcb_present.py',pathlib.Path(__file__),*sorted((ROOT/'tests/linux_xcb_lifetime').glob('*')),*sorted((ROOT/'tests/linux_xcb_surface').glob('*'))]
    if args.validation_dir:files += [args.validation_dir/'libVkLayer_khronos_validation.so',args.validation_dir/'VkLayer_khronos_validation.json']
    frozen={str(p.resolve()):hashlib.sha256(p.read_bytes()).hexdigest() for p in files if p.is_file()}
    receipt=dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),SDK_source_fingerprint=fp,status='running',frozen_inputs=frozen)
    def save():(args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    save()
    try:
        env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','LD_','GFX_SURFACE_'))}
        env.update(VK_DRIVER_FILES=str(args.icd.resolve()),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str((args.validation_dir or empty).resolve()),VK_LAYER_SETTINGS_PATH=str(empty.resolve()),LP_NUM_THREADS='2',GFX_SURFACE_CONTROL='0',GFX_SURFACE_REAL_LOADER=str(pathlib.Path('/usr/lib/x86_64-linux-gnu/libvulkan.so.1').resolve()),LD_LIBRARY_PATH=str(args.proxy.parent.resolve()))
        validation=bool(args.validation_dir);native=[str(args.binary.resolve()),'--proxy',str(args.proxy.resolve())]+(['--validation'] if validation else []);stderr=args.output/'native.stderr'
        if validation:native=[sys.executable,'-c',DIAGNOSTIC_WRAPPER,str(stderr.resolve()),*native]
        command=['xvfb-run','-a','-s','-screen 0 800x600x24',*native];receipt.update(command=command,environment={k:v for k,v in env.items() if k.startswith(('VK_','LD_','GFX_SURFACE_'))});save()
        child=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=180)
        (args.output/'native.stdout').write_bytes(child.stdout)
        (args.output/('wrapper.stderr' if validation else 'native.stderr')).write_bytes(child.stderr)
        receipt['returncode']=child.returncode;save();require(child.returncode==0,'Native lifetime process failed')
        receipt['result']=validate_rows([json.loads(l) for l in child.stdout.decode().splitlines()],fp,validation)
        if validation:
            loaded={pathlib.Path(s.strip()).resolve() for s in re.findall(r'calling init:\s+([^\n]*libVkLayer_khronos_validation\.so)\s*$',stderr.read_text(),re.M)}
            require(loaded=={(args.validation_dir/'libVkLayer_khronos_validation.so').resolve()},'Unobserved pinned validation layer');require(not re.search(r'^validation:',stderr.read_text(),re.M),'Delivered validation errors including teardown');receipt['loaded_layers']=[str(p) for p in loaded]
        receipt['status']='passed'
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as e:receipt.update(status='failed',error=str(e))
    receipt['inputs_unchanged']=fp==sdk_fingerprint(ROOT) and all(pathlib.Path(p).exists() and hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h for p,h in frozen.items());save();print(receipt['status'],flush=True)
    return 0 if receipt['status']=='passed' and receipt['inputs_unchanged'] else 1


if __name__=='__main__':sys.exit(main())
