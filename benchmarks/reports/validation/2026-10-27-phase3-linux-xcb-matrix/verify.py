"""Offline replay of P3-WSI-07 evidence. Does not execute native/GPU work."""
import sys
sys.dont_write_bytecode=True
import hashlib,importlib.util,json,pathlib,re,tempfile,zipfile
F=pathlib.Path(__file__).resolve().parent;R=F.parents[3]
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def require(ok,message):
 if not ok:raise ValueError(message)
def load(name,p):
 spec=importlib.util.spec_from_file_location(name,p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def frozen(r,c):
 for p,h in r['frozen_inputs'].items():
  names=[n for n in c if p.replace('\\','/').endswith('/'+n)]
  if names:require(sha(c[max(names,key=len)])==h,'Frozen source changed: '+p)
def main():
 inventory=read(F/'inventory.json');summary=inventory['summary'];s=read(R/summary['path']);require(sha((R/summary['path']).read_bytes())==summary['sha256'],'Summary changed')
 require({p.relative_to(F).as_posix() for p in F.rglob('*') if p.is_file() and p.name not in ('inventory.json','offline-verification.log')}=={r['path'] for r in inventory['files']},'Unexpected payload')
 for row in inventory['files']:
  b=(F/row['path']).read_bytes();require(len(b)==row['bytes'] and sha(b)==row['sha256'],'Evidence bytes changed')
 contents={};manifests={}
 for p in F.glob('source-manifest-*.json'):
  m=read(p);require(m['snapshot_revision']==s['accepted_commit'],'Wrong source snapshot revision');a=m['archive'];b=(R/a['path']).read_bytes();require(len(b)==a['bytes'] and sha(b)==a['sha256'],'Source archive changed')
  with zipfile.ZipFile(R/a['path']) as z:
   require(set(z.namelist())=={'source-receipt.json'}|{x['path'] for x in m['files']},'Wrong source list');require(json.loads(z.read('source-receipt.json'))=={k:v for k,v in m.items() if k!='archive'},'Source receipt changed');c={x['path']:z.read(x['path']) for x in m['files']}
   for row in m['files']:require(len(c[row['path']])==row['bytes'] and sha(c[row['path']])==row['sha256'],'Source bytes changed')
   for n in z.namelist():require('..' not in pathlib.PurePosixPath(n).parts and not pathlib.PurePosixPath(n).is_absolute() and not n.lower().endswith(('.dll','.exe','.so','.a','.lib','.pdb','.pyc')) and '__pycache__' not in n,'Source binary/cache payload')
  sdk=sorted(n for n in c if n=='CMakeLists.txt' or n.startswith(('include/','src/')));fp=sha(''.join(n+'|'+sha(c[n])+'\n' for n in sdk).encode());require(len(sdk)==28 and fp==m['SDK_source_fingerprint'],'SDK fingerprint changed')
  old=read(R/f"benchmarks/reports/validation/2026-10-27-phase3-linux-xcb-events/source-manifest-{m['platform_bytes']}.json");require(fp==old['SDK_source_fingerprint'],'SDK product changed since WSI-06')
  with zipfile.ZipFile(R/old['archive']['path']) as z:require(all(z.read(n)==c[n] for n in sdk),'Prior SDK source differs')
  contents[m['platform_bytes']]=c;manifests[m['platform_bytes']]=m
 for row in read(F/'artifacts.json')+read(F/'history/artifacts.json'):
  p=F/row['path'];require(sha(p.read_bytes())==row['sha256'],'Raw artifact digest changed')
  with zipfile.ZipFile(p) as z:
   for n in z.namelist():
    require(not n.lower().endswith(('.dll','.exe','.so','.a','.lib','.pdb','.pyc')) and '__pycache__' not in n,'Artifact binary/cache payload')
    if not n.endswith('/'):require(z.read(n)==(F/row['extracted']/n).read_bytes(),'Extracted artifact changed')
 with tempfile.TemporaryDirectory() as folder:
  root=pathlib.Path(folder);c=contents['linux-git']
  for n,b in c.items():p=root/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
  sys.path.insert(0,str(root/'tools'));campaign=load('archived_campaign',root/'tools/run_linux_xcb_campaign.py');runners={n:load('archived_'+n,root/f'tools/run_linux_xcb_{n}.py') for n in campaign.SUITES};fp=manifests['linux-git']['SDK_source_fingerprint']
  for name,validation in [('normal',False),('validation',True)]:
   base=F/name/'wsi-evidence';top=read(base/'receipt.json');require(top['status']=='passed' and top['inputs_unchanged'] is True and top['source_commit']==s['accepted_commit'] and top['SDK_source_fingerprint']==fp,'Wrong campaign identity');frozen(top,c);inputs={}
   library_hashes=[]
   for suite in campaign.SUITES:
    b=base/suite;r=read(b/'receipt.json');inputs[suite]=r;frozen(r,c);require(r['source_commit']==s['accepted_commit'],'Mixed native source revisions')
    hashes=[h for p,h in r['frozen_inputs'].items() if p.endswith('/libGraphicsSDKCore.a')];require(len(hashes)==1,'Missing installed Core hash');library_hashes+=hashes
    results=r['cases'] if suite=='present' else [r]
    for case in results:
     stem='control-'+str(case['control']) if suite=='present' else 'native';rows=[json.loads(line) for line in (b/(stem+'.stdout')).read_text().splitlines()];args=(rows,fp,case['control'],validation) if suite=='present' else (rows,fp,validation);require(runners[suite].validate_rows(*args)==case['result'],'Native result changed')
     stderr=(b/(stem+'.stderr')).read_text();require(not re.search(r'^validation:',stderr,re.M),'Delivered SDK error including teardown')
     if validation:
      layer=top['controls']['loaded_layers'][0];require(case['loaded_layers']==[layer] and campaign.validate_control_load(stderr,layer)==[layer],'Pinned layer not actually loaded')
   require(len(set(library_hashes))==1 and next(h for p,h in top['frozen_inputs'].items() if p.endswith('/libGraphicsSDKCore.a'))==library_hashes[0],'Mixed installed Core binaries')
   controls=None
   if validation:
    controls=campaign.validate_controls((base/'controls.stdout').read_text(),fp,top['controls']['loaded_layers'][0]);require(controls==top['controls'],'Isolated controls changed');stderr=(base/'controls.stderr').read_text();require(campaign.validate_control_load(stderr,controls['loaded_layers'][0])==top['controls_loaded_paths'],'Control runtime not observed')
   require(campaign.validate_campaign(inputs,fp,validation,controls)==top['result'],'Integrated result changed')
   for suite in ('events','matrix'):
    b=base/suite;r=inputs[suite];wm=r['window_manager'];log=(b/'window-manager.log').read_text();require(wm['runtime_redistributed'] is False and r['frozen_inputs'][wm['path']]==wm['sha256'] and 'Openbox 3.6.1' in wm['version'] and 'openbox' in (b/'window-runtime.txt').read_text() and 'WM startup acknowledged before native client' in log and 'WM status after native client: None' in log and (b/'window-manager.log.ready').read_text()=='ready\n','Missing actual WM startup/liveness evidence')
 runtime=read(F/'validation/linux-xcb-validation-evidence/runtime/receipt.json');require(runtime['profile']['WSI_support']=='XCB' and runtime['runtime_redistributed'] is False and runtime['system_installation'] is False,'Wrong runtime scope');top=read(F/'validation/wsi-evidence/receipt.json')
 for key in ('layer_library','layer_manifest'):
  row=runtime[key];require(any(p.endswith('/'+row['path']) and h==row['sha256'] for p,h in top['frozen_inputs'].items()),'Pinned runtime hash mismatch')
 for row in runtime['diagnostics']:
  p=F/'validation/linux-xcb-validation-evidence/runtime'/pathlib.PurePosixPath(row['path']).name;require(sha(p.read_bytes())==row['sha256'],'Runtime diagnostic changed')
 for name in ('python-local','python-linux','python-windows'):
  r=read(F/name/'receipt.json');require(r['successful'] is True and r['inputs_unchanged'] is True and not r['errors'] and not r['failures'] and not r['skipped'] and len(r['test_ids'])==736,'Python regression failed');require(sha((F/name/'unittest.log').read_bytes())==r['log']['sha256'],'Python log changed')
 history=read(F/'history/analysis.json');require(history['accepted'] is False and history['SDK_product_changed'] is False,'Historical failures relabeled')
 for row in history['artifacts']:
  base=F/row['extracted']/'wsi-evidence';r=read(base/'receipt.json');require(r['status']=='failed' and r['SDK_source_fingerprint']==manifests['linux-git']['SDK_source_fingerprint'],'Historical failure changed')
  root=F/'history/source'/r['source_commit'];historical={p.relative_to(root).as_posix():p.read_bytes() for p in root.rglob('*') if p.is_file()};require(len(historical)==4,'Historical harness sources missing');frozen(r,historical)
  for p in base.glob('*/receipt.json'):frozen(read(p),historical)
  if row['expected_failure']=='WM-deadline':
   events=read(base/'events/receipt.json');require(events['returncode']==1 and events['status']=='failed','Historical event failure changed');text=(base/'events/native.stdout').read_text()+(base/'events/native.stderr').read_text();require('Native XCB/WM event deadline' in text,'Missing historical native diagnostic')
  else:require(r['error']=='Control runtime load not observed' and all(read(base/n/'receipt.json')['status']=='passed' for n in ('present','lifetime','resize','events','matrix')) and '/./libVkLayer_khronos_validation.so' in (base/'controls.stderr').read_text() and 'invalid_GPU_submissions=0' in (base/'controls.stdout').read_text(),'Historical path-only failure changed')
 for p in F.rglob('*'):require(p.name!='__pycache__' and p.suffix!='.pyc','Cache payload')
 print('PASS two source snapshots and unchanged SDK28; all five same-Core XCB suites normal/validation; multi-window 102 complete references per configuration; isolated core/sync controls without invalid GPU submissions; pinned runtime and WM; Python736; historical failures preserved; no native execution')
if __name__=='__main__':main()
