import pathlib,hashlib,json,subprocess,sys,shutil,time
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-23-phase2-virtual-ranges'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,x):p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
sys.path.insert(0,str(root/'tools'))
from audit_licenses import sdk_fingerprint
fp=sdk_fingerprint(root);assert fp=='50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf'
# Retain the earlier development replay locally before producing the frozen final replay.
old=(v/'replay').resolve();local=(root/'out/virtual-ranges-first-replay').resolve()
assert old.is_relative_to(root.resolve()) and local.is_relative_to((root/'out').resolve()) and not local.exists()
old.rename(local)
for name in ('tools/analyze_virtual_ranges.py','tests/test_virtual_ranges.py'):
    p=root/name;p.write_bytes(p.read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'))
prior=root/'benchmarks/reports/2026-10-23-phase2-fault-investigation/evidence.json';e=load(prior)
preserved=e['files']+e['preserved_inputs']+e['source_files']+[e[k] for k in ('summary','source_archive','previous_evidence')]
for r in preserved:assert sha(root/r['path'])==r['sha256'],r['path']
manifest=root/'benchmarks/reports/validation/2026-10-22-phase2-fault-investigation/virtual-call-paths.json'
symbols=manifest.with_name('resolved-virtual-call-paths.json')
paths=[root/'tools/analyze_virtual_ranges.py',root/'tests/test_virtual_ranges.py',root/'tools/analyze_memory_stress.py',root/'out/verify_virtual_ranges.py',manifest,symbols,root/'tools/memory_stress_policy.json',root/'out/core/Release/GraphicsSDKCore.lib',prior]
paths += [root/c['filtered_events']['path'] for c in load(manifest)]
frozen={str(p):sha(p) for p in paths};write(v/'inputs-before.json',frozen)
commands=[]
for name,args in (
    ('python-tests',[sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py']),
    ('replay',[sys.executable,'tools/analyze_virtual_ranges.py','--trace-manifest',str(manifest),'--symbols',str(symbols),'--output',str(v/'replay')]),
    ('audit-recheck',[sys.executable,'tools/audit_licenses.py','--profile','sdk-crash','--output','licenses/generated/windows-sdk-thread-snapshot-crash-release','--check']),
    ('git-prestage-fsck',['git','fsck','--full','--strict'])):
    started=time.monotonic()
    with (v/(name+'.log')).open('xb') as log:p=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=180)
    commands.append(dict(name=name,command=args,returncode=p.returncode,seconds=time.monotonic()-started))
    write(v/'commands.json',commands);assert p.returncode==0,name
    print('PASS '+name,flush=True)
assert 'Ran 182 tests' in (v/'python-tests.log').read_text() and 'OK' in (v/'python-tests.log').read_text()
assert all(sha(pathlib.Path(p))==h for p,h in frozen.items());assert sdk_fingerprint(root)==fp
for r in preserved:assert sha(root/r['path'])==r['sha256'],r['path']
write(v/'inputs-after.json',frozen)
scripts=v/'verification-scripts';scripts.mkdir();shutil.copyfile(root/'out/verify_virtual_ranges.py',scripts/'verify_virtual_ranges.py')
print('PASS frozen inputs, previous investigation, SDK and eleven SDK packages preserved',flush=True)
