"""Exercise installed SDK XCB surfaces with actual native loader observation."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys
from audit_licenses import ROOT,sdk_fingerprint


def require(b,message):
    if not b:raise ValueError(message)


def validate_rows(rows,fp,mode,disabled=False,headless=False):
    require(rows and rows[-1].get('event')=='result','Missing surface result')
    r=rows[-1];adapters=[x for x in rows if x.get('event')=='adapter'];n=len(adapters)
    require(n>0 and r['adapters']==n and len({a['id'] for a in adapters})==n and all(a['software'] is True for a in adapters),'Wrong software adapter set')
    require(r['source']==fp and r['mode']==mode and r['disabled'] is disabled and r['headless'] is headless,'Wrong source/configuration/control')
    require(r['swapchain_implemented'] is (not disabled) and r['presented'] is (mode==0 and not disabled and not headless),'Wrong compiled/present scope')
    created=not disabled and not headless and mode not in (1,4)
    require(r['instances']==r['destroyed_instances']==n+1,'Instance closure failure')
    require(r['surfaces']==r['destroyed_surfaces']==r['surface_queries']==r['visual_queries']==(2*n if created else 0),'Surface/queue closure failure')
    require(r['xcb_enabled_instances']==(0 if disabled or mode==1 else n+1),'Instance extension configuration mismatch')
    require(r['supported_surfaces']==(n if mode==0 and not disabled and not headless else 0),'False public surface support')
    require(r['native_surface_true']==(2*n if created and mode!=5 else 0)
            and r['native_visual_true']==(2*n if created else 0),'Missing actual native support query')
    require(r['buffer_bytes']==514*n and r['assertions']>0 and r['family']==0,'Missing headless reference or unexpected software queue')
    return r


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','disabled-binary','sdk-library','disabled-library','proxy','real-loader','icd','output'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    args=p.parse_args();require(platform.system()=='Linux','Actual Linux required');args.output.mkdir(parents=True,exist_ok=False)
    settings=args.output/'empty-settings';settings.mkdir();fp=sdk_fingerprint(ROOT)
    inputs=[args.binary,args.disabled_binary,args.sdk_library,args.disabled_library,args.proxy,args.real_loader,args.icd,pathlib.Path(__file__),*sorted((ROOT/'tests/linux_xcb_surface').glob('*'))]
    frozen={str(x.resolve()):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs if x.is_file()}
    require(all(fp.encode() in x.read_bytes() for x in (args.binary,args.disabled_binary)),'Stale installed SDK consumer')
    state=dict(task='P3-WSI-02',source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),SDK_source_fingerprint=fp,status='running',frozen_inputs=frozen,cases=[])
    def save():(args.output/'receipt.json').write_text(json.dumps(state,indent=2)+'\n',encoding='utf-8')
    save()
    try:
        base={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','LD_','GFX_SURFACE_'))}
        base.update(VK_DRIVER_FILES=str(args.icd.resolve()),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(settings.resolve()),VK_LAYER_SETTINGS_PATH=str(settings.resolve()),LP_NUM_THREADS='2',LD_LIBRARY_PATH=str(args.proxy.parent.resolve()),GFX_SURFACE_REAL_LOADER=str(args.real_loader.resolve()))
        cases=[('normal',0,False,False),('missing-extension',1,False,False),('surface-queue-rejected',2,False,False),('visual-queue-rejected',3,False,False),('missing-procedure',4,False,False),('surface-lost',5,False,False),('headless',0,False,True),('disabled',0,True,False)]
        for name,mode,disabled,headless in cases:
            command=[str((args.disabled_binary if disabled else args.binary).resolve()),'--proxy',str(args.proxy.resolve())]+(['--disabled'] if disabled else [])+(['--headless'] if headless else [])
            if not headless:command=['xvfb-run','-a','-s','-screen 0 800x600x24',*command]
            env={**base,'GFX_SURFACE_CONTROL':str(mode)}
            if headless:env['DISPLAY']=':65533'
            child=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=90)
            (args.output/(name+'.stdout')).write_bytes(child.stdout);(args.output/(name+'.stderr')).write_bytes(child.stderr)
            case=dict(name=name,command=command,returncode=child.returncode,environment={k:v for k,v in env.items() if k.startswith(('VK_','GFX_SURFACE_','LD_','DISPLAY'))});state['cases'].append(case);save()
            require(child.returncode==0,'Native surface case failed: '+name)
            case['result']=validate_rows([json.loads(l) for l in child.stdout.decode().splitlines()],fp,mode,disabled,headless);save()
        state['status']='passed'
    except (ValueError,KeyError,OSError,subprocess.SubprocessError) as error:state.update(status='failed',error=str(error))
    state['inputs_unchanged']=sdk_fingerprint(ROOT)==fp and all(pathlib.Path(n).exists() and hashlib.sha256(pathlib.Path(n).read_bytes()).hexdigest()==h for n,h in frozen.items());save();print(state['status'],flush=True)
    return 0 if state['status']=='passed' and state['inputs_unchanged'] else 1


if __name__=='__main__':sys.exit(main())
