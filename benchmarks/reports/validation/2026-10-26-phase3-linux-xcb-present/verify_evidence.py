"""Offline replay of archived XCB presentation evidence; no native execution."""
import sys
sys.dont_write_bytecode=True
import hashlib,importlib.util,json,pathlib,re,tempfile,zipfile
F=pathlib.Path(__file__).resolve().parent
R=F.parents[3]
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(b):return hashlib.sha256(b).hexdigest()
def require(ok,msg):
    if not ok:raise ValueError(msg)
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
def frozen_sources(receipt,contents):
    for path,h in receipt.get('frozen_inputs',{}).items():
        matches=[n for n in contents if path.replace('\\','/').endswith('/'+n)]
        if matches:
            name=max(matches,key=len);require(sha(contents[name])==h,'Frozen input mismatch '+name)
def main():
    inventory=read(F/'inventory.json')
    for row in inventory['files']:
        p=F/row['path'];b=p.read_bytes();require(len(b)==row['bytes'] and sha(b)==row['sha256'],'Changed raw evidence '+str(p))
    manifests={};snapshots={}
    for p in F.glob('source-manifest-*.json'):
        m=read(p);a=m['archive'];b=(R/a['path']).read_bytes();require(sha(b)==a['sha256'] and len(b)==a['bytes'],'Source ZIP changed')
        with zipfile.ZipFile(R/a['path']) as z:
            require(set(z.namelist())=={f['path'] for f in m['files']}|{'source-receipt.json'},'Source file list changed')
            files={f['path']:z.read(f['path']) for f in m['files']}
            for row in m['files']:require(sha(files[row['path']])==row['sha256'] and len(files[row['path']])==row['bytes'],'Source entry changed')
            for n in z.namelist():require(not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts and not n.lower().endswith(('.exe','.dll','.so','.a','.lib','.pdb','.pyc')) and '__pycache__' not in n,'Unexpected source payload')
        sdk=sorted(n for n in files if n=='CMakeLists.txt' or n.startswith(('src/','include/')))
        fp=sha(''.join(n+'|'+sha(files[n])+'\n' for n in sdk).encode());require(len(sdk)==28 and fp==m['SDK_source_fingerprint'],'SDK snapshot mismatch')
        manifests[m['platform_bytes']]=m;snapshots[m['platform_bytes']]=files
    require({'windows','linux-git','linux-first'}<=manifests.keys(),'Missing reproduction snapshots')
    for row in read(F/'ci/artifacts.json'):
        p=F/row['path'];require(sha(p.read_bytes())==row['sha256'],'Artifact digest mismatch')
        with zipfile.ZipFile(p) as z:
            for n in z.namelist():
                require(not n.lower().endswith(('.exe','.dll','.so','.a','.lib','.pyc')) and '__pycache__' not in n,'Binary artifact payload')
                if row.get('extracted') and not n.endswith('/'):require(z.read(n)==(F/row['extracted']/n).read_bytes(),'Extracted artifact differs from raw ZIP')
    with tempfile.TemporaryDirectory() as folder:
        root=pathlib.Path(folder)
        for n,b in snapshots['linux-git'].items():p=root/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
        sys.path.insert(0,str(root/'tools'));runner=load('archived_present_runner',root/'tools/run_linux_xcb_present.py')
        for kind,validation in [('normal',False),('validation',True)]:
            base=F/kind/'present-evidence';receipt=read(base/'receipt.json');require(receipt['status']=='passed' and receipt['inputs_unchanged'] is True,'Native campaign failed')
            require(receipt['SDK_source_fingerprint']==manifests['linux-git']['SDK_source_fingerprint'],'Wrong Linux SDK bytes');frozen_sources(receipt,snapshots['linux-git'])
            require([c['control'] for c in receipt['cases']]==[0,6,7,8],'Missing controls')
            for case in receipt['cases']:
                require(case['returncode']==0,'Native process failed');rows=[json.loads(l) for l in (base/(case['name']+'.stdout')).read_text().splitlines()]
                result=runner.validate_rows(rows,receipt['SDK_source_fingerprint'],case['control'],validation);require(result==case['result'],'Native result changed')
                if validation:
                    layers=case['loaded_layers'];require(len(layers)==1 and layers[0].endswith('/libVkLayer_khronos_validation.so'),'Missing actual layer')
                    stderr=(base/(case['name']+'.stderr')).read_text();require('calling init:' in stderr and 'libVkLayer_khronos_validation.so' in stderr,'No actual load diagnostics')
                    require(not re.search(r'^validation:',stderr,re.M),'Delivered validation error after final counter check')
            require(sum(c['result']['presents'] for c in receipt['cases'])==24 and sum(c['result']['images'] for c in receipt['cases'])==48,'Wrong native totals')
    for kind,code,passed,total in [('windows-first',8,58,65),('bgra-partial',8,3,5),('bgra-final',0,5,5),('windows-final',8,63,65),('windows-mode-final',8,63,65),('bgra-mode-final',0,5,5)]:
        receipt=read(F/kind/'receipt.json');require(receipt['returncode']==code and receipt['inputs_unchanged'] is True,'Windows campaign changed')
        snapshot='windows' if 'mode' in kind else 'windows-pre-mode-query';require(receipt['SDK_source_fingerprint']==manifests[snapshot]['SDK_source_fingerprint'],'Wrong Windows SDK bytes')
        log=(F/kind/'ctest.log').read_text();require(f'{total-passed} tests failed out of {total}' in log,'Wrong Windows test totals')
    frozen_sources(read(F/'bgra-final/receipt.json'),snapshots['windows-pre-mode-query']);frozen_sources(read(F/'bgra-mode-final/receipt.json'),snapshots['windows'])
    original=read(F/'windows-first/receipt.json')
    for name in ('BgraSupport.hpp','BgraTests.cpp'):
        b=(F/'first-tests'/name).read_bytes();require(any(p.replace('\\','/').endswith('/tests/'+name) and h==sha(b) for p,h in original['frozen_inputs'].items()),'Original Windows oracle changed')
    fences=read(F/'fence-isolated/receipt.json');require(len(fences)==2 and all(x['returncode']==0xc0000008 for x in fences),'Known fence failures changed')
    fences=read(F/'fence-mode-isolated/receipt.json');require(len(fences)==2 and all(x['returncode']==0xc0000008 and x['SDK_source_fingerprint']==manifests['windows']['SDK_source_fingerprint'] for x in fences),'Current known fence failures changed')
    for name,count in [('python-first',695),('python-intermediate',695),('python-final',696),('python-ci-linux',696),('python-ci-windows',696),('python-mode-final',697),('python-mode-ci-linux',697),('python-mode-ci-windows',697)]:
        receipt=read(F/name/'receipt.json');require(receipt['successful'] is True and not receipt['failures'] and not receipt['errors'] and not receipt['skipped'],'Python campaign failed')
        require(len(receipt['test_ids'])==count,'Python count changed');require(sha((F/name/'unittest.log').read_bytes())==receipt['log']['sha256'],'Python log changed')
        require(receipt['inputs_unchanged'] is True,'Python inputs changed')
    first=read(F/'linux-first/present-evidence/receipt.json');require(first['status']=='failed' and first['cases'][0]['returncode']==1 and first['inputs_unchanged'],'Initial failed oracle lost')
    frozen_sources(first,snapshots['linux-first'])
    for kind in ('validation-capture-first','validation-capture-r2'):
        receipt=read(F/kind/'present-evidence/receipt.json');require(receipt['status']=='failed' and 'Extra data' in receipt['error'] and len(receipt['cases'])==1 and receipt['cases'][0]['returncode']==0,'Capture failure relabeled')
    mailbox=read(F/'validation-mailbox-first/present-evidence/receipt.json');require(mailbox['status']=='failed' and len(mailbox['cases'])==4 and mailbox['cases'][-1]['returncode']==1,'MAILBOX failure relabeled')
    frozen_sources(mailbox,snapshots['linux-pre-diagnostics'])
    diagnostic=read(F/'validation-mailbox-diagnostic/present-evidence/receipt.json');require(diagnostic['status']=='failed' and diagnostic['cases'][-1]['returncode']==1,'MAILBOX diagnostic failure lost');frozen_sources(diagnostic,snapshots['linux-diagnostic'])
    require('VUID-VkSwapchainCreateInfoKHR-presentMode-02839' in (F/'validation-mailbox-diagnostic/present-evidence/control-8.stderr').read_text(),'Original native limit diagnostic lost')
    runtime=read(F/'validation/linux-xcb-validation-evidence/runtime/receipt.json');require(runtime['profile']['WSI_support']=='XCB' and runtime['profile']['runtime_release_flags']=='-O0 -DNDEBUG','Wrong pinned runtime profile')
    require(runtime['runtime_redistributed'] is False and runtime['system_installation'] is False,'External runtime scope changed')
    native=read(F/'validation/present-evidence/receipt.json')
    for key in ('layer_library','layer_manifest'):
        row=runtime[key];require(any(n.endswith('/'+row['path']) and h==row['sha256'] for n,h in native['frozen_inputs'].items()),'Unpinned native layer')
    for row in runtime['diagnostics']:
        p=F/'validation/linux-xcb-validation-evidence/runtime'/pathlib.PurePosixPath(row['path']).name;require(sha(p.read_bytes())==row['sha256'],'Runtime diagnostics changed')
    for p in F.rglob('*'):require(p.name!='__pycache__' and p.suffix!='.pyc','Generated cache in evidence')
    print('PASS source snapshots, raw hashes, native modes/completion/pixels, pinned validation, Windows 63/65 and preserved failures, Python 697; no native execution')
if __name__=='__main__':main()
