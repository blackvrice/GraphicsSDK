"""Offline, fail-closed review of exact loader matrix and raw process evidence."""
import argparse
import json
import pathlib
import run_vulkan_loader_fault as native
ROOT=native.ROOT
require=native.require
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def same(a,b):
    # Python container equality considers False == 0 and True == 1. Evidence
    # copies must preserve JSON scalar types as well as values.
    return json.dumps(a,sort_keys=True,separators=(',',':'))==json.dumps(b,sort_keys=True,separators=(',',':'))
def review(folder):
    x=load(folder/'results.json');commands=load(folder/'commands.json');receipt=load(folder/'runtime-receipt.json')
    require(type(x['schema_version']) is int and x['schema_version']==1 and x['contract']=='vulkan_loader_fault_matrix_v1' and x['pilot'] is False,'Full matrix required')
    require(x['product_changed'] is False and x['buffer_pool_adopted'] is False and x['physical_memory_reclamation_verified'] is False,'Scope')
    require(x['SDK_source_fingerprint']==native.SDK and x['prior_SDK_source_fingerprint']==native.OLD and x['source_fingerprint']==native.fingerprint(),'Matrix identity')
    require(x['probe']['SDK_source_fingerprint']==native.SDK and len(x['probe']['adapters'])==2 and len(set(x['probe']['adapters']))==2,'Adapter set')
    require(len(x['probe']['presentation'])==2 and all(type(v) is bool for v in x['probe']['presentation']) and any(x['probe']['presentation']),'Real WSI capabilities')
    vvl=next(ROOT/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
    for r in x['artifacts']+receipt['artifacts']:require(native.record(ROOT/r['path'])==r,'Changed binary/runtime artifact')
    require(len({r['path'] for r in x['artifacts']})==8,'Executable/layer/manifest closure')
    expected=[];rows=[];identities={};used=set()
    def process(name,variant,args,validation,exitcode,overrides=None):
        require(name not in used,'Duplicate process');used.add(name)
        stdout=(folder/(name+'.stdout')).read_bytes();stderr=(folder/(name+'.stderr')).read_bytes()
        exe=ROOT/f'out/vulkan-loader-fault-{variant}-v4/Release/vulkan_loader_fault.exe'
        expected.append(dict(name=name,variant=variant,argv=[str(exe),*args],validation=validation,expected=exitcode,returncode=exitcode,overrides=overrides or {}))
        if exitcode==1:require(not stdout and b'FAIL:' in stderr,'Fail-closed process emitted results');return
        require(not stderr,'Positive process diagnostics');return json.loads(stdout)
    require(same(process('probe','after',['--probe'],False,0),x['probe']),'Raw probe differs')
    for i,gpu in enumerate(x['probe']['adapters']):
        for kind,validation,code,poison in native.profiles():
            if kind=='surface' and not x['probe']['presentation'][i]:continue
            for variant in (('after','before') if poison else ('after',)):
                name=f'{variant}-gpu{i}-v{int(validation)}-{kind}-{abs(code)}-p{poison}';before=variant=='before';args=[kind,'host' if code==-1 else 'device',gpu,str(int(validation)),str(poison)]
                raw=process(name,variant,args,validation,2 if before else 0);r=native.validate(raw,kind,validation,code,poison,native.OLD if before else native.SDK,before)
                require(r['device_name']==gpu,'Adapter name differs')
                if validation:require(pathlib.Path(r['loaded_validation_layer']).samefile(vvl),'Validation runtime path')
                identity={k:r[k] for k in ('device_name','device_uuid','driver_uuid','environment')}
                require(identity['environment']['gpu']==gpu and identity['environment']['software_renderer'] is False and identity['environment']['pointer_bits']==64,'GPU environment')
                if gpu in identities:require(identities[gpu]==identity,'Per-GPU identity/environment drift')
                else:identities[gpu]=identity
                rows.append(dict(name=name,variant=variant,result=r))
    require(same(rows,x['runs']),'Missing/duplicate/reordered or modified result')
    gpu=x['probe']['adapters'][0];args=['instance','host',gpu,'0','0'];missing=folder.resolve()/'missing'
    process('missing-manifest','after',args,False,1,{'VK_LAYER_PATH':str(missing)})
    process('wrong-layer-dll','after',args,False,1,{'GFX_EXPECT_LOADER_LAYER':str(missing/'absent.dll')})
    process('wrong-gpu','after',['instance','host','absent GPU','0','0'],False,1)
    for i,a in enumerate(([],['bad','host',gpu,'0','0'],['instance','unknown',gpu,'0','0'],['instance','host',gpu,'2','0'],['instance','host',gpu,'0','1'],['messenger','host',gpu,'0','0'],args+['extra'])):process('invalid-cli-'+str(i),'after',a,False,1)
    require(same(commands,expected),'Process command ledger differs')
    after=[r['result'] for r in rows if r['variant']=='after'];before=[r['result'] for r in rows if r['variant']=='before']
    return dict(schema_version=1,contract='vulkan_loader_fault_summary_v1',SDK_source_fingerprint=native.SDK,prior_SDK_source_fingerprint=native.OLD,source_fingerprint=native.fingerprint(),product_changed=False,buffer_pool_adopted=False,performance_remeasured=False,physical_memory_reclamation_verified=False,GPUs=list(identities.values()),surface_supported_adapters=[gpu for gpu,v in zip(x['probe']['adapters'],x['probe']['presentation']) if v],surface_unsupported_adapters=[gpu for gpu,v in zip(x['probe']['adapters'],x['probe']['presentation']) if not v],clean_processes=len(after),regression_processes=len(before),fail_closed_processes=10,probe_processes=1,total_processes=len(commands),cycles=sum(len(r['cycles']) for r in after),injections=sum(r['final']['injections'] for r in after),invalid_cleanup=sum(r['final']['invalid'] for r in after),prior_invalid_cleanup=sum(r['final']['invalid'] for r in before),verified_buffers=sum(r['verified_buffers'] for r in after),presented_frames=sum(r['frames'] for r in after),verified_bytes=sum(r['verified_bytes'] for r in after),assertions=sum(r['assertions'] for r in after),final_live_objects=sum(sum(r['final']['live']) for r in after),native_objects_created=sum(sum(r['final']['created']) for r in after),native_objects_destroyed=sum(sum(r['final']['destroyed']) for r in after))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=pathlib.Path);p.add_argument('--output',type=pathlib.Path);a=p.parse_args();s=review(a.folder.resolve());text=json.dumps(s,indent=2)+'\n'
    if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(text)
    print(text)
