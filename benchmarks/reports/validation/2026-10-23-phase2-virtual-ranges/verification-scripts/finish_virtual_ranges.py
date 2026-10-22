import pathlib,json,hashlib,subprocess,sys,zipfile,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-23-phase2-virtual-ranges';report=root/'benchmarks/reports/2026-10-23-phase2-virtual-ranges'
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint,audit_package
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rec(p):return dict(path=p.relative_to(root).as_posix(),sha256=sha(p),bytes=p.stat().st_size)
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
fp=sdk_fingerprint(root);assert fp=='50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'
frozen=load(v/'inputs-before.json');assert frozen==load(v/'inputs-after.json')
assert all(sha(pathlib.Path(p))==h for p,h in frozen.items())
previous=root/'benchmarks/reports/2026-10-23-phase2-fault-investigation/evidence.json';prior=load(previous)
preserved=prior['files']+prior['preserved_inputs']+[prior[k] for k in ('summary','source_archive','previous_evidence')]+[rec(previous)]
assert all(sha(root/r['path'])==r['sha256'] for r in preserved)
assert len(prior['SDK_packages'])==11 and all(sha(root/r['path'])==r['sha256'] for r in prior['SDK_packages'])
package=audit_package(root,root/'out/GraphicsSDK-0.0.1-dev-windows-x64-copy-atomicity.zip')
commands=load(v/'commands.json');assert len(commands)==4 and all(c['returncode']==0 for c in commands)
tests=(v/'python-tests.log').read_text();assert 'Ran 182 tests' in tests and 'OK' in tests and 'skipped' not in tests
replay=load(v/'replay/summary.json');assert len(replay['cases'])==4 and not replay['memory_acceptance']
assert sum(c['summary']['events'] for c in replay['cases'])==18204
assert [c['summary']['individual_origin_peaks']['gbv_resource_info_command_buffer'] for c in replay['cases']]==[320028672,0,111587328,0]
for c in replay['cases']:
    s=c['summary'];assert s['final_observed_live_bytes']==0 and not s['surviving_ranges']
    assert sum(s['overlapping_recommit_bytes'].values())==0 and s['peak_observed_live_bytes']==s['peak_upper_bound_bytes']
    assert s['individual_origin_peaks']==s['individual_origin_peak_upper_bounds']
    p=v/'replay'/c['timeline']['path'];assert sha(p)==c['timeline']['sha256']
    rows=[json.loads(line) for line in p.read_text().splitlines()]
    assert len(rows)==s['events']-s['disjoint_same_time_events'] and rows[-1]['observed_live_bytes']==0
    assert all(sum(r['origins'].values())==r['observed_live_bytes'] for r in rows)
    assert all(a['time_us']<b['time_us'] for a,b in zip(rows,rows[1:]))
for name in ('.gitattributes','README.md','docs/ROADMAP.md','docs/TESTING.md','docs/PHASE2_VIRTUAL_RANGES_REPORT.md'):
    p=root/name;p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
shutil.copyfile(root/'out/finish_virtual_ranges.py',v/'verification-scripts/finish_virtual_ranges.py')
report.mkdir(exist_ok=False)
write(report/'summary.json',dict(SDK_source_fingerprint=fp,production_SDK_changed=False,memory_acceptance=False,phase2_complete=False,Intel_AV_cause_resolved=False,
    tool='tools/analyze_virtual_ranges.py',new_contract_tests=13,python_tests=182,native_regression_rerun=False,new_GPU_workload=False,new_trace=False,
    cases=[dict(case=c['case'],adapter=c['adapter'],summary=c['summary']) for c in replay['cases']],events=18204,
    interval_oracle_operations=700,all_observed_ranges_retired=True,whole_process_memory_claim=False,VRAM_claim=False,
    original_policy_unchanged=True,prior_evidence_preserved=True,eleven_SDK_packages_preserved=True,SDK_repackaged=False,SDK_package=package,commands=commands))
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE')}
paths|={p for p in (root/'docs').glob('*.md')}|{p for p in (root/'licenses').glob('*.json')}|{p for p in (root/'benchmarks/competitors').glob('*.json')}
paths|={root/'out'/p.name for p in (v/'verification-scripts').iterdir()}
sources=[rec(p) for p in sorted(paths)];manifest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f'benchmarks/reports/sources/virtual-ranges-{fp}-{manifest[:16]}.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=fp,source_manifest_sha256=manifest,files=sources),indent=2)+'\n')
with zipfile.ZipFile(archive) as z:
    assert len(z.namelist())==len(sources)+1
    for r in sources:assert hashlib.sha256(z.read(r['path'])).hexdigest()==r['sha256']
files=[rec(p) for p in sorted(v.rglob('*')) if p.is_file()]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.pdb','.lib','.dmp','.etl','.csv','.nupkg') for r in files+sources)
write(report/'evidence.json',dict(base_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),SDK_source_fingerprint=fp,
    summary=rec(report/'summary.json'),source_archive=rec(archive),source_files=sources,files=files,preserved_inputs=preserved,
    SDK_packages=prior['SDK_packages'],previous_evidence=rec(previous)))
print(f'PASS: {len(files)} frozen verification files, {len(sources)} verified archive members, four reconstructed traces, prior evidence preserved',flush=True)
