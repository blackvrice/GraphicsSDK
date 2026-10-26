"""Offline replay of XCB resize/recreation evidence; executes no native/GPU tests."""
import sys
sys.dont_write_bytecode=True
import hashlib,importlib.util,json,pathlib,re,tempfile,zipfile
F=pathlib.Path(__file__).resolve().parent;R=F.parents[3]
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def require(ok,message):
 if not ok:raise ValueError(message)
def load(name,p):
 s=importlib.util.spec_from_file_location(name,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def frozen(receipt,contents):
 for p,h in receipt['frozen_inputs'].items():
  names=[n for n in contents if p.replace('\\','/').endswith('/'+n)]
  if names:
   n=max(names,key=len);require(sha(contents[n])==h,'Frozen source changed: '+n)
def main():
 for row in read(F/'inventory.json')['files']:
  b=(F/row['path']).read_bytes();require(sha(b)==row['sha256'] and len(b)==row['bytes'],'Raw inventory mismatch')
 manifests={};contents={}
 for p in F.glob('source-manifest-*.json'):
  m=read(p);a=m['archive'];b=(R/a['path']).read_bytes();require(sha(b)==a['sha256'] and len(b)==a['bytes'],'Source ZIP changed')
  with zipfile.ZipFile(R/a['path']) as z:
   require(set(z.namelist())=={'source-receipt.json'}|{r['path'] for r in m['files']},'Wrong source list');c={r['path']:z.read(r['path']) for r in m['files']}
   for row in m['files']:require(sha(c[row['path']])==row['sha256'] and len(c[row['path']])==row['bytes'],'Source payload changed')
   for n in z.namelist():require('..' not in pathlib.PurePosixPath(n).parts and not pathlib.PurePosixPath(n).is_absolute() and not n.lower().endswith(('.exe','.dll','.so','.a','.lib','.pdb','.pyc')) and '__pycache__' not in n,'Unexpected source payload')
  sdk=sorted(n for n in c if n=='CMakeLists.txt' or n.startswith(('include/','src/')));fp=sha(''.join(n+'|'+sha(c[n])+'\n' for n in sdk).encode());require(len(sdk)==28 and fp==m['SDK_source_fingerprint'],'SDK bytes changed');manifests[m['platform_bytes']]=m;contents[m['platform_bytes']]=c
 baseline=read(F/'baseline/analysis.json');require(baseline['native_reproduction'] is False,'Source analysis mislabeled as native reproduction')
 for kind,c in contents.items():
  old=(F/'baseline'/(kind+'-Presentation.hpp')).read_bytes();original={**c,'src/backends/vulkan/Presentation.hpp':old};sdk=sorted(n for n in original if n=='CMakeLists.txt' or n.startswith(('include/','src/')))
  require(sha(''.join(n+'|'+sha(original[n])+'\n' for n in sdk).encode())==baseline['SDK_source_fingerprints'][kind],'Baseline SDK bytes changed')
  before=old.decode().split('Result<void> resize(')[1];after=c['src/backends/vulkan/Presentation.hpp'].decode().split('Result<void> resize(')[1];require(before.index('allocation.use_count()')<before.index('DeviceWaitIdle') and after.index('allocation.use_count()')>after.index('DeviceWaitIdle'),'Resize wait order mismatch')
 for row in read(F/'artifacts.json'):
  p=F/row['path'];require(sha(p.read_bytes())==row['sha256'],'Raw artifact digest changed')
  with zipfile.ZipFile(p) as z:
   for n in z.namelist():
    require(not n.lower().endswith(('.dll','.exe','.lib','.so','.a','.pyc')) and '__pycache__' not in n,'Unexpected artifact binary')
    if not n.endswith('/'):require(z.read(n)==(F/row['extracted']/n).read_bytes(),'Extracted artifact differs')
 with tempfile.TemporaryDirectory() as folder:
  root=pathlib.Path(folder)
  for n,b in contents['linux-git'].items():p=root/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  sys.path.insert(0,str(root/'tools'));runner=load('archived_resize_runner',root/'tools/run_linux_xcb_resize.py')
  for name,validation in [('normal',False),('validation',True)]:
   base=F/name/'resize-evidence';r=read(base/'receipt.json');require(r['status']=='passed' and r['inputs_unchanged'] is True and r['returncode']==0,'Failed native campaign')
   require(r['source_commit']=='54c14af29df976fc712b86fb45702776fb8261b2' and r['SDK_source_fingerprint']==manifests['linux-git']['SDK_source_fingerprint'],'Wrong native identity');frozen(r,contents['linux-git'])
   result=runner.validate_rows([json.loads(l) for l in (base/'native.stdout').read_text().splitlines()],r['SDK_source_fingerprint'],validation);require(result==r['result'],'Native results changed')
   if validation:
    stderr=(base/'native.stderr').read_text();layers=r['loaded_layers'];require(len(layers)==1 and layers[0].endswith('/libVkLayer_khronos_validation.so') and 'calling init:' in stderr and 'libVkLayer_khronos_validation.so' in stderr and not re.search(r'^validation:',stderr,re.M),'Runtime load/teardown mismatch')
 runtime=read(F/'validation/linux-xcb-validation-evidence/runtime/receipt.json');require(runtime['profile']['WSI_support']=='XCB' and runtime['runtime_redistributed'] is False and runtime['system_installation'] is False,'Wrong runtime scope');native=read(F/'validation/resize-evidence/receipt.json')
 for key in ('layer_library','layer_manifest'):
  row=runtime[key];require(any(p.endswith('/'+row['path']) and h==row['sha256'] for p,h in native['frozen_inputs'].items()),'Layer pin mismatch')
 for row in runtime['diagnostics']:
  p=F/'validation/linux-xcb-validation-evidence/runtime'/pathlib.PurePosixPath(row['path']).name;require(sha(p.read_bytes())==row['sha256'],'Runtime build diagnostic changed')
 r=read(F/'windows/receipt.json');require(r['returncode']==8 and r['inputs_unchanged'] is True and r['SDK_source_fingerprint']==manifests['windows']['SDK_source_fingerprint'],'Windows regression identity changed');require('2 tests failed out of 65' in (F/'windows/ctest.log').read_text(),'Wrong Windows totals');frozen(r,contents['windows'])
 failed=(F/'windows/LastTestsFailed.log').read_text();require(set(l.split(':')[1] for l in failed.splitlines())=={'fence_wait_gpu','fence_wait_validation'},'Unexpected native Windows failures')
 for name in ('python-local','python-linux','python-windows'):
  r=read(F/name/'receipt.json');require(r['successful'] is True and r['inputs_unchanged'] is True and not r['errors'] and not r['failures'] and not r['skipped'] and len(r['test_ids'])==711,'Python campaign failed');require(sha((F/name/'unittest.log').read_bytes())==r['log']['sha256'],'Python log changed')
 for p in F.rglob('*'):require(p.name!='__pycache__' and p.suffix!='.pyc','Cache payload')
 print('PASS two source snapshots, baseline source order, actual XCB resize/retirement/retry/status controls and GPU gates, pinned validation, Windows 63/65 with known exceptions, Python 711; no native execution')
if __name__=='__main__':main()
