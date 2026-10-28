"""Independently replay saved CMP-02 metadata; never executes archived code/binaries."""
import hashlib
import json
import pathlib
import re
import zipfile

HERE=pathlib.Path(__file__).resolve().parent


def require(value,message):
    if not value:raise ValueError(message)


def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
def sha(data):return hashlib.sha256(data).hexdigest()


def verify(root=HERE):
    manifest=read(root/'manifest.json')
    require(manifest['task']=='P3-CMP-02' and manifest['performance_samples']==0,'Wrong archive scope')
    for item in manifest['files']:
        p=pathlib.PurePosixPath(item['path'])
        require(not p.is_absolute() and '..' not in p.parts and ':' not in str(p) and '\\' not in str(p),'Unsafe archive path')
        require('__pycache__' not in p.parts and p.suffix!='.pyc','Interpreter cache is not archived evidence')
        data=(root/p).read_bytes()
        require(sha(data)==item['sha256'] and len(data)==item['bytes'],'Archived input hash differs: '+str(p))
    receipt=read(root/'probe/receipt.json')
    require(receipt['status']=='native_probe_passed' and receipt['task']=='P3-CMP-02' and receipt['inputs_unchanged'] is True and
            receipt['minimum_reference_bytes']==24576 and receipt['performance_samples']==0 and
            receipt['workload_correctness_accepted'] is receipt['performance_accepted'] is False,'Receipt scope drift')
    pin='649c0cc8fa608312c5e4b94dae409a5859fc7553'
    with zipfile.ZipFile(root/'source.zip') as archive:
        names=archive.namelist()
        require(len(names)==len(set(names)) and all(not pathlib.PurePosixPath(n).is_absolute() and '..' not in pathlib.PurePosixPath(n).parts for n in names),'Unsafe/duplicate source ZIP')
        selected=['benchmarks/competitors/sources.lock.json','benchmarks/llgl_vulkan_probe/CMakeLists.txt','benchmarks/llgl_vulkan_probe/main.cpp']
        identity='llgl|'+pin+'\n'+''.join(n+'|'+sha(archive.read(n))+'\n' for n in sorted(selected))
        fingerprint=sha(identity.encode())
        require(fingerprint==receipt['source_fingerprint'],'Saved binary/source identity mismatch')
        require(json.loads(archive.read(selected[0]))['llgl']['commit']==pin,'LLGL source pin drift')
        suffix='benchmarks/llgl_vulkan_probe/main.cpp'
        original=next(r['path'][:-len(suffix)] for r in receipt['source_inputs'] if r['path'].endswith(suffix))
        frozen={r['path'][len(original):]:r for r in receipt['source_inputs'] if r['path'].startswith(original)}
        for name in names:
            if name in frozen:
                data=archive.read(name)
                require(sha(data)==frozen[name]['sha256'] and len(data)==frozen[name]['bytes'],'Archived source differs from executed source: '+name)
    commands=read(root/'probe/commands.json')
    names=['plain-identity','plain-smoke','validation-smoke','wrong-target','missing-layer']
    require([j['name'] for j in commands]==names,'Missing native probe or rejection control')
    require(commands==receipt['commands'],'Receipt/command records differ')
    for job in commands:
        require(job['returncode']==job['expected']==(1 if job['name'] in names[-2:] else 0),'Unexpected native exit')
        out=(root/'probe'/(job['name']+'.stdout')).read_bytes()
        err=(root/'probe'/(job['name']+'.stderr')).read_bytes()
        if job['expected']:
            token=b'Wrong target GPU' if job['name']=='wrong-target' else b'Required validation layer absent'
            require(not out and token in err,'Rejection failed for another reason')
            continue
        row=json.loads(out)
        require(row['source_fingerprint']==fingerprint and row['LLGL_commit']==pin and row['api']=='vulkan' and row['configuration']=='Release' and row['native_handle_verified'] is True,'Stale/non-native probe')
        require(all(row[k]==value for k,value in receipt['identity'].items()),'Native device/driver/queue mismatch')
        require(row['native_vendor_id']==4318 and row['queue_flags']&1 and row['queue_index']==0,'Wrong native target/queue')
        for key,length in [('device_uuid',32),('driver_uuid',32),('device_luid',16)]:
            require(re.fullmatch('[0-9a-f]{'+str(length)+'}',row[key]) and int(row[key],16),'Native identity absent')
        smoke=job['name']!='plain-identity';validation=job['name']=='validation-smoke'
        require(row['operation']==('smoke' if smoke else 'identity') and row['verified_bytes']==(12288 if smoke else 0) and row['reference_rounds']==(3 if smoke else 0),'Incomplete full changing reference')
        require(row['validation'] is validation and bool(row['loaded_validation_layer'])==validation and row['delivered_errors']==0,'Layer/error evidence differs')
        require(row['performance_samples']==0 and row['gpu_ms'] is None and row['performance_accepted'] is row['workload_correctness_accepted'] is row['instance_API_request_observed'] is False,'Minimal probe promoted to measurement')
        require(not re.search(rb'Validation Error|SYNC-HAZARD',err),'Native validation error in saved output')
    inputs=read(root/'probe/inputs.json')
    require(len(inputs['files'])==729 and len(inputs['read_logs'])==6 and len(inputs['compiler_commands'])==3,'Compiler/linker inputs incomplete')
    counts={n:sum(r['component']==n for r in inputs['files']) for n in ('llgl','vulkan_headers','own_probe','system_prerequisite','generated_build_input')}
    require(counts==dict(llgl=373,vulkan_headers=13,own_probe=1,system_prerequisite=340,generated_build_input=2),'Input classification changed')
    runtime=read(root/'probe/runtime-inputs.json')['files']
    require(runtime and all(r['redistributed'] is False for r in runtime),'Runtime redistribution claim drift')
    require(all(r['component']=='system_driver_or_audited_probe' for r in runtime),'Unexpected external capture hook/runtime module')
    require(len(receipt['source_inputs'])==754,'Frozen source/tool/artifact inputs incomplete')
    print('PASS P3-CMP-02 saved evidence: 3 native processes, 2 rejection controls, 24576 reference bytes; no performance acceptance')
    return receipt


if __name__=='__main__':verify()
