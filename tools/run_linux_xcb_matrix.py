"""Run installed-SDK XCB multi-window references against an actual software ICD."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import shutil
from audit_licenses import ROOT,sdk_fingerprint
from run_linux_xcb_present import DIAGNOSTIC_WRAPPER

def require(ok,message):
    if not ok:raise ValueError(message)


def validate_rows(rows,fp,validation):
    require(rows and rows[-1].get('event')=='matrix_result','Missing multi-window result')
    r=rows[-1];pairs=[x for x in rows if x.get('event')=='pair'];configs=[x for x in rows if x.get('event')=='configuration'];images=[x for x in rows if x.get('event')=='image']
    require(r['source']==fp and r['validation'] is validation and r['physical_gpu_accepted'] is False and r['human_display_accepted'] is False,'Wrong source/configuration/scope')
    for key,n in [('pairs',6),('images',102),('pixel_bytes',383832),('padding_rows',1062),('rejections',24),('acquires',114),('presents',102),('chains',30),('surfaces',18),('instances',2),('devices',1),('order_errors',0),('validation_errors',0)]:
        require(type(r[key]) is int and r[key]==n,'Wrong multi-window coverage: '+key)
    require([(p['pair'],p['buffers'],p['first_vsync'],p['images'],p['rejections']) for p in pairs]==[(i,b,v,17,4) for i,(b,v) in enumerate((b,v) for b in (2,3,4) for v in (False,True))],'Missing buffer/vsync pair coverage')
    require(len(configs)==18 and len(images)==102,'Missing configurations/full image references')
    for i,p in enumerate(pairs):
        c=[x for x in configs if x['pair']==i];require([x['vsync'] for x in c]==[p['first_vsync'],not p['first_vsync'],p['first_vsync']],'Missing peer/replacement modes')
        for x in c:require(x['buffers']==p['buffers'] and x['format']==44 and x['mode']==(2 if x['vsync'] else 0) and x['images']>=x['minimum']>=max(x['buffers'],x['mode_minimum'])>0 and (not x['mode_maximum'] or x['minimum']<=x['mode_maximum']),'Wrong native mode/count constraints')
        sequence=[(w,h,k,d) for d in range(3) for w,h,k,d in [(17,9,0,d),(65,7,1,2-d)]]+[(65,7,1,1),(129,13,1,2)]+[(w,h,k,d) for d in range(3) for w,h,k,d in [(83,11,0,d),(129,13,1,2-d)]]+[(129,13,1,0),(83,11,0,2),(129,13,1,1)]
        require([(x['width'],x['height'],x['window'],x['draw']) for x in images if x['pair']==i]==sequence,'Missing interleaved independent window references')
    require(r['presents']==r['fenced_presents']==r['waited_presents'],'Missing present completion')
    for k in ('views','semaphores','fences','chains','instances','devices','surfaces'):require(type(r[k]) is int and r[k]>0 and r[k]==r['destroyed_'+k],'Unclosed native objects: '+k)
    return r


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','proxy','sdk-library','icd','output'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--validation-dir',type=pathlib.Path);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=False);empty=args.output/'empty-settings';empty.mkdir()
    fp=sdk_fingerprint(ROOT);require(fp.encode() in args.binary.read_bytes(),'Stale installed SDK executable')
    manager=pathlib.Path(shutil.which('openbox') or 'missing-openbox').resolve();require(manager.is_file(),'Actual Openbox runtime required')
    files=[args.binary,args.proxy,args.sdk_library,args.icd,manager,ROOT/'tests/BgraSupport.hpp',ROOT/'tests/fixtures/SpirvFixtures.hpp',ROOT/'tools/run_linux_xcb_present.py',pathlib.Path(__file__),*sorted((ROOT/'tests/linux_xcb_matrix').glob('*')),*sorted((ROOT/'tests/linux_xcb_surface').glob('*')),*sorted((ROOT/'tests/linux_xcb_lifetime').glob('*')),*sorted((ROOT/'examples/xcb_presentation').glob('*')),ROOT/'examples/presentation/PresentationPolicy.hpp',ROOT/'tools/xcb_window_manager.py']
    if args.validation_dir:files += [args.validation_dir/'libVkLayer_khronos_validation.so',args.validation_dir/'VkLayer_khronos_validation.json']
    frozen={str(p.resolve()):hashlib.sha256(p.read_bytes()).hexdigest() for p in files if p.is_file()}
    receipt=dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),SDK_source_fingerprint=fp,status='running',frozen_inputs=frozen)
    def save():(args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    save()
    try:
        versions=subprocess.check_output(['dpkg-query','-W','openbox','libxcb1','libvulkan1','mesa-vulkan-drivers','xvfb'],text=True)
        version=subprocess.check_output([str(manager),'--version'],text=True);(args.output/'window-runtime.txt').write_text(versions+'\n'+version)
        receipt['window_manager']=dict(path=str(manager),sha256=frozen[str(manager)],version=version.strip(),runtime_redistributed=False)
        env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','LD_','GFX_SURFACE_'))}
        env.update(VK_DRIVER_FILES=str(args.icd.resolve()),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str((args.validation_dir or empty).resolve()),VK_LAYER_SETTINGS_PATH=str(empty.resolve()),LP_NUM_THREADS='2',GFX_SURFACE_CONTROL='0',GFX_SURFACE_REAL_LOADER=str(pathlib.Path('/usr/lib/x86_64-linux-gnu/libvulkan.so.1').resolve()),LD_LIBRARY_PATH=str(args.proxy.parent.resolve()))
        validation=bool(args.validation_dir);native=[str(args.binary.resolve()),'--proxy',str(args.proxy.resolve())]+(['--validation'] if validation else []);stderr=args.output/'native.stderr'
        if validation:native=[sys.executable,'-c',DIAGNOSTIC_WRAPPER,str(stderr.resolve()),*native]
        command=['xvfb-run','-a','-s','-screen 0 1024x768x24',sys.executable,str(ROOT/'tools/xcb_window_manager.py'),str((args.output/'window-manager.log').resolve()),str(ROOT/'examples/xcb_presentation/openbox.xml'),*native];receipt.update(command=command,environment={k:v for k,v in env.items() if k.startswith(('VK_','LD_','GFX_SURFACE_'))});save()
        child=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=180)
        (args.output/'native.stdout').write_bytes(child.stdout)
        (args.output/('wrapper.stderr' if validation else 'native.stderr')).write_bytes(child.stderr)
        receipt['returncode']=child.returncode;save();require(child.returncode==0,'Native multi-window process failed')
        receipt['result']=validate_rows([json.loads(l) for l in child.stdout.decode().splitlines()],fp,validation)
        if validation:
            loaded={pathlib.Path(s.strip()).resolve() for s in re.findall(r'calling init:\s+([^\n]*libVkLayer_khronos_validation\.so)\s*$',stderr.read_text(),re.M)}
            require(loaded=={(args.validation_dir/'libVkLayer_khronos_validation.so').resolve()},'Unobserved pinned validation layer');require(not re.search(r'^validation:',stderr.read_text(),re.M),'Delivered validation errors including teardown');receipt['loaded_layers']=[str(p) for p in loaded]
        receipt['status']='passed'
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as e:receipt.update(status='failed',error=str(e))
    receipt['inputs_unchanged']=fp==sdk_fingerprint(ROOT) and all(pathlib.Path(p).exists() and hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()==h for p,h in frozen.items());save();print(receipt['status'],flush=True)
    return 0 if receipt['status']=='passed' and receipt['inputs_unchanged'] else 1


if __name__=='__main__':sys.exit(main())
