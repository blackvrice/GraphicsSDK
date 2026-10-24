"""Sequential real-loader creation errors; no physical OOM or product test switches."""
import argparse
import hashlib
import json
import os
import pathlib
import re
import subprocess
import audit_licenses as audit
ROOT=pathlib.Path(__file__).resolve().parents[1]
SDK='2e511b8e53dbef4bbb2bf3d86ab4b19cf03e8765111de2b3dad4fe45fa551401'
OLD='44b92cdf3a4ae01c97af10fe25d57b6dea487bf6ed7826a7f322e338a1d39e25'
KINDS=('instance','messenger','device','surface')
def require(x,m):
    if not x:raise ValueError(m)
def record(p):return audit.record_file(ROOT,p)
def fingerprint():
    paths=list((ROOT/'tests/vulkan_loader_fault').glob('*.cpp'))+list((ROOT/'tests/vulkan_loader_fault').glob('*.hpp'))+[ROOT/p for p in ('tests/vulkan_loader_fault/CMakeLists.txt','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/vulkan_upload/Identity.hpp')]
    return hashlib.sha256(''.join(f'{p.relative_to(ROOT).as_posix()}|{audit.sha256(p.read_bytes())}\n' for p in sorted(paths,key=lambda p:p.as_posix())).encode()).hexdigest()
def profiles():
    for kind in KINDS:
        for validation in (False,True):
            if kind=='messenger' and not validation:continue
            for code in (-1,-2):
                for poison in ((0,1) if kind in ('messenger','surface') else (0,)):
                    yield kind,validation,code,poison
def validate(r,kind,validation,code,poison,sdk,before=False):
    require(type(r['schema_version']) is int and r['schema_version']==1 and r['contract']=='vulkan_loader_fault_v1','Contract')
    require((r['kind'],r['validation'],r['result'],r['poison'])==(kind,validation,code,poison),'Profile')
    require(type(r['validation']) is bool and type(r['result']) is int and type(r['poison']) is int,'Profile types')
    require(r['SDK_source_fingerprint']==sdk and r['source_fingerprint']==fingerprint(),'Source identity')
    require(bool(r['loaded_validation_layer'])==validation and all(re.fullmatch('[0-9a-f]{16,32}',r[k]) for k in ('device_uuid','driver_uuid')),'Runtime identity')
    require(len(r['cycles'])==3 and type(r['assertions']) is int and r['assertions']>=80,'Cycle count')
    require(r['verified_buffers']==(9 if kind=='surface' else 12) and r['frames']==(3 if kind=='surface' else 0),'Full readback count')
    require(r['verified_bytes']==r['verified_buffers']*4096+r['frames']*321*197*4,'Readback bytes')
    invalid=int(before and poison and kind in ('messenger','surface'))
    def snap(s):
        require(set(s)=={'attempts','created','destroyed','live','invalid','errors','injections','pending'},'Snapshot schema')
        for k in ('attempts','created','destroyed','live'):
            require(len(s[k])==4 and all(type(v) is int and v>=0 for v in s[k]),'Snapshot counters')
        for k in ('invalid','errors','injections','pending'):require(type(s[k]) is int and s[k]>=0,'Snapshot scalar')
        require(s['errors']==s['pending']==0,'Trace errors or unconsumed error')
        require(all(c-d==l and a>=c for a,c,d,l in zip(s['attempts'],s['created'],s['destroyed'],s['live'])),'Native conservation')
    def delta(a,b,attempts,created,destroyed,inj=0,bad=0):
        for field,expected in (('attempts',attempts),('created',created),('destroyed',destroyed)):
            require([y-x for x,y in zip(a[field],b[field])]==expected,'Unexpected '+field+' delta')
        require(b['injections']-a['injections']==inj and b['invalid']-a['invalid']==bad,'Injection/invalid cleanup delta')
    zero=[0]*4;index=KINDS.index(kind);prev=None
    for i,c in enumerate(r['cycles']):
        require(set(c)=={'before','failed','retry','clean'},'Cycle schema')
        a,b,t,z=(c[k] for k in ('before','failed','retry','clean'))
        for s in (a,b,t,z):snap(s)
        require(a['live']==[1,int(validation),1,0] and a['injections']==i and a['invalid']==i*invalid,'Guard baseline')
        if prev is not None:require(a==prev,'Unexpected loader activity between cycles')
        fa=zero.copy();fc=zero.copy()
        if kind in ('messenger','device'):fa[0]=fc[0]=1
        if kind=='device':fa[1]=fc[1]=int(validation)
        fa[index]=1
        delta(a,b,fa,fc,fc,1,invalid);require(a['live']==b['live'],'Failure changes ownership')
        retry=[0,0,0,1] if kind=='surface' else [1,int(validation),1,0]
        delta(b,t,retry,retry,zero);delta(t,z,zero,zero,retry);require(z['live']==a['live'],'Retry leaks')
        prev=z
    f=r['final'];snap(f);delta(prev,f,zero,zero,[1,int(validation),1,0]);require(f['live']==zero and f['invalid']==3*invalid and f['injections']==3,'Final native cleanup')
    return r
def run(out,pilot=False):
    out=out.resolve();require(out.is_relative_to(ROOT) and not out.exists(),'Exclusive output root');out.mkdir(parents=True)
    require(audit.sdk_fingerprint(ROOT)==SDK,'Unmodified product required');fp=fingerprint()
    builds={v:ROOT/f'out/vulkan-loader-fault-{v}-v4/Release' for v in ('before','after')}
    artifacts=[record(p) for d in builds.values() for p in sorted(d.iterdir()) if p.suffix.lower() in ('.exe','.dll','.json')]
    receipt=json.loads((ROOT/'out/vulkan-validation-runtime/receipt.json').read_text(encoding='utf-8'));vvl=next(ROOT/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
    clean={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_','GFX_TRACE_'))};clean['VK_LOADER_LAYERS_DISABLE']='~implicit~'
    settings=out/'settings';settings.mkdir();(settings/'vk_layer_settings.txt').write_text('# SDK pNext supplies validation settings.\n')
    commands=[];runs=[]
    def call(name,variant,args,validation=False,expected=0,overrides=None):
        require(audit.sdk_fingerprint(ROOT)==SDK and fingerprint()==fp,'Sources changed during run')
        for r in artifacts+receipt['artifacts']:require(record(ROOT/r['path'])==r,'Runtime artifact changed')
        d=builds[variant];env={**clean,'VK_LAYER_PATH':str(d)+(os.pathsep+str(vvl.parent) if validation else ''),'VK_INSTANCE_LAYERS':'VK_LAYER_GFX_loader_fault','GFX_EXPECT_LOADER_LAYER':str(d/'VkLayer_gfx_loader_fault.dll'),'VK_LAYER_SETTINGS_PATH':str(settings)}
        if validation:env['GFX_EXPECT_VULKAN_LAYER']=str(vvl)
        p=subprocess.run([str(d/'vulkan_loader_fault.exe'),*args],cwd=ROOT,env={**env,**(overrides or {})},stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=120)
        for suffix,data in (('stdout',p.stdout),('stderr',p.stderr)):(out/(name+'.'+suffix)).write_bytes(data)
        commands.append(dict(name=name,variant=variant,argv=[str(d/'vulkan_loader_fault.exe'),*args],validation=validation,expected=expected,returncode=p.returncode,overrides=overrides or {}));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        print(name,p.returncode,p.stderr.decode(errors='replace')[:500],flush=True)
        require(p.returncode==expected,name+' unexpected exit')
        if expected==1:require(not p.stdout and b'FAIL:' in p.stderr,'Control emitted valid result');return
        require(not p.stderr,name+' diagnostics');return json.loads(p.stdout)
    probe=call('probe','after',['--probe']);require(probe['SDK_source_fingerprint']==SDK and len(set(probe['adapters']))==2,'Two exact GPUs required')
    require(len(probe['presentation'])==2 and all(type(v) is bool for v in probe['presentation']) and any(probe['presentation']),'At least one real WSI adapter required')
    matrix=list(profiles()) if not pilot else [('instance',False,-1,0),('device',True,-2,0),('messenger',True,-1,1),('surface',False,-2,1),('surface',True,-1,1)]
    for gpu_index,gpu in enumerate(probe['adapters'][:1] if pilot else probe['adapters']):
        for kind,validation,code,poison in matrix:
            if kind=='surface' and not probe['presentation'][gpu_index]:continue
            for variant in (('after','before') if poison else ('after',)):
                name=f'{variant}-gpu{gpu_index}-v{int(validation)}-{kind}-{abs(code)}-p{poison}';args=[kind,'host' if code==-1 else 'device',gpu,str(int(validation)),str(poison)]
                expected=2 if variant=='before' else 0
                r=call(name,variant,args,validation,expected);validate(r,kind,validation,code,poison,OLD if variant=='before' else SDK,variant=='before');require(r['device_name']==gpu,'Exact adapter differs');runs.append(dict(name=name,variant=variant,result=r))
    if not pilot:
        gpu=probe['adapters'][0];missing=out/'missing';missing.mkdir();args=['instance','host',gpu,'0','0']
        call('missing-manifest','after',args,False,1,{'VK_LAYER_PATH':str(missing)})
        call('wrong-layer-dll','after',args,False,1,{'GFX_EXPECT_LOADER_LAYER':str(missing/'absent.dll')})
        call('wrong-gpu','after',['instance','host','absent GPU','0','0'],False,1)
        for i,a in enumerate(([],['bad','host',gpu,'0','0'],['instance','unknown',gpu,'0','0'],['instance','host',gpu,'2','0'],['instance','host',gpu,'0','1'],['messenger','host',gpu,'0','0'],args+['extra'])):call('invalid-cli-'+str(i),'after',a,False,1)
    result=dict(schema_version=1,contract='vulkan_loader_fault_matrix_v1',SDK_source_fingerprint=SDK,prior_SDK_source_fingerprint=OLD,source_fingerprint=fp,pilot=pilot,product_changed=False,buffer_pool_adopted=False,physical_memory_reclamation_verified=False,probe=probe,artifacts=artifacts,runs=runs)
    (out/'results.json').write_text(json.dumps(result,indent=2)+'\n');(out/'runtime-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--pilot',action='store_true');a=p.parse_args();run(a.output,a.pilot)
