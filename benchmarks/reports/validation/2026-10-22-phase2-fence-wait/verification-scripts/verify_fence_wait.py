import pathlib,subprocess,sys,json,shutil,hashlib
root=pathlib.Path.cwd();v=root/'benchmarks/reports/validation/2026-10-22-phase2-fence-wait';v.mkdir(parents=True,exist_ok=True);assert not (v/'inputs-before.json').exists()
sys.path.insert(0,str(root/'tools'));from audit_licenses import sdk_fingerprint
paths={p for folder in ('include','src','tests','examples','tools') for p in (root/folder).rglob('*') if p.is_file() and p.suffix in ('.cpp','.hpp','.py','.manifest')}
paths.add(root/'CMakeLists.txt');paths.add(root/'tests/consumer/CMakeLists.txt')
frozen={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}
(v/'inputs-before.json').write_text(json.dumps({'SDK_source_fingerprint':sdk_fingerprint(root),'inputs':frozen},indent=2)+'\n')
tasks=[('core-build',['cmake','--build','out/core','--config','Release','--parallel','1']),('core-ctest',['ctest','--test-dir','out/core','-C','Release','--output-on-failure']),
 ('no-backend-configure',['cmake','-S','.','-B','out/fence-wait-no-backend','-G','Visual Studio 17 2022','-A','x64','-DGFX_ENABLE_D3D12=OFF','-DGFX_BUILD_EXAMPLES=OFF']),
 ('no-backend-build',['cmake','--build','out/fence-wait-no-backend','--config','Release','--parallel','1']),('no-backend-ctest',['ctest','--test-dir','out/fence-wait-no-backend','-C','Release','--output-on-failure']),
 ('benchmark-build',['cmake','--build','out/bench','--config','Release','--parallel','1']),('benchmark-ctest',['ctest','--test-dir','out/bench','-C','Release','--output-on-failure']),
 ('install',['cmake','--install','out/core','--config','Release','--prefix','out/install']),('consumer-configure',['cmake','-S','tests/consumer','-B','out/consumer','-A','x64','-DCMAKE_PREFIX_PATH='+str(root/'out/install')]),
 ('consumer-build',['cmake','--build','out/consumer','--config','Release','--parallel','1']),('consumer-main',['out/consumer/Release/consumer.exe']),('consumer-recovery',['out/consumer/Release/recovery_consumer.exe']),('consumer-recovery-warp',['out/consumer/Release/recovery_consumer.exe','--warp']),
 ('consumer-adapter',['out/consumer/Release/adapter_consumer.exe']),('consumer-adapter-validation',['out/consumer/Release/adapter_consumer.exe','--validation']),('consumer-budget',['out/consumer/Release/budget_consumer.exe']),('consumer-budget-validation',['out/consumer/Release/budget_consumer.exe','--validation']),
 ('presentation-configure',['cmake','-S','examples/presentation','-B','out/presentation','-A','x64','-DCMAKE_PREFIX_PATH='+str(root/'out/install')]),('presentation-build',['cmake','--build','out/presentation','--config','Release','--parallel','1']),('presentation-ctest',['ctest','--test-dir','out/presentation','-C','Release','--output-on-failure']),
 ('presentation-campaign',[sys.executable,'tools/run_presentation_validation.py','--output',str(v/'presentation-campaign')]),('adapter-campaign',[sys.executable,'tools/run_adapter_validation.py','--output',str(v/'adapter-campaign')]),
 ('diagnostic-build',['cmake','--build','out/memory-validation-probe','--config','Release','--parallel','1']),('crash-symbol-build',['cmake','--build','out/crash-capture','--config','Release','--parallel','1']),
 ('repro-relink',['cmake','--build','out/wait-timeout-repro/build','--config','Release','--parallel','1']),('installed-timeout-repro',['out/wait-timeout-repro/build/Release/repro.exe']),
 ('python-tests',[sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py','-v'])]
rs=[]
for name,cmd in tasks:
 for attempt in range(1,4):
  print('Running '+name+f' attempt {attempt}',flush=True);p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=600)
  log=f'{name}-{attempt}.log';(v/log).write_bytes(p.stdout);rs.append({'name':name,'attempt':attempt,'command':cmd,'returncode':p.returncode,'log':log});(v/'commands.json').write_text(json.dumps(rs,indent=2)+'\n')
  if p.returncode==0:break
  retry=cmd[:2]==['cmake','--build'] and b'MSB6006' in p.stdout and b'-1073741819' in p.stdout
  if not retry or attempt==3:print(p.stdout.decode(errors='replace'));sys.exit(p.returncode)
  print('Preserved compiler access violation; unchanged-source serial retry',flush=True)
 for tag,folder in [('core','core'),('no-backend','fence-wait-no-backend'),('benchmark','bench'),('presentation','presentation')]:
  if name==tag+'-ctest':shutil.copyfile(root/f'out/{folder}/Testing/Temporary/LastTest.log',v/(tag+'-native.log'))
 print('PASS '+name,flush=True)
for path,h in frozen.items():assert hashlib.sha256((root/path).read_bytes()).hexdigest()==h,path
(v/'inputs-after.json').write_bytes((v/'inputs-before.json').read_bytes())
for mode in ('gpu','validation','warp'):shutil.copyfile(root/f'out/core/memory-{mode}.jsonl',v/f'core-memory-{mode}.jsonl')
print('PASS new SDK/30 core/6 no-backend/72 common/7 presentation/current consumers/installed timeout fix and diagnostics; frozen inputs unchanged',flush=True)
