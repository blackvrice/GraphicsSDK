import hashlib,json,pathlib,shutil,subprocess,sys,zipfile
sys.dont_write_bytecode=True
ROOT=pathlib.Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from audit_licenses import sdk_fingerprint
F=ROOT/'benchmarks/reports/validation/2026-10-26-phase3-vulkan-recovery'
COMMIT='5d8af5c5632edbffa93bd8ab3c93ef4474c108e9'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def rec(p):return dict(path=p.relative_to(ROOT).as_posix(),bytes=p.stat().st_size,sha256=sha(p.read_bytes()))
def save(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
assert not F.exists();F.mkdir(parents=True)
for src,dest in [('out/vulkan-recovery-observed','windows'),('out/vulkan-recovery-ctest','native'),('out/vulkan-recovery-python','python-local'),('out/vulkan-recovery-first-build','first-build'),('out/github-linux-recovery','linux'),('out/github-recovery-python-linux','python-ci-linux'),('out/github-recovery-python-windows','python-ci-windows')]:
 for p in (ROOT/src).rglob('*'):
  if p.is_file():
   assert p.suffix.lower() not in ('.pyc','.exe','.dll','.lib','.pdb','.dmp','.so','.a')
   q=F/dest/p.relative_to(ROOT/src);q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
for src,dest in [('out/github-linux-recovery.zip','ci/linux-artifact.zip'),('out/github-recovery-python-linux.zip','ci/python-linux-artifact.zip'),('out/github-recovery-python-windows.zip','ci/python-windows-artifact.zip'),('out/github-linux-recovery-job.json','ci/linux-job.json'),('out/github-recovery-job-121392145592.json','ci/windows-job.json'),('out/github-recovery-job-121392145882.json','ci/portable-job.json'),('out/github-recovery-runs.json','ci/runs.json'),('out/vulkan-recovery-configure.log','setup/configure.log'),('out/vulkan-recovery-build.log','setup/first-build.log'),('out/vulkan-recovery-build-final.log','setup/second-build.log'),('out/vulkan-recovery-build-accepted.log','setup/accepted-build.log'),('out/vulkan-recovery-install.log','setup/install.log'),('out/vulkan-recovery-sdk/Testing/Temporary/LastTest.log','native/LastTest.log')]:
 q=F/dest;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/src,q)
save(F/'setup/build-attempts.json',dict(first='Harness beginRenderPass omitted required clear argument',second='Harness used nonexistent footprint width field',accepted='Corrected harness; serial build succeeded',compiler_crash=False,driver_or_system_changes=False))
for root in ('out/vulkan-recovery-sdk','out/vulkan-recovery-observer'):
 source=ROOT/root
 for p in [source/'CMakeCache.txt',*source.rglob('CL.command.1.tlog'),*source.rglob('link.command.1.tlog'),*source.rglob('Lib.command.1.tlog')]:
  q=F/'setup'/p.relative_to(ROOT);q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
sdk=[ROOT/'CMakeLists.txt',*(ROOT/'include').rglob('*.hpp'),*(ROOT/'src').rglob('*.cpp'),*(ROOT/'src').rglob('*.hpp')]
extra=[ROOT/n for n in ('CMakePresets.json','LICENSE','tools/audit_licenses.py','tools/run_vulkan_recovery.py','tools/vulkan-validation.lock.json','tests/VulkanRecoveryTests.cpp','tests/VulkanTests.cpp','tests/test_vulkan_recovery.py','tests/fixtures/SpirvFixtures.hpp','.github/workflows/vulkan-recovery.yml')]+[p for p in (ROOT/'tests/vulkan_recovery').iterdir() if p.is_file()]
paths=sorted(set(sdk+extra));git=['git','--no-optional-locks','-c','gc.auto=0','-c','maintenance.auto=false']
def archive(kind,content,fp):
 manifest=dict(schema=1,platform_bytes=kind,SDK_source_fingerprint=fp,implementation_commit=COMMIT,files=[dict(path=n,bytes=len(b),sha256=sha(b)) for n,b in sorted(content.items())],external_or_compiled_binaries_included=False)
 tag=sha(json.dumps(manifest,sort_keys=True).encode())[:16];p=ROOT/('benchmarks/reports/sources/vulkan-recovery-'+kind+'-'+fp+'-'+tag+'.zip');assert not p.exists()
 with zipfile.ZipFile(p,'w',zipfile.ZIP_DEFLATED) as z:
  for n,b in sorted(content.items()):z.writestr(n,b)
  z.writestr('source-receipt.json',json.dumps(manifest,indent=2)+'\n')
 save(F/('source-manifest-'+kind+'.json'),{**manifest,'archive':rec(p)})
win={p.relative_to(ROOT).as_posix():p.read_bytes() for p in paths};fp=sdk_fingerprint(ROOT);assert fp==read(F/'windows/campaign.json')['SDK_source_fingerprint'];archive('windows',win,fp)
linux={p.relative_to(ROOT).as_posix():subprocess.check_output(git+['show',COMMIT+':'+p.relative_to(ROOT).as_posix()],cwd=ROOT) for p in paths}
names={p.relative_to(ROOT).as_posix() for p in sdk};lfp=sha(''.join(n+'|'+sha(linux[n])+'\n' for n in sorted(names)).encode());assert lfp==read(F/'linux/campaign.json')['SDK_source_fingerprint'];archive('linux-git',linux,lfp)
save(F/'binary-identities.json',[{**rec(ROOT/n),'redistributed':False} for n in ('out/vulkan-recovery-sdk/Release/GraphicsSDKCore.lib','out/vulkan-recovery-install/lib/GraphicsSDKCore.lib','out/vulkan-recovery-sdk/Release/gfx_vulkan_recovery_tests.exe','out/vulkan-recovery-observer/Release/gfx_recovery_observe.exe','out/vulkan-recovery-observer/Release/VkLayer_gfx_recovery.dll')])
save(F/'ci/artifacts.json',dict(implementation_commit=COMMIT,run_id=40267223990,job_id=121392145791,artifacts=[dict(id=12627250812,name='linux-artifact.zip',sha256='b8b4a4eb753ff5fdf5627908c4db8860fa4639336fb824681b9a6262f3c051c6'),dict(id=12627245680,name='python-linux-artifact.zip',sha256='0b1555df2a6a1b13ba0e78158363c659796fae8ce4941eb0c326918ba41985a5'),dict(id=12626454873,name='python-windows-artifact.zip',sha256='a222414878f0392a42ea2550837987e581b19c597af59fba4ffbef977c13f39d')]))
print('Archived',len(paths),'source files per platform;',fp,lfp)
