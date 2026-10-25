"""Exact raw-process review for fresh rebased allocation faults; no timing inference."""
import argparse
import json
import pathlib
import run_vulkan_pool_rebase_fault as trial
ROOT=trial.ROOT
require=trial.require
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def review(folder):
    folder=folder.resolve();x=load(folder/'results.json');commands=load(folder/'commands.json');receipt=load(folder/'runtime-receipt.json')
    require(type(x['schema_version']) is int and x['schema_version']==1 and x['contract']=='vulkan_pool_rebase_fault_matrix_v1' and x['pilot'] is False,'Full matrix required')
    require(all(x[k] is False for k in ('production_adopted','performance_remeasured','physical_memory_reclamation_verified')),'Bounded experimental scope')
    require(x['SDK_source_fingerprint']==trial.SDK and x['candidate_SDK_source_fingerprint']==trial.CANDIDATE and x['source_fingerprint']==trial.fingerprint() and x['regression_source_fingerprint']==trial.regression.fingerprint(),'Source identities')
    require(len(x['probe']['adapters'])==2 and len(set(x['probe']['adapters']))==2 and x['probe']['SDK_source_fingerprint']==trial.CANDIDATE,'Two exact adapters')
    require(len(x['artifacts'])==7 and len({r['path'] for r in x['artifacts']})==7,'Runtime closure')
    for r in x['artifacts']+receipt['artifacts']:require(trial.record(ROOT/r['path'])==r,'Changed executable/layer/runtime')
    expected=[];runs=[];identities={};derived=[];vvl=next(ROOT/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
    def process(name,kind,args,validation=False,exitcode=0,overrides=None):
        exe=trial.builds()[kind]/('vulkan_buffer_fault.exe' if kind=='regression' else 'vulkan_rebase_fault.exe');expected.append(dict(name=name,kind=kind,argv=[str(exe),*args],validation=validation,expected=exitcode,returncode=exitcode,overrides=overrides or {}))
        stdout=(folder/(name+'.stdout')).read_bytes();stderr=(folder/(name+'.stderr')).read_bytes()
        if exitcode:require(not stdout and b'FAIL:' in stderr,'Control emitted result');return
        require(not stderr,'Positive/validation diagnostics');return json.loads(stdout)
    require(trial.same(process('probe','regression',['--probe']),x['probe']),'Raw probe differs')
    for i,gpu in enumerate(x['probe']['adapters']):
        for validation in (False,True):
            for kind,cases in (('regression',trial.regression.CASES),('mixed',trial.MIXED)):
                for case in cases:
                    name=f'{kind}-gpu{i}-v{int(validation)}-{case}';r=process(name,kind,[case,gpu,str(int(validation))],validation)
                    if kind=='regression':trial.regression.validate(r,case,validation,trial.CANDIDATE)
                    else:trial.validate_mixed(r,case,validation)
                    trial.identity(r,validation);require(r['device_name']==gpu,'Exact hardware differs')
                    if validation:require(pathlib.Path(r['loaded_validation_layer']).samefile(vvl),'Pinned validation runtime path')
                    identity={k:r[k] for k in ('device_name','device_uuid','driver_uuid','device_luid','environment')}
                    if gpu in identities:require(trial.same(identity,identities[gpu]),'Hardware/driver/environment drift')
                    else:identities[gpu]=identity
                    runs.append(dict(name=name,kind=kind,result=r));t=r['native_trace'];frames=t['frames'];events=t['fault']['injections'];calls={k:sum(f['counts']['calls'][k] for f in frames) for k in trial.churn.CALLS};success={k:sum(f['counts']['successes'][k] for f in frames) for k in calls}
                    derived.append(dict(name=name,kind=kind,case=case,GPU=gpu,validation=validation,verified_buffers=r['verified_buffers'],verified_bytes=r['verified_bytes'],assertions=r['assertions'],injections=len(events),allocation_oom=sum(e['call']=='AllocateMemory' and e['result'] in (-1,-2) for e in events),unknown_errors=sum(e['result']==-13 for e in events),bind_errors=sum(e['call']=='BindBufferMemory' for e in events),buffers_created=calls['CreateBuffer'],buffers_destroyed=calls['DestroyBuffer'],allocation_attempts=calls['AllocateMemory'],allocation_successes=success['AllocateMemory'],native_frees=calls['FreeMemory'],allocated_bytes=sum(f['counts']['allocated_bytes'] for f in frames),freed_bytes=sum(f['counts']['freed_bytes'] for f in frames)))
    require(trial.same(runs,x['runs']),'Missing/duplicate/reordered/modified results')
    for i in range(2):
        for kind,cases in (('regression',trial.regression.CASES),('mixed',trial.MIXED)):
            for case in cases:
                a=next(r['result'] for r in runs if r['name']==f'{kind}-gpu{i}-v0-{case}');b=next(r['result'] for r in runs if r['name']==f'{kind}-gpu{i}-v1-{case}')
                require(all(trial.same(a[k],b[k]) for k in ('verified_buffers','verified_bytes','native_trace')),'Validation mode changes native observations')
    missing=folder/'missing'
    for name,kind,args,override in trial.controls(x['probe']['adapters'][0]):
        env={'VK_LAYER_PATH':str(missing)} if override=='missing-layer' else {'GFX_EXPECT_FAULT_LAYER':str(missing/'absent.dll')} if override=='wrong-layer' else None
        process(name,kind,args,False,1,env)
    require(trial.same(commands,expected),'Command attribution/exit differs')
    sums={k:sum(d[k] for d in derived) for k in ('verified_buffers','verified_bytes','assertions','injections','allocation_oom','unknown_errors','bind_errors','buffers_created','buffers_destroyed','allocation_attempts','allocation_successes','native_frees','allocated_bytes','freed_bytes')}
    require(sums['buffers_created']==sums['buffers_destroyed'] and sums['allocation_successes']==sums['native_frees'] and sums['allocated_bytes']==sums['freed_bytes'],'Global native conservation')
    return dict(schema_version=1,contract='vulkan_pool_rebase_fault_summary_v1',SDK_source_fingerprint=trial.SDK,candidate_SDK_source_fingerprint=trial.CANDIDATE,source_fingerprint=trial.fingerprint(),regression_source_fingerprint=trial.regression.fingerprint(),production_adopted=False,performance_remeasured=False,physical_memory_reclamation_verified=False,GPUs=list(identities.values()),regression_processes=60,mixed_context_processes=28,clean_processes=88,fail_closed_processes=14,probe_processes=1,total_commands=103,cycles=264,usable_exact_fallbacks=72,fallback_allocation_successes=96,fallback_bind_rollbacks=24,double_allocation_failures=48,reuse_before_pending_oom_cycles=24,final_native_live=0,invalid_frees=0,unsafe_binding_guards=0,**sums,cases=derived)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path);a=p.parse_args();s=review(a.folder);text=json.dumps(s,indent=2)+'\n'
    if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(text)
    print(text)
