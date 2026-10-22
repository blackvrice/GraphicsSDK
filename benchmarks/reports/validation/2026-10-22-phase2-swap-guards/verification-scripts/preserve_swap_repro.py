import pathlib,subprocess,json,hashlib,shutil
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-swap-guards';v.mkdir(parents=True,exist_ok=False)
p=subprocess.run(['out/swap-guard-repro/build/Release/repro.exe'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(v/'before-repro.log').write_bytes(p.stdout);assert p.returncode==2
result=json.loads(p.stdout);assert result['wrong_buffer_recording_accepted']==result['destroyed_chain_recording_accepted']==1
old=json.loads((root/'benchmarks/reports/2026-10-22-phase2-fence-wait/evidence.json').read_text())
paths=[root/r['path'] for r in old['files']+old['SDK_packages']+[old['summary'],old['source_archive']]]
paths+=[root/'benchmarks/reports/2026-10-22-phase2-fence-wait/evidence.json',root/'out/core/Release/GraphicsSDKCore.lib',root/'out/install/lib/GraphicsSDKCore.lib',root/'out/swap-guard-repro/build/Release/repro.exe']
frozen={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
(v/'historical-before.json').write_text(json.dumps(dict(inputs=frozen,SDK_packages=old['SDK_packages'],before_repro_exit=2),indent=2)+'\n')
for name in ('CMakeLists.txt','main.cpp'):shutil.copyfile(root/'out/swap-guard-repro'/name,v/('before-repro-'+name))
print('PASS two stale-backbuffer recording bugs reproduced without invalid submission; nine SDK packages and prior evidence frozen')
