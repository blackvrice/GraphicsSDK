import pathlib, json, hashlib, shutil, sys, gzip, xml.etree.ElementTree as ET
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation'
sys.path.insert(0,str(root/'tools'))
from audit_licenses import parse_tlog, sdk_fingerprint
from crash_capture.evidence import validate, read_records
from run_memory_stress import validate_native
from analyze_memory_stress import analyze
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rec(p):return dict(path=p.relative_to(root).as_posix(),sha256=sha(p),bytes=p.stat().st_size)
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
fp=sdk_fingerprint(root);assert fp=='50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'
# Publish only the fields needed for the crash comparison. Preserve original XML locally.
p=v/'target-application-errors.json';local=root/'out/fault-target-application-errors-original.json'
if not local.exists():shutil.copyfile(p,local)
events=[]
for event in load(local)['events']:
    tree=ET.fromstring(event['xml']);fields={e.attrib['Name']:e.text for e in tree.findall('.//{*}EventData/{*}Data')}
    keep=('AppName','AppVersion','ModuleName','ModuleVersion','ExceptionCode','FaultingOffset','AppPath','ModulePath')
    events.append(dict(time=event['time'],record_id=event['record_id'],fields={k:fields[k] for k in keep}))
write(p,dict(scope='Application Error 1000 for own stress executable and CL.exe only; XML/computer/SID omitted',events=events,original_local_only=rec(local)))
assert len(events)==5 and sum(e['fields']['AppName']=='gfx_memory_stress.exe' for e in events)==1
# Freeze all actual diagnostic projects and the compiler/linker read evidence, without binaries.
projects=('gbv-allocation-origin','gbv-retention-control','gbv-spaced-control','private-virtual-controller')
inventory=[];union=set();evidence=v/'helper-build';evidence.mkdir(exist_ok=True)
for project in projects:
    folder=root/'out'/project;logs=[p for p in (folder/'build').rglob('CL.read.1.tlog') if 'Release' in p.parts];assert len(logs)==1
    inputs=parse_tlog(logs[0].read_bytes());own=[];platform=[]
    for name in inputs:
        p=pathlib.Path(name)
        if p.is_relative_to(root):
            assert p.suffix.lower()=='.cpp',name
            assert p==folder/'main.cpp' or p==root/'tools/memory_validation_probe/Lifetime.cpp',name
            own.append(rec(p));union.add(p)
        else:
            low=name.lower().replace('\\','/')
            assert low.startswith('c:/program files (x86)/windows kits/10/include/') or low.startswith('c:/program files/microsoft visual studio/2022/community/vc/tools/msvc/14.44.35207/') or low in ('c:/windows/globalization/sorting/sortdefault.nls','c:/windows/system32/tzres.dll'),name
            platform.append(name)
    artifacts=[rec(p) for p in (folder/'build/Release').iterdir() if p.suffix in ('.exe','.pdb')];assert len(artifacts)==(1 if project=="private-virtual-controller" else 2)
    copied=[]
    for p in [folder/'main.cpp',folder/'CMakeLists.txt',*logs,*[p for p in (folder/'build').rglob('link.read.1.tlog') if 'Release' in p.parts],*list((folder/'build').glob('*.vcxproj'))]:
        dest=evidence/p.relative_to(root/'out');dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest);copied.append(rec(dest))
    inventory.append(dict(project=project,own_compiler_inputs=own,platform_input_count=len(platform),platform_input_paths_sha256=hashlib.sha256('\n'.join(sorted(platform)).encode()).hexdigest(),artifacts_local_only=artifacts,evidence=copied))
assert len(union)==5
write(evidence/'inventory.json',dict(scope='Conservative actual MSVC Release compiler-read inputs for four SDK-free investigation executables; Windows SDK/MSVC/CRT are system prerequisites, not redistributed. No competitor dependency; not linker symbol reachability or legal approval.',own_input_union=[rec(p) for p in sorted(union)],projects=inventory))
controls=[]
for family in ('allocation-origin','retention-controls','spaced-controls'):
    base=v/family;before=load(base/'inputs-before.json');assert before==load(base/'inputs-after.json')
    assert all(sha(pathlib.Path(p))==h for p,h in before.items())
    paths=sorted(base.glob('adapter-*.jsonl')) if family=='spaced-controls' else sorted(base.glob('adapter-*/memory.jsonl'))
    for path in paths:
        rows=read_records(path);assert rows[-1]['passed'] and not rows[-1]['memory_acceptance'];samples=[r for r in rows if r['type']=='sample'];last=samples[-1]
        assert last['resources_created']==last['resources_destroyed']==1536 and last['devices_destroyed']==1 and last['validation_errors']==0
        capture=path.parent/'capture.jsonl'
        if family!='spaced-controls':assert validate(read_records(capture),0,0)['second_chance_count']==0
        phases={r['phase']:r for r in samples}
        controls.append(dict(family=family,path=path.relative_to(root).as_posix(),header=rows[0],samples=samples,footer=rows[-1]))
assert len(controls)==12
long=[]
for campaign in (v/'campaign.json',v/'extended-intel/campaign.json'):
    data=load(campaign);assert data['inputs_unchanged'] and data['completed_utc']
    for case in data['cases']:
        folder=campaign.parent/case['case'];rows=read_records(folder/'memory.jsonl');header=rows[0]
        reference=validate_native(rows,fp,header['mode'],header['adapter_id'],0);assert reference==case['references']
        cap=validate(read_records(folder/'capture.jsonl'),0,0);assert cap==case['capture'] and cap['second_chance_count']==0
        try:analysis=analyze(rows,fp);numeric=dict(accepted=True)
        except ValueError as e:numeric=dict(accepted=False,error=str(e))
        long.append(dict(case=case['case'],debugger=True,seconds=header['seconds'],warmup=header['warmup'],reference=reference,numeric_policy=numeric,capture=cap))
    before=load(campaign.parent/'inputs-before.json');assert before==load(campaign.parent/'inputs-after.json')
    assert all(sha(pathlib.Path(p))==h for p,h in before.items())
normal=load(v/'unmonitored-result-v2.json');assert normal['runner_exit']==1 and normal['campaign']['inputs_unchanged']
assert normal['campaign']['processes'][0]['returncode']==0
rows=read_records(v/'intel-unmonitored-v2/validation.jsonl');h=rows[0];reference=validate_native(rows,fp,h['mode'],h['adapter_id'],0)
try:analyze(rows,fp)
except ValueError as e:assert str(e)=='Memory plateau gate failed: private_bytes'
else:raise AssertionError('Expected original-policy memory rejection')
long.append(dict(case='intel-normal-unmonitored',debugger=False,seconds=h['seconds'],warmup=h['warmup'],reference=reference,numeric_policy=dict(accepted=False,error='Memory plateau gate failed: private_bytes')))
assert len(long)==5 and sum(c['reference']['frames'] for c in long)==53936
assert sum(c['reference']['full_images'] for c in long)==161808
write(v/'verified-observations.json',dict(SDK_source_fingerprint=fp,controls=controls,long_Intel_runs=long,total_frames=53936,total_full_images=161808,total_verified_buffer_bytes=sum(c['reference']['verified_buffer_bytes'] for c in long),memory_acceptance=False,Intel_AV_cause_resolved=False))
# Store exact controllers, commands and failure receipts, retaining whole ETL/CSV/PDB locally.
receipts=v/'local-trace-receipts';receipts.mkdir(exist_ok=True)
for name in ('gbv-origin-heap-status.json','gbv-origin-heap-status-v2.json','gbv-origin-virtual-status.json','gbv-origin-virtual-status-v2.json'):
    shutil.copyfile(root/'out'/name,receipts/name)
shutil.copyfile(root/'out/fault-public-symbols/symbols.json',receipts/'public-symbol-download-identities.json')
for r in load(v/'virtual-call-paths.json'):
    assert r['controller']['events_lost']==0 and 'Events Lost: 0' in r['trace_header'] and 'Buffers lost: 0' in r['trace_header']
    for k in ('etl','csv'):assert sha(pathlib.Path(r[k]['path']))==r[k]['sha256']
    p=root/r['filtered_events']['path'];assert sha(p)==r['filtered_events']['sha256']
    events=[json.loads(s) for s in gzip.decompress(p.read_bytes()).splitlines()];assert all(e['pid']==r['pid'] for e in events)
    assert sum(e['bytes'] for e in events if e['event']=='VirtualAlloc' and 'COMMIT' in e['flags'])==r['counters']['traced_commit_call_bytes']
symbols=load(v/'resolved-virtual-call-paths.json');assert len(symbols['public_PDB_identity_matches'])==2 and all(p['sym_type']==3 and not p['pdb_redistributed'] for p in symbols['public_PDB_identity_matches'])
scripts=v/'verification-scripts';scripts.mkdir(exist_ok=True)
names=('investigate_faults.py','run_origin_controls.py','sample_fault_regions.py','trace_own_heap-first.ps1','trace_own_heap.ps1','run_unmonitored_fault-first.py','run_unmonitored_fault.py','make_retention_control.py','run_retention_controls.py','observe_fault_trends.py','make_virtual_trace.py','trace_own_virtual.ps1','trace_own_virtual_v2.ps1','analyze_origin_heaps.py','analyze_origin_virtual.py','fetch_fault_symbols.py','resolve_fault_symbols.py','quantify_fault_paths.py','make_spaced_control.py','run_spaced_controls.py','make_extended_fault.py','investigate_extended_fault.py','prepare_fault_evidence.py')
for name in names:shutil.copyfile(root/'out'/name,scripts/name)
print('PASS: 12 lifetime controls; five Intel long runs, 53,936 frames/161,808 full images; four loss-free virtual traces; five own compiler inputs; original XML retained locally',flush=True)
