"""Validate real GPU rebuilding after process-local synthetic Vulkan loss results."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
from audit_licenses import ROOT,sdk_fingerprint


def require(value,message):
    if not value:raise ValueError(message)


def validate_rows(rows,fingerprint,validation,software=False):
    require(rows and rows[-1].get('event')=='result','Incomplete recovery result')
    result=rows[-1];adapters=[r for r in rows if r.get('event')=='adapter'];cycles=[r for r in rows if r.get('event')=='cycle']
    require(result['source']==fingerprint and result['validation'] is validation and result['synthetic_loss'] is True
            and result['actual_TDR'] is False,'Wrong source, validation or loss origin')
    ids={a['adapter_id'] for a in adapters};count=3*len(adapters)+1
    require(adapters and len(ids)==len(adapters)==result['adapters'] and (not software or all(a['software'] is True for a in adapters)),'Adapter scope mismatch')
    require(len(cycles)==result['cycles']==count,'Missing recovery cycle')
    generations=[]
    for adapter in ids:
        ordinary=[c for c in cycles if c['adapter_id']==adapter and c['automatic'] is False]
        require(sorted(c['mode'] for c in ordinary)==[1,2,3],'Missing submit/poll/fence loss coverage')
    automatic=[c for c in cycles if c['automatic'] is True]
    require(len(automatic)==1 and automatic[0]['mode']==1 and automatic[0]['adapter_id'] in ids,'Missing automatic selection recovery')
    for c in cycles:
        require(c['passed'] is True and type(c['old_generation']) is int and type(c['fresh_generation']) is int
                and c['old_generation']>0 and c['fresh_generation']>0 and c['old_generation']!=c['fresh_generation'],'Invalid generation')
        generations += [c['old_generation'],c['fresh_generation']]
    require(len(set(generations))==len(generations),'Generation alias or reuse')
    require(result['created']==result['destroyed']==result['failed_creates']==2*count and result['drains']==count
            and result['errors']==result['premature_creations']==result['live_instances']==result['live_devices']==0,'Unsafe or incomplete native closure')
    require(0<=result['window_images']<=count and result['images']==2*count+result['window_images']
            and result['verified_bytes']==result['images']*33*17*4 and result['verified_buffer_bytes']==2*count*4097,'Missing rebuilt GPU references')
    require(result['assertions']>0 and (len(adapters)==1 or result['reversed_enumerations']>0),'Selection reordering not exercised')
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','layer','manifest-dir','sdk-library','output'):p.add_argument('--'+name,type=pathlib.Path,required=True)
    p.add_argument('--validation-dir',type=pathlib.Path);p.add_argument('--icd',type=pathlib.Path);p.add_argument('--require-software',action='store_true');args=p.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    settings=args.output/'empty-layer-settings';settings.mkdir()
    paths=[args.binary,args.layer,args.sdk_library,args.manifest_dir/'VkLayer_gfx_recovery.json',pathlib.Path(__file__),ROOT/'tests/VulkanRecoveryTests.cpp',ROOT/'tests/fixtures/SpirvFixtures.hpp',*sorted((ROOT/'tests/vulkan_recovery').glob('*'))]
    if args.icd:paths.append(args.icd)
    if args.validation_dir:paths += [q for q in args.validation_dir.iterdir() if q.suffix.lower() in ('.dll','.so','.json')]
    frozen={str(q.resolve()):hashlib.sha256(q.read_bytes()).hexdigest() for q in paths if q.is_file()}
    fp=sdk_fingerprint(ROOT);require(fp.encode() in args.binary.read_bytes(),'Stale recovery SDK consumer')
    state=dict(SDK_source_fingerprint=fp,cases=[],status='running',serialized=True,require_software=args.require_software,
               source_base_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),frozen_inputs=frozen)
    def save():(args.output/'campaign.json').write_text(json.dumps(state,indent=2)+'\n',encoding='utf-8')
    save()
    try:
        for validation in ([False,True] if args.validation_dir else [False]):
            name='validation' if validation else 'normal';env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_'))}
            env.update(VK_LOADER_LAYERS_DISABLE='~implicit~',VK_INSTANCE_LAYERS='VK_LAYER_GFX_recovery',VK_LAYER_SETTINGS_PATH=str(settings.resolve()),
                       VK_LAYER_PATH=str(args.manifest_dir.resolve())+(os.pathsep+str(args.validation_dir.resolve()) if args.validation_dir else ''))
            if args.icd:env['VK_DRIVER_FILES']=str(args.icd.resolve())
            command=[str(args.binary.resolve()),'--observe',str(args.layer.resolve())]+(['--validation'] if validation else [])
            native=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=240)
            (args.output/(name+'.stdout')).write_bytes(native.stdout);(args.output/(name+'.stderr')).write_bytes(native.stderr)
            row=dict(name=name,command=command,returncode=native.returncode,environment={k:v for k,v in env.items() if k.startswith('VK_')});state['cases'].append(row);save()
            require(native.returncode==0,'Native recovery failure: '+name)
            row['result']=validate_rows([json.loads(l) for l in native.stdout.decode('utf-8').splitlines()],fp,validation,args.require_software);save();print('PASS '+name,flush=True)
        state['status']='passed'
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:state.update(status='failed',error=str(error))
    state['inputs_unchanged']=sdk_fingerprint(ROOT)==fp and all(pathlib.Path(q).exists() and hashlib.sha256(pathlib.Path(q).read_bytes()).hexdigest()==h for q,h in frozen.items());save()
    return 0 if state['status']=='passed' and state['inputs_unchanged'] else 1


if __name__=='__main__':sys.exit(main())
