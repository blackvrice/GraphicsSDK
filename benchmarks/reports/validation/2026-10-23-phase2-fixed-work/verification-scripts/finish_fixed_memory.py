import pathlib,json,hashlib,subprocess,sys,zipfile,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-23-phase2-fixed-work';report=root/'benchmarks/reports/2026-10-23-phase2-fixed-work'
sys.path.insert(0,str(root/'tools'))
import run_fixed_memory as fixed
import audit_licenses as audit
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def rec(p):return dict(path=p.relative_to(root).as_posix(),sha256=sha(p),bytes=p.stat().st_size)
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
fp=audit.sdk_fingerprint(root);assert fp=='50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'
frozen=load(v/'inputs-before.json');assert frozen==load(v/'inputs-after.json') and all(sha(pathlib.Path(p))==h for p,h in frozen.items())
commands=load(v/'commands.json');assert len(commands)==20 and all(c['returncode']==c['expected_exit'] for c in commands)
log=(v/'python-tests.log').read_text();assert 'Ran 208 tests' in log and 'OK' in log and 'skipped' not in log
cases=load(v/'cases.json');assert len(cases)==7
assert sum(c['references']['frames'] for c in cases[:5])==720 and sum(c['references']['full_images'] for c in cases[:5])==2160
assert [c['references']['frames'] for c in cases[5:]]==[8,88]
assert fixed.compare(v/'intel-a/case.json',v/'intel-b/case.json','none')==load(v/'comparison.json')
for c in cases:
    name=c['case'];path=v/name/'memory.jsonl' if name in ('intel-a','intel-b') else v/(name+'.jsonl')
    code=0 if c['references']['completed'] else 2 if name=='intel-guard' else 3
    assert fixed.validate(fixed.read_records(path),fp,code)==c['references']
# Fresh union of the unchanged core's actual compiler closure and this helper's closure.
inventory,notices=audit.collect(root,'sdk',None);helper=load(v/'build-traces/inventory.json')
files={r['path'].lower():r for r in inventory['files']}
canonical={str(p.relative_to(root)).replace('\\','/').lower():p for folder in ('include','tools') for p in (root/folder).rglob('*') if p.is_file()}
for r in helper['own_compiler_inputs']:
    p=canonical[r['path'].lower()];assert sha(p)==r['sha256'];files[p.relative_to(root).as_posix().lower()]={**rec(p),'component':'graphicssdk'}
assert len(helper['own_compiler_inputs'])==15 and len(files)==22 and len(inventory['components'])==1
linklogs=[p for p in (v/'build-traces').rglob('link.read.1.tlog')];assert len(linklogs)==1
libraries=[]
for name in audit.parse_tlog(linklogs[0].read_bytes()):
    p=pathlib.Path(name)
    if p.is_relative_to(root):assert p==root/'out/core/Release/GraphicsSDKCore.lib' or p.is_relative_to(root/'out/memory-fixed-work'),name
    else:
        low=name.lower().replace('\\','/');assert low.startswith(('c:/program files/microsoft visual studio/','c:/program files (x86)/windows kits/')) or low in ('c:/windows/globalization/sorting/sortdefault.nls','c:/windows/system32/tzres.dll'),name
    if p.suffix.lower()=='.lib':libraries.append(dict(path=name,sha256=sha(p),system_prerequisite=not p.is_relative_to(root)))
inventory.update(profile='sdk-fixed-work',scope='Conservative actual compiler-read closure of unchanged SDK core plus fixed-work helper; not symbol reachability or legal redistribution approval.',
    files=sorted(files.values(),key=lambda r:r['path'].lower()),helper_build_inputs=[rec(root/'tools/memory_fixed_work/CMakeLists.txt')],
    helper_compiler_read_evidence=[rec(p) for p in (v/'build-traces').rglob('CL.read.1.tlog')],helper_linker_read_evidence=[rec(p) for p in linklogs],
    helper_libraries=libraries,helper_artifacts=[rec(fixed.BINARY),rec(fixed.BINARY.with_suffix('.pdb'))],
    helper_project=rec(root/'out/memory-fixed-work/gfx_memory_fixed_work.vcxproj'),reference_timed_binary=rec(root/'out/core/Release/gfx_memory_stress.exe'))
notices=notices.replace('Profile: sdk\n','Profile: sdk-fixed-work\n')
out=root/'licenses/generated/windows-sdk-fixed-work-release';assert not out.exists();out.mkdir()
for name,text in audit.output_files(inventory,notices).items():(out/name).write_bytes(text.encode())
for r in inventory['files']+inventory['artifacts']+inventory['compiler_read_evidence']+list(inventory['links'].values())+inventory['helper_build_inputs']+inventory['helper_compiler_read_evidence']+inventory['helper_linker_read_evidence']+inventory['helper_artifacts']+[inventory['helper_project'],inventory['reference_timed_binary']]:assert sha(root/r['path'])==r['sha256'],r['path']
previous=root/'benchmarks/reports/2026-10-23-phase2-memory-environment/evidence.json';prior=load(previous)
preserved=prior['files']+prior['preserved_inputs']+[prior[k] for k in ('summary','source_archive','previous_evidence')]+[rec(previous)]
assert all(sha(root/r['path'])==r['sha256'] for r in preserved)
assert len(prior['SDK_packages'])==11 and all(sha(root/r['path'])==r['sha256'] for r in prior['SDK_packages'])
package=audit.audit_package(root,root/'out/GraphicsSDK-0.0.1-dev-windows-x64-copy-atomicity.zip')
for name in ('.gitattributes','README.md','docs/ROADMAP.md','docs/TESTING.md','docs/DEPENDENCY_LICENSES.md','docs/PHASE2_FIXED_WORK_REPORT.md'):
    p=root/name;p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
shutil.copyfile(root/'out/finish_fixed_memory.py',v/'verification-scripts/finish_fixed_memory.py')
report.mkdir(exist_ok=False)
write(report/'summary.json',dict(SDK_source_fingerprint=fp,production_SDK_changed=False,phase2_complete=False,memory_acceptance=False,Intel_AV_cause_resolved=False,
    native_helper_built=True,new_tests=12,python_tests=208,completed_fixed_work_cases=5,frames=720,full_images=2160,verified_buffer_bytes=188743680,
    warmup_rounds_each=2,measured_rounds_each=16,checkpoint_rounds_each=4,abort_verified_prefix_frames=[8,88],new_long_soak=False,
    actual_driver_change=False,cases=cases,comparison=load(v/'comparison.json'),original_policy_unchanged=True,timed_executable_unchanged=True,
    eleven_SDK_packages_preserved=True,SDK_repackaged=False,native_full_regression_rerun=False,
    helper_compiler_inputs=15,combined_core_helper_inputs=22,components=1,inventory=rec(out/'inventory.json'),SDK_package=package,commands=commands))
suffixes={'.cpp','.hpp','.py','.txt','.in','.json','.md','.manifest','.ps1','.hlsl'}
paths={p for folder in ('include','src','cmake','tests','examples','tools','shaders') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in suffixes}
paths|={root/n for n in ('.gitattributes','CMakeLists.txt','CMakePresets.json','README.md','LICENSE')}
paths|={p for p in (root/'docs').glob('*.md')}|{p for p in (root/'licenses').glob('*.json')}|{p for p in (root/'benchmarks/competitors').glob('*.json')}
paths|={root/'out'/p.name for p in (v/'verification-scripts').iterdir()}
sources=[rec(p) for p in sorted(paths)];manifest=hashlib.sha256(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()).hexdigest()
archive=root/f'benchmarks/reports/sources/fixed-work-{fp}-{manifest[:16]}.zip'
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED) as z:
    for r in sources:z.writestr(r['path'],(root/r['path']).read_bytes())
    z.writestr('source-manifest.json',json.dumps(dict(SDK_source_fingerprint=fp,source_manifest_sha256=manifest,files=sources),indent=2)+'\n')
with zipfile.ZipFile(archive) as z:
    assert len(z.namelist())==len(sources)+1
    for r in sources:assert hashlib.sha256(z.read(r['path'])).hexdigest()==r['sha256']
files=[rec(p) for p in sorted(v.rglob('*')) if p.is_file()]+[rec(p) for p in sorted(out.iterdir())]
assert not any(pathlib.Path(r['path']).suffix.lower() in ('.exe','.dll','.pdb','.lib','.dmp','.etl','.csv','.nupkg') for r in files+sources)
write(report/'evidence.json',dict(base_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),SDK_source_fingerprint=fp,
    summary=rec(report/'summary.json'),source_archive=rec(archive),source_files=sources,files=files,preserved_inputs=preserved,
    SDK_packages=prior['SDK_packages'],previous_evidence=rec(previous),helper_artifacts=inventory['helper_artifacts']))
print(f'PASS: {len(files)} verification/audit files; {len(sources)} archive sources; 22-input/one-component fresh inventory; SDK and historical evidence preserved',flush=True)
