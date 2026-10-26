"""Run one installed Core through the complete scoped XCB software-ICD matrix."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
from audit_licenses import ROOT,sdk_fingerprint

SUITES=('present','lifetime','resize','events','matrix')


def require(ok,message):
    if not ok:raise ValueError(message)


def validate_campaign(receipts,fp,validation,controls):
    require(set(receipts)==set(SUITES),'Incomplete integrated XCB suite set')
    for name,r in receipts.items():
        require(r['status']=='passed' and r['inputs_unchanged'] is True and r['SDK_source_fingerprint']==fp,'Failed, changed or mixed SDK: '+name)
        results=r['cases'] if name=='present' else [r]
        if name=='present':require([c['control'] for c in results]==[0,6,7,8],'Missing unsupported format/FIFO-only/mailbox controls')
        for case in results:
            x=case['result'];require(case['returncode']==0 and x['source']==fp and x['validation'] is validation and x['validation_errors']==0,'Wrong integrated native result: '+name)
            require(x['human_display_accepted'] is False and x.get('physical_gpu_accepted',False) is False,'Software scope changed')
    require((controls is not None)==validation,'Isolated validation controls missing/unexpected')
    if validation:
        require(controls['source']==fp and controls['adapters']==1 and controls['native_core_errors']==32 and controls['native_sync_hazards']==1 and controls['sync_disabled_controls']==1 and controls['synthetic_errors']==1025 and controls['invalid_GPU_submissions']==0,'Incomplete core/sync/counter controls')
    return dict(suites=list(SUITES),physical_gpu_accepted=False,human_display_accepted=False,validation=validation)


def validate_controls(stdout,fp,layer):
    require('source='+fp+' isolated_expected_errors=true' in stdout,'Controls use a different SDK')
    matches=re.findall(r'PASS assertions=(\d+) adapters=(\d+) native_core_errors=(\d+) native_sync_hazards=(\d+) sync_disabled_controls=(\d+) synthetic_errors=(\d+) invalid_GPU_submissions=(\d+)',stdout)
    require(len(matches)==1,'Missing unique positive-control result')
    loaded=re.findall(r'^loaded_layer=(.+)$',stdout,re.M);require(len(loaded)==3 and set(loaded)=={layer},'Controls did not observe all pinned layer instances')
    return dict(source=fp,loaded_layers=loaded,**dict(zip(('assertions','adapters','native_core_errors','native_sync_hazards','sync_disabled_controls','synthetic_errors','invalid_GPU_submissions'),map(int,matches[0]))))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('consumer-root','sdk-library','icd','output'):p.add_argument('--'+n,type=pathlib.Path,required=True)
    p.add_argument('--validation-dir',type=pathlib.Path);args=p.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    fp=sdk_fingerprint(ROOT);files=[args.sdk_library,args.icd,pathlib.Path(__file__),ROOT/'tests/VulkanValidationTests.cpp',ROOT/'src/backends/vulkan/VulkanValidation.hpp']
    files.extend(x for folder in (ROOT/'tests',ROOT/'examples/xcb_presentation',ROOT/'tools',args.consumer_root) for x in folder.rglob('*') if x.is_file() and ((folder==args.consumer_root and (x.name.startswith('gfx_xcb_') or x.name=='libvulkan.so.1')) or (folder!=args.consumer_root and (x.name.startswith(('run_linux_xcb_','test_linux_xcb_')) or 'linux_xcb_' in x.as_posix() or folder.name=='xcb_presentation'))))
    if args.validation_dir:files.extend(args.validation_dir/n for n in ('libVkLayer_khronos_validation.so','VkLayer_khronos_validation.json'))
    frozen={str(x.resolve()):hashlib.sha256(x.read_bytes()).hexdigest() for x in files if x.is_file() and x.suffix!='.pyc' and '__pycache__' not in x.parts}
    receipt=dict(source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),SDK_source_fingerprint=fp,status='running',frozen_inputs=frozen,suites={})
    def save():(args.output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    save()
    try:
        validation=bool(args.validation_dir)
        for name in SUITES:
            build=args.consumer_root/name;command=[sys.executable,str(ROOT/f'tools/run_linux_xcb_{name}.py'),'--binary',str(build/f'gfx_xcb_{name}'),'--proxy',str(build/'proxy/libvulkan.so.1'),'--sdk-library',str(args.sdk_library),'--icd',str(args.icd),'--output',str(args.output/name)]
            if validation:command+=['--validation-dir',str(args.validation_dir)]
            child=subprocess.run(command,cwd=ROOT,capture_output=True,timeout=800);(args.output/(name+'.runner.stdout')).write_bytes(child.stdout);(args.output/(name+'.runner.stderr')).write_bytes(child.stderr)
            receipt['suites'][name]=dict(returncode=child.returncode,command=command);save();require(child.returncode==0,'Integrated suite failed: '+name)
            # Inspect native stderr all the way through teardown, including old suites.
            for stderr in (args.output/name).glob('*.stderr'):require(not re.search(rb'^validation:',stderr.read_bytes(),re.M),'Native delivered error in '+str(stderr))
        controls=None
        if validation:
            layer=str((args.validation_dir/'libVkLayer_khronos_validation.so').resolve());empty=args.output/'controls-empty-settings';empty.mkdir()
            env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','LD_','GFX_'))}
            env.update(VK_DRIVER_FILES=str(args.icd.resolve()),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_LAYER_PATH=str(args.validation_dir.resolve()),VK_LAYER_SETTINGS_PATH=str(empty.resolve()),GFX_EXPECT_VULKAN_LAYER=layer,LD_DEBUG='libs',LP_NUM_THREADS='2')
            binary=args.consumer_root/'matrix/gfx_xcb_validation_controls';require(fp.encode() in binary.read_bytes(),'Stale control executable')
            child=subprocess.run([str(binary.resolve())],cwd=ROOT,env=env,capture_output=True,timeout=180);(args.output/'controls.stdout').write_bytes(child.stdout);(args.output/'controls.stderr').write_bytes(child.stderr);require(child.returncode==0,'Isolated error controls failed')
            controls=validate_controls(child.stdout.decode(),fp,layer);require('calling init:' in child.stderr.decode() and layer in child.stderr.decode(),'Control runtime load not observed');receipt['controls']=controls
        inputs={n:json.loads((args.output/n/'receipt.json').read_text()) for n in SUITES};receipt['result']=validate_campaign(inputs,fp,validation,controls);receipt['status']='passed'
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as e:receipt.update(status='failed',error=str(e))
    receipt['inputs_unchanged']=fp==sdk_fingerprint(ROOT) and all(pathlib.Path(n).is_file() and hashlib.sha256(pathlib.Path(n).read_bytes()).hexdigest()==h for n,h in frozen.items());save();print(receipt['status'],flush=True)
    return 0 if receipt['status']=='passed' and receipt['inputs_unchanged'] else 1


if __name__=='__main__':sys.exit(main())
