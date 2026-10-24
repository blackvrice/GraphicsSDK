"""Bounded mixed-size/occupancy/retained-recording contracts, separate from timing."""
import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import audit_licenses as audit
import prepare_vulkan_pool_churn as prepare
ROOT=prepare.ROOT
SDK=prepare.BASE
CANDIDATE='40a467f7d34faaf412ce3e0568c8f76ccc0c2cd80baf7cf4d7b402551bf8aed8'
SOURCE=ROOT/'out/vulkan-pool-churn-source'
PROFILES={'mixed':16,'sparse':4,'held':8}
SIZES=(1,63,64,65,127,128,129,255,256,257,511,512,513,1023,1024,1025,2047,2048,2049,4095,4096,4097,8192,65537)
CALLS=('CreateBuffer','DestroyBuffer','CreateImage','DestroyImage','AllocateMemory','FreeMemory','BindBufferMemory','BindImageMemory')
def require(x,m):
    if not x:raise ValueError(m)
def record(p):return audit.record_file(ROOT,p)
def fingerprint():
    own=list((ROOT/'experiments/vulkan_pool_churn').glob('*.cpp'))+list((ROOT/'experiments/vulkan_pool_churn').glob('*.hpp'))
    own += [ROOT/p for p in ('experiments/vulkan_pool_churn/CMakeLists.txt','benchmarks/common/Adapter.hpp','benchmarks/common/Environment.cpp','benchmarks/vulkan_upload/Identity.hpp','experiments/vulkan_buffer_pool_fault/Layer.cpp')]
    own += list((ROOT/'experiments/vulkan_buffer_pool_fault').glob('*.hpp'))
    return hashlib.sha256(''.join(f'{p.relative_to(ROOT).as_posix()}|{audit.sha256(p.read_bytes())}\n' for p in sorted(own,key=lambda p:p.as_posix())).encode()).hexdigest()
def same(a,b):return json.dumps(a,sort_keys=True,separators=(',',':'))==json.dumps(b,sort_keys=True,separators=(',',':'))
def integer(v):return type(v) is int and v>=0
def validate(r,profile,validation,candidate):
    require(type(r['schema_version']) is int and r['schema_version']==1 and r['contract']=='vulkan_pool_churn_v1','Contract')
    require(r['profile']==profile and r['validation'] is validation and r['candidate'] is candidate,'Profile')
    require(r['SDK_source_fingerprint']==(CANDIDATE if candidate else SDK) and r['source_fingerprint']==fingerprint(),'Source identity')
    require(bool(r['loaded_validation_layer'])==validation and integer(r['assertions']) and r['assertions']>500,'Validation/assertions')
    require(len(r['strides'])>=(3 if profile=='sparse' else 4) and sorted(set(r['strides']))==r['strides'] and all(type(s) is int and s in (64,128,256,512,1024,2048,4096) for s in r['strides']),'Actual native size classes')
    model=r['model'];require(set(model)=={'creates','reuses','growths','dedicated','checks'} and all(integer(v) for v in model.values()),'Model schema/counters')
    require(model['creates']==model['reuses']+model['growths']+model['dedicated'],'Model operation partition')
    require((model['reuses']>0 and model['growths']>0) if candidate else (model['reuses']==model['growths']==0),'Candidate actually shared/dedicated baseline')
    rows=r['checkpoints'];expected=[('initial',0)];count=PROFILES[profile]
    labels={'mixed':('full','sparse','survivors','released'),'sparse':('full','one-per-block','holes-refilled','empty','reset-singleton','reset-empty'),'held':('ended-retained','replacements','pending-retained','completed','empty')}[profile]
    expected += [(label,i) for i in range(count) for label in labels]
    if profile=='mixed':expected.append(('empty',16))
    expected.append(('final',99));require([(a['label'],a['round']) for a in rows]==expected,'Checkpoint matrix')
    prev={'reuses':0,'growths':0}
    for row in rows:
        require(set(row)=={'label','round','buffers','allocations','reserved','required','payload','reuses','growths'} and all(integer(v) for k,v in row.items() if k!='label'),'Checkpoint schema/types')
        require(row['payload']<=row['required']<=row['reserved'] and row['allocations']<=row['buffers'],'Footprint conservation')
        if row['buffers']==0:require(row['allocations']==row['reserved']==row['required']==row['payload']==0,'Empty cache retained')
        require(all(row[k]>=prev[k] for k in ('reuses','growths')),'Cumulative model counters');prev=row
        if row['label'] in ('initial','empty','reset-empty','final'):require(row['buffers']==0,'Nonempty release/reset')
        if not candidate:require(row['buffers']==row['allocations'] and row['reserved']==row['required'],'Dedicated baseline accounting')
    require(all(rows[-1][k]==model[k] for k in ('reuses','growths')) and model['checks']==model['creates']+len(rows)+count,'Model checkpoint count')
    by={(a['label'],a['round']):a for a in rows};creates=initializes=verified=byte_count=held_reads=0
    def row(label,i,buffers,payload):
        a=by[label,i];require(a['buffers']==buffers and a['payload']==payload,'Logical workload differs');return a
    if profile=='mixed':
        live=[]
        for i in range(count):
            new=[SIZES[(index+i*7)%24] for index in range(len(live),96)];live+=new;creates+=len(new);initializes+=len(new);verified+=len(new);byte_count+=sum(new)
            row('full',i,96,sum(live));live=[size for index,size in enumerate(live) if (index+i)%(2+i%3)==0]
            row('sparse',i,len(live),sum(live));row('survivors',i,len(live),sum(live));verified+=len(live);byte_count+=sum(live)
            if i%4==3:live=[]
            row('released',i,len(live),sum(live))
    elif profile=='sparse':
        for i in range(count):
            full=row('full',i,124,31*(64+256+1024+4096));one=by['one-per-block',i]
            require(one['buffers']==full['allocations'] and one['allocations']==full['allocations'] and one['reserved']==full['reserved'],'One live lease per native block')
            row('holes-refilled',i,one['buffers']+32,one['payload']+8*(64+256+1024+4096))
            row('reset-singleton',i,1,4096);require(by['reset-singleton',i]['reserved']==4096,'Empty class resets to one 4KiB slot')
            creates+=157;initializes+=156;verified+=156+2*one['buffers'];byte_count+=31*5440+8*5440+2*one['payload']
    else:
        for i in range(count):
            row('ended-retained',i,48,2*sum(SIZES));row('replacements',i,120,5*sum(SIZES));row('pending-retained',i,120,5*sum(SIZES));row('completed',i,72,3*sum(SIZES))
        creates=120*count;initializes=96*count;verified=192*count;held_reads=24*count;byte_count=8*sum(SIZES)*count
    require(model['creates']==creates and integer(r['verified_buffers']) and r['verified_buffers']==verified and integer(r['verified_bytes']) and r['verified_bytes']==byte_count,'Exact data verification work')
    t=r['native_trace'];require(type(t['schema_version']) is int and t['schema_version']==1 and t['source_fingerprint']==fingerprint() and all(type(t[k]) is int and t[k]==0 for k in ('errors','dropped_events','device_cleanup_resources','device_cleanup_bytes')),'Native trace scope/errors')
    zero=dict(memories=0,bytes=0,buffers=0,images=0);require(same(t['live'],zero),'Native final live')
    frames=t['frames'];require([(f['phase'],f['iteration']) for f in frames]==[(phase,i) for i in range(count) for phase in (1,2,3,4)]+[(6,99)],'Native phase matrix')
    require(all(type(f[k]) is int for f in frames for k in ('phase','iteration')),'Phase scalar types')
    totals={k:0 for k in CALLS};successes={k:0 for k in CALLS};allocated=freed=0;prior=zero
    for f in [dict(before=zero,after=zero,counts=t['outside']),*frames]:
        a,b,c=f['before'],f['after'],f['counts'];require(same(a,prior),'Native interval continuity');prior=b
        require(set(a)==set(b)==set(zero) and all(integer(v) for s in (a,b) for v in s.values()),'Live types')
        require(type(c['errors']) is int and c['errors']==0 and integer(c['allocated_bytes']) and integer(c['freed_bytes']) and set(c['calls'])==set(c['successes'])==set(CALLS),'Call counter schema')
        require(all(integer(c['calls'][k]) and integer(c['successes'][k]) and c['successes'][k]<=c['calls'][k] for k in CALLS),'Call counter types')
        require(all(c['calls'][k]-c['successes'][k]==(1 if f.get('phase')==2 and k=='BindBufferMemory' else 0) for k in CALLS),'Exactly one planned error in each injection interval')
        require(b['buffers']==a['buffers']+c['successes']['CreateBuffer']-c['successes']['DestroyBuffer'] and b['memories']==a['memories']+c['successes']['AllocateMemory']-c['successes']['FreeMemory'] and b['bytes']==a['bytes']+c['allocated_bytes']-c['freed_bytes'] and a['images']==b['images']==0,'Native interval conservation')
        for k in CALLS:totals[k]+=c['calls'][k];successes[k]+=c['successes'][k]
        allocated+=c['allocated_bytes'];freed+=c['freed_bytes']
    require(same(prior,zero) and allocated==freed,'Native byte conservation')
    expected_creates=creates+initializes+verified-held_reads+count
    require(totals['CreateBuffer']==totals['DestroyBuffer']==totals['BindBufferMemory']==expected_creates,'Actual native buffer workload')
    require(totals['AllocateMemory']==totals['FreeMemory'] and successes['BindBufferMemory']==totals['BindBufferMemory']-count,'Actual allocation/bind counts')
    require(all(successes[k]==totals[k] for k in CALLS if k!='BindBufferMemory') and all(totals[k]==0 for k in ('CreateImage','DestroyImage','BindImageMemory')),'Only planned bind errors allowed')
    if not candidate:require(totals['AllocateMemory']==expected_creates,'Dedicated baseline native allocation count')
    fault=t['fault'];require(all(type(fault[k]) is int and fault[k]==0 for k in ('pending','invalid_frees','guard_errors')) and len(fault['injections'])==count,'Fault cleanup/consumption')
    for i,e in enumerate(fault['injections']):require(type(e['iteration']) is int and e['iteration']==i and e['call']=='BindBufferMemory' and type(e['result']) is int and e['result']==(-1 if i%2 else -2) and e['poisoned'] is False and integer(e['bytes_or_offset']) and e['bytes_or_offset']<65536,'Injected call/error')
    return dict(creates=expected_creates,allocations=totals['AllocateMemory'],allocated_bytes=allocated,verified_buffers=verified,verified_bytes=byte_count,injections=count,peak_reserved=max(a['reserved'] for a in rows),peak_slack=max(a['reserved']-a['required'] for a in rows))
def run(out,pilot=False):
    out=out.resolve();require(out.is_relative_to(ROOT) and not out.exists(),'Exclusive output root');out.mkdir(parents=True)
    require(audit.sdk_fingerprint(ROOT)==SDK and audit.sdk_fingerprint(SOURCE)==CANDIDATE,'Reviewed source identities');fp=fingerprint()
    builds={v:ROOT/f'out/vulkan-pool-churn-{v}-v2/Release' for v in ('product','candidate')}
    artifacts=[record(p) for d in builds.values() for p in sorted(d.iterdir()) if p.suffix.lower() in ('.exe','.dll','.json')]
    receipt=json.loads((ROOT/'out/vulkan-validation-runtime/receipt.json').read_text(encoding='utf-8'));vvl=next(ROOT/r['path'] for r in receipt['artifacts'] if r['path'].endswith('.dll'))
    clean={k:v for k,v in os.environ.items() if not k.upper().startswith(('VK_','VVL_','SDL_','GFX_EXPECT_','GFX_TRACE_'))};clean['VK_LOADER_LAYERS_DISABLE']='~implicit~'
    settings=out/'settings';settings.mkdir();(settings/'vk_layer_settings.txt').write_text('# SDK pNext validation controls.\n');commands=[];runs=[]
    def call(name,variant,args,validation=False,expected=0,overrides=None):
        require(fingerprint()==fp and audit.sdk_fingerprint(ROOT)==SDK and audit.sdk_fingerprint(SOURCE)==CANDIDATE,'Source changed during run')
        for r in artifacts+receipt['artifacts']:require(record(ROOT/r['path'])==r,'Changed runtime binary')
        d=builds[variant];env={**clean,'VK_LAYER_PATH':str(d)+(os.pathsep+str(vvl.parent) if validation else ''),'VK_INSTANCE_LAYERS':'VK_LAYER_GFX_buffer_fault','GFX_EXPECT_FAULT_LAYER':str(d/'VkLayer_gfx_buffer_fault.dll'),'VK_LAYER_SETTINGS_PATH':str(settings)}
        if validation:env['GFX_EXPECT_VULKAN_LAYER']=str(vvl)
        p=subprocess.run([str(d/'vulkan_pool_churn.exe'),*args],cwd=ROOT,env={**env,**(overrides or {})},stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=180)
        for suffix,data in (('stdout',p.stdout),('stderr',p.stderr)):(out/(name+'.'+suffix)).write_bytes(data)
        commands.append(dict(name=name,variant=variant,argv=[str(d/'vulkan_pool_churn.exe'),*args],validation=validation,expected=expected,returncode=p.returncode,overrides=overrides or {}));(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        print(name,p.returncode,p.stderr.decode(errors='replace')[:400],flush=True);require(p.returncode==expected,name+' unexpected exit')
        if expected:require(not p.stdout and b'FAIL:' in p.stderr,'Control emitted valid results');return
        require(not p.stderr,name+' unexpected diagnostics');return json.loads(p.stdout)
    probe=call('probe','product',['--probe']);require(probe['SDK_source_fingerprint']==SDK and len(set(probe['adapters']))==2,'Two GPUs required')
    for i,gpu in enumerate(probe['adapters'][:1] if pilot else probe['adapters']):
        for variant in ('product','candidate'):
            for validation in ((False,) if pilot else (False,True)):
                for profile in PROFILES:
                    name=f'{variant}-gpu{i}-v{int(validation)}-{profile}';r=call(name,variant,[profile,gpu,str(int(validation)),str(int(variant=='candidate'))],validation);validate(r,profile,validation,variant=='candidate');require(r['device_name']==gpu,'Exact GPU differs');runs.append(dict(name=name,variant=variant,result=r))
    if not pilot:
        gpu=probe['adapters'][0];missing=out/'missing';missing.mkdir();args=['mixed',gpu,'0','1']
        call('missing-layer','candidate',args,False,1,{'VK_LAYER_PATH':str(missing)})
        call('wrong-layer','candidate',args,False,1,{'GFX_EXPECT_FAULT_LAYER':str(missing/'absent.dll')})
        call('wrong-gpu','candidate',['mixed','absent GPU','0','1'],False,1)
        for i,args in enumerate(([],['bad',gpu,'0','1'],['mixed',gpu,'2','1'],['mixed',gpu,'0','2'],['mixed',gpu,'0','1','extra'])):call('invalid-cli-'+str(i),'candidate',args,False,1)
    x=dict(schema_version=1,contract='vulkan_pool_churn_matrix_v1',SDK_source_fingerprint=SDK,candidate_SDK_source_fingerprint=CANDIDATE,source_fingerprint=fp,pilot=pilot,production_adopted=False,performance_remeasured=False,physical_memory_reclamation_verified=False,probe=probe,artifacts=artifacts,runs=runs)
    (out/'results.json').write_text(json.dumps(x,indent=2)+'\n');(out/'runtime-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--pilot',action='store_true');a=p.parse_args();run(a.output,a.pilot)
