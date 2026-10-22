import pathlib,subprocess,json,hashlib,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-fence-wait';v.mkdir(parents=True,exist_ok=False)
p=subprocess.run(['out/wait-timeout-repro/build/Release/repro.exe'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(v/'before-repro.log').write_bytes(p.stdout);assert p.returncode==2 and b'completed_negative_timeout_accepted=1' in p.stdout
old=json.loads((root/'benchmarks/reports/2026-10-22-phase2-budget-campaign/evidence.json').read_text())
paths=[root/r['path'] for r in old['files']+old['SDK_packages']+[old['summary'],old['source_archive']]]
paths+=[root/'benchmarks/reports/2026-10-22-phase2-budget-campaign/evidence.json',root/'out/core/Release/GraphicsSDKCore.lib',root/'out/install/lib/GraphicsSDKCore.lib',root/'out/wait-timeout-repro/build/Release/repro.exe']
frozen={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
(v/'historical-before.json').write_text(json.dumps(dict(inputs=frozen,SDK_packages=old['SDK_packages'],before_repro_exit=2),indent=2)+'\n')
for name in ('CMakeLists.txt','main.cpp'):shutil.copyfile(root/'out/wait-timeout-repro'/name,v/('before-repro-'+name))
print('PASS original completed-fence timeout bug reproduced with installed SDK; eight SDK packages and previous evidence frozen')
