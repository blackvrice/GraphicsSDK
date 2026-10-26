"""Run installed-SDK XCB resize/retirement controls against an actual software ICD."""
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

CASES=['geometry-and-retained-prechecks','pending-submission-resize','acquire-out-of-date','acquire-suboptimal','present-out-of-date','present-suboptimal','failed-recreation-retirement-and-retry']


def require(ok,message):
    if not ok:raise ValueError(message)


def validate_rows(rows,fp,validation):
    require(rows and rows[-1].get('event')=='resize_result','Missing resize result')
    r=rows[-1];cases=[x for x in rows if x.get('event')=='case']
    require([x['name'] for x in cases]==CASES and r['cases']==7,'Missing ordered resize cases')
    require(r['source']==fp and r['validation'] is validation and r['physical_gpu_accepted'] is False and r['human_display_accepted'] is False,'Wrong source/configuration/scope')
    for key,n in [('resizes',13),('rejections',24),('images',43),('references',3),('work_readback_bytes',10752),('creates',23),('old_attempts',13),('fresh_attempts',10),('native_failures',1),('enum_failures',1),('view_failures',1),('acquire_out_of_date',1),('acquire_suboptimal',1),('present_out_of_date',1),('present_suboptimal',1),('gates',1),('blocked',1),('released',1),('cleaned',1),('order_errors',0),('validation_errors',0),('chains',22),('presents',45),('instances',2),('devices',1),('surfaces',7)]:
        require(type(r[key]) is int and r[key]==n,'Wrong resize coverage: '+key)
    require(r['allocation_callbacks']>0,'No actual failing allocation callback')
    # 43 ordinary full images plus three complete 65x7 queued-work readbacks.
    ordinary=[(65,7,12),(83,11,21),(17,9,6),(1,1,1),(129,13,3)]
    require(sum(c for w,h,c in ordinary)==43,'Validator reference matrix inconsistent')
    require(r['pixel_bytes']==sum(w*h*c*4 for w,h,c in ordinary)+3*65*7*4 and r['padding_rows']==sum(h*c for w,h,c in ordinary),'Incomplete full-image references')
    require(r['presents']==r['fenced_presents']==r['waited_presents'],'Missing actual present completion')
    for key in ('views','semaphores','fences','chains','instances','devices','surfaces'):
        require(r[key]>0 and r[key]==r['destroyed_'+key],'Unclosed native objects: '+key)
    for key in ('resizes','rejections','images','references'):require(cases[-1][key]==r[key],'Wrong cumulative result: '+key)
    return r


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','proxy','sdk-library','icd','output'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--validation-dir',type=pathlib.Path);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=False);empty=args.output/'empty-settings';empty.mkdir()
    fp=sdk_fingerprint(ROOT);require(fp.encode() in args.binary.read_bytes(),'Stale installed SDK executable')
    files=[args.binary,args.proxy,args.sdk_library,args.icd,ROOT/'tests/BgraSupport.hpp',ROOT/'tests/fixtures/SpirvFixtures.hpp',ROOT/'tools/run_linux_xcb_present.py',pathlib.Path(__file__),*sorted((ROOT/'tests/linux_xcb_resize').glob('*')),*sorted((ROOT/'tests/linux_xcb_surface').glob('*')),*sorted((ROOT/'tests/linux_xcb_lifetime').glob('*'))]
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
        receipt['returncode']=child.returncode;save();require(child.returncode==0,'Native resize process failed')
        receipt['result']=validate_rows([json.loads(l) for l in child.stdout.decode().splitlines()],fp,validation)
        if validation:
            loaded={pathlib.Path(s.strip()).resolve() for s in re.findall(r'calling init:\s+([^\n]*libVkLayer_khronos_validation\.so)\s*$',stderr.read_text(),re.M)}
            require(loaded=={(args.validation_dir/'libVkLayer_khronos_validation.so').resolve()},'Unobserved pinned validation layer');require(not re.search(r'^validation:',stderr.read_text(),re.M),'Delivered validation errors including teardown');receipt['loaded_layers']=[str(p) for p in loaded]
        receipt['status']='passed'
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as e:receipt.update(status='failed',error=str(e))
    receipt['inputs_unchanged']=fp==sdk_fingerprint(ROOT) and all(pathlib.Path(p).exists() and hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h for p,h in frozen.items());save();print(receipt['status'],flush=True)
    return 0 if receipt['status']=='passed' and receipt['inputs_unchanged'] else 1


if __name__=='__main__':sys.exit(main())
