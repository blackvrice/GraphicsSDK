import pathlib,json,hashlib,zipfile,tempfile,sys,re,subprocess
sys.dont_write_bytecode=True
R=next(p for p in pathlib.Path(__file__).resolve().parents if (p/'docs/ROADMAP_TASKS.json').is_file());sys.path.insert(0,str(R/'tools'))
import prepare_vulkan_comparison as v
F=R/'benchmarks/reports/validation/2026-10-27-phase3-comparison-inputs';S=R/'benchmarks/reports/2026-10-27-phase3-comparison-inputs'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
r=read(F/'accepted/receipt.json');assert v.check(F/'accepted')['input_preparation_verified'];assert all(c['returncode']==c['expected'] for c in r['commands']);assert len(r['commands'])==29
assert all('--probe' in c['argv'] for c in r['commands'] if c['name'].endswith(('probe','wrong-target')))
inventory=read(F/'accepted/inputs.json')['files'];sourceprefix=r['installed_SDK_library']['path'].split('/'+r['roots']+'/')[0]+'/';rootprefix=sourceprefix+r['roots']+'/'
lookup={p['path'].casefold():p for p in inventory}
copied=list((F/'accepted/build-inputs').rglob('*'));assert copied
for p in copied:
 if p.is_file():
  key=(rootprefix+p.relative_to(F/'accepted/build-inputs').as_posix()).casefold();assert key in lookup and sha(p.read_bytes())==lookup[key]['sha256']
  if p.name=='CL.command.1.tlog':
   for line in p.read_bytes().decode('utf-16').splitlines():
    if line and not line.startswith('^'):
     flags=set(re.findall(r'(?<!\S)(/[^\s]+)',line));assert '/O2' in flags and '/MD' in flags and not flags.intersection(('/GL','/GL+','/Od','/MDd','/MT','/MTd'))
for n in v.NAMES:
 trace=list((F/'accepted/build-inputs/consumers'/n).rglob('link.read.1.tlog'));assert len(trace)==1
 libs={p.casefold().replace('\\','/') for p in v.parse_tlog(trace[0].read_bytes()) if p.casefold().endswith('.lib')};assert (rootprefix+'sdk-install/lib/GraphicsSDKCore.lib').casefold() in libs and (rootprefix+'sdl-build/Release/SDL3-static.lib').casefold() in libs
for kind in ('windows','linux-git'):
 m=read(F/f'source-manifest-{kind}.json');a=m['archive'];assert sha((R/a['path']).read_bytes())==a['sha256'];assert m['SDK_product_unchanged'] and m['SDK_files']==28 and len(m['files'])==64
 with zipfile.ZipFile(R/a['path']) as z,tempfile.TemporaryDirectory() as td:
  names={x['path'] for x in m['files']};assert set(z.namelist())==names|{'source-receipt.json'}
  assert all(not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts for n in names)
  for x in m['files']:assert len(z.read(x['path']))==x['bytes'] and sha(z.read(x['path']))==x['sha256']
  z.extractall(td);root=pathlib.Path(td);assert v.sdk_fingerprint(root)==m['SDK_source_fingerprint']
  if kind=='windows':
   assert all(v.MODULES[n].fingerprint(root)==r['profile_fingerprints'][n] for n in v.NAMES)
   byname={x['path']:x for x in m['files']}
   for x in r['source_inputs']:
    assert x['path'].startswith(sourceprefix);name=x['path'][len(sourceprefix):];assert x['sha256']==byname[name]['sha256'] and x['bytes']==byname[name]['bytes']
  sdk={n for n in names if n=='CMakeLists.txt' or n.startswith(('src/','include/'))};assert len(sdk)==28
  previous=read(R/f'benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-manual-readiness/source-manifest-{kind}.json')
  with zipfile.ZipFile(R/previous['archive']['path']) as old:assert all(z.read(n)==old.read(n) for n in sdk)
py=read(F/'python-local/receipt.json');assert py['successful'] and py['inputs_unchanged'] and py['source_commit']==r['source_commit'] and py['tests']==766 and len(set(py['test_ids']))==766 and not py['skipped'] and not py['failures'] and not py['errors'] and sha((F/'python-local/unittest.log').read_bytes())==py['log']['sha256']
assert read(F/'failed-flags/receipt.json')['error']=='Unreviewed Release compiler flags: sdl';assert read(F/'failed-target-message/receipt.json')['error']=='Wrong-target probe did not reject explicitly'
s=read(S/'summary.json');assert s['performance_samples']==0 and not any(s[k] for k in ('historical_timings_reused','correctness_accepted','performance_comparison_accepted','overall_SDK_ranking','SDL_native_handle_verified','Intel_GPU_device_created','Linux_benchmark_executed','human_display_accepted'))
for root in (F,S):
 for p in root.rglob('*'):assert p.name!='__pycache__' and not p.name.casefold().endswith(('.exe','.dll','.obj','.lib','.a','.so','.pdb','.pyc'))
ci=read(F/'ci-runs.json');assert len(ci['workflow_runs'])==2 and all(x['head_sha']==r['source_commit'] and x['conclusion']=='success' and x['status']=='completed' for x in ci['workflow_runs'])
if (F/'inventory.json').is_file():
 inv=read(F/'inventory.json');assert sha((R/inv['summary']['path']).read_bytes())==inv['summary']['sha256']
 assert {p.relative_to(F).as_posix() for p in F.rglob('*') if p.is_file() and p.name not in ('inventory.json','offline-verification.log')}=={x['path'] for x in inv['files']}
 for x in inv['files']:assert sha((F/x['path']).read_bytes())==x['sha256'] and (F/x['path']).stat().st_size==x['bytes']
if (R/'out/ben01-user-files.json').is_file():
 for n,h in read(R/'out/ben01-user-files.json').items():assert sha((R/n).read_bytes()).upper()==h
print('PASS archived eight same-GPU probes/eight exact-target controls; actual fresh Core+SDL linker reads and per-command Release flags; two64-file source ZIPs/unchangedSDK28; Python766; two honest failed attempts; successful Windows/Linux CI; zero timings/ranking; archived inventory unchanged. No native workload executed by this verifier.')
