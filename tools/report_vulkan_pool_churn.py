"""Offline exact process/raw-ledger review; no timing or physical-memory inference."""
import argparse
import json
import pathlib
import run_vulkan_pool_churn as native
ROOT=native.ROOT
require=native.require
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def review(folder):
    folder=folder.resolve();x=load(folder/'results.json');commands=load(folder/'commands.json');receipt=load(folder/'runtime-receipt.json')
    require(type(x['schema_version']) is int and x['schema_version']==1 and x['contract']=='vulkan_pool_churn_matrix_v1' and x['pilot'] is False,'Full matrix required')
    require(all(x[k] is False for k in ('production_adopted','performance_remeasured','physical_memory_reclamation_verified')),'Bounded correctness scope')
    require(x['SDK_source_fingerprint']==native.SDK and x['candidate_SDK_source_fingerprint']==native.CANDIDATE and x['source_fingerprint']==native.fingerprint(),'Matrix source identity')
    require(len(x['probe']['adapters'])==2 and len(set(x['probe']['adapters']))==2 and x['probe']['SDK_source_fingerprint']==native.SDK,'Adapter set')
    require(len(x['artifacts'])==8 and len({r['path'] for r in x['artifacts']})==8,'Native runtime closure')
    for r in x['artifacts']+receipt['artifacts']:require(native.record(ROOT/r['path'])==r,'Changed binary/runtime')
    vvl=next(ROOT/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'));expected=[];runs=[];identities={};metrics=[]
    def process(name,variant,args,validation=False,exitcode=0,overrides=None):
        exe=ROOT/f'out/vulkan-pool-churn-{variant}-v2/Release/vulkan_pool_churn.exe';expected.append(dict(name=name,variant=variant,argv=[str(exe),*args],validation=validation,expected=exitcode,returncode=exitcode,overrides=overrides or {}))
        stdout=(folder/(name+'.stdout')).read_bytes();stderr=(folder/(name+'.stderr')).read_bytes()
        if exitcode:require(not stdout and b'FAIL:' in stderr,'Fail-closed control emitted result');return
        require(not stderr,'Positive process diagnostics');return json.loads(stdout)
    require(native.same(process('probe','product',['--probe']),x['probe']),'Raw probe differs')
    for i,gpu in enumerate(x['probe']['adapters']):
        for variant in ('product','candidate'):
            for validation in (False,True):
                for profile in native.PROFILES:
                    name=f'{variant}-gpu{i}-v{int(validation)}-{profile}';r=process(name,variant,[profile,gpu,str(int(validation)),str(int(variant=='candidate'))],validation)
                    m=native.validate(r,profile,validation,variant=='candidate');require(r['device_name']==gpu,'Exact GPU differs')
                    if validation:require(pathlib.Path(r['loaded_validation_layer']).samefile(vvl),'Pinned VVL path')
                    identity={k:r[k] for k in ('device_name','device_uuid','driver_uuid','environment')};require(identity['environment']['gpu']==gpu and identity['environment']['software_renderer'] is False and identity['environment']['pointer_bits']==64,'GPU environment')
                    if gpu in identities:require(native.same(identity,identities[gpu]),'Per-GPU identity/environment changed')
                    else:identities[gpu]=identity
                    runs.append(dict(name=name,variant=variant,result=r));metrics.append(dict(name=name,variant=variant,device_name=gpu,validation=validation,profile=profile,**m))
    require(native.same(runs,x['runs']),'Missing/duplicate/reordered/modified process result')
    for i,gpu in enumerate(x['probe']['adapters']):
        for variant in ('product','candidate'):
            for profile in native.PROFILES:
                a=next(r['result'] for r in runs if r['name']==f'{variant}-gpu{i}-v0-{profile}');b=next(r['result'] for r in runs if r['name']==f'{variant}-gpu{i}-v1-{profile}')
                require(all(native.same(a[k],b[k]) for k in ('verified_buffers','verified_bytes','model','strides','checkpoints','native_trace')),'Validation OFF/ON native/logical observations differ')
    gpu=x['probe']['adapters'][0];missing=folder/'missing';args=['mixed',gpu,'0','1']
    process('missing-layer','candidate',args,False,1,{'VK_LAYER_PATH':str(missing)})
    process('wrong-layer','candidate',args,False,1,{'GFX_EXPECT_FAULT_LAYER':str(missing/'absent.dll')})
    process('wrong-gpu','candidate',['mixed','absent GPU','0','1'],False,1)
    for i,args in enumerate(([],['bad',gpu,'0','1'],['mixed',gpu,'2','1'],['mixed',gpu,'0','2'],['mixed',gpu,'0','1','extra'])):process('invalid-cli-'+str(i),'candidate',args,False,1)
    require(native.same(commands,expected),'Command ledger differs')
    return dict(schema_version=1,contract='vulkan_pool_churn_summary_v1',SDK_source_fingerprint=native.SDK,candidate_SDK_source_fingerprint=native.CANDIDATE,source_fingerprint=native.fingerprint(),production_adopted=False,performance_remeasured=False,physical_memory_reclamation_verified=False,GPUs=list(identities.values()),clean_processes=24,fail_closed_processes=8,probe_processes=1,total_processes=33,rounds=sum(m['injections'] for m in metrics),injected_bind_errors=sum(m['injections'] for m in metrics),verified_buffers=sum(m['verified_buffers'] for m in metrics),verified_bytes=sum(m['verified_bytes'] for m in metrics),native_buffers_created=sum(m['creates'] for m in metrics),native_buffers_destroyed=sum(m['creates'] for m in metrics),native_allocations=sum(m['allocations'] for m in metrics),native_frees=sum(m['allocations'] for m in metrics),native_allocated_bytes=sum(m['allocated_bytes'] for m in metrics),native_freed_bytes=sum(m['allocated_bytes'] for m in metrics),checkpoint_count=sum(len(r['result']['checkpoints']) for r in runs),model_checks=sum(r['result']['model']['checks'] for r in runs),assertions=sum(r['result']['assertions'] for r in runs),metrics=metrics)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path);a=p.parse_args();s=review(a.folder);text=json.dumps(s,indent=2)+'\n'
    if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(text)
    print(text)
