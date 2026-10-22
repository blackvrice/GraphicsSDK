import pathlib,subprocess,json,hashlib,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-copy-atomicity';v.mkdir(parents=True,exist_ok=False)
p=subprocess.run(['out/copy-rejection-repro/build/Release/repro.exe'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(v/'before-repro.log').write_bytes(p.stdout);assert p.returncode==2
result=json.loads(p.stdout);assert result['rejected_copy_poisoned_submit']==1 and result['retained_resources_after_drop']==3
old=json.loads((root/'benchmarks/reports/2026-10-22-phase2-swap-guards/evidence.json').read_text())
paths=[root/r['path'] for r in old['files']+old['SDK_packages']+[old['summary'],old['source_archive']]]
paths+=[root/'benchmarks/reports/2026-10-22-phase2-swap-guards/evidence.json',root/'out/core/Release/GraphicsSDKCore.lib',root/'out/install/lib/GraphicsSDKCore.lib',root/'out/copy-rejection-repro/build/Release/repro.exe']
frozen={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
(v/'historical-before.json').write_text(json.dumps(dict(inputs=frozen,SDK_packages=old['SDK_packages'],before_repro_exit=2),indent=2)+'\n')
for name in ('CMakeLists.txt','main.cpp'):shutil.copyfile(root/'out/copy-rejection-repro'/name,v/('before-repro-'+name))
print('PASS rejected-copy retention and stale submission reproduced; ten SDK packages and prior evidence frozen')
