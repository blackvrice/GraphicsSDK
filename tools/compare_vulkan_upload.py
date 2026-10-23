"""Fresh interleaved before/after/SDL Vulkan B-upload comparison; diagnostic CPU intervals stay separate."""
import argparse
import json
import os
import pathlib
import random
import subprocess
import time
import math
import run_vulkan_upload as u
import audit_licenses as audit

PROFILE_CONTRACT='vulkan_upload_cpu_stage_partition_v1'
def validate_profile(r,reference,sdk_source,samples=15,warmup=3):
    u.require(r.get('category')=='diagnostic_CPU_API_intervals' and r.get('contract')==PROFILE_CONTRACT and r.get('api')=='vulkan' and r.get('gpu_ms') is None,'Different CPU diagnostic scope')
    u.require(r.get('SDK_source_fingerprint')==sdk_source and r.get('validation') is False and r.get('sample_count')==samples and r.get('warmup')==warmup,'Different profile build/mode/sample count')
    u.require(all(r.get(k)==reference.get(k) for k in ('device_name','device_uuid','driver_uuid','device_luid','environment')),'Profile device/environment changed')
    w,h,bpp=u.SHAPES[r['scenario']];u.require(r['verified_images']==samples+warmup and r['verified_bytes']==w*h*bpp*(samples+warmup),'Profile image verification incomplete')
    u.require(r.get('columns')==['write_ms','begin_ms','record_ms','submit_ms','wait_ms','total_ms'],'Different stage boundaries')
    u.require(len(r['samples_ms'])==samples,'Wrong profile sample count')
    for row in r['samples_ms']:
        u.require(len(row)==6 and all(type(x) in (int,float) and math.isfinite(x) and x>=0 for x in row) and row[-1]>0,'Invalid CPU API interval')
        u.require(abs(sum(row[:5])-row[5])<=max(1e-6,row[5]*1e-10),'Stage intervals do not partition total')

def run(args):
    root=u.ROOT;out=args.output.resolve();u.require(not out.exists() and out.is_relative_to(root),'New repository-local output required')
    programs={k:getattr(args,k.replace('-','_')).resolve() for k in ('before','after','profile-before','profile-after')}
    binaries={k:u.record(p) for k,p in programs.items()};pin=json.loads((root/'benchmarks/competitors/sources.lock.json').read_text())['sdl']['commit']
    source=u.fingerprint();baseline=json.loads((root/'benchmarks/reports/2026-10-23-phase3-vulkan-upload/summary.json').read_text())['SDK_source_fingerprint'];current=audit.sdk_fingerprint(root)
    if args.candidate_receipt:
        import vulkan_upload_experiment as experiment
        u.require(args.candidate_receipt.resolve()==experiment.FOLDER/'receipt.json','Only the frozen reviewed candidate receipt is accepted')
        candidate,_=experiment.candidate();current=candidate['SDK_source_fingerprint']
    u.require(current!=baseline,'Before and after SDK fingerprints must differ')
    receipt=json.loads((root/'out/vulkan-validation-runtime/receipt.json').read_text());dll=next(root/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
    def frozen():
        for r in list(binaries.values())+receipt['artifacts']:u.require(u.record(root/r['path'])==r,'Frozen executable/runtime changed')
    frozen();out.mkdir(parents=True);settings=out/'settings';settings.mkdir();(settings/'vk_layer_settings.txt').write_text('khronos_validation.enables = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT\nkhronos_validation.syncval_submit_time_validation = true\nkhronos_validation.duplicate_message_limit = 0\nkhronos_validation.message_id_filter =\n')
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_VULKAN_'))};env['VK_LOADER_LAYERS_DISABLE']='~implicit~'
    validated={**env,'VK_LAYER_PATH':str(dll.parent),'VK_INSTANCE_LAYERS':'VK_LAYER_KHRONOS_validation','VK_LAYER_SETTINGS_PATH':str(settings),'GFX_EXPECT_VULKAN_LAYER':str(dll),'VK_KHRONOS_VALIDATION_VALIDATE_SYNC':'true'}
    (out/'environment.json').write_text(json.dumps(dict(cleaned_prefixes=['VK_','VVL_','SDL_','GFX_EXPECT_VULKAN_'],performance_overrides={'VK_LOADER_LAYERS_DISABLE':'~implicit~'},validation_overrides={k:v for k,v in validated.items() if k not in env}),indent=2)+'\n')
    commands=[]
    def call(name,program,argv,validation=False):
        frozen();start=time.monotonic();p=subprocess.run([str(programs[program]),*argv],cwd=root,env=validated if validation else env,capture_output=True,timeout=120)
        (out/(name+'.stdout')).write_bytes(p.stdout);(out/(name+'.stderr')).write_bytes(p.stderr);commands.append(dict(name=name,program=program,argv=[str(programs[program]),*argv],validation=validation,returncode=p.returncode,seconds=time.monotonic()-start))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n');u.require(p.returncode==0,name+' failed')
        u.require(b'Validation Error' not in p.stderr and b'SYNC-HAZARD' not in p.stderr,name+' native validation error')
        frozen();print(name,'PASS',flush=True);return json.loads(p.stdout)
    probes={}
    for pref in ('high','low'):
        r=call('probe-'+pref,'before',['--probe','sdl',pref]);u.validate_identity(r);u.require(r['source_fingerprint']==source and r['SDK_source_fingerprint']==baseline,'Before executable is not the frozen baseline');probes[pref]=r
    u.require(probes['high']['device_uuid']!=probes['low']['device_uuid'],'Two distinct hardware adapters required')
    records=[];profiles=[]
    for pref,p in probes.items():
        for sdk in ('graphicssdk','sdl'):
            for scenario in u.SHAPES:
                name=f'validation-{pref}-{sdk}-{scenario}';r=call(name,'after',[sdk,scenario,pref,'1','3','1',p['device_name']],True)
                u.validate_result(r,sdk,scenario,pref,1,3,True,p,source,current,pin);records.append(dict(name=name,kind='correctness',variant='after',result=r))
    jobs=[(pref,variant,scenario,rep) for pref in probes for variant in ('before','after','sdl') for scenario in u.SHAPES if scenario!='odd' for rep in range(3)]
    random.Random(20261024).shuffle(jobs)
    for pref,variant,scenario,rep in jobs:
        sdk='sdl' if variant=='sdl' else 'graphicssdk';program='after' if variant=='after' else 'before';sdk_source=current if variant=='after' else baseline
        name=f'measure-{pref}-{variant}-{scenario}-{rep}';r=call(name,program,[sdk,scenario,pref,'5','30','0',probes[pref]['device_name']])
        u.validate_result(r,sdk,scenario,pref,5,30,False,probes[pref],source,sdk_source,pin);records.append(dict(name=name,kind='measurement',variant=variant,result=r))
    jobs=[(pref,variant,scenario) for pref in probes for variant in ('before','after') for scenario in u.SHAPES if scenario!='odd'];random.Random(7412).shuffle(jobs)
    for pref,variant,scenario in jobs:
        name=f'profile-{pref}-{variant}-{scenario}';r=call(name,'profile-'+variant,[probes[pref]['device_name'],scenario,'3','15','0']);validate_profile(r,probes[pref],current if variant=='after' else baseline)
        profiles.append(dict(name=name,preference=pref,variant=variant,result=r))
    groups=[]
    for pref in probes:
        for scenario in u.SHAPES:
            if scenario=='odd':continue
            row=dict(preference=pref,scenario=scenario,device_name=probes[pref]['device_name'],statistics={})
            for variant in ('before','after','sdl'):
                rr=[r['result'] for r in records if r['kind']=='measurement' and r['variant']==variant and r['result']['preference']==pref and r['result']['scenario']==scenario];u.require(len(rr)==3,'Missing independent process');row['statistics'][variant]=u.summarize(rr)
            row['cost_change_percent']=100*(row['statistics']['after']['p50_ms']/row['statistics']['before']['p50_ms']-1);groups.append(row)
    x=dict(contract=u.CONTRACT,api='vulkan',category='end_to_end',benchmark_source_fingerprint=source,before_SDK_source_fingerprint=baseline,after_SDK_source_fingerprint=current,SDL_commit=pin,binaries=binaries,
        probes=probes,runs=records,profiles=profiles,groups=groups,performance_samples=1620,validation_timings_excluded=True,diagnostic_timings_excluded=True,samples_removed=0,overall_SDK_ranking=False,
        verified_images=sum(r['result']['verified_images'] for r in records),verified_bytes=sum(r['result']['verified_bytes'] for r in records),profile_verified_images=sum(r['result']['verified_images'] for r in profiles),profile_verified_bytes=sum(r['result']['verified_bytes'] for r in profiles))
    (out/'results.json').write_text(json.dumps(x,indent=2)+'\n');(out/'runtime-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('PASS: 54 unvalidated measurement processes, 16 validated correctness processes, 12 CPU diagnostic processes',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('before','after','profile-before','profile-after','output'):p.add_argument('--'+key,type=pathlib.Path,required=True)
    p.add_argument('--candidate-receipt',type=pathlib.Path,help='Use the reviewed rejected candidate archive instead of the current production source identity')
    run(p.parse_args())
