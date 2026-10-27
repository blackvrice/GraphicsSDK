"""Offline P3-WSI-08 preparation evidence check; no native/GPU/human acceptance."""
import sys
sys.dont_write_bytecode=True
import hashlib,importlib.util,json,pathlib,re,tempfile,zipfile
F=pathlib.Path(__file__).resolve().parent;R=F.parents[3]
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def require(ok,message):
 if not ok:raise ValueError(message)
def frozen(r,c):
 for p,h in r['frozen_inputs'].items():
  names=[n for n in c if p.replace('\\','/').endswith('/'+n)]
  if names:require(sha(c[max(names,key=len)])==h,'Frozen source changed: '+p)
def main():
 inventory=read(F/'inventory.json');row=inventory['summary'];s=read(R/row['path']);require(sha((R/row['path']).read_bytes())==row['sha256'],'Summary changed')
 require(s['status']=='prepared' and s['roadmap_status']=='todo' and s['interactive_native_runs']==s['human_observations']==0 and s['human_display_accepted'] is s['physical_GPU_accepted'] is s['task_completed'] is False,'Preparation promoted to acceptance')
 require({p.relative_to(F).as_posix() for p in F.rglob('*') if p.is_file() and p.name not in ('inventory.json','offline-verification.log')}=={x['path'] for x in inventory['files']},'Unexpected payload')
 for row in inventory['files']:
  b=(F/row['path']).read_bytes();require(len(b)==row['bytes'] and sha(b)==row['sha256'],'Raw evidence changed')
 contents={};manifests={}
 for p in F.glob('source-manifest-*.json'):
  m=read(p);a=m['archive'];b=(R/a['path']).read_bytes();require(sha(b)==a['sha256'] and len(b)==a['bytes'] and m['snapshot_revision']==s['tool_commit'],'Source archive changed')
  with zipfile.ZipFile(R/a['path']) as z:
   require(set(z.namelist())=={'source-receipt.json'}|{x['path'] for x in m['files']} and json.loads(z.read('source-receipt.json'))=={k:v for k,v in m.items() if k!='archive'},'Wrong source receipt/list');c={x['path']:z.read(x['path']) for x in m['files']}
   for row in m['files']:require(len(c[row['path']])==row['bytes'] and sha(c[row['path']])==row['sha256'],'Source payload changed')
   for n in z.namelist():require('..' not in pathlib.PurePosixPath(n).parts and not pathlib.PurePosixPath(n).is_absolute() and not n.lower().endswith(('.dll','.exe','.so','.so.1','.a','.lib','.pdb','.pyc')) and '__pycache__' not in n,'Source binary/cache payload')
  sdk=sorted(n for n in c if n=='CMakeLists.txt' or n.startswith(('include/','src/')));fp=sha(''.join(n+'|'+sha(c[n])+'\n' for n in sdk).encode());require(len(sdk)==28 and fp==m['SDK_source_fingerprint'],'Wrong SDK source identity')
  old=read(R/f"benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-matrix/source-manifest-{m['platform_bytes']}.json");require(fp==old['SDK_source_fingerprint'],'SDK product changed')
  with zipfile.ZipFile(R/old['archive']['path']) as z:require(all(z.read(n)==c[n] for n in sdk),'Prior SDK bytes differ')
  contents[m['platform_bytes']]=c;manifests[m['platform_bytes']]=m
 for row in read(F/'artifacts.json'):
  p=F/row['path'];require(sha(p.read_bytes())==row['sha256'],'Official artifact digest changed')
  with zipfile.ZipFile(p) as z:
   for n in z.namelist():
    require(not n.lower().endswith(('.exe','.dll','.so','.so.1','.a','.lib','.pyc')) and '__pycache__' not in n,'Artifact binary/cache payload')
    if not n.endswith('/'):require(z.read(n)==(F/row['extracted']/n).read_bytes(),'Extracted artifact differs')
 with tempfile.TemporaryDirectory() as directory:
  root=pathlib.Path(directory);c=contents['linux-git']
  for n,b in c.items():p=root/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  sys.path.insert(0,str(root/'tools'));spec=importlib.util.spec_from_file_location('archived_manual',root/'tools/linux_xcb_manual.py');tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
  for name,validation in [('normal',False),('validation',True)]:
   base=F/name/'manual-evidence';r=read(base/'receipt.json');result=tool.check(base);require(r['status']=='smoke-passed' and r['source_commit']==s['tool_commit'] and r['SDK_source_fingerprint']==manifests['linux-git']['SDK_source_fingerprint'] and r['validation'] is validation and result['task_completed'] is result['human_display_accepted'] is result['physical_gpu_accepted'] is False,'Wrong readiness scope');frozen(r,c)
   form=read(base/'human-observation.json');require(form['observer'] is form['environment_description'] is None and form['direct_user_statements']==[] and all(v is None for v in form['checks'].values()),'CI manufactured human observations')
   for p,h in r['runtime_libraries'].items():require(r['frozen_inputs'][p]==h,'Runtime library pin mismatch')
   require(len(r['runtime_libraries'])==2 and 'WM startup acknowledged before native client' in (base/'window-manager.log').read_text() and 'WM status after native client: None' in (base/'window-manager.log').read_text() and (base/'window-manager.log.ready').read_text()=='ready\n','Missing real private WM/runtime observations')
 runtime=read(F/'validation/linux-xcb-validation-evidence/runtime/receipt.json');require(runtime['profile']['WSI_support']=='XCB' and runtime['system_installation'] is False and runtime['runtime_redistributed'] is False,'Wrong pinned runtime scope');native=read(F/'validation/manual-evidence/receipt.json')
 for key in ('layer_library','layer_manifest'):
  row=runtime[key];require(any(p.endswith('/'+row['path']) and h==row['sha256'] for p,h in native['frozen_inputs'].items()),'Pinned layer identity mismatch')
 for row in runtime['diagnostics']:
  p=F/'validation/linux-xcb-validation-evidence/runtime'/pathlib.PurePosixPath(row['path']).name;require(sha(p.read_bytes())==row['sha256'],'Runtime diagnostic changed')
 for name in ('python-local','python-linux','python-windows'):
  r=read(F/name/'receipt.json');require(r['successful'] is r['inputs_unchanged'] is True and not r['errors'] and not r['failures'] and not r['skipped'] and len(r['test_ids'])==750 and sha((F/name/'unittest.log').read_bytes())==r['log']['sha256'],'Python regression evidence changed')
 for p in F.rglob('*'):require(p.name!='__pycache__' and p.suffix!='.pyc','Cache payload')
 print('PASS two source snapshots and unchanged SDK28; normal/validation software smoke each 12 full animated references/presents; exact native UUID/driver/runtime and empty bound human forms; Python750; P3-WSI-08 todo and all acceptance flags false; no native execution')
if __name__=='__main__':main()
